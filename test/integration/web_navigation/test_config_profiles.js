/* Run the production profile code from both source and embedded web pages. */
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const zlib=require('node:zlib');
const root=path.resolve(__dirname,'../../..');
const fixtures=JSON.parse(fs.readFileSync(path.join(__dirname,'profile_fixtures.json'),'utf8'));
const source=fs.readFileSync(path.join(root,'web-page/index.html'),'utf8');
const c=fs.readFileSync(path.join(root,'Application/web-server/index_html.c'),'utf8').split('};')[0];
const embedded=zlib.gunzipSync(Buffer.from([...c.matchAll(/0x([0-9a-f]{2})/gi)].map(m=>parseInt(m[1],16)))).toString();
let checks=0;
for(const [label,html] of [['source',source],['embedded',embedded]]){
  const block=html.match(/\/\/ CONFIG PROFILE BEGIN([\s\S]*?)\/\/ CONFIG PROFILE END/);
  // Minification removes comments, so locate by the stable contract/functions.
  const code=block?block[1]:html.slice(html.indexOf('const CONFIG_PROFILE_SCHEMA='),html.indexOf('const readPage='));
  assert.ok(code.includes('function parseConfigProfile'));
  let posts=0,rendered=null;
  const element={classList:{add(){},remove(){}},closest:()=>null};
  const context=vm.createContext({console,TextEncoder,
    document:{querySelectorAll:()=>[element]},
    pageCache:{device:{PreserveOutputStates:true}},curId:'device',s_role:'admin',
    extractPage:()=>({PreserveOutputStates:true}),
    renderPage:()=>{rendered=context.pageCache.device},setDirty(){},
    fetch:()=>{posts++;throw Error('Import must not send requests')}});
  vm.runInContext(code,context);
  const run=s=>vm.runInContext(s,context);
  for(const fixture of fixtures){
    context.fixture=fixture;
    const data=run('parseConfigProfile(JSON.stringify(fixture.profile),fixture.domain)');
    assert.deepEqual(JSON.parse(JSON.stringify(data)),fixture.profile.config);
    const again=run('makeConfigProfile(fixture.domain,fixture.profile.config)');
    assert.deepEqual(JSON.parse(JSON.stringify(again)),fixture.profile);
    checks+=2;
  }
  for(const [domain,config] of [['device',{PreserveOutputStates:'true'}],['device',{Time:1}],
      ['device',{SimKartPin:12}],['modbus',{StopBits:3}],['iec104',{T1:5,T2:6}],
      ['rf',{R_DEVICEID:['bad']}],['rf',{inUse:Array(7).fill(1)}]]){
    context.bad={format:'troika-config',version:1,domain,config};
    assert.throws(()=>run('parseConfigProfile(JSON.stringify(bad),bad.domain)'));checks++;
  }
  const text=JSON.stringify({format:'troika-config',version:1,domain:'device',config:{PreserveOutputStates:false}});
  context.text=text;
  run('stageConfigProfile("device",text)');
  assert.equal(rendered.PreserveOutputStates,false);
  assert.equal(context.pageCache.device.PreserveOutputStates,true,'device snapshot retained');
  assert.equal(posts,0);checks++;
  context.text=text.replace('false','"false"');rendered=null;
  assert.throws(()=>run('stageConfigProfile("device",text)'));
  assert.equal(rendered,null,'invalid file must not change the form');checks++;
  context.curId='rf';context.text=text;
  assert.throws(()=>run('stageConfigProfile("device",text)'));checks++;
  context.curId='device';context.s_role='user';
  assert.throws(()=>run('stageConfigProfile("device",text)'));checks++;
  context.s_role='admin';context.pageCache={};
  assert.throws(()=>run('stageConfigProfile("device",text)'));checks++;
  console.log('PASS:',label,'portable profiles, invalid files, read-first, role, no POST, original snapshot');
}
console.log('Profile checks:',checks);
