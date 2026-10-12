import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

const file = path.resolve('scripts/install-windows.ps1');
const source = fs.readFileSync(file,'utf8');

test('never recursively delete active current pointer',()=>{
  assert.doesNotMatch(source,/Remove-Item\s+-LiteralPath\s+\$Current\b/);
  assert.match(source,/Move-Item -LiteralPath \$Current -Destination \$rollback/);
  assert.match(source,/Move-Item -LiteralPath \$rollback -Destination \$Current/);
});
test('stage and hash-validate complete payload before swapping current',()=>{
  const verified = source.indexOf('BROWSER_STAGED_PAYLOAD_SHA_OR_MEMBER_MISMATCH');
  const current = source.indexOf('Move-Item -LiteralPath $Current -Destination $rollback');
  assert.ok(verified > 0 && current > verified);
  assert.match(source,/BROWSER_COMMITTED_VERSION_EXE_SHA_DRIFT/);
  assert.match(source,/BROWSER_EXACT_VERSION_OR_SWAP_PATH_ALREADY_EXISTS/);
});
test('existing baseline cannot be symlink-escaped',()=>{
  assert.match(source,/BROWSER_CURRENT_NOT_A_JUNCTION/);
  assert.match(source,/BROWSER_CURRENT_TARGET_OUTSIDE_VERSION_ROOT/);
  assert.match(source,/LinkType -cne 'Junction'/);
});
test('only isolated test fixtures can trigger failpoints',()=>{
  assert.match(source,/R57_FAULT_INJECTION_RESTRICTED_TO_PRIVATE_TEST_ROOT/);
  assert.match(source,/-not \$NoPublicIntegration/);
  assert.match(source,/test\\swap-r57/);
  assert.match(source,/AfterBaselineMove/);
  assert.match(source,/AfterCandidateMove/);
});
test('failed upgrade restores baseline and leaves forensic candidate',()=>{
  assert.match(source,/BROWSER_SWAP_ABORTED_AND_BASELINE_PRESERVED/);
  assert.match(source,/BROWSER_ROLLBACK_UNRESOLVED_OR_DEGRADED/);
  assert.match(source,/current\.failed-/);
  assert.match(source,/current\.rollback-/);
  assert.doesNotMatch(source,/(?<!# )\$ErrorActionPreference\s*=\s*'SilentlyContinue'/);
});
test('no browser auth or sandbox policy regression in installer',()=>{
  assert.doesNotMatch(source,/--no-sandbox|--disable-web-security|DeleteAllCookies|--purge-profile/);
  assert.match(source,/NoPublicIntegration/);
  assert.match(source,/companionRoot/);
});
