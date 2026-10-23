'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'../src');
const code=fs.readFileSync(path.join(root,'controller.cc'),'utf8');
const color=fs.readFileSync(path.join(root,'ui_chrome_palette.h'),'utf8');
const swatches=(name,size)=>{
 const m=color.match(new RegExp(String.raw`k${name}\s*\{([\s\S]*?)\};`));
 assert.ok(m,'missing palette '+name);
 const result=[...m[1].matchAll(/0xFF([A-F0-9]{6})u/g)].map(x=>x[1]);
 assert.equal(result.length,size);return result;
};
const lum=h=>[0,2,4].map(i=>parseInt(h.slice(i,i+2),16)/255)
 .map(x=>x<=.04045?x/12.92:((x+.055)/1.055)**2.4)
 .reduce((sum,x,i)=>sum+x*[.2126,.7152,.0722][i],0);
const contrast=(a,b)=>(Math.max(lum(a),lum(b))+.05)/(Math.min(lum(a),lum(b))+.05);
test('soft shell maintains readable selected/hover controls in each theme',()=>{
 for(const mode of ['Light','Dark']){
  const p=swatches(mode+'Palette',16),s=swatches(mode+'Sidebar',6);
  for(const [a,b,role] of [[p[9],p[0],'title'],[p[10],p[1],'inactive tab'],
   [p[11],p[6],'quiet CTA'],[p[11],p[2],'hover CTA'],
   [p[15],p[7],'primary action'],[p[15],p[8],'primary hover'],
   [s[4],s[3],'active rail'],[s[5],s[1],'inactive rail']])
   assert.ok(contrast(a,b)>=4.5,mode+' '+role+' contrast '+contrast(a,b).toFixed(2));
 }
});
test('native tabs remain visually distinct from ChatGPT page controls',()=>{
 assert.match(code,/◈  RC Browser/);
 assert.match(code,/Open Browser tab \(Ctrl\+T\); this does not start a new ChatGPT conversation/);
 assert.match(code,/SetText\(compact \? "\+" : "＋  New tab"\)/);
 assert.match(code,/CEF_HORIZONTAL_ALIGNMENT_CENTER : CEF_HORIZONTAL_ALIGNMENT_LEFT/);
 assert.match(code,/SetHorizontalAlignment\(rail_alignment\)/);
 assert.match(code,/sidebar_companion_button_->SetBackgroundColor\(UiPalette\(\)\.softAccent\)/);
 assert.doesNotMatch(code,/ExecuteJavaScript|insertAdjacentHTML|--disable-web-security|--no-sandbox/);
 assert.equal((code.match(/CreateBrowserView\(/g)||[]).length,1);
});
