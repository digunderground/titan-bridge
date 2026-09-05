/* webui.h — the single-page console, served from :80. Kept in PROGMEM.

   Three tabs:
     Remote    a virtual remote, plus buttons you assign yourself. The landing
               page, because when something is wrong this is what you want.
     Macros    grouped list, the recorder, and a step editor that treats a
               script as chips rather than a line of text.
     Settings  key channel, power, link state, log, and the raw escape hatches.

   Everything talks to the same REST API the hub and Home Assistant use, so
   nothing here is a private path that can rot separately.                    */
#pragma once
#include <Arduino.h>

static const char UI_HTML[] PROGMEM = R"HTML(<!doctype html>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Titan Bridge</title>
<style>
:root{--bg:#0f1115;--pan:#171a21;--ln:#252a35;--tx:#e6e9ef;--dim:#8b93a7;--ac:#5aa9ff;--ok:#3ddc97;--wn:#ffb454;--er:#ff6b6b}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:14px/1.5 ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:14px 18px;border-bottom:1px solid var(--ln);display:flex;gap:14px;align-items:baseline;flex-wrap:wrap}
h1{font-size:16px;margin:0;letter-spacing:.02em}
.sub{color:var(--dim);font-size:12px}
main{max-width:1000px;margin:0 auto;padding:18px;display:grid;gap:16px}
.card{background:var(--pan);border:1px solid var(--ln);border-radius:10px;padding:14px 16px}
.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:var(--dim);margin:0 0 10px}
.grid{display:flex;flex-wrap:wrap;gap:8px}
button{background:#222736;color:var(--tx);border:1px solid var(--ln);border-radius:7px;padding:8px 12px;font:inherit;cursor:pointer}
button:hover{border-color:var(--ac);color:var(--ac)}
button.pri{background:var(--ac);border-color:var(--ac);color:#06121f;font-weight:600}
button.dg{border-color:#4a2530;color:#ff9b9b}
button.rec{background:#4a1f27;border-color:#7a3040;color:#ffb3bd}
button.on{background:var(--ac);border-color:var(--ac);color:#06121f}
input,textarea,select{background:#0d1017;color:var(--tx);border:1px solid var(--ln);border-radius:7px;padding:8px 10px;font:inherit;width:100%}
textarea{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;min-height:56px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.row>input,.row>select{flex:1;min-width:150px}
table{width:100%;border-collapse:collapse;font-size:13px}
td{padding:4px 8px 4px 0;vertical-align:top}
td.k{color:var(--dim);white-space:nowrap;width:34%}
pre{background:#0b0e14;border:1px solid var(--ln);border-radius:7px;padding:10px;overflow:auto;max-height:280px;font-size:11.5px;line-height:1.45;margin:0}
.pill{display:inline-block;padding:1px 8px;border-radius:99px;font-size:11px;border:1px solid var(--ln)}
.awake,.on\.assumed{color:var(--ok);border-color:#1e4d3a}.asleep{color:var(--dim)}.unknown{color:var(--wn);border-color:#4d3a1e}
.mac{border-top:1px solid var(--ln);padding:10px 0}
.mac:first-child{border-top:0}
.mac code{color:var(--dim);font-size:11.5px;word-break:break-all}
.nav{display:grid;grid-template-columns:repeat(3,58px);gap:6px;justify-content:center}
.nav button{padding:12px 0}
.hide{display:none}
/* tabs */
nav.tabs{display:flex;gap:4px;padding:0 18px;border-bottom:1px solid var(--ln)}
nav.tabs button{border:0;border-bottom:2px solid transparent;border-radius:0;background:none;padding:10px 14px;color:var(--dim)}
nav.tabs button.sel{color:var(--tx);border-bottom-color:var(--ac)}
/* macro step chips */
.chips{display:flex;flex-wrap:wrap;gap:0;align-items:center;margin:8px 0}
.chip{background:#222736;border:1px solid var(--ln);border-radius:6px;padding:3px 6px;font:12px ui-monospace,Menlo,monospace;display:inline-flex;gap:6px;align-items:center}
.chip.sel{border-color:var(--ac);color:var(--ac)}
.chip.gap{background:none;border-style:dashed;color:var(--dim)}
.chip b{cursor:pointer}
.chip i{cursor:pointer;font-style:normal;color:var(--dim)}
.chip i:hover{color:var(--er)}
.ins{width:16px;height:22px;border:0;background:none;color:#3a4256;cursor:pointer;padding:0;font-size:15px;line-height:1}
.ins:hover{color:var(--ac)}
.grp{margin:0 0 4px;font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:var(--dim)}
/* ------------------------- virtual remote -------------------------- */
.remote{max-width:340px;margin:0 auto;background:#12151c;border:1px solid var(--ln);
  border-radius:28px;padding:18px 16px 22px;display:grid;gap:14px;
  box-shadow:0 18px 40px rgba(0,0,0,.45)}
.rrow{display:flex;gap:10px;justify-content:center;flex-wrap:wrap}
.circ{width:46px;height:46px;border-radius:50%;padding:0;display:inline-flex;
  align-items:center;justify-content:center;background:#232838;border:1px solid #2c3346;
  font-size:15px}
.circ:hover{border-color:var(--ac);color:var(--ac)}
.circ.pwr{background:#4a1620;border-color:#7a2634;color:#ff8b96}
.circ.pwr:hover{background:#5e1b28;border-color:#ff6b6b;color:#ffb3bd}
.pillb{border-radius:22px;padding:9px 16px;background:#232838;border:1px solid #2c3346}
/* ring d-pad: four wedges around a hub, the way a real remote reads */
.dpad{position:relative;width:212px;height:212px;margin:2px auto;border-radius:50%;
  overflow:hidden;background:#3d2a72}
.seg{position:absolute;inset:0;width:100%;height:100%;border:0;border-radius:0;
  background:#6d3ff0;color:#fff;font-size:20px;display:flex;padding:0;cursor:pointer}
.seg:hover{background:#8257ff;color:#fff}
.seg:active{background:#5a2fd0}
.seg.up{clip-path:polygon(50% 50%,0 0,100% 0);align-items:flex-start;justify-content:center;padding-top:16px}
.seg.dn{clip-path:polygon(50% 50%,100% 100%,0 100%);align-items:flex-end;justify-content:center;padding-bottom:16px}
.seg.lf{clip-path:polygon(50% 50%,0 100%,0 0);align-items:center;justify-content:flex-start;padding-left:16px}
.seg.rt{clip-path:polygon(50% 50%,100% 0,100% 100%);align-items:center;justify-content:flex-end;padding-right:16px}
/* The wedges meet on the diagonals, so the seams are the two diagonals — and
   a line has to be ~1.5x the box to reach corner to corner once rotated. */
.xh,.xv{position:absolute;left:50%;top:50%;width:150%;height:2px;
  background:#12151c;pointer-events:none;transform-origin:center}
.xh{transform:translate(-50%,-50%) rotate(45deg)}
.xv{transform:translate(-50%,-50%) rotate(-45deg)}
.okb{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);
  width:96px;height:96px;border-radius:50%;background:#2a1c52;border:3px solid #12151c;
  color:#fff;font-weight:600;font-size:16px;letter-spacing:.04em}
.okb:hover{background:#3a2770;color:#fff;border-color:#12151c}
.rocker{display:flex;flex-direction:column;background:#232838;border:1px solid #2c3346;
  border-radius:24px;overflow:hidden}
.rocker button{border:0;border-radius:0;background:none;padding:9px 15px}
.rocker span{height:1px;background:#2c3346}
.rlabel{text-align:center;font-size:10px;color:var(--dim);letter-spacing:.1em;text-transform:uppercase}
.warn{color:var(--wn)}
</style>
<header>
  <h1>Titan Bridge</h1>
  <span class=sub id=hdr>connecting…</span>
</header>
<nav class=tabs>
  <button id=t_remote class=sel onclick="tab('remote')">Remote</button>
  <button id=t_macros onclick="tab('macros')">Macros</button>
  <button id=t_settings onclick="tab('settings')">Settings</button>
</nav>

<!-- ============================== REMOTE ============================== -->
<main id=p_remote>
  <div class=remote>

    <div class=rrow>
      <button class="circ pwr" title="Power toggle" onclick="go('/api/power?state=toggle')">⏻</button>
      <button class=circ title="Power on"  onclick="go('/api/power?state=on')">On</button>
      <button class=circ title="Power off" onclick="go('/api/power?state=off')">Off</button>
      <button class=circ title="Mute"      onclick="go('/api/nav?name=mute')">🔇</button>
    </div>

    <div class=rrow>
      <button class=pillb onclick="go('/api/nav?name=menu')">Menu</button>
      <button class=pillb onclick="go('/api/nav?name=back')">Back</button>
    </div>

    <div class=dpad>
      <button class="seg up" onclick="go('/api/nav?name=up')">▲</button>
      <button class="seg rt" onclick="go('/api/nav?name=right')">▶</button>
      <button class="seg dn" onclick="go('/api/nav?name=down')">▼</button>
      <button class="seg lf" onclick="go('/api/nav?name=left')">◀</button>
      <span class=xh></span><span class=xv></span>
      <button class=okb onclick="go('/api/nav?name=ok')">OK</button>
    </div>

    <div class=rrow style=align-items:flex-start;gap:26px>
      <div>
        <div class=rocker>
          <button onclick="go('/api/nav?name=volup')">+</button><span></span>
          <button onclick="go('/api/nav?name=voldn')">−</button>
        </div>
        <div class=rlabel style=margin-top:6px>Vol</div>
      </div>
      <div>
        <div class=rocker>
          <button onclick="go('/api/hid?key=focus%2B')">+</button><span></span>
          <button onclick="go('/api/hid?key=focus-')">−</button>
        </div>
        <div class=rlabel style=margin-top:6px>Focus</div>
      </div>
    </div>

    <div class=rrow>
      <span class=sub id=pwrnote></span>
    </div>
  </div>

  <div class=card>
    <h2>My buttons <span class=sub id=editnote></span></h2>
    <div class=grid id=mybtns></div>
    <div class=row style=margin-top:10px>
      <button id=editbtn onclick=toggleEdit()>Edit</button>
      <button id=addbtn class="hide" onclick=addButton()>+ Add button</button>
      <span class=sub>Assign any action: a key (<code>k:up</code>), a macro
        (<code>m:movie</code>), or a serial command (<code>s:hdmi1</code>).</span>
    </div>
  </div>
</main>

<!-- ============================== MACROS ============================== -->
<main id=p_macros class=hide>
  <div class=card>
    <h2>Recorder</h2>
    <div class=row>
      <button id=recbtn onclick=recToggle()>● Record</button>
      <button onclick="rec('clear')">Clear</button>
      <span class=sub id=recnote>Press buttons on the Remote tab while recording.
        Gaps are measured, so the delays are the ones the OSD actually kept up with.</span>
    </div>
    <div class=chips id=recsteps></div>
    <div class=row style=margin-top:8px>
      <input id=rn placeholder="macro name">
      <input id=rg placeholder="group (optional)" list=grouplist>
      <button class=pri onclick=recSave()>Save as macro</button>
    </div>
  </div>

  <div class=card>
    <h2>Macros</h2>
    <div id=macs></div>
  </div>

  <div class=card>
    <h2>Editor</h2>
    <div class=row>
      <input id=mn placeholder="name" style=max-width:200px>
      <input id=mg placeholder="group" list=grouplist style=max-width:200px>
      <button class=pri onclick=saveMacro()>Save</button>
      <button onclick=runEditor()>Run</button>
      <button onclick="go('/api/macabort')">Abort</button>
    </div>
    <div class=chips id=edsteps></div>
    <div class=row style=margin-top:6px>
      <textarea id=msc placeholder="anchor; k:down*3; k:ok; k:back*3"
                oninput=syncFromText()></textarea>
    </div>
    <p class=sub style=margin:6px_0_0>Chips and text are the same script — edit
      either. <code>k:</code> follows the active key channel, <code>h:</code> and
      <code>s:</code> force HID or serial, <code>m:</code> runs another macro,
      <code>d500</code> waits, <code>tok*3</code> repeats.</p>
  </div>
  <datalist id=grouplist></datalist>
</main>

<!-- ============================= SETTINGS ============================= -->
<main id=p_settings class=hide>
  <div class=card>
    <h2>Key channel</h2>
    <div class=row>
      <button id=chser onclick="go('/api/keychan?mode=serial')">Serial</button>
      <button id=chhid onclick="go('/api/keychan?mode=hid')">HID</button>
      <span class=sub id=kc></span>
    </div>
    <p class=sub style=margin:8px_0_0>Navigation travels over whichever channel
      this projector honours. Explicit <code>h:</code> and <code>s:</code> macro
      steps ignore this and go where they say.</p>
  </div>

  <div class=card>
    <h2>Power state</h2>
    <div class=row>
      <span class=sub>If the state below is wrong — someone used the real remote —
        correct it without sending anything:</span>
      <button onclick="go('/api/power?state=sync&is=on')">It's on</button>
      <button onclick="go('/api/power?state=sync&is=off')">It's off</button>
    </div>
    <p class=sub id=obsnote style=margin:8px_0_0></p>
  </div>

  <div class=card>
    <h2>Status</h2>
    <table id=st></table>
  </div>

  <div class=card>
    <h2>Serial commands</h2>
    <p class=sub id=serialnote style=margin:0_0_8px></p>
    <div class=grid id=quick></div>
  </div>

  <div class=card>
    <h2>Raw frame</h2>
    <div class=row>
      <input id=raw placeholder="2A2A 02 01 01 04">
      <button onclick="go('/api/raw?hex='+encodeURIComponent(v('raw')))">Send</button>
    </div>
    <div class=row style=margin-top:8px>
      <input id=hidr placeholder="HID usage hex, e.g. 66">
      <button onclick="go('/api/hidraw?usage='+encodeURIComponent(v('hidr')))">Send HID usage</button>
    </div>
    <p class=sub style=margin:8px_0_0>Checksums are not recalculated — bytes go out as typed.</p>
  </div>

  <div class=card>
    <h2>Infrared</h2>
    <div id=irmaps></div>
    <p class=sub id=irlast></p>
    <div class=row style=margin-top:8px>
      <input id=irc placeholder="code, e.g. 21DE:4D" style=max-width:180px>
      <input id=ira placeholder="action, e.g. m:movie">
      <button onclick="go('/api/irmap?code='+encodeURIComponent(v('irc'))+'&action='+encodeURIComponent(v('ira')))">Bind</button>
    </div>
  </div>

  <div class=card id=wifiCard>
    <h2>Wi-Fi</h2>
    <div class=row><input id=ss placeholder=SSID><input id=pw placeholder=password type=password>
      <button class=pri onclick=saveWifi()>Save</button></div>
  </div>

  <div class=card>
    <h2>Log</h2>
    <pre id=log></pre>
    <div class=row style=margin-top:10px>
      <button onclick="go('/api/reboot')">Reboot</button>
      <button class=dg onclick="if(confirm('Forget Wi-Fi and reboot into setup mode?'))go('/api/forget')">Forget Wi-Fi</button>
    </div>
  </div>
</main>

<script>
const $=i=>document.getElementById(i), v=i=>$(i).value.trim();
function esc(s){return String(s).replace(/[<>&"]/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]))}

/* ------------------------------- tabs -------------------------------- */
let curTab='remote';
function tab(n){
  curTab=n;
  for(const t of ['remote','macros','settings']){
    $('p_'+t).classList.toggle('hide',t!==n);
    $('t_'+t).classList.toggle('sel',t===n);
  }
  if(n==='macros'){loadMacros();pollRec();}
  if(n==='settings'){loadIr();}
}

async function go(u){try{await fetch(u,{method:'POST'});}catch(e){}refresh();}

/* ----------------------------- recorder ------------------------------ */
let recOn=false;
async function rec(state,extra){
  const u='/api/rec?state='+state+(extra||'');
  try{const r=await fetch(u,{method:'POST'});renderRec(await r.json());}catch(e){}
}
async function pollRec(){try{renderRec(await (await fetch('/api/rec')).json());}catch(e){}}
function recToggle(){rec(recOn?'stop':'start');}
function renderRec(d){
  recOn=d.recording;
  const b=$('recbtn');
  b.textContent=recOn?'■ Stop':'● Record';
  b.className=recOn?'rec':'';
  $('recsteps').innerHTML=d.steps.length
    ? d.steps.map((s,i)=>chipHtml(s.tok,s.gap,i,'recEdit','recDel')).join('')
    : '<span class=sub>nothing recorded yet</span>';
}
async function recDel(i){await fetch('/api/recstep?op=del&idx='+i,{method:'POST'});pollRec();}
async function recEdit(i){
  const t=prompt('Step token (e.g. k:down, d400, m:movie):');
  if(t===null)return;
  await fetch('/api/recstep?op=ins&idx='+i+'&tok='+encodeURIComponent(t)+'&gap=0',{method:'POST'});
  pollRec();
}
async function recSave(){
  if(!v('rn'))return alert('name required');
  await rec('save','&name='+encodeURIComponent(v('rn'))+'&group='+encodeURIComponent(v('rg')));
  loadMacros();
}

/* --------------------------- step chips ------------------------------ */
/* A script is a list of tokens. Rendering them as chips makes insert,
   delete and reorder obvious operations rather than careful text editing. */
function chipHtml(tok,gap,i,editFn,delFn){
  const g=gap?`<span class="chip gap">${gap}ms</span>`:'';
  return `<button class=ins onclick="${editFn}(${i})" title="insert before">+</button>${g}
    <span class=chip><b onclick="${editFn}(${i})">${esc(tok)}</b><i onclick="${delFn}(${i})">×</i></span>`;
}

/* ----------------------------- editor -------------------------------- */
let edToks=[];
function parseScript(s){return s.split(';').map(x=>x.trim()).filter(x=>x.length);}
function renderEd(){
  $('edsteps').innerHTML=edToks.length
    ? edToks.map((t,i)=>chipHtml(t,0,i,'edIns','edDel')).join('')
      +`<button class=ins onclick="edIns(${edToks.length})" title="append">+</button>`
    : '<span class=sub>no steps — type a script below, or add one with +</span>';
  $('msc').value=edToks.join('; ');
}
function edDel(i){edToks.splice(i,1);renderEd();}
function edIns(i){
  const t=prompt('Token to insert (k:down, d400, m:movie, anchor, k:down*3):');
  if(t===null||!t.trim())return;
  edToks.splice(i,0,t.trim());renderEd();
}
function syncFromText(){edToks=parseScript($('msc').value);
  $('edsteps').innerHTML=edToks.map((t,i)=>chipHtml(t,0,i,'edIns','edDel')).join('')
    +`<button class=ins onclick="edIns(${edToks.length})">+</button>`;}
function editMacro(name){
  const m=(window._macs||[]).find(x=>x.name===name); if(!m)return;
  $('mn').value=m.name; $('mg').value=m.group==='built-in'?'':m.group;
  edToks=parseScript(m.script); renderEd();
  $('mn').scrollIntoView({behavior:'smooth',block:'center'});
}
async function saveMacro(){
  if(!v('mn'))return alert('name required');
  if(!edToks.length)return alert('no steps');
  await fetch('/api/macdef?name='+encodeURIComponent(v('mn'))
    +'&group='+encodeURIComponent(v('mg'))
    +'&script='+encodeURIComponent(edToks.join('; ')),{method:'POST'});
  loadMacros();
}
async function runEditor(){
  await fetch('/api/macro?script='+encodeURIComponent(edToks.join('; ')),{method:'POST'});
}

/* ---------------------------- macro list ----------------------------- */
async function loadMacros(){
  let m;try{m=await (await fetch('/api/macros')).json();}catch(e){return;}
  window._macs=m;
  const groups={};
  for(const x of m){const g=x.group||'ungrouped';(groups[g]=groups[g]||[]).push(x);}
  const names=Object.keys(groups).sort((a,b)=>a==='built-in'?1:b==='built-in'?-1:a.localeCompare(b));
  $('macs').innerHTML=names.map(g=>`<p class=grp>${esc(g)}</p>`+groups[g].map(x=>`
    <div class=mac><div class=row>
      <button class=pri onclick="go('/api/macro?name=${encodeURIComponent(x.name)}')">${esc(x.name)}</button>
      <span class=sub>${esc(x.desc)}</span>
      <span style=margin-left:auto>
        <button onclick="editMacro('${esc(x.name).replace(/'/g,"\\'")}')">Edit</button>
        ${x.user?`<button class=dg onclick="delMacro('${esc(x.name).replace(/'/g,"\\'")}')">Delete</button>`:''}
      </span></div><code>${esc(x.script)}</code></div>`).join('')).join('');
  $('grouplist').innerHTML=names.filter(g=>g!=='built-in'&&g!=='ungrouped')
    .map(g=>`<option value="${esc(g)}">`).join('');
}
async function delMacro(n){
  if(!confirm('Delete macro "'+n+'"?'))return;
  await fetch('/api/macdel?name='+encodeURIComponent(n),{method:'POST'});loadMacros();
}

/* --------------------------- my buttons ------------------------------ */
let editMode=false;
function toggleEdit(){
  editMode=!editMode;
  $('editbtn').textContent=editMode?'Done':'Edit';
  $('editbtn').className=editMode?'on':'';
  $('addbtn').classList.toggle('hide',!editMode);
  $('editnote').textContent=editMode?'— click a button to reassign it':'';
  loadButtons();
}
async function loadButtons(){
  let b;try{b=await (await fetch('/api/buttons')).json();}catch(e){return;}
  $('mybtns').innerHTML=b.length?b.map(x=>
    `<button onclick="${editMode?`editButton('${esc(x.id)}')`:`go('/api/press?id=${encodeURIComponent(x.id)}')`}">
       ${esc(x.label||x.id)}</button>`).join('')
    :'<span class=sub>none yet — press Edit, then Add button</span>';
}
async function addButton(){
  const id='b'+Date.now().toString(36);
  const label=prompt('Button label (e.g. HDMI1, Movie night):'); if(!label)return;
  const action=prompt('Action (k:up, m:movie night, s:hdmi1):'); if(!action)return;
  await fetch('/api/button?id='+id+'&label='+encodeURIComponent(label)
    +'&action='+encodeURIComponent(action),{method:'POST'});
  loadButtons();
}
async function editButton(id){
  const b=await (await fetch('/api/buttons')).json();
  const cur=b.find(x=>x.id===id)||{};
  const label=prompt('Label (empty to delete):',cur.label||'');
  if(label===null)return;
  if(!label){await fetch('/api/button?id='+id+'&label=&action=',{method:'POST'});return loadButtons();}
  const action=prompt('Action:',cur.action||'');
  if(action===null)return;
  await fetch('/api/button?id='+id+'&label='+encodeURIComponent(label)
    +'&action='+encodeURIComponent(action),{method:'POST'});
  loadButtons();
}

/* ------------------------------ serial ------------------------------- */
const QUICK=[['HDMI1','hdmi1'],['HDMI2','hdmi2'],['HDMI3?','hdmi3'],['USB','usbsrc'],
 ['Filmmaker','filmmaker'],['Movie','movie'],['IMAX','imax'],['Vivid','vivid'],
 ['Perf','perf'],['Sport','sport'],['TV','tvmode'],
 ['Bright 3','b3'],['Bright 7','b7'],['Bright 10','b10'],
 ['Blank','blank'],['Unblank','unblank'],
 ['HRR off','hrroff'],['HRR basic','hrrbasic'],['HRR max','hrrmax']];
$('quick').innerHTML=QUICK.map(([l,c])=>`<button onclick="go('/api/cmd?name=${c}')">${l}</button>`).join('');

async function saveWifi(){await fetch('/api/wifi?ssid='+encodeURIComponent(v('ss'))+'&pass='+encodeURIComponent(v('pw')),{method:'POST'});
  alert('Saved. The bridge is rebooting — reconnect to your own network.');}

async function loadIr(){
  let m;try{m=await (await fetch('/api/irmaps')).json();}catch(e){return;}
  $('irmaps').innerHTML=m.length?m.map(x=>`<div class=row><code>${esc(x.code)}</code> → <code>${esc(x.action)}</code>
    <button class=dg onclick="go('/api/irdel?code='+encodeURIComponent('${x.code}')).then(loadIr)">×</button></div>`).join('')
    :'<span class=sub>no bindings yet</span>';
}

/* ------------------------------ refresh ------------------------------ */
async function refresh(){
  let s;try{s=await (await fetch('/api/status')).json();}catch(e){$('hdr').textContent='offline';return;}
  const cls=s.power.startsWith('on')||s.power==='awake'?'awake':
            s.power.startsWith('off')||s.power==='asleep'?'asleep':'unknown';
  $('hdr').innerHTML=`<span class="pill ${cls}">${esc(s.power)}</span> &nbsp;${esc(s.ip)} &nbsp;· up ${s.uptime}s`
    +(s.busy?` &nbsp;· <b>${esc(s.busy)}</b>`:'')+(s.macro?` &nbsp;· macro <b>${esc(s.macro)}</b>`:'');
  $('pwrnote').textContent=s.powerobserved?'state is measured':'state is assumed — correct it in Settings';
  $('chser').className=s.keychan==='serial'?'on':'';
  $('chhid').className=s.keychan==='hid'?'on':'';

  if(curTab!=='settings')return;   // the rest only exists on the Settings tab
  $('kc').textContent='active: '+s.keychan+(s.linkalive?'':' — serial link has never answered');
  $('obsnote').textContent=s.powerobserved
    ? 'The bridge can observe this projector, so on/off are verified.'
    : 'Nothing about this projector is observable: the serial link never binds and '
      +'the USB bus stays awake in standby. The state below is what the bridge last did.';
  $('wifiCard').style.display=s.ap?'block':'none';
  $('serialnote').innerHTML=s.linkalive
    ? 'Serial commands (instructions 0x01/0x03/0x05).'
    : '<b class=warn>Unavailable.</b> These are serial-only and this projector\'s serial '
      +'link has never answered. Reach these functions with recorded menu macros instead.';
  $('quick').style.opacity=s.linkalive?'1':'0.35';
  $('quick').style.pointerEvents=s.linkalive?'auto':'none';
  const rows=[['Power state',s.power],['Observed?',s.powerobserved?'yes':'no — assumed'],
    ['Temperature',s.temp],['Key channel',s.keychan],['Link',s.link],
    ['Serial link',s.linkalive?'alive':'never received a valid frame'],
    ['Native CDC port',s.cdc?'open (host bound it)':'not open'],
    ['Frames sent',s.tx],['Bytes received',s.rx],
    ['Last reply',s.since<0?'never':s.since+' ms ago'],['Last frame',s.lastrx],
    ['Wi-Fi',(s.ap?'setup AP ':'')+esc(s.ssid)+' · '+esc(s.ip)],['Firmware',esc(s.fw)],
    ['Free heap',s.heap]];
  $('st').innerHTML=rows.map(([k,x])=>`<tr><td class=k>${k}</td><td>${x}</td></tr>`).join('');
  $('log').textContent=await (await fetch('/api/log')).text();
  $('log').scrollTop=$('log').scrollHeight;
  if(s.irlast)$('irlast').textContent='last IR code seen: '+s.irlast;
}

loadButtons();refresh();setInterval(()=>{refresh();if(curTab==='macros'&&recOn)pollRec();},2500);
</script>
)HTML";
