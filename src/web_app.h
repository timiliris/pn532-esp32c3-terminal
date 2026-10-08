#pragma once

// Web app served by the terminal at "/". It only talks to the API (/api/v1), like any
// other client would. English and French, picked from the browser language.
static const char WEB_APP[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RFID Terminal</title>
<style>
:root{--bg:#0f1115;--panel:#171a21;--line:#262b35;--text:#e6e8ec;--dim:#8b93a3;--acc:#4f8cff;--ok:#2fbf71;--err:#e5484d;--warn:#e0a526;--field:#0c0e12;--tab:#232838}
@media (prefers-color-scheme:light){:root{--bg:#f4f5f7;--panel:#fff;--line:#e1e4ea;--text:#1b1f27;--dim:#667085;--field:#f8f9fb;--tab:#e8ecf4}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}
main{max-width:760px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;gap:10px;margin:4px 0 14px}
header h1{font-size:19px;margin:0;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
nav{display:flex;gap:4px;background:var(--panel);border:1px solid var(--line);border-radius:9px;padding:3px;margin-bottom:14px}
nav button{flex:1;margin:0;background:transparent;color:var(--dim);padding:8px}
nav button.on{background:var(--tab);color:var(--text)}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);margin:0 0 12px}
section{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:14px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--dim);display:inline-block;flex:none}
.dot.on{background:var(--ok)}.dot.err{background:var(--err)}
.grid{display:grid;grid-template-columns:110px 1fr;gap:6px 12px}.grid>span:nth-child(odd){color:var(--dim)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
label{display:block;color:var(--dim);font-size:13px;margin:10px 0 4px}
input,select{width:100%;background:var(--field);border:1px solid var(--line);color:var(--text);border-radius:7px;padding:9px 10px;font:inherit}
input[type=range]{padding:9px 0}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end}.row>*{flex:1;min-width:110px}
.actions{display:flex;gap:10px;flex-wrap:wrap}
button{background:var(--acc);border:0;color:#fff;border-radius:7px;padding:10px 16px;font:inherit;font-weight:600;cursor:pointer;margin-top:12px}
button.ghost{background:transparent;border:1px solid var(--line);color:var(--text)}
button.danger{background:transparent;border:1px solid var(--err);color:var(--err)}
button.small{padding:6px 10px;margin:0;font-weight:500}
button:disabled{opacity:.5;cursor:default}
#banner{display:none;padding:10px 12px;border-radius:8px;margin-bottom:14px;border:1px solid var(--line);align-items:center;justify-content:space-between;gap:10px;background:var(--panel)}
#banner.waiting,#banner.running{display:flex;border-color:var(--warn)}
#banner.done{display:flex;border-color:var(--ok)}
#banner.error,#banner.timeout,#banner.cancelled{display:flex;border-color:var(--err)}
#banner button{margin:0}
table{width:100%;border-collapse:collapse;font-size:13px}
td,th{padding:3px 8px;border-bottom:1px solid var(--line);text-align:left;white-space:nowrap}
th{color:var(--dim);font-weight:500}
.tbl{overflow-x:auto;margin-top:12px}.sys{color:var(--dim)}
.rec{background:var(--field);border:1px solid var(--line);border-radius:7px;padding:10px;margin-top:10px;word-break:break-all}
.rec small{color:var(--dim);display:block;margin-bottom:2px}
.hint{color:var(--dim);font-size:13px;margin-top:8px}
.list>div{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid var(--line);gap:10px}
.list>div:last-child{border:0}
.code{font-size:28px;letter-spacing:.2em;text-align:center}
.hidden{display:none!important}
a{color:var(--acc)}
</style></head><body><main>

<header><span class="dot" id="dot"></span><h1 id="title">RFID Terminal</h1><span class="hint" id="conn"></span></header>

<section id="pairing" class="hidden">
  <h2 data-t="pairTitle"></h2>
  <p data-t="pairText"></p>
  <button onclick="pairStart()" data-t="pairShow"></button>
  <div id="pairStep" class="hidden">
    <label data-t="pairCode"></label>
    <input id="pcode" class="mono code" inputmode="numeric" maxlength="6" autocomplete="one-time-code">
    <label data-t="pairName"></label>
    <input id="pname">
    <button onclick="pairConfirm()" data-t="pairOk"></button>
  </div>
  <div class="hint" id="pairMsg"></div>
</section>

<div id="app" class="hidden">
<nav>
  <button data-tab="badge" class="on" data-t="tabBadge"></button>
  <button data-tab="history" data-t="tabHistory"></button>
  <button data-tab="settings" data-t="tabSettings"></button>
</nav>

<div id="banner"><span id="bannerText"></span><button class="ghost small" id="cancelBtn" onclick="cancelJob()" data-t="cancel"></button></div>

<div data-page="badge">
<section>
  <h2 data-t="badge"></h2>
  <div class="grid">
    <span data-t="presence"></span><span id="pres">-</span>
    <span data-t="type"></span><span id="type">-</span>
    <span>UID</span><span id="uid" class="mono">-</span>
    <span data-t="capacity"></span><span id="cap">-</span>
  </div>
</section>

<section>
  <h2 data-t="read"></h2>
  <label data-t="keyA"></label>
  <input id="rkey" class="mono" placeholder="FFFFFFFFFFFF" maxlength="17">
  <button onclick="job('/read',{key:v('rkey')})" data-t="readBtn"></button>
  <div id="dump"></div>
</section>

<section>
  <h2 data-t="ndefTitle"></h2>
  <div id="records"></div>
  <div class="actions">
    <button class="ghost" onclick="addRecord()" data-t="addRecord"></button>
    <button onclick="writeNdef()" data-t="writeBtn"></button>
  </div>
  <div class="hint" data-t="ndefHint"></div>
</section>

<section>
  <h2 data-t="rawTitle"></h2>
  <div class="row">
    <div style="flex:0 0 100px"><label id="blbl" data-t="blockPage"></label><input id="blk" type="number" min="1" value="4"></div>
    <div style="flex:0 0 100px"><label data-t="format"></label>
      <select id="fmt"><option value="text" data-t="text"></option><option value="hex">Hex</option></select></div>
    <div><label data-t="data"></label><input id="bval" class="mono" data-tp="dataPh"></div>
  </div>
  <label data-t="keyA"></label>
  <input id="wkey" class="mono" placeholder="FFFFFFFFFFFF" maxlength="17">
  <button onclick="writeRaw()" data-t="write"></button>
  <div class="hint" data-t="rawHint"></div>
</section>

<section>
  <h2 data-t="eraseTitle"></h2>
  <p class="hint" style="margin-top:0" data-t="eraseHint"></p>
  <label data-t="keyA"></label>
  <input id="ekey" class="mono" placeholder="FFFFFFFFFFFF" maxlength="17">
  <button class="danger" onclick="if(confirm(t('eraseConfirm')))job('/erase',{key:v('ekey')})" data-t="eraseBtn"></button>
</section>
</div>

<div data-page="history" class="hidden">
<section>
  <h2 data-t="historyTitle"></h2>
  <div class="list" id="hist"></div>
  <button class="ghost" onclick="loadHistory()" data-t="refresh"></button>
</section>
</div>

<div data-page="settings" class="hidden">
<section>
  <h2 data-t="terminal"></h2>
  <div class="row">
    <div><label data-t="name"></label><input id="cname" maxlength="24"></div>
    <div><label data-t="hostname"></label><input id="chost" class="mono" maxlength="24"></div>
  </div>
  <div class="row">
    <div><label data-t="deviceLang"></label><select id="clang"><option value="en">English</option><option value="fr">Français</option></select></div>
    <div><label data-t="brightness"></label><input id="ccontrast" type="range" min="1" max="255"></div>
  </div>
  <button onclick="saveConfig()" data-t="save"></button>
  <div class="hint" data-t="hostHint"></div>
  <div class="grid" style="margin-top:14px" id="infoGrid"></div>
</section>

<section>
  <h2 data-t="msgTitle"></h2>
  <div class="row">
    <div><label data-t="text"></label><input id="dtext" maxlength="120"></div>
    <div style="flex:0 0 90px"><label data-t="seconds"></label><input id="dsec" type="number" value="10" min="0" max="3600"></div>
  </div>
  <button onclick="api('POST','/display',{text:v('dtext'),seconds:+v('dsec')}).catch(err)" data-t="show"></button>
</section>

<section>
  <h2>Wi-Fi</h2>
  <div class="row">
    <div><label data-t="network"></label><input id="ssid" list="nets"><datalist id="nets"></datalist></div>
    <div><label data-t="password"></label><input id="pass" type="password"></div>
  </div>
  <div class="actions">
    <button class="ghost" onclick="scan()" data-t="scan"></button>
    <button onclick="saveWifi()" data-t="saveReboot"></button>
  </div>
</section>

<section>
  <h2 data-t="pairedTitle"></h2>
  <div class="list" id="tokens"></div>
  <button class="ghost" onclick="logout()" data-t="forget"></button>
</section>

<section>
  <h2 data-t="maintenance"></h2>
  <label data-t="firmware"></label>
  <input id="fwfile" type="file" accept=".bin">
  <div class="actions">
    <button onclick="upload()" id="upBtn" data-t="update"></button>
    <button class="ghost" onclick="if(confirm(t('rebootConfirm')))api('POST','/reboot').catch(err)" data-t="reboot"></button>
    <button class="danger" onclick="if(confirm(t('resetConfirm')))api('POST','/factory-reset').then(logout).catch(err)" data-t="factoryReset"></button>
  </div>
  <div class="hint" data-t="resetHint"></div>
  <div class="hint">App: <select id="uilang" style="width:auto;padding:2px 6px"><option value="en">English</option><option value="fr">Français</option></select>
  · <a href="https://github.com/timiliris/pn532-esp32c3-terminal" target="_blank" rel="noopener">GitHub</a></div>
</section>
</div>
</div>

<script>
const I18N={
en:{pairTitle:'Pair this browser',pairText:'The terminal shows a 6-digit code on its display: it proves you are next to it.',pairShow:'Show a code on the terminal',
pairCode:'Code shown',pairName:'Name of this device',pairOk:'Pair',pairValid:'Code valid for 2 minutes.',unreachable:'Terminal unreachable.',
tabBadge:'Badge',tabHistory:'History',tabSettings:'Settings',cancel:'Cancel',badge:'Badge',presence:'Presence',type:'Type',capacity:'Capacity',
read:'Read',keyA:'Key A (MIFARE Classic, optional)',readBtn:'Read the badge',ndefTitle:'Write a message (NDEF)',addRecord:'Add a record',remove:'Remove',
writeBtn:'Write to the badge',ndefHint:'NTAG213 / 215 / 216 and Ultralight, readable by any phone afterwards.',rawTitle:'Raw write',
blockPage:'Block / page',block:'Block',page:'Page',format:'Format',text:'Text',data:'Data',dataPh:'16 bytes max (Classic), 4 (NTAG)',write:'Write',
rawHint:'Block 0, sector trailers (keys) and system pages are protected.',eraseTitle:'Erase',eraseHint:'Clears the badge user area (NTAG: back to an empty NDEF message, as from the factory).',
eraseBtn:'Erase the badge',eraseConfirm:'Erase the next badge presented?',historyTitle:'Recent badges',refresh:'Refresh',noHistory:'No badge yet.',
terminal:'Terminal',name:'Name',hostname:'Network name',deviceLang:'Display language',brightness:'Display brightness',save:'Save',
hostHint:'The network name (name.local) applies after a reboot.',msgTitle:'Message on the display',seconds:'Seconds',show:'Show',network:'Network',password:'Password',
scan:'Scan',saveReboot:'Save and reboot',pairedTitle:'Paired devices',forget:'Forget this browser',revoke:'Revoke',revokeConfirm:'Revoke this device?',
maintenance:'Maintenance',firmware:'Firmware update (.bin)',update:'Update',reboot:'Reboot',factoryReset:'Factory reset',
rebootConfirm:'Reboot the terminal?',resetConfirm:'Erase Wi-Fi, paired devices and settings?',resetHint:'Factory reset is also available by holding the BOOT button for 5 seconds.',
on:'Badge present',off:'No badge',bytes:'bytes NDEF',sectors:'sectors',placeBadge:'place the badge on the terminal',running:'in progress, keep the badge still',
read_:'Read',write_ndef:'NDEF write',write_raw:'Raw write',erase:'Erase',noNdef:'No NDEF message.',link:'Link',record:'Record',sector:'Sector',keyRefused:'key refused',
saved:'Settings saved',wifiSaved:'Saved, the terminal is rebooting.',uploading:'Uploading firmware...',updated:'Updated, the terminal is rebooting.',
reconnecting:'reconnecting...',pairingRequired:'Pairing required',reader:'Reader',display:'Display',address:'Address',signal:'Signal',absent:'missing',ap:'Setup network '},
fr:{pairTitle:'Appairer ce navigateur',pairText:'La borne affiche un code à 6 chiffres sur son écran : il prouve que tu es à côté d\'elle.',pairShow:'Afficher un code sur la borne',
pairCode:'Code affiché',pairName:'Nom de cet appareil',pairOk:'Valider',pairValid:'Code valable 2 minutes.',unreachable:'Borne injoignable.',
tabBadge:'Badge',tabHistory:'Historique',tabSettings:'Réglages',cancel:'Annuler',badge:'Badge',presence:'Présence',type:'Type',capacity:'Capacité',
read:'Lire',keyA:'Clé A (MIFARE Classic, optionnelle)',readBtn:'Lire le badge',ndefTitle:'Écrire un message (NDEF)',addRecord:'Ajouter un enregistrement',remove:'Retirer',
writeBtn:'Écrire sur le badge',ndefHint:'NTAG213 / 215 / 216 et Ultralight, lisibles ensuite par n\'importe quel téléphone.',rawTitle:'Écriture brute',
blockPage:'Bloc / page',block:'Bloc',page:'Page',format:'Format',text:'Texte',data:'Données',dataPh:'16 octets max (Classic), 4 (NTAG)',write:'Écrire',
rawHint:'Le bloc 0, les blocs de clés et les pages système sont protégés.',eraseTitle:'Effacer',eraseHint:'Remet à zéro la zone utilisateur du badge (NTAG : message NDEF vide, comme en sortie d\'usine).',
eraseBtn:'Effacer le badge',eraseConfirm:'Effacer le prochain badge présenté ?',historyTitle:'Derniers badges',refresh:'Actualiser',noHistory:'Aucun badge pour le moment.',
terminal:'Borne',name:'Nom',hostname:'Nom réseau',deviceLang:'Langue de l\'écran',brightness:'Luminosité de l\'écran',save:'Enregistrer',
hostHint:'Le nom réseau (nom.local) s\'applique après un redémarrage.',msgTitle:'Message sur l\'écran',seconds:'Secondes',show:'Afficher',network:'Réseau',password:'Mot de passe',
scan:'Rechercher',saveReboot:'Enregistrer et redémarrer',pairedTitle:'Appareils appairés',forget:'Oublier ce navigateur',revoke:'Révoquer',revokeConfirm:'Révoquer cet appareil ?',
maintenance:'Maintenance',firmware:'Mise à jour du firmware (.bin)',update:'Mettre à jour',reboot:'Redémarrer',factoryReset:'Réinitialiser',
rebootConfirm:'Redémarrer la borne ?',resetConfirm:'Effacer le Wi-Fi, les appareils appairés et les réglages ?',resetHint:'La réinitialisation se fait aussi en maintenant le bouton BOOT 5 secondes.',
on:'Badge posé',off:'Aucun badge',bytes:'octets NDEF',sectors:'secteurs',placeBadge:'pose le badge sur la borne',running:'en cours, ne bouge pas le badge',
read_:'Lecture',write_ndef:'Écriture NDEF',write_raw:'Écriture brute',erase:'Effacement',noNdef:'Aucun message NDEF.',link:'Lien',record:'Enregistrement',sector:'Secteur',keyRefused:'clé refusée',
saved:'Réglages enregistrés',wifiSaved:'Enregistré, la borne redémarre.',uploading:'Envoi du firmware...',updated:'Mis à jour, la borne redémarre.',
reconnecting:'reconnexion...',pairingRequired:'Appairage requis',reader:'Lecteur',display:'Écran',address:'Adresse',signal:'Signal',absent:'absent',ap:'Réseau de configuration '}};

let L='en';
try{L=localStorage.getItem('rfid_lang')||(navigator.language||'').slice(0,2)}catch(e){}
if(!I18N[L])L='en';
const t=k=>I18N[L][k]??I18N.en[k]??k;
function applyLang(){
  document.documentElement.lang=L;
  document.querySelectorAll('[data-t]').forEach(e=>e.textContent=t(e.dataset.t));
  document.querySelectorAll('[data-tp]').forEach(e=>e.placeholder=t(e.dataset.tp));
  $('uilang').value=L;if(!$('pname').value)$('pname').value=L==='fr'?'Navigateur':'Browser';
  setCard(card);
}

const $=id=>document.getElementById(id), v=id=>$(id).value.trim();
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
let tok=null, card=null, curJob=null, ws=null;
try{tok=localStorage.getItem('rfid_token')}catch(e){}

async function api(method,path,body){
  const r=await fetch('/api/v1'+path,{method,headers:{'Authorization':'Bearer '+tok,'Content-Type':'application/json'},
    body:body===undefined?undefined:JSON.stringify(body)});
  if(r.status===401){logout();throw new Error(t('pairingRequired'));}
  if(r.status===204||r.status===202&&!r.headers.get('content-type'))return null;
  const j=await r.json().catch(()=>null);
  if(!r.ok)throw new Error(j&&j.error?j.error.message:'HTTP '+r.status);
  return j;
}
function err(e){banner('error',e.message||e,false);}
function banner(state,text,cancel){$('banner').className=state;$('bannerText').textContent=text;$('cancelBtn').classList.toggle('hidden',!cancel);}

// --- Pairing ---
async function pairStart(){
  try{const r=await fetch('/api/v1/pair/start',{method:'POST'});if(!r.ok)throw 0;
    $('pairStep').classList.remove('hidden');$('pairMsg').textContent=t('pairValid');$('pcode').focus();}
  catch(e){$('pairMsg').textContent=t('unreachable');}
}
async function pairConfirm(){
  const r=await fetch('/api/v1/pair/confirm',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({code:v('pcode'),name:v('pname')})});
  const j=await r.json().catch(()=>({}));
  if(!r.ok){$('pairMsg').textContent=j.error?j.error.message:'Error';return;}
  tok=j.token;try{localStorage.setItem('rfid_token',tok)}catch(e){}
  start();
}
function logout(){tok=null;try{localStorage.removeItem('rfid_token')}catch(e){}if(ws){ws.onclose=null;ws.close();}
  $('app').classList.add('hidden');$('pairing').classList.remove('hidden');}

// --- Live events ---
function connect(){
  ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/api/v1/events?token='+tok);
  ws.onopen=()=>$('conn').textContent='';
  ws.onclose=()=>{$('conn').textContent=t('reconnecting');setTimeout(()=>tok&&connect(),2000);};
  ws.onmessage=m=>{const e=JSON.parse(m.data);handle(e.event,e.data);};
}
function handle(ev,d){
  if(ev==='hello'){info(d.info);setCard(d.card||null);if(d.job)onJob(d.job);}
  else if(ev==='card.present')setCard(d);
  else if(ev==='card.removed')setCard(null);
  else if(ev==='reader.status')$('dot').className='dot '+(d.ok?'':'err');
  else if(ev==='job.updated')onJob(d);
}
function info(i){
  $('title').textContent=i.name;document.title=i.name;
  $('dot').className='dot '+(i.reader.ok?'':'err');
  const n=i.network||{};
  $('infoGrid').innerHTML=[['Firmware',i.firmware],[t('reader'),i.reader.ok?'PN532 '+i.reader.firmware:t('absent')],
    [t('display'),i.display.ok?'OK':t('absent')],[t('network'),(n.mode==='ap'?t('ap'):'')+(n.ssid||'-')],
    [t('address'),n.ip?n.ip+' ('+n.hostname+'.local)':'-'],[t('signal'),n.rssi!==undefined?n.rssi+' dBm':'-']]
    .map(([a,b])=>`<span>${esc(a)}</span><span>${esc(b)}</span>`).join('');
}
function setCard(c){
  card=c;
  $('dot').classList.toggle('on',!!c);
  $('pres').textContent=c?t('on'):t('off');
  $('type').textContent=c?c.type:'-';$('uid').textContent=c?c.uid:'-';
  $('cap').textContent=c&&c.ndef_capacity?c.ndef_capacity+' '+t('bytes'):c&&c.sectors?c.sectors+' '+t('sectors'):'-';
  $('blbl').textContent=c?(c.kind==='type2'?t('page'):t('block')):t('blockPage');
}

// --- Jobs ---
async function job(path,body){
  if(body&&'key'in body&&!body.key)delete body.key;
  try{onJob(await api('POST',path,body));}catch(e){err(e);}
}
function onJob(j){
  curJob=j;
  const label=t(j.kind==='read'?'read_':j.kind)+' : ';
  if(j.state==='waiting')banner('waiting',label+t('placeBadge'),true);
  else if(j.state==='running')banner('running',label+t('running'),false);
  else banner(j.state,label+j.message,false);
  if(j.kind==='read'&&j.state==='done')api('GET','/jobs/'+j.id).then(r=>renderDump(r.result)).catch(err);
}
function cancelJob(){if(curJob)api('DELETE','/jobs/'+curJob.id).then(onJob).catch(err);}
function writeRaw(){
  const b={block:+v('blk'),key:v('wkey')};
  if(v('fmt')==='hex')b.hex=v('bval');else b.text=$('bval').value;
  job('/write/raw',b);
}

// NDEF: editable list of records
function addRecord(type='url',value=''){
  const d=document.createElement('div');d.className='row';
  d.innerHTML=`<div style="flex:0 0 100px"><label>${esc(t('type'))}</label><select><option value="url">URL</option><option value="text">${esc(t('text'))}</option></select></div>
  <div><label>${esc(t('data'))}</label><input></div><div style="flex:0 0 auto"><button class="ghost small">${esc(t('remove'))}</button></div>`;
  const sel=d.querySelector('select'),inp=d.querySelector('input'),ph=()=>inp.placeholder=sel.value==='url'?'https://example.com':'Hello';
  sel.value=type;inp.value=value;ph();sel.onchange=ph;
  d.querySelector('button').onclick=()=>{if($('records').children.length>1)d.remove();};
  $('records').appendChild(d);
}
function writeNdef(){
  const records=[...$('records').children].map(d=>({type:d.querySelector('select').value,value:d.querySelector('input').value}));
  job('/write/ndef',{records});
}

const bytes=h=>h.match(/../g).map(x=>parseInt(x,16));
const ascii=h=>bytes(h).map(c=>c>=32&&c<127?String.fromCharCode(c):'.').join('');
const safeUrl=u=>/^(https?:|mailto:|tel:)/i.test(u)?u:'#';
function renderDump(r){
  if(!r){$('dump').innerHTML='';return;}
  let h='';
  if(r.kind==='type2'){
    (r.ndef||[]).forEach(x=>h+=`<div class="rec"><small>${x.type==='url'?t('link'):x.type==='text'?t('text')+' ('+esc(x.lang)+')':t('record')+' '+esc(x.record_type)}</small>${
      x.type==='url'?`<a href="${esc(safeUrl(x.value))}" target="_blank" rel="noopener">${esc(x.value)}</a>`:esc(x.value)}</div>`);
    if(!(r.ndef||[]).length)h+=`<div class="hint">${esc(t('noNdef'))}</div>`;
    h+='<div class="tbl"><table class="mono"><tr><th>'+esc(t('page'))+'</th><th>Hex</th><th>ASCII</th></tr>';
    r.pages.forEach((p,i)=>h+=`<tr class="${i<4?'sys':''}"><td>${i}</td><td>${p.replace(/(..)/g,'$1 ')}</td><td>${esc(ascii(p))}</td></tr>`);
  }else{
    h+='<div class="tbl"><table class="mono"><tr><th>'+esc(t('block'))+'</th><th>Hex</th><th>ASCII</th></tr>';
    for(const s of r.sectors){
      if(s.locked){h+=`<tr class="sys"><td colspan="3">${esc(t('sector'))} ${s.sector} : ${esc(t('keyRefused'))}</td></tr>`;continue;}
      const first=s.sector<32?s.sector*4:128+(s.sector-32)*16;
      s.blocks.forEach((b,i)=>{const n=first+i,sys=n===0||i===s.blocks.length-1;
        h+=`<tr class="${sys?'sys':''}"><td>${n}</td><td>${b?b.replace(/(..)/g,'$1 '):'-'}</td><td>${b?esc(ascii(b)):''}</td></tr>`;});
    }
  }
  $('dump').innerHTML=h+'</table></div>';
}

// --- History and settings ---
const ago=s=>L==='fr'?(s<60?'il y a '+s+' s':s<3600?'il y a '+Math.floor(s/60)+' min':'il y a '+Math.floor(s/3600)+' h')
  :(s<60?s+' s ago':s<3600?Math.floor(s/60)+' min ago':Math.floor(s/3600)+' h ago');
async function loadHistory(){
  try{const l=await api('GET','/history');
    $('hist').innerHTML=l.length?l.map(x=>`<div><span><span class="mono">${esc(x.uid)}</span><br><small class="hint">${esc(x.type)}</small></span>
      <small class="hint">${x.at?new Date(x.at*1000).toLocaleString(L):ago(x.seconds_ago)}</small></div>`).join(''):`<div class="hint">${esc(t('noHistory'))}</div>`;
  }catch(e){err(e);}
}
async function loadSettings(){
  try{const c=await api('GET','/config');$('cname').value=c.name;$('chost').value=c.hostname;$('clang').value=c.lang;$('ccontrast').value=c.contrast;
    const k=await api('GET','/tokens');
    $('tokens').innerHTML=k.map(x=>`<div><span>${esc(x.name)}<br><small class="hint mono">${esc(x.id)}</small></span>
      <button class="ghost small" data-id="${esc(x.id)}">${esc(t('revoke'))}</button></div>`).join('');
    $('tokens').querySelectorAll('button').forEach(b=>b.onclick=()=>revoke(b.dataset.id));
    info(await api('GET','/info'));
  }catch(e){err(e);}
}
function revoke(id){if(confirm(t('revokeConfirm')))api('DELETE','/tokens/'+encodeURIComponent(id)).then(loadSettings).catch(err);}
async function saveConfig(){
  try{const c=await api('PUT','/config',{name:v('cname'),hostname:v('chost'),lang:$('clang').value,contrast:+$('ccontrast').value});
    $('title').textContent=c.name;banner('done',t('saved'),false);}catch(e){err(e);}
}
async function scan(){
  try{let r;for(let i=0;i<10;i++){r=await api('GET','/wifi/scan');if(r&&r.networks)break;await new Promise(f=>setTimeout(f,1000));}
    if(r&&r.networks)$('nets').innerHTML=r.networks.map(n=>`<option value="${esc(n.ssid)}">${n.rssi} dBm</option>`).join('');$('ssid').focus();
  }catch(e){err(e);}
}
async function saveWifi(){try{await api('PUT','/wifi',{ssid:v('ssid'),password:$('pass').value});banner('done',t('wifiSaved'),false);}catch(e){err(e);}}
async function upload(){
  const f=$('fwfile').files[0];if(!f)return;
  const fd=new FormData();fd.append('firmware',f);
  $('upBtn').disabled=true;banner('running',t('uploading'),false);
  try{const r=await fetch('/api/v1/update',{method:'POST',headers:{'Authorization':'Bearer '+tok},body:fd});
    const j=await r.json().catch(()=>({}));
    if(!r.ok)throw new Error(j.error?j.error.message:'HTTP '+r.status);
    banner('done',t('updated'),false);setTimeout(()=>location.reload(),8000);
  }catch(e){err(e);}finally{$('upBtn').disabled=false;}
}

document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('on',x===b));
  document.querySelectorAll('[data-page]').forEach(p=>p.classList.toggle('hidden',p.dataset.page!==b.dataset.tab));
  if(b.dataset.tab==='history')loadHistory();
  if(b.dataset.tab==='settings')loadSettings();
});
$('uilang').onchange=e=>{L=e.target.value;try{localStorage.setItem('rfid_lang',L)}catch(x){}applyLang();};

function start(){
  $('pairing').classList.add('hidden');$('app').classList.remove('hidden');
  if(!$('records').children.length)addRecord();
  connect();
}
applyLang();
tok?start():logout();
</script>
</main></body></html>
)HTML";
