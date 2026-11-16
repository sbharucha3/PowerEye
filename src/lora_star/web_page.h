#pragma once
// Live status + on-device history page served by the hub at http://192.168.4.1
//
// The page saves every sample into the phone/iPad's own browser storage
// (IndexedDB) while it is open, syncing every 5 s. The hub keeps the last 8 h
// in PSRAM, so when the page is reopened after being closed (screen locked,
// app switched) it catches up on everything it missed.
// No internet on the hub's Wi-Fi, so everything here is self-contained.
const char INDEX_HTML[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="apple-mobile-web-app-capable" content="yes"><meta name="apple-mobile-web-app-title" content="LoRa Hub">
<title>LoRa Hub</title>
<style>
:root{--bg:#f6f7f9;--fg:#1d2330;--muted:#5b6475;--card:#fff;--head:#eef0f3;--line:#e3e6eb;--on:#137333;--off:#b3261e;--warn:#a05a00}
@media(prefers-color-scheme:dark){:root{--bg:#15181d;--fg:#e6e8eb;--muted:#9aa3b2;--card:#1d2128;--head:#262b33;--line:#2f353e;--on:#5fd38a;--off:#ff8a80;--warn:#f0b35a}}
body{font-family:system-ui,-apple-system,sans-serif;margin:16px;background:var(--bg);color:var(--fg)}
h1{font-size:20px;margin:0 0 4px}h2{font-size:16px;margin:0 0 10px}
#sum{color:var(--muted);font-size:14px}.note{color:var(--muted);font-size:12px;margin:6px 0 12px;line-height:1.5}
.bar,.ctl{display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap;align-items:center}
.wrap{overflow-x:auto;border:1px solid var(--line);border-radius:8px}
table{border-collapse:collapse;width:100%;background:var(--card);font-size:13px;font-variant-numeric:tabular-nums}
th,td{padding:6px 10px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}
th{background:var(--head);position:sticky;top:0;font-weight:600}
td:first-child,th:first-child,td:nth-child(2),th:nth-child(2){text-align:left}
.on{color:var(--on);font-weight:600}.off{color:var(--off);font-weight:600}.warn{color:var(--warn);font-weight:600}
button,select,a.btn{font:inherit;font-size:13px;padding:5px 10px;border-radius:6px;border:1px solid var(--line);background:var(--card);color:var(--fg);cursor:pointer;text-decoration:none}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px;margin-top:16px}
canvas{width:100%;height:320px;display:block;touch-action:none}
#tip{font-size:12px;color:var(--muted);min-height:36px;margin-top:8px;line-height:1.6}
#store{font-size:13px;margin-bottom:10px;line-height:1.5}
</style></head><body>
<h1>LoRa Hub</h1><div id="sum">Loading...</div><div id="clk" class="note"></div>
<div class="bar"><button onclick="cmd('all','identify')">Identify all nodes</button></div>
<div class="wrap"><table><thead><tr>
<th>Node</th><th>Status</th><th>Last seen</th><th>Voltage (V)</th><th>Current (A)</th><th>Power (W)</th><th>Temp (&deg;C)</th>
<th>RSSI at hub</th><th>SNR at hub</th><th>RSSI at node</th><th>Link</th><th>Data</th><th>Backlog</th><th>Chip &deg;C</th><th></th>
</tr></thead><tbody id="rows"></tbody></table></div>
<div class="note">Link = polls answered. Data = samples that reached the hub, counting backfill. Backlog = samples still waiting on the node.</div>

<div class="card"><h2>Saved on this device</h2>
<div id="store">Opening storage...</div>
<div class="ctl">
<select id="exp"><option value="1">Last 1 h</option><option value="24" selected>Last 24 h</option><option value="168">Last 7 days</option><option value="all">Everything</option></select>
<button onclick="exportCsv()">Export CSV</button>
<button onclick="clearData()">Delete saved data</button>
<a class="btn" href="/history.csv?min=480">Hub's last 8 h (CSV)</a>
</div>
<div class="note">Keep this page open (and the screen awake) to save data live. If it closes, it catches up on reopening, as long as that's within 8 h. Add this page to your Home Screen so the browser doesn't clear the saved data, and export a CSV regularly as a backup.</div>
</div>

<div class="card"><h2>History</h2>
<div class="ctl">
<select id="met"><option value="w">Power (W)</option><option value="v">Voltage (V)</option><option value="a">Current (A)</option><option value="t">Temperature (&deg;C)</option></select>
<select id="win"><option value="15">Last 15 min</option><option value="60" selected>Last 1 h</option><option value="480">Last 8 h</option><option value="1440">Last 24 h</option></select>
</div>
<canvas id="cv"></canvas>
<div id="tip">Hover or drag across the chart to compare every node at the same moment.</div>
</div>

<script>
const $=id=>document.getElementById(id);
const css=v=>getComputedStyle(document.documentElement).getPropertyValue(v).trim();
async function cmd(id,c){try{await fetch('/api/cmd?id='+id+'&c='+c)}catch(e){}}
const pct=(a,b)=>b?(100*a/b).toFixed(1)+'%':'-';
const fmtT=t=>new Date(t).toLocaleString([],{month:'short',day:'numeric',hour:'2-digit',minute:'2-digit'});

// ---------------- live table ----------------
async function tick(){
 try{
  const d=await (await fetch('/api/nodes')).json();
  let on=0,h='';
  for(const n of d.nodes){
   const ok=n.age<d.timeout; if(ok)on++;
   h+=`<tr><td>${n.id}</td><td class="${ok?'on':'off'}">${ok?'online':'offline'}</td><td>${n.age.toFixed(1)} s</td>`+
      `<td>${n.v.toFixed(2)}</td><td>${n.a.toFixed(2)}</td><td>${n.w.toFixed(1)}</td><td>${n.t.toFixed(2)}</td>`+
      `<td>${n.rssi.toFixed(1)}</td><td>${n.snr.toFixed(1)}</td><td>${n.nrssi}</td>`+
      `<td>${pct(n.replies,n.polls)}</td><td>${pct(n.rx,n.rx+n.lost)}</td>`+
      `<td class="${n.backlog>2?'warn':''}">${n.backlog}</td>`+
      `<td class="${n.chip>=75?'warn':''}" title="radio resets: ${n.resets}">${n.chip}${n.resets?' &middot; '+n.resets+'R':''}</td>`+
      `<td><button onclick="cmd(${n.id},'identify')">Identify</button></td></tr>`;
  }
  $('rows').innerHTML=h||'<tr><td colspan="15" style="text-align:left">No nodes heard yet</td></tr>';
  $('sum').textContent=`Cycle ${d.cycle} · ${on} of ${d.nodes.length} known nodes online · last cycle: ${d.answered}/${d.polled} answered, ${d.retries} retries, ${d.ms} ms · hub chip ${d.hubChip} °C`+
   (d.hubHot?' (hot: Wi-Fi power lowered)':'')+(d.hubRadioResets?` · hub radio resets: ${d.hubRadioResets}`:'');
  $('clk').textContent=(d.clock?`Hub clock set from ${d.clock}. `:'Hub clock not set yet. ')+`Every node samples on the same ${d.sample/1000} s grid, so timestamps line up across nodes.`;
 }catch(e){$('sum').textContent='Hub not reachable'}
}

// ---------------- device storage (IndexedDB) ----------------
// One record per time point: {t: epoch ms, m: {nodeId: [cV, cA, dW, cC, status]}}
const ST='slots';let db=null,syncing=false,lastSync=0,syncErr='',step=3000;
const rq=r=>new Promise((res,rej)=>{r.onsuccess=()=>res(r.result);r.onerror=()=>rej(r.error)});
const store=mode=>db.transaction(ST,mode||'readonly').objectStore(ST);
function openDB(){return new Promise((res,rej)=>{const r=indexedDB.open('lorahub',1);
 r.onupgradeneeded=()=>r.result.createObjectStore(ST,{keyPath:'t'});r.onsuccess=()=>res(r.result);r.onerror=()=>rej(r.error)})}
function getCur(){try{return JSON.parse(localStorage.getItem('cur'))||{}}catch(e){return {}}}
function setCur(c){try{localStorage.setItem('cur',JSON.stringify(c))}catch(e){}}

function saveRows(r){
 return new Promise((res,rej)=>{
  if(!r.rows.length){res();return}
  const tx=db.transaction(ST,'readwrite'),os=tx.objectStore(ST);
  for(const [slot,flat] of r.rows){
   const t=Math.round(slot*r.step+r.off);
   const g=os.get(t);
   g.onsuccess=()=>{const m=g.result?g.result.m:{};          // merge: late backfill adds nodes to a time point
    for(let i=0;i<flat.length;i+=6)m[flat[i]]=flat.slice(i+1,i+6);os.put({t,m})};
  }
  tx.oncomplete=()=>res();tx.onerror=()=>rej(tx.error);tx.onabort=()=>rej(tx.error);
 });
}

async function sync(){
 if(syncing||!db)return;syncing=true;
 try{
  let c=getCur(),first=true;
  for(let page=0;page<100;page++){
   // Re-read the last few minutes each time: backfilled samples arrive late
   const from=c.boot===undefined?0:(first?Math.max(0,c.next-(c.rewind||70)):c.next);
   const r=await (await fetch('/api/sync?from='+from)).json();
   step=r.step;
   if(!r.real){syncErr='waiting for the hub clock';break}
   if(c.boot!==r.boot){c={boot:r.boot,next:r.oldest,rewind:r.rewind};first=false;continue}   // hub rebooted
   await saveRows(r);
   c.next=r.next;c.rewind=r.rewind;setCur(c);first=false;
   if(r.next>r.newest||r.next===from)break;                  // caught up
  }
  lastSync=Date.now();if(syncErr!=='waiting for the hub clock')syncErr='';
 }catch(e){syncErr='hub not reachable'}
 finally{syncing=false}
 showStore();
}

async function showStore(){
 if(!db)return;
 try{
  const n=await rq(store().count());
  if(!n){$('store').textContent='Nothing saved on this device yet.'+(syncErr?' ('+syncErr+')':'');return}
  const a=await rq(store().openCursor()),b=await rq(store().openCursor(null,'prev'));
  const ago=lastSync?Math.round((Date.now()-lastSync)/1000):null;
  $('store').textContent=`${n.toLocaleString()} time points saved, ${fmtT(a.key)} to ${fmtT(b.key)}.`+
   (ago!==null?` Last sync ${ago} s ago.`:'')+(syncErr?` (${syncErr})`:'');
 }catch(e){$('store').textContent='Storage error: '+e}
}

async function exportCsv(){
 if(!db)return;
 const sel=$('exp').value,lo=sel==='all'?0:Date.now()-(+sel)*3600000;
 const parts=['time_utc,epoch_ms,node,voltage_V,current_A,power_W,temp_C,status\n'];let buf='',rows=0;
 await new Promise((res,rej)=>{
  const q=store().openCursor(IDBKeyRange.lowerBound(lo));
  q.onsuccess=()=>{const c=q.result;if(!c){res();return}
   const r=c.value,iso=new Date(r.t).toISOString();
   for(const id of Object.keys(r.m).sort((x,y)=>x-y)){const d=r.m[id];rows++;
    buf+=`${iso},${r.t},${id},${(d[0]/100).toFixed(2)},${(d[1]/100).toFixed(2)},${(d[2]/10).toFixed(1)},${(d[3]/100).toFixed(2)},${d[4]}\n`}
   if(buf.length>1e6){parts.push(buf);buf=''}
   c.continue()};
  q.onerror=()=>rej(q.error);
 });
 parts.push(buf);
 if(!rows){alert('No saved data in that range.');return}
 const a=document.createElement('a');a.href=URL.createObjectURL(new Blob(parts,{type:'text/csv'}));
 a.download='lora_'+new Date().toISOString().slice(0,16).replace(/[:T]/g,'-')+'.csv';
 document.body.appendChild(a);a.click();setTimeout(()=>{URL.revokeObjectURL(a.href);a.remove()},10000);
}

async function clearData(){
 if(!db||!confirm('Delete all data saved on this device? (The hub still has its last 8 h.)'))return;
 await rq(store('readwrite').clear());setCur({});showStore();loadHist();
}

// ---------------- chart (from device storage) ----------------
let H=null,hi=-1;const L=52,R=12,T=10,B=26;
const col=(i,n)=>`hsl(${Math.round(i*360/Math.max(n,1))} 70% 50%)`;
async function loadHist(){
 if(!db)return;
 const win=+$('win').value*60000,now=Date.now();
 let recs=[];try{recs=await rq(store().getAll(IDBKeyRange.lowerBound(now-win)))}catch(e){}
 const k={v:0,a:1,w:2,t:3}[$('met').value],sc=[100,100,10,100][k];
 const bucket=Math.max(step,Math.ceil(win/600/step)*step),t0=Math.floor((now-win)/bucket)*bucket,n=Math.ceil((now-t0)/bucket);
 const sums={},cnts={};
 for(const r of recs){const b=Math.floor((r.t-t0)/bucket);if(b<0||b>=n)continue;
  for(const id in r.m){(sums[id]??=new Float64Array(n))[b]+=r.m[id][k]/sc;(cnts[id]??=new Uint32Array(n))[b]++}}
 H={t0,step:bucket,n,series:Object.keys(sums).map(Number).sort((x,y)=>x-y)
   .map(id=>({id,v:Array.from(sums[id],(s,i)=>cnts[id][i]?s/cnts[id][i]:null)}))};
 draw();if(hi>=0)tipAt(hi);
}
function tl(i,sec){const o={hour:'2-digit',minute:'2-digit'};if(sec)o.second='2-digit';return new Date(H.t0+i*H.step).toLocaleTimeString([],o)}
function draw(){
 const c=$('cv'),x=c.getContext('2d'),dpr=devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
 c.width=w*dpr;c.height=h*dpr;x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,h);
 x.font='11px system-ui,sans-serif';x.fillStyle=css('--muted');
 if(!H||!H.n||!H.series.length){x.fillText('No saved data in this window yet',L,30);return}
 let lo=1/0,up=-1/0;
 for(const s of H.series)for(const v of s.v)if(v!==null){if(v<lo)lo=v;if(v>up)up=v}
 if(lo===1/0){x.fillText('No saved data in this window yet',L,30);return}
 if(up-lo<1e-6){up+=1;lo-=1}const p=(up-lo)*.06;lo-=p;up+=p;
 const pw=w-L-R,ph=h-T-B,X=i=>L+pw*i/Math.max(H.n-1,1),Y=v=>T+ph*(1-(v-lo)/(up-lo));
 x.strokeStyle=css('--line');x.lineWidth=1;
 for(let k=0;k<=4;k++){const v=lo+(up-lo)*k/4,y=Y(v);x.beginPath();x.moveTo(L,y);x.lineTo(w-R,y);x.stroke();x.fillText(v.toFixed(1),4,y+4)}
 for(let k=0;k<=4;k++){const i=Math.round((H.n-1)*k/4),s=tl(i,false),tw=x.measureText(s).width;x.fillText(s,Math.min(Math.max(X(i)-tw/2,L),w-R-tw),h-8)}
 H.series.forEach((s,si)=>{
  x.strokeStyle=col(si,H.series.length);x.lineWidth=1.5;x.beginPath();let on=false;
  s.v.forEach((v,i)=>{if(v===null){on=false;return}on?x.lineTo(X(i),Y(v)):x.moveTo(X(i),Y(v));on=true});x.stroke();
 });
 if(hi>=0&&hi<H.n){x.strokeStyle=css('--fg');x.globalAlpha=.5;x.beginPath();x.moveTo(X(hi),T);x.lineTo(X(hi),T+ph);x.stroke();x.globalAlpha=1}
}
function tipAt(i){
 if(!H||!H.n)return;
 const r=H.series.map((s,si)=>({id:s.id,v:s.v[i],c:col(si,H.series.length)})).filter(o=>o.v!==null&&o.v!==undefined);
 $('tip').innerHTML=`<b>${tl(i,true)}</b> &nbsp; `+(r.length?r.map(o=>`<span style="color:${o.c};font-weight:600">N${o.id}</span> ${o.v.toFixed(2)}`).join(' &middot; '):'no data');
}
$('cv').addEventListener('pointermove',e=>{
 if(!H||!H.n)return;const b=e.currentTarget.getBoundingClientRect(),pw=b.width-L-R;
 hi=Math.max(0,Math.min(H.n-1,Math.round((e.clientX-b.left-L)/pw*(H.n-1))));draw();tipAt(hi);
});
$('cv').addEventListener('pointerleave',()=>{hi=-1;draw()});
$('met').onchange=loadHist;$('win').onchange=loadHist;window.onresize=draw;

// ---------------- start ----------------
(async()=>{
 try{await fetch('/api/time?ms='+Date.now())}catch(e){}       // set the hub clock from this device
 try{db=await openDB()}catch(e){$('store').textContent='This browser blocked storage: data will not be saved here.'}
 try{navigator.storage&&navigator.storage.persist&&navigator.storage.persist()}catch(e){}
 tick();setInterval(tick,1000);
 await sync();loadHist();
 setInterval(async()=>{await sync();loadHist()},5000);
 document.addEventListener('visibilitychange',()=>{if(!document.hidden)sync().then(loadHist)});
})();
</script></body></html>)HTML";
