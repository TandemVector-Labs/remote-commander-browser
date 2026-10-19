"""Offline fail-closed tests; never invoke a real update or user profile."""
import importlib.util
import io
import shutil
import hashlib
import subprocess
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parent.parent / 'scripts/update-preview-from-github.py'
spec = importlib.util.spec_from_file_location('rc_browser_preview_updater', SCRIPT)
u = importlib.util.module_from_spec(spec)
spec.loader.exec_module(u)

class GitHubUpdateSafety(unittest.TestCase):
    def test_semver_stable_newer_than_matching_rc(self):
        self.assertGreater(u.parse_version('0.8.3'), u.parse_version('0.8.3-rc.2'))
        self.assertLess(u.parse_version('0.8.1'), u.parse_version('0.8.3-rc.2'))
        self.assertGreater(u.parse_version('0.8.4-rc.1'), u.parse_version('0.8.3-rc.2'))

    def test_invalid_version_denied(self):
        for s in ('../0.8.9', '0.8.4+evil', '0.8.4-rc.1;cmd', 'v0.8.4', '0.8.4-dev.1'):
            with self.subTest(s=s), self.assertRaises(u.UpdateRefused):u.parse_version(s)

    def fixture_release(self, version='0.8.4', immutable=True, status='ahead'):
        archive=f'remote-commander-browser-v{version}-linux-x86_64.tar.gz'
        meta={'tag_name':'v'+version,'immutable':immutable,'prerelease':False,'draft':False,
              'assets':[{'name':archive,'digest':'sha256:'+'a'*64},
                        {'name':archive+'.sha256','digest':'sha256:'+'b'*64}]}
        compare={'status':status,'commits':[{'sha':'b'*40}]}
        return meta,compare

    def test_older_release_no_write_no_compare(self):
        meta,_=self.fixture_release('0.8.1')
        with mock.patch.object(u,'call',return_value=json.dumps(meta)) as fn:
            cand,state=u.release_candidate({'version':'0.8.3-rc.2','source_commit':u.INITIAL_COMMIT})
        self.assertIsNone(cand);self.assertEqual(state,'NO_NEWER_STABLE_RELEASE')
        self.assertEqual(fn.call_count,1)

    def test_new_release_must_be_immutable(self):
        meta,_=self.fixture_release(immutable=False)
        with mock.patch.object(u,'call',return_value=json.dumps(meta)):
            with self.assertRaisesRegex(u.UpdateRefused,'immutable'):u.release_candidate({'version':u.INITIAL_VERSION,'source_commit':u.INITIAL_COMMIT})

    def test_candidate_requires_accepted_lineage(self):
        meta,compare=self.fixture_release(status='diverged')
        with mock.patch.object(u,'call',side_effect=[json.dumps(meta),json.dumps(compare)]):
            with self.assertRaisesRegex(u.UpdateRefused,'descend'):u.release_candidate({'version':u.INITIAL_VERSION,'source_commit':u.INITIAL_COMMIT})

    def test_candidate_newer_signed_release_metadata(self):
        meta,compare=self.fixture_release()
        with mock.patch.object(u,'call',side_effect=[json.dumps(meta),json.dumps(compare)]) as fn:
            candidate,msg=u.release_candidate({'version':u.INITIAL_VERSION,'source_commit':u.INITIAL_COMMIT})
        self.assertEqual(msg,'ELIGIBLE_SIGNED_RELEASE')
        self.assertEqual(candidate['source_commit'],'b'*40)
        self.assertEqual(fn.call_args_list[-1].args[1],f'repos/{u.REPO}/compare/{u.INITIAL_COMMIT}...v0.8.4')

    def test_tampered_checksum_fails(self):
        with tempfile.TemporaryDirectory() as d:
            m=Path(d)/'sum';m.write_text('f'*64+'  artifact.tar.gz\n')
            with self.assertRaisesRegex(u.UpdateRefused,'disagree'):u.verify_checksum(m,'artifact.tar.gz','a'*64)
            m.write_text('a'*64+'  artifact.tar.gz\n')
            u.verify_checksum(m,'artifact.tar.gz','a'*64)

    def make_tar(self,tmp,entries,version='0.8.4'):
        p=Path(tmp)/'archive.tar.gz'
        with tarfile.open(p,'w:gz') as t:
            for name, content, kind in entries:
                obj=tarfile.TarInfo(name)
                if kind=='file':
                    obj.size=len(content);obj.mode=0o755 if name.endswith('chatgpt-cef-v2') else 0o644
                    t.addfile(obj,io.BytesIO(content))
                elif kind=='link':
                    obj.type=tarfile.SYMTYPE;obj.linkname='/etc/passwd';t.addfile(obj)
        return p

    def entries(self,extra=None):
        prefix='remote-commander-browser-v0.8.4-linux-x86_64/runtime/'
        values=[(prefix+'chatgpt-cef-v2',b'executable','file'),
                (prefix+'chrome-sandbox',b'trusted-suid-bytes','file')]
        values += [(prefix+'lib-'+str(n)+'.so',b'abcdef','file') for n in range(10)]
        return values+(extra or [])

    def test_extract_requires_matching_sandbox_and_preserves_own_binary(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)
            tar=self.make_tar(d,self.entries())
            helper=path/'trusted';helper.write_bytes(b'trusted-suid-bytes')
            dest=path/'stage';dest.mkdir()
            got=u.extract_runtime(tar,'0.8.4',dest,helper)
            self.assertEqual(got,u.sha(dest/'chatgpt-cef-v2'))
            self.assertTrue((dest/'chrome-sandbox').is_symlink())
            self.assertEqual((dest/'chrome-sandbox').resolve(),helper)

    def test_archive_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);tar=self.make_tar(d,self.entries([('remote-commander-browser-v0.8.4-linux-x86_64/runtime/link',b'','link')]))
            helper=path/'trusted';helper.write_bytes(b'trusted-suid-bytes');(path/'stage').mkdir()
            with self.assertRaisesRegex(u.UpdateRefused,'linked/special'):u.extract_runtime(tar,'0.8.4',path/'stage',helper)

    def test_archive_path_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);tar=self.make_tar(d,self.entries([('remote-commander-browser-v0.8.4-linux-x86_64/runtime/../outside',b'evil','file')]))
            helper=path/'trusted';helper.write_bytes(b'trusted-suid-bytes');(path/'stage').mkdir()
            with self.assertRaisesRegex(u.UpdateRefused,'unsafe'):u.extract_runtime(tar,'0.8.4',path/'stage',helper)
            self.assertFalse((path/'outside').exists())

    def test_archive_missing_or_changed_suid_refused(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);tar=self.make_tar(d,self.entries())
            helper=path/'trusted';helper.write_bytes(b'wrong-cef-version');(path/'stage').mkdir()
            with self.assertRaisesRegex(u.UpdateRefused,'sandbox version changed'):u.extract_runtime(tar,'0.8.4',path/'stage',helper)

    def test_full_newer_release_activation_is_atomic_and_preserves_one_rollback(self):
        with tempfile.TemporaryDirectory() as root:
            home=Path(root);app=home/'.local/state/rc-browser-glass-preview'
            runtime=home/'.local/share/chatgpt-cef-v2'
            (runtime).mkdir(parents=True);app.mkdir(parents=True)
            launcher=home/'.local/bin/rc-browser-glass-preview'
            launcher.parent.mkdir(parents=True)
            launcher.write_bytes(b'current approved Preview launcher')
            fallback=runtime/'trusted-helper'
            fallback.write_bytes(b'trusted-suid-bytes')
            old={'version':'0.8.3-rc.2','source_commit':u.INITIAL_COMMIT,
                 'launcher_sha256':u.sha(launcher),'elf_sha256':'a'*64,
                 'runtime':str(runtime/'old')}
            (runtime/'old').mkdir()
            # The test is entirely offline. GitHub and CEF native smoke are mocked.
            for version in ('0.8.4','0.8.5'):
                filename=f'remote-commander-browser-v{version}-linux-x86_64.tar.gz'
                entries=[(n.replace('v0.8.4-','v'+version+'-'),v,k)
                         for n,v,k in self.entries()]
                bundle=home/'archive.tar.gz'
                self.make_tar(home,entries)
                checksum=home/'fixture.sha256'
                digest=u.sha(bundle)
                checksum.write_text(f'{digest}  {filename}\n')
                candidate={'version':version,'tag':'v'+version,
                    'archive':filename,'checksum':filename+'.sha256',
                    'source_commit':'b'*40,
                    'assets':{filename:{'digest':'sha256:'+digest},
                              filename+'.sha256':{'digest':'sha256:'+u.sha(checksum)}}}
                calls=[]
                def fake_call(*args,**kwargs):
                    calls.append(args)
                    if args[:2]==('release','download'):
                        dst=Path(args[args.index('--dir')+1]);shutil.copy2(bundle,dst/filename)
                        shutil.copy2(checksum,dst/(filename+'.sha256'))
                    elif args[:2]==('attestation','verify'):
                        self.assertEqual(args[args.index('--source-ref')+1],'refs/tags/v'+version)
                        self.assertEqual(args[args.index('--signer-workflow')+1],u.WORKFLOW)
                        return 'signed official release'
                    else:self.fail(f'unexpected GitHub operation {args}')
                    return ''
                with mock.patch.object(u,'call',side_effect=fake_call), \
                     mock.patch.object(u,'wait_smoke') as smoke, \
                     mock.patch.object(u.subprocess,'run',return_value=subprocess.CompletedProcess(['pgrep'],1)):
                    u.apply_release(home,app,runtime,launcher,fallback,old,candidate)
                smoke.assert_called_once()
                self.assertEqual(len(calls),2)
                state=json.loads((app/'github-update-state.json').read_text())
                self.assertEqual(state['version'],version)
                self.assertEqual(state['launcher_sha256'],u.sha(launcher))
                self.assertIn('--disable-gpu --disable-gpu-rasterization',launcher.read_text())
                self.assertTrue((Path(state['runtime'])/'chrome-sandbox').is_symlink())
                if version=='0.8.4':
                    self.assertEqual((app/'previous-launcher.before-github-update').read_bytes(),b'current approved Preview launcher')
                else:
                    self.assertEqual((app/'previous-launcher.before-github-update').read_bytes(),first_new_launcher)
                first_new_launcher=launcher.read_bytes()
                old=state

    def test_timer_service_does_not_block_chromium_root_owned_suid_sandbox(self):
        installer=(Path(__file__).resolve().parents[1]/'scripts/install-preview-update-timer.sh').read_text()
        self.assertIn('PrivateTmp=false',installer)
        self.assertIn('UMask=0077',installer)
        self.assertNotIn('\nPrivateTmp=true\n',installer)
        self.assertNotIn('\nNoNewPrivileges=true\n',installer.split('cat > "$SERVICE" <<SERVICE')[1].split('SERVICE',1)[0])
        self.assertIn('ExecStart=/usr/bin/python3',installer)
        self.assertIn('OnUnitActiveSec=12h',installer)

    def test_rendered_single_launcher_keeps_gpu_and_sandbox(self):
        path=Path('/home/tester')
        s=u.render_launcher(path,path/'.local/share/chatgpt-cef-v2/runtime-local-preview-github-v0.8.4/chatgpt-cef-v2','a'*64,path/'.local/share/chatgpt-cef-v2/runtime-v0.8.3-rc.2-qa114.3b818244/chrome-sandbox').decode()
        self.assertIn('--disable-gpu --disable-gpu-rasterization',s)
        self.assertIn('stat -Lc',s)
        self.assertIn('CGWA_PROFILE_ROOT',s)
        self.assertNotIn('--no-sandbox',s)
        self.assertNotIn('--disable-web-security',s)
        self.assertIn('profile=/home/tester/.local/state/rc-browser-glass-preview/profile',s)

if __name__ == '__main__': unittest.main()
