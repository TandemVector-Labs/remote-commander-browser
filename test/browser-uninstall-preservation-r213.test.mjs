import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const script=path.join(repo,'scripts','uninstall-local.sh');
const source=fs.readFileSync(script,'utf8');
function fixture(run){
 const home=fs.mkdtempSync(path.join(os.tmpdir(),'r213-cef-'));
 try{
  const version=fs.readFileSync(path.join(repo,'VERSION'),'utf8').trim();
  const runtime=path.join(home,'.local','share','chatgpt-cef-v2','runtime-v'+version);
  const profile=path.join(home,'.config','chatgpt-cef-v2');
  const launcher=path.join(home,'.local','bin','chatgpt-cef-v2');
  const desktop=path.join(home,'.local','share','applications','chatgpt-cef-v2.desktop');
  for(const p of [runtime,profile,path.dirname(launcher),path.dirname(desktop)])fs.mkdirSync(p,{recursive:true});
  fs.writeFileSync(path.join(runtime,'chrome-sandbox'),'FAKE_SANDBOX_KEEP');
  fs.writeFileSync(path.join(runtime,'chatgpt-cef-v2'),'FAKE_BINARY_KEEP');
  fs.writeFileSync(path.join(profile,'Local State'),'PRIVATE_PROFILE_SENTINEL_KEEP');
  fs.writeFileSync(launcher,'#!/bin/sh\nRUNTIME="'+runtime+'"\nSANDBOX="'+runtime+'/chrome-sandbox"\n');
  fs.writeFileSync(desktop,'[Desktop Entry]\nExec='+launcher+'\n');
  const execute=(...args)=>spawnSync('bash',[script,...args],{env:{...process.env,HOME:home},encoding:'utf8',timeout:5000});
  return run({home,runtime,profile,launcher,desktop,execute});
 }finally{
  assert.ok(home.startsWith(path.join(os.tmpdir(),'r213-cef-')));
  fs.rmSync(home,{recursive:true,force:true});
 }
}
test('R213 legacy uninstall and purge intents never remove a Browser login or runtime',()=>fixture(f=>{
 for(const args of [[],['--retire-runtime'],['--purge-profile']]){
  const r=f.execute(...args);
  assert.notEqual(r.status,0,'legacy mutation should be refused');
  assert.equal(fs.readFileSync(path.join(f.profile,'Local State'),'utf8'),'PRIVATE_PROFILE_SENTINEL_KEEP');
  assert.equal(fs.readFileSync(path.join(f.runtime,'chrome-sandbox'),'utf8'),'FAKE_SANDBOX_KEEP');
  assert.ok(fs.existsSync(f.launcher));
  assert.ok(fs.existsSync(f.desktop));
 }
}));
test('R213 audit is read-only and protects both active launcher and sandbox',()=>fixture(f=>{
 const r=f.execute('--audit');
 assert.equal(r.status,0,r.stderr);
 assert.match(r.stdout,/R213_RETIREMENT_STATUS=DEFER reason=ACTIVE_RUNTIME_OR_SANDBOX_REFERENCE/);
 assert.match(r.stdout,/R213_DELETE_COUNT=0/);
 assert.ok(!r.stdout.includes('PRIVATE_PROFILE_SENTINEL_KEEP'));
 assert.equal(fs.readFileSync(path.join(f.profile,'Local State'),'utf8'),'PRIVATE_PROFILE_SENTINEL_KEEP');
}));
test('R213 unknown args fail closed; no direct profile or runtime deletion instructions',()=>fixture(f=>{
 const r=f.execute('--delete-everything');
 assert.notEqual(r.status,0);
 assert.doesNotMatch(source,/\brm\s+(-[rf]+\s+)*["']?\$?(?:PROFILE|RUNTIME|LAUNCHER)\b/);
 assert.doesNotMatch(source,/sudo\s+rm/);
 assert.equal(fs.readFileSync(path.join(f.profile,'Local State'),'utf8'),'PRIVATE_PROFILE_SENTINEL_KEEP');
}));
