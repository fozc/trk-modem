// Log windows use the existing offset/limit API; index 1 is newest.
let sysLogKnown=false,sysLogStart=0,sysLogEnd=0;
function sysLogSummary(){
  return tf('logsProgress',{total:sysLogKnown?sysLogTotal:'—',read:sysLogLines.length,target:Math.max(0,sysLogEnd-sysLogStart),range:sysLogLines.length?(sysLogStart+1)+'–'+sysLogOffset:'—'});
}
function sysLogEmptyText(){return t(sysLogEnd>0||(sysLogKnown&&sysLogTotal===0)?'noRecords':'logsPress')}
function renderLogs(){
  const field=(id,key,value)=>'<div class="field"><label for="'+id+'">'+esc(t(key))+'</label><input id="'+id+'" type="number" min="1" max="1000000" value="'+value+'"></div>';
  const action=(mode,key)=>'<button class="btn btn-read btn-small" onclick="loadSysLogs(\''+mode+'\')">'+esc(t(key))+'</button>';
  return '<div class="card"><div class="card-head"><div><h3>'+esc(t('mLogs'))+'</h3><div class="sub">'+esc(t('logsSub'))+'</div></div></div><div class="card-body"><p id="sysLogSummary" role="status" aria-live="polite">'+esc(sysLogSummary())+'</p><div class="grid g2">'+field('sysLogFirst','logsFirst',sysLogStart+1)+field('sysLogLast','logsLast',(sysLogEnd>0?sysLogEnd:sysLogStart+30))+'</div><div class="card-actions" style="margin:12px 0">'+action('count','logsCount')+action('range','logsRange')+action('all','logsAll')+'</div><p class="hint">'+esc(t('logsOrder'))+'</p><div id="sysLogContent">'+(sysLogLines.length?sysLogLines.map(line=>'<div class="log">'+esc(line)+'</div>').join(''):esc(sysLogEmptyText()))+'</div></div></div>';
}
function updateSysLogView(){
  const summary=sysLogSummary();
  if($('sysLogSummary'))$('sysLogSummary').textContent=summary;
  if($('sysLogWait'))$('sysLogWait').textContent=summary;
  if($('sysLogContent'))$('sysLogContent').innerHTML=sysLogLines.map(line=>'<div class="log">'+esc(line)+'</div>').join('')||esc(sysLogEmptyText());
}
async function loadSysLogs(mode='range'){
  if(loading)return;
  const token=s_sessionToken;
  let first=Number($('sysLogFirst')?.value||1),last=Number($('sysLogLast')?.value||30);
  if(mode==='range'&&(!Number.isInteger(first)||!Number.isInteger(last)||first<1||last<first||last>1000000)){
    toast(t('logsInvalidRange'),'err');return;
  }
  try{
    showLoad();
    const progress=document.createElement('p');progress.id='sysLogWait';progress.style.color='white';
    const overlay=$('requestLoading');if(overlay){overlay.style.flexDirection='column';overlay.appendChild(progress)}
    const count=await api('/syslogs?offset=0&limit=0');
    if(count.success===false)throw new Error(count.error||t('tFailed'));
    if(!Number.isInteger(count.t)||count.t<0)throw new Error(t('logsInvalidPage'));
    sysLogTotal=count.t;sysLogKnown=true;
    if(mode==='count'){updateSysLogView();return}
    if(mode==='all'){first=1;last=count.t}
    sysLogStart=first-1;sysLogEnd=Math.min(last,count.t);sysLogOffset=sysLogStart;sysLogLines=[];
    if($('sysLogFirst'))$('sysLogFirst').value=String(first);
    if($('sysLogLast'))$('sysLogLast').value=String(Math.max(1,sysLogEnd));
    updateSysLogView();
    while(sysLogOffset<sysLogEnd){
      const result=await api('/syslogs?offset='+sysLogOffset+'&limit='+Math.min(100,sysLogEnd-sysLogOffset));
      if(result.success===false)throw new Error(result.error||t('tFailed'));
      if(result.t!==sysLogTotal)throw new Error(t('logsChanged'));
      if(typeof result.recs!=='string')throw new Error(t('logsInvalidPage'));
      const lines=result.recs.split('\n').filter(line=>line.trim()).slice(0,sysLogEnd-sysLogOffset);
      if(!lines.length)throw new Error(t('logsEmptyPage'));
      sysLogLines.push(...lines);sysLogOffset+=lines.length;updateSysLogView();
    }
    toast(tf('tLogsLoaded',{n:sysLogLines.length}),'ok');
  }catch(error){if(token===s_sessionToken)toast(t('tReadErr')+userError(error),'err')}
  finally{if(token===s_sessionToken)hideLoad()}
}
