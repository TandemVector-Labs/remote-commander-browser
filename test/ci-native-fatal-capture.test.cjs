'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {spawnSync} = require('node:child_process');

const workflow = fs.readFileSync(
  path.join(__dirname, '..', '.github', 'workflows', 'build.yml'), 'utf8'
).replace(/\r\n/g, '\n');
const start = '          # R105_CI_CHILD_OUTPUT_BEGIN\n';
const end = '          # R105_CI_CHILD_OUTPUT_END\n';

function extractGuard() {
  const a = workflow.indexOf(start);
  const b = workflow.indexOf(end);
  assert.ok(a >= 0 && b > a, 'the native stdout fatal gate must exist');
  assert.ok(workflow.indexOf(start, a + 1) < 0, 'the gate must occur exactly once');
  const afterProcessExit = workflow.indexOf('[[ "$companion_ci_rc" -eq 0 ]]');
  const beforePositiveMarker = workflow.indexOf("grep -q 'CGWA_COMPANION_SELFTEST PASS'");
  assert.ok(afterProcessExit >= 0 && beforePositiveMarker > afterProcessExit);
  assert.ok(a > afterProcessExit && b < beforePositiveMarker,
    'captured stdout must be checked before normal native PASS admission');
  return workflow.slice(a + start.length, b)
    .split('\n').map(line => line.replace(/^ {10}/, '')).join('\n');
}

test('R105 GitHub native X11 workflow refuses child fatal stdout even with parent exit zero', () => {
  const guard = extractGuard();
  assert.match(guard, /companion-native-runtime\.out/);
  assert.match(guard, /grep -Eiq/);
  assert.match(guard, /segmentation fault\|aborted/);
  assert.match(guard, /stack smashing/);
  assert.match(guard, /native_output_scan_rc/);
  assert.match(guard, /exit 36/);
  assert.match(guard, /exit 37/);
});

test('R105 actual Linux Bash gate handles fatal, clean, absent and scan-error fixtures',
  {skip: process.platform === 'win32' ? 'Bash behavior is checked on Linux CI; Windows checks the source contract' : false}, () => {
  const guard = extractGuard();
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'r105-ci-native-output-'));
  const qa = path.join(dir, '.qa');
  fs.mkdirSync(qa, {mode: 0o700});
  const fixture = path.join(qa, 'companion-native-runtime.out');
  const runner = path.join(dir, 'ci-native-output-guard.sh');
  fs.writeFileSync(runner, 'set -euo pipefail\n' + guard + '\nprintf "GATE_OK\n"\n', {mode: 0o700});
  const run = (env = {}) => spawnSync('bash', [runner], {
    cwd: dir, env: {...process.env, ...env}, encoding: 'utf8', timeout: 5000
  });
  const fatal = [
    '*** stack smashing detected ***: terminated',
    'Segmentation fault (core dumped)',
    'Aborted (core dumped)',
    'fatal: CEF process failed',
    'child SIGSEGV', 'child SIGABRT', 'segfault', 'stack buffer overflow',
    'AddressSanitizer: detected error', 'ThreadSanitizer: detected data race',
    "GPU process isn't usable. Goodbye."
  ];
  try {
    for (const marker of fatal) {
      fs.writeFileSync(fixture, 'private fixture DO_NOT_ECHO\n' + marker + '\n', {mode: 0o600});
      const result = run();
      assert.equal(result.error, undefined, marker);
      assert.equal(result.status, 36, 'fatal stdout bypassed: ' + marker);
      assert.match(result.stderr, /native child fatal/i);
      assert.doesNotMatch(result.stderr, /DO_NOT_ECHO|private fixture/,
        'raw captured output must not be echoed');
    }
    fs.writeFileSync(fixture, 'CGWA_COMPANION_SELFTEST PASS\n');
    const clean = run();
    assert.equal(clean.error, undefined);
    assert.equal(clean.status, 0, clean.stderr);
    assert.match(clean.stdout, /GATE_OK/);
    fs.rmSync(fixture);
    const missing = run();
    assert.equal(missing.status, 37, 'unavailable captured stdout must fail closed');
    fs.writeFileSync(fixture, 'CGWA_COMPANION_SELFTEST PASS\n', {mode: 0o600});
    const fakeGrep = path.join(dir, 'grep');
    fs.writeFileSync(fakeGrep, '#!/bin/sh\nexit 2\n', {mode: 0o700});
    const ioError = run({PATH: dir + path.delimiter + process.env.PATH});
    assert.equal(ioError.status, 37, 'grep scanner errors must fail closed');
  } finally {
    fs.rmSync(dir, {recursive: true, force: true});
  }
});
