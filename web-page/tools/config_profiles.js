/* Portable configuration file validation. Synced into the embedded page. */
function validateProfileConfig(domain,data){
  const schema=CONFIG_PROFILE_SCHEMA[domain];
  const object=v=>v!==null&&typeof v==='object'&&!Array.isArray(v);
  if(!schema||!object(data)||!Object.keys(data).length)throw Error('Invalid configuration');
  const scalar=(rule,value,active=true)=>{
    const kind=rule.kind;
    if(kind==='bool')return typeof value==='boolean';
    if(kind==='eui')return typeof value==='string'&&(value===''||/^[0-9a-fA-F]{16}$/.test(value));
    if(kind==='str'||kind==='ip'){
      if(typeof value!=='string')return false;
      if(kind==='ip')return /^[0-9]+(?:\.[0-9]+){3}$/.test(value)&&value.split('.').every(n=>Number(n)<=255);
      return rule.hi===null||new TextEncoder().encode(value).length<=rule.hi;
    }
    if(typeof value!=='number'||!Number.isFinite(value))return false;
    if((kind==='int'||kind==='combo')&&!Number.isInteger(value))return false;
    if(kind==='combo')return !active||rule.options.includes(value);
    return active?value>=rule.lo&&value<=rule.hi:value>=0&&value<=4294967295;
  };
  for(const [key,value] of Object.entries(data)){
    if(!Object.prototype.hasOwnProperty.call(schema,key))throw Error('Unknown field: '+key);
    const rule=schema[key];
    if(rule.kind==='lines'){
      if(!object(value))throw Error('Invalid line settings');
      for(const [name,values] of Object.entries(value)){
        if(!rule.keys.includes(name)||!Array.isArray(values)||values.length!==7||values.some(v=>name==='inUse'?typeof v!=='boolean':!Number.isInteger(v)||v<1||v>16777215))throw Error('Invalid line field: '+name);
      }
    }else if(rule.array){
      const active=data.inUse||Array(7).fill(true);
      if(!Array.isArray(active)||active.length!==7||!Array.isArray(value)||value.length!==7||value.some((v,i)=>!scalar(rule,v,active[i])))throw Error('Invalid RF field: '+key);
    }else if(!scalar(rule,value))throw Error('Invalid field: '+key);
  }
  if(domain==='device'&&((data.SimKartPin>0&&data.SimKartPin<1000)||(data.PeriyodikModemResetPeriyodu>0&&data.PeriyodikModemResetPeriyodu<3600)))throw Error('Invalid PIN or reset period');
  if(domain==='iec104'&&((data.T1!==undefined&&data.T2!==undefined&&data.T2>=data.T1)||(data.SBO&&!(data.SBOTimeout>=1&&data.SBOTimeout<=300))))throw Error('Invalid IEC104 timer');
  if(domain==='rf'){
    const ids=new Set(),euis=new Set();
    for(let i=0;i<7;i++){
      if(data.inUse?.[i]){
        const id=data.HatID?.[i];
        if(!(id>=1&&id<=4)||ids.has(id))throw Error('Invalid or repeated feeder ID');
        ids.add(id);
      }
      for(const phase of ['R','S','T']){
        const eui=(data[phase+'_DEVICEID']?.[i]||'').toUpperCase();
        if(eui&&!/^0{16}$/.test(eui)){
          if(euis.has(eui))throw Error('Repeated EUI');
          euis.add(eui);
        }
      }
    }
  }
  return data;
}
function makeConfigProfile(domain,data){
  const schema=CONFIG_PROFILE_SCHEMA[domain];
  if(!schema)throw Error('Invalid domain');
  const config=Object.fromEntries(Object.entries(data).filter(([key])=>Object.prototype.hasOwnProperty.call(schema,key)));
  validateProfileConfig(domain,config);
  return {format:'troika-config',version:1,domain,config};
}
function parseConfigProfile(text,domain){
  if(new TextEncoder().encode(text).length>65536)throw Error('File too large');
  const obj=JSON.parse(text);
  if(!obj||Array.isArray(obj)||Object.keys(obj).sort().join(',')!=='config,domain,format,version'||obj.format!=='troika-config'||obj.version!==1||obj.domain!==domain)throw Error('File version or domain mismatch');
  return validateProfileConfig(domain,obj.config);
}

function profileActions(page){
  if(!CONFIG_PROFILE_SCHEMA[page]||!pageCache[page])return '';
  return `<button class="btn btn-muted btn-small" onclick="exportConfigFile('${page}')">${LNG==='tr'?'Dışa aktar':'Export'}</button>`+(s_role==='admin'?`<button class="btn btn-muted btn-small" data-admin="1" onclick="document.getElementById('profile-${page}').click()">${LNG==='tr'?'İçe aktar':'Import'}</button><input type="file" accept=".json,application/json" id="profile-${page}" hidden onchange="importConfigFile('${page}',this)">`:'');
}
function exportConfigFile(page){
  if(loading||!pageCache[page])return;
  try{
    const profile=makeConfigProfile(page,extractPage(page));
    const url=URL.createObjectURL(new Blob([JSON.stringify(profile,null,2)],{type:'application/json'}));
    const link=document.createElement('a');link.href=url;link.download='troika-'+page+'.json';
    document.body.appendChild(link);link.click();link.remove();URL.revokeObjectURL(url);
    toast(LNG==='tr'?'Ayarlar dışa aktarıldı. Dosya parolaları da içerir.':'Settings exported. The file includes passwords.','ok');
  }catch(error){toast(userError(error),'err')}
}
function stageConfigProfile(page,text){
  if(s_role!=='admin'||!pageCache[page]||curId!==page)throw Error('Read configuration first');
  const imported=parseConfigProfile(text,page),current=extractPage(page);
  const candidate={...current,...imported};
  if(page==='iec104')candidate.Hatlar={...current.Hatlar,...imported.Hatlar};
  validateProfileConfig(page,candidate);
  // Only after complete validation: retain the original device snapshot for
  // RF removal/move confirmation and normal dirty/read-discard behavior.
  const snapshot=pageCache[page];
  pageCache[page]={...snapshot,...candidate};renderPage(page);pageCache[page]=snapshot;
  document.querySelectorAll('#body input:not([type="file"]),#body select').forEach(el=>setDirty(page,el,true));
  return candidate;
}
async function importConfigFile(page,input){
  if(loading)return;
  const file=input.files?.[0];input.value='';if(!file)return;
  try{
    if(file.size>65536)throw Error('File too large');
    const text=await file.text();stageConfigProfile(page,text);
    toast(LNG==='tr'?'Dosya forma alındı. Cihaza uygulamak için Kaydet kullanın.':'Imported into the form. Use Save to apply to the device.','info');
  }catch(error){toast(userError(error),'err')}
}
