'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {spawnSync}=require('node:child_process');
const source=fs.readFileSync(path.join(__dirname,'../scripts/runtime-self-test.sh'),'utf8');
function runCase(mode) {
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'rc-native-stderr-'));
 try {
   for(const dir of ['scripts','build/bin','fakebin'])fs.mkdirSync(path.join(root,dir),{recursive:true});
   fs.writeFileSync(path.join(root,'scripts/runtime-self-test.sh'),source,{mode:0o700});
   fs.writeFileSync(path.join(root,'build/bin/chrome-sandbox'),'fixture\n');
   fs.writeFileSync(path.join(root,'build/bin/chatgpt-cef-v2'),[
     '#!/usr/bin/env bash','set -euo pipefail',
     'for arg in "$@"; do case "$arg" in --cgwa-profile-dir=*) profile="$(printf %s "$arg" | cut -d= -f2-)";; esac; done',
     'printf "CGWA_SELFTEST PASS\\nCGWA_SHUTDOWN FINALIZE\\nCGWA_SHUTDOWN WINDOW_DESTROYED\\n" > "$profile/chrome_debug.log"',
     'case "$QA_MOCK_MODE" in stack) printf "*** stack smashing detected ***: terminated\\n" >&2;; fatal) printf "FATAL child SIGSEGV\\n" >&2;; esac','exit 0',''
   ].join('\n'),{mode:0o700});
   fs.writeFileSync(path.join(root,'fakebin/stat'),[
     '#!/usr/bin/env bash',
     'if [[ "$1" == "-Lc" && "$2" == "%a %U %G" && "$3" == */chrome-sandbox ]]; then',
     ' printf "4755 root root\\n"',
     'else exec /usr/bin/stat "$@"; fi',''
   ].join('\n'),{mode:0o700});
   return spawnSync('bash',[path.join(root,'scripts/runtime-self-test.sh')],{
     cwd:root,encoding:'utf8',timeout:8000,
     env:{...process.env,DISPLAY:':mock-x11',QA_MOCK_MODE:mode,PATH:path.join(root,'fakebin')+':'+process.env.PATH}
   });
 }finally{fs.rmSync(root,{recursive:true,force:true});}
}
const skip=process.platform!=='linux';
test('runtime self-test allows clean fixture',{skip},()=>{
 const x=runCase('normal');
 assert.equal(x.status,0,x.stdout+x.stderr);
 assert.match(x.stdout,/RUNTIME_SELFTEST=PASS/);
});
test('runtime self-test blocks stderr stack smashing even when parent returns zero',{skip},()=>{
 const x=runCase('stack');
 assert.equal(x.status,30,x.stdout+x.stderr);
 assert.match(x.stderr,/fatal child diagnostic/);
});
test('runtime self-test blocks FATAL SIGSEGV even when parent returns zero',{skip},()=>{
 const x=runCase('fatal');
 assert.equal(x.status,30,x.stdout+x.stderr);
 assert.match(x.stderr,/fatal child diagnostic/);
});
