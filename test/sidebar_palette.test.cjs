'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const {join}=require('node:path');
const root=join(__dirname,'../src');
const palette=fs.readFileSync(join(root,'ui_chrome_palette.h'),'utf8');
const code=fs.readFileSync(join(root,'controller.cc'),'utf8');
const values=name=>{
 const match=palette.match(new RegExp('k'+name+'Sidebar\\s*\\{([^}]*)\\}'));
 assert.ok(match,name);
 return [...match[1].matchAll(/0xFF([A-F0-9]{6})u/g)].map(x=>x[1]);
};
const luminance=x=>[0,2,4].map(i=>parseInt(x.slice(i,i+2),16)/255)
 .map(v=>v<=0.04045?v/12.92:((v+.055)/1.055)**2.4)
 .reduce((acc,v,i)=>acc+v*[.2126,.7152,.0722][i],0);
const contrast=(a,b)=>(Math.max(luminance(a),luminance(b))+.05)/
                         (Math.min(luminance(a),luminance(b))+.05);
test('sidebar light and dark each have 6 semantic roles and WCAG text contrast',()=>{
 for(const kind of ['Light','Dark']){
  const v=values(kind);
  assert.equal(v.length,6);
  assert.ok(contrast(v[4],v[0])>=7,kind+' primary');
  assert.ok(contrast(v[5],v[1])>=4.5,kind+' secondary');
  assert.ok(contrast(v[4],v[3])>=4.5,kind+' selected');
 }
});
test('all native sidebar state colors resolve through the selected theme',()=>{
 assert.match(code,/UiSidebarPalette\(\)/);
 assert.match(code,/Sidebar\(g_ui_theme\)/);
 for(const field of ['background','hover','active','text','muted','tab'])
  assert.match(code,new RegExp('UiSidebarPalette\\(\\)\\.'+field));
 assert.doesNotMatch(code,/kSidebarBackground|kSidebarActive|kSidebarHover/);
});
