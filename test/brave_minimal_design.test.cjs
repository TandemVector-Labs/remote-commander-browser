'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const colors=fs.readFileSync(path.join(root,'src/ui_chrome_palette.h'),'utf8');
const ui=fs.readFileSync(path.join(root,'src/controller.cc'),'utf8');
const read=(name,n)=> {
  const m=colors.match(new RegExp('k'+name+'\\s*\\{([\\s\\S]*?)\\};'));
  assert.ok(m,'color tokens for '+name);
  const values=[...m[1].matchAll(/0xFF([0-9A-Fa-f]{6})u/g)].map(x=>x[1]);
  assert.equal(values.length,n);
  return values;
};
const lum=(hex)=>{
  const rgb=[0,2,4].map(i=>parseInt(hex.slice(i,i+2),16)/255).map(v=>v<=.04045?v/12.92:Math.pow((v+.055)/1.055,2.4));
  return rgb[0]*.2126+rgb[1]*.7152+rgb[2]*.0722;
};
const contrast=(a,b)=>(Math.max(lum(a),lum(b))+.05)/(Math.min(lum(a),lum(b))+.05);
test('Modern quiet browser chrome is accessible in both themes',()=>{
  const pairs=[['LightPalette',16],['DarkPalette',16],['LightSidebar',6],['DarkSidebar',6]];
  for(const [name,count] of pairs){
    const c=read(name,count);
    for(const token of c) assert.match(token,/^[0-9A-F]{6}$/);
    if(count===16){
      assert.ok(contrast(c[9],c[0])>=7,name+' title');
      assert.ok(contrast(c[10],c[1])>=4.5,name+' inactive tab');
      assert.ok(contrast(c[11],c[3])>=4.5,name+' selected tab');
      assert.ok(contrast(c[14],c[13])>=4.5,name+' close');
    } else {
      assert.ok(contrast(c[4],c[3])>=4.5,name+' active sidebar');
      assert.ok(contrast(c[5],c[1])>=4.5,name+' secondary sidebar');
    }
  }
});
test('Minimal native toolbar still exposes critical actions and shortcuts',()=>{
  assert.match(ui,/kBrandWidth = 108;/);
  assert.match(ui,/"◆  RC"/);
  assert.match(ui,/"◈ Commander"/);
  assert.match(ui,/"☾" : "☼"/);
  assert.match(ui,/SetAccessibleName\("Toggle read-only Commander companion"\)/);
  assert.match(ui,/SetAccessibleName\("Close application"\)/);
  assert.match(ui,/SetAccelerator\(kAccelNewTab/);
  assert.match(ui,/SetAccelerator\(kAccelOrientation/);
  assert.match(ui,/SetVisible\(!vertical && !compact_toolbar && tabs_\.size\(\) > 1\)/);
  assert.doesNotMatch(ui,/--no-sandbox|--disable-web-security|DeleteAllCookies/);
});
test('The layout remains native; page, profile and renderer untouched',()=>{
  assert.match(ui,/CefPanel::CreatePanel\(sidebar_surface_delegate_\)/);
  assert.match(ui,/CefLabelButton::CreateLabelButton/);
  assert.match(ui,/SetInkDropEnabled\(true\)/);
  assert.match(ui,/kSidebarCompactWidth = 54;/);
  assert.match(ui,/kHeaderHeight = 52;/);
  assert.match(ui,/RetireTabUi\(\*it\)/);
  assert.match(ui,/SetFontList\(kUIFont\)/);
  assert.doesNotMatch(ui,/ExecuteJavaScript\(.+chrome|querySelector\(.+chatgpt/);
});
