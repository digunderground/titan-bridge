/* webui.h — the console, served from :80. Kept in PROGMEM.

   Built as an app, not a page: bottom tab bar, grouped inset lists, segmented
   controls, safe-area insets, and the meta tags that make "Add to Home Screen"
   launch it chromeless with its own icon (served from /icon.png — iOS ignores
   a data: URI there).

   Three tabs:
     Remote    power, D-pad, rockers, and a segmented strip carrying the ~25
               serial actions that would otherwise be three stacked grids.
     Macros    grouped list, recorder, and a chip-based step editor.
     Settings  control channel, power, the ECP routing table, status, log.

   Everything talks to the same REST API the hub and Home Assistant use, so
   nothing here is a private path that can rot separately.                    */
#pragma once
#include <Arduino.h>

static const char UI_HTML[] PROGMEM = R"HTML(<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover,user-scalable=no">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Titan">
<meta name="theme-color" content="#0b0d12">
<link rel="apple-touch-icon" href="/icon.png">
<link rel="icon" href="/icon.png">
<title>Titan</title>
<style>
:root{
  --bg:#0b0d12; --grp:#151922; --grp2:#1c212c; --sep:#262c38;
  --tx:#f2f4f8; --dim:#8e97a8; --dim2:#5f6878;
  --ac:#6d3ff0; --ac2:#8257ff; --ok:#32d74b; --wn:#ff9f0a; --er:#ff453a;
  --tab:72px;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;height:100%}
body{
  background:var(--bg);color:var(--tx);
  font:16px/1.4 -apple-system,BlinkMacSystemFont,"SF Pro Text","Segoe UI",Roboto,sans-serif;
  -webkit-font-smoothing:antialiased;
  padding-bottom:calc(var(--tab) + env(safe-area-inset-bottom));
}
header{padding:calc(env(safe-area-inset-top) + 14px) 20px 6px}
h1{font-size:32px;line-height:1.1;letter-spacing:-.02em;margin:0;font-weight:700}
.status{color:var(--dim);font-size:13px;margin-top:4px;display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.dotp{width:8px;height:8px;border-radius:50%;background:var(--dim2);display:inline-block}
.dotp.on{background:var(--ok)}.dotp.off{background:var(--dim2)}.dotp.unk{background:var(--wn)}
main{padding:8px 16px 24px;display:none}
main.sel{display:block}
.grp{background:var(--grp);border-radius:14px;margin:14px 0;overflow:hidden}
.grp h2{font-size:13px;font-weight:600;color:var(--dim);margin:0;padding:12px 16px 6px;
  text-transform:uppercase;letter-spacing:.05em}
.note{color:var(--dim);font-size:13px;line-height:1.45;padding:10px 16px 14px;margin:0}
.note b{color:var(--tx)} .warn{color:var(--wn)}
button{font:inherit;color:var(--tx);background:var(--grp2);border:0;border-radius:11px;
  padding:12px 16px;min-height:44px;cursor:pointer;transition:transform .08s,opacity .08s}
button:active{transform:scale(.96);opacity:.75}
button.pri{background:var(--ac);font-weight:600}
button.dg{color:var(--er)}
button[disabled]{opacity:.35;pointer-events:none}
input,select,textarea{font:inherit;color:var(--tx);background:var(--grp2);border:0;
  border-radius:11px;padding:12px 14px;width:100%;min-height:44px}
textarea{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:13px;min-height:64px}
.hstack{display:flex;gap:8px;align-items:center;flex-wrap:wrap;padding:12px 16px}
.hstack>input,.hstack>select{flex:1;min-width:130px}
.seg{display:flex;background:var(--grp2);border-radius:11px;padding:3px;margin:12px 16px}
.seg button{flex:1;background:none;border-radius:8px;padding:8px 4px;min-height:36px;
  font-size:14px;color:var(--dim);font-weight:500}
.seg button.on{background:var(--ac);color:#fff}
.acts{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;padding:4px 16px 16px}
.acts button{padding:12px 4px;font-size:13px;line-height:1.25}
.acts button .ico{display:block;font-size:19px;margin-bottom:3px}
@media(max-width:360px){.acts{grid-template-columns:repeat(3,1fr)}}
.remote{display:grid;gap:16px;justify-items:center;padding:8px 0 4px}
.pwrrow{display:flex;gap:8px;justify-content:center;flex-wrap:wrap}
.pwritem{display:flex;flex-direction:column;align-items:center;gap:5px;width:52px}
.clab{font-size:10px;color:var(--dim);text-align:center;line-height:1.1}
.circ{width:48px;height:48px;border-radius:50%;padding:0;display:inline-flex;
  align-items:center;justify-content:center;font-size:17px}
.circ.pwr{background:#3a1218;color:#ff8b96}
.circ.wake{background:#12331c;color:#7ef0a0}
/* Rockers match the D-pad's height exactly, so the three read as one control
   surface rather than a circle with oddments beside it. Top-aligned, so the
   labels hang below a shared baseline instead of pushing the rockers off-centre. */
.dpadwrap{display:flex;gap:14px;align-items:flex-start;justify-content:center;width:100%}
.rockcol{flex:0 0 auto}
.dpad{position:relative;width:min(46vw,204px);aspect-ratio:1;border-radius:50%;
  overflow:hidden;background:#2a1c52;flex:0 0 auto}
.seg4{position:absolute;inset:0;width:100%;height:100%;border:0;border-radius:0;
  background:var(--ac);color:#fff;font-size:19px;display:flex;padding:0}
.seg4:active{background:var(--ac2);transform:none}
.seg4.up{clip-path:polygon(50% 50%,0 0,100% 0);align-items:flex-start;justify-content:center;padding-top:8%}
.seg4.dn{clip-path:polygon(50% 50%,100% 100%,0 100%);align-items:flex-end;justify-content:center;padding-bottom:8%}
.seg4.lf{clip-path:polygon(50% 50%,0 100%,0 0);align-items:center;justify-content:flex-start;padding-left:8%}
.seg4.rt{clip-path:polygon(50% 50%,100% 0,100% 100%);align-items:center;justify-content:flex-end;padding-right:8%}
.xa,.xb{position:absolute;left:50%;top:50%;width:150%;height:2px;background:var(--bg);
  pointer-events:none;transform-origin:center}
.xa{transform:translate(-50%,-50%) rotate(45deg)}
.xb{transform:translate(-50%,-50%) rotate(-45deg)}
.okb{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);
  width:42%;height:42%;border-radius:50%;background:#1d1436;border:3px solid var(--bg);
  color:#fff;font-weight:600;font-size:15px}
.okb:active{transform:translate(-50%,-50%) scale(.94)}
.rock{display:flex;flex-direction:column;background:var(--grp2);border-radius:26px;
  overflow:hidden;height:min(46vw,204px)}
.rock button{border-radius:0;background:none;padding:0 13px;min-height:0;flex:1;font-size:19px}
.rock i{height:1px;background:var(--sep);font-style:normal}
.rlab{text-align:center;font-size:11px;color:var(--dim);margin-top:5px}
.chips{display:flex;flex-wrap:wrap;align-items:center;gap:0;padding:6px 16px 12px}
.chip{background:var(--grp2);border-radius:8px;padding:5px 8px;font:13px ui-monospace,Menlo,monospace;
  display:inline-flex;gap:7px;align-items:center}
.chip.gapc{background:none;color:var(--dim2);border:1px dashed var(--sep)}
.chip b{cursor:pointer;font-weight:400}
.chip i{cursor:pointer;font-style:normal;color:var(--dim2);font-size:15px}
.ins{width:18px;min-height:0;height:26px;border:0;background:none;color:var(--dim2);
  padding:0;font-size:17px;line-height:1;border-radius:0}
.mac{padding:12px 16px;border-top:1px solid var(--sep)}
.mac code{color:var(--dim);font-size:12px;word-break:break-all;display:block;margin-top:6px;
  font-family:ui-monospace,Menlo,monospace}
table{width:100%;border-collapse:collapse;font-size:13px}
td,th{padding:7px 16px;text-align:left;border-top:1px solid var(--sep);vertical-align:top}
th{color:var(--dim);font-weight:600;font-size:11px;text-transform:uppercase;letter-spacing:.05em}
td.val{color:var(--dim);text-align:right;word-break:break-all}
td code{font-family:ui-monospace,Menlo,monospace;color:var(--ac2);font-size:12px}
pre{background:#070910;border-radius:11px;padding:12px;overflow:auto;max-height:280px;
  font:11.5px/1.5 ui-monospace,Menlo,monospace;margin:12px 16px}
nav{position:fixed;left:0;right:0;bottom:0;height:calc(var(--tab) + env(safe-area-inset-bottom));
  padding-bottom:env(safe-area-inset-bottom);
  background:rgba(11,13,18,.86);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);
  border-top:1px solid var(--sep);display:flex;z-index:10}
nav button{flex:1;background:none;border-radius:0;min-height:0;padding:8px 0 4px;
  display:flex;flex-direction:column;align-items:center;gap:3px;color:var(--dim2);font-size:10px}
nav button:active{transform:none;opacity:.6}
nav button .ti{font-size:21px;line-height:1}
nav button.on{color:var(--ac2)}
.hide{display:none!important}
</style>

<header>
  <h1 id=title>Titan Bridge</h1>
  <div class=status id=hdr><span class="dotp unk"></span>connecting…</div>
</header>

<main id=p_remote class=sel>
  <div class=remote>
    <div class=pwrrow>
      <div class=pwritem><button class="circ pwr" onclick="go('/api/power?state=off')">⏻</button><span class=clab>Off</span></div>
      <div class=pwritem><button class="circ wake" onclick="go('/api/power?state=on')">☀</button><span class=clab>Wake</span></div>
      <div class=pwritem><button class=circ onclick="runAct('s:blank')">▤</button><span class=clab>Blank</span></div>
      <div class=pwritem><button class=circ onclick="go('/api/nav?name=mute')">🔇</button><span class=clab>Mute</span></div>
      <div class=pwritem><button class=circ onclick="go('/api/nav?name=menu')">☰</button><span class=clab>Menu</span></div>
      <div class=pwritem><button class=circ onclick="go('/api/nav?name=back')">↩</button><span class=clab>Back</span></div>
    </div>

    <div class=dpadwrap>
      <div class=rockcol>
        <div class=rock>
          <button onclick="go('/api/hid?key=focus%2B')">＋</button><i></i>
          <button onclick="go('/api/hid?key=focus-')">－</button>
        </div>
        <div class=rlab>FOCUS</div>
      </div>
      <div class=dpad>
        <button class="seg4 up" onclick="go('/api/nav?name=up')">▲</button>
        <button class="seg4 rt" onclick="go('/api/nav?name=right')">▶</button>
        <button class="seg4 dn" onclick="go('/api/nav?name=down')">▼</button>
        <button class="seg4 lf" onclick="go('/api/nav?name=left')">◀</button>
        <span class=xa></span><span class=xb></span>
        <button class=okb onclick="go('/api/nav?name=ok')">OK</button>
      </div>
      <div class=rockcol>
        <div class=rock>
          <button onclick="go('/api/nav?name=volup')">＋</button><i></i>
          <button onclick="go('/api/nav?name=voldn')">－</button>
        </div>
        <div class=rlab>VOL</div>
      </div>
    </div>
  </div>

  <div class=grp>
    <div class=seg id=actseg>
      <button class=on onclick="pickSeg('inputs')">Inputs</button>
      <button onclick="pickSeg('picture')">Picture</button>
      <button onclick="pickSeg('display')">Display</button>
      <button onclick="pickSeg('mine')">Mine</button>
    </div>
    <div class=acts id=acts></div>
    <p class=note id=actnote></p>
  </div>
</main>

<main id=p_macros>
  <div class=grp>
    <h2>Recorder</h2>
    <div class=hstack>
      <button id=recbtn onclick=recToggle()>● Record</button>
      <button onclick="rec('clear')">Clear</button>
    </div>
    <p class=note>Press buttons on the Remote tab while recording. Gaps are
      measured, so the delays are the ones the projector actually kept up with.</p>
    <div class=chips id=recsteps></div>
    <div class=hstack>
      <input id=rn placeholder="Name"><input id=rg placeholder="Group" list=grouplist>
    </div>
    <div class=hstack>
      <select id=rspeed>
        <option value=300>Fast — cap gaps at 300ms</option>
        <option value=600 selected>Normal — cap at 600ms</option>
        <option value=1000>Relaxed — cap at 1s</option>
        <option value=10000>Exactly as recorded</option>
      </select>
      <button class=pri onclick=recSave()>Save</button>
    </div>
    <p class=note>Recording captures <b>your</b> timing, including the pauses you
      take while thinking. Cap them here, or keep them and use the <b>Fast</b>
      switch on a saved macro to drop the waits at run time — useful when a
      macro only fires direct commands and never walks a menu. Menu walking
      usually needs the pauses.</p>
  </div>

  <div class=grp><h2>Saved</h2><div id=macs></div></div>

  <div class=grp>
    <h2>Editor</h2>
    <div class=hstack>
      <input id=mn placeholder="Name"><input id=mg placeholder="Group" list=grouplist>
    </div>
    <div class=chips id=edsteps></div>
    <div class=hstack><textarea id=msc placeholder="anchor; k:down*3; k:ok" oninput=syncText()></textarea></div>
    <div class=hstack>
      <button class=pri onclick=saveMacro()>Save</button>
      <button onclick=runEditor()>Run</button>
      <button onclick="go('/api/macabort')">Abort</button>
    </div>
    <p class=note><code>k:</code> follows the active channel ·
      <code>h:</code>/<code>s:</code> force HID or serial ·
      <code>m:</code> runs another macro · <code>d500</code> waits ·
      <code>tok*3</code> repeats · <code>fast</code> anywhere drops every wait.</p>
  </div>
  <datalist id=grouplist></datalist>
</main>

<main id=p_settings>
  <div class=grp>
    <h2>Control channel</h2>
    <div class=seg>
      <button id=chhid onclick="go('/api/keychan?mode=hid')">ESP32 · USB HID</button>
      <button id=chser onclick="go('/api/keychan?mode=serial')">Serial adapter</button>
    </div>
    <p class=note id=chnote></p>
  </div>

  <div class=grp>
    <h2>Power</h2>
    <div class=seg>
      <button id=pmObey onclick="go('/api/power?state=mode&m=obey')">Always act</button>
      <button id=pmAssume onclick="go('/api/power?state=mode&m=assume')">Skip if already there</button>
    </div>
    <p class=note id=pmnote></p>
    <div class=hstack>
      <button onclick="go('/api/power?state=sync&is=on')">It's on</button>
      <button onclick="go('/api/power?state=sync&is=off')">It's off</button>
    </div>
    <p class=note id=obsnote></p>
  </div>

  <div class=grp>
    <h2>Hub buttons — what each one runs</h2>
    <table id=routing></table>
    <div class=hstack>
      <button class=dg onclick=ecpReset()>Reset all to defaults</button>
    </div>
    <p class=note>Tap a row to change what a SofaBaton button does — a saved
      macro, a built-in action, or your own script. <b>Overridden</b> rows are
      marked; clearing one restores the default.
      <code>k:</code> follows the control channel above, so switching that
      re-routes the arrow keys with no change at the hub.</p>
  </div>

  <div class=grp id=ecpCard style=display:none>
    <h2 id=ecpTitle>Map button</h2>
    <div class=hstack><select id=ecpAction onchange=ecpChanged()></select></div>
    <div class=hstack id=ecpCustomRow style=display:none>
      <input id=ecpCustom placeholder="s:power; d900; s:ok">
    </div>
    <p class=note id=ecpDefNote></p>
    <div class=hstack>
      <button class=pri onclick=ecpSave()>Save</button>
      <button onclick=ecpClose()>Cancel</button>
      <button class=dg onclick=ecpRestore()>Restore default</button>
    </div>
  </div>

  <div class=grp><h2>Status</h2><table id=st></table></div>

  <div class=grp>
    <h2>Diagnostics</h2>
    <div class=hstack>
      <input id=raw placeholder="2A2A 02 01 01 04">
      <button onclick="go('/api/raw?hex='+encodeURIComponent(v('raw')))">Send frame</button>
    </div>
    <div class=hstack>
      <input id=hidr placeholder="HID usage hex, e.g. 66">
      <button onclick="go('/api/hidraw?usage='+encodeURIComponent(v('hidr')))">Send usage</button>
    </div>
    <div class=hstack>
      <button onclick="go('/api/announce')">Re-announce (SSDP)</button>
      <button onclick="go('/api/reboot')">Reboot</button>
    </div>
    <pre id=log></pre>
  </div>

  <div class=grp id=wifiCard>
    <h2>Wi-Fi</h2>
    <div class=hstack><input id=ss placeholder=SSID><input id=pw placeholder=Password type=password></div>
    <div class=hstack><button class=pri onclick=saveWifi()>Save</button>
      <button class=dg onclick="if(confirm('Forget Wi-Fi and reboot into setup mode?'))go('/api/forget')">Forget</button></div>
  </div>
</main>

<nav>
  <button id=t_remote class=on onclick="tab('remote')"><span class=ti>◉</span>Remote</button>
  <button id=t_macros onclick="tab('macros')"><span class=ti>❑</span>Macros</button>
  <button id=t_settings onclick="tab('settings')"><span class=ti>⚙</span>Settings</button>
</nav>

<script>
const $=i=>document.getElementById(i), v=i=>$(i).value.trim();
function esc(s){return String(s).replace(/[<>&"]/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]))}

let curTab='remote';
const TITLES={remote:'Titan Bridge',macros:'Macros',settings:'Settings'};
function tab(n){
  curTab=n; $('title').textContent=TITLES[n];
  for(const t of ['remote','macros','settings']){
    $('p_'+t).classList.toggle('sel',t===n);
    $('t_'+t).classList.toggle('on',t===n);
  }
  window.scrollTo(0,0);
  if(n==='macros'){loadMacros();pollRec();}
  if(n==='settings'){loadRouting();}
  refresh();
}
async function go(u){try{await fetch(u,{method:'POST'});}catch(e){}refresh();}

/* Serial unlocked ~25 actions. Three stacked grids would bury the D-pad, so
   they share one row-height space behind a segmented control. */
const SEGS={
  inputs:[['📺','HDMI1','s:hdmi1'],['📺','HDMI2','s:hdmi2'],['📺','HDMI3','s:hdmi3'],
          ['🔌','USB','s:usbsrc'],['🗂','Source','s:source'],['🏠','Home','k:home'],
          ['🎯','Autofocus','s:autofocus'],['🔍','Man focus','s:manfocus']],
  picture:[['🎬','Filmmaker','s:filmmaker'],['🎞','Movie','s:movie'],['🎥','IMAX','s:imax'],
           ['🌈','Vivid','s:vivid'],['⚡','Perf','s:perf'],['🏈','Sport','s:sport'],
           ['📻','TV','s:tvmode']],
  display:[['🔅','Bright 3','s:b3'],['🔆','Bright 7','s:b7'],['☀','Bright 10','s:b10'],
           ['▤','Blank','s:blank'],['▣','Unblank','s:unblank'],
           ['🐢','HRR off','s:hrroff'],['🚶','HRR basic','s:hrrbasic'],['🏃','HRR max','s:hrrmax'],
           ['🧪','Calibration','r:2A2A0207060F']],
  mine:[]
};
let curSeg='inputs';
function pickSeg(k){
  curSeg=k;
  const order=['inputs','picture','display','mine'];
  [...$('actseg').children].forEach((b,i)=>b.classList.toggle('on',order[i]===k));
  drawActs();
}
function drawActs(){
  if(curSeg==='mine'){drawMine();return;}
  const live=window._st?window._st.linkalive:true;
  $('acts').innerHTML=SEGS[curSeg].map(([ic,lb,act])=>
    `<button onclick="runAct('${act}')"><span class=ico>${ic}</span>${esc(lb)}</button>`).join('');
  /* Everything on these three strips is serial-only; say so rather than
     leaving buttons that quietly do nothing. */
  $('acts').style.opacity=live?'1':'.35';
  $('acts').style.pointerEvents=live?'auto':'none';
  $('actnote').innerHTML=live?''
    :'<b class=warn>Serial unavailable.</b> These need a USB-serial adapter the '
     +'projector will bind (FTDI works; CP2102 and CH340 do not). Connect one and '
     +'they enable themselves.';
}
async function runAct(a){
  await fetch('/api/macro?script='+encodeURIComponent(a),{method:'POST'});refresh();
}
async function drawMine(){
  let b;try{b=await (await fetch('/api/buttons')).json();}catch(e){return;}
  window._btns=b;
  $('actnote').textContent='Your own buttons. An action is any macro script fragment.';
  $('acts').style.opacity='1';$('acts').style.pointerEvents='auto';
  $('acts').innerHTML=b.map(x=>
    `<button onclick="go('/api/press?id=${encodeURIComponent(x.id)}')">
       <span class=ico>${esc(x.icon||'★')}</span>${esc(x.label||x.id)}</button>`).join('')
    +`<button onclick=addButton()><span class=ico>＋</span>Add</button>`;
}
async function addButton(){
  const label=prompt('Button label:'); if(!label)return;
  const action=prompt('Action (k:up, m:movie night, s:hdmi1):'); if(!action)return;
  const icon=prompt('Icon (one emoji, or blank):','★')||'';
  await fetch('/api/button?id=b'+Date.now().toString(36)+'&label='+encodeURIComponent(label)
    +'&action='+encodeURIComponent(action)+'&icon='+encodeURIComponent(icon)
    +'&style=pill:default',{method:'POST'});
  drawMine();
}

let recOn=false;
async function rec(state,extra){
  try{const r=await fetch('/api/rec?state='+state+(extra||''),{method:'POST'});renderRec(await r.json());}catch(e){}
}
async function pollRec(){try{renderRec(await (await fetch('/api/rec')).json());}catch(e){}}
function recToggle(){rec(recOn?'stop':'start');}
function renderRec(d){
  recOn=d.recording;
  const b=$('recbtn'); b.textContent=recOn?'■ Stop':'● Record'; b.className=recOn?'pri':'';
  $('recsteps').innerHTML=d.steps.length
    ? d.steps.map((s,i)=>chip(s.tok,s.gap,i,'recEdit','recDel')).join('')
    : '<span class=note style=padding:0>nothing recorded yet</span>';
}
async function recDel(i){await fetch('/api/recstep?op=del&idx='+i,{method:'POST'});pollRec();}
async function recEdit(i){
  const t=prompt('Insert step before this one (k:down, d400, m:movie):'); if(!t)return;
  await fetch('/api/recstep?op=ins&idx='+i+'&tok='+encodeURIComponent(t)+'&gap=0',{method:'POST'});pollRec();
}
async function recSave(){
  if(!v('rn'))return alert('Name required');
  await rec('save','&name='+encodeURIComponent(v('rn'))+'&group='+encodeURIComponent(v('rg'))
    +'&maxgap='+encodeURIComponent($('rspeed').value));
  loadMacros();
}

function chip(tok,gap,i,ed,del){
  const g=gap?`<span class="chip gapc">${gap}ms</span>`:'';
  return `<button class=ins onclick="${ed}(${i})">＋</button>${g}`
    +`<span class=chip><b onclick="${ed}(${i})">${esc(tok)}</b><i onclick="${del}(${i})">×</i></span>`;
}
let edToks=[];
function parseScript(s){return s.split(';').map(x=>x.trim()).filter(Boolean);}
function drawChips(){
  $('edsteps').innerHTML=edToks.map((t,i)=>chip(t,0,i,'edIns','edDel')).join('')
    +`<button class=ins onclick="edIns(${edToks.length})">＋</button>`;
}
function renderEd(){drawChips();$('msc').value=edToks.join('; ');}
function edDel(i){edToks.splice(i,1);renderEd();}
function edIns(i){const t=prompt('Token (k:down, d400, m:movie, anchor, k:down*3):');
  if(t&&t.trim()){edToks.splice(i,0,t.trim());renderEd();}}
function syncText(){edToks=parseScript($('msc').value);drawChips();}
function editMacro(n){
  const m=(window._macs||[]).find(x=>x.name===n); if(!m)return;
  $('mn').value=m.name; $('mg').value=m.group==='built-in'?'':m.group;
  edToks=parseScript(m.script); renderEd();
  $('mn').scrollIntoView({behavior:'smooth',block:'center'});
}
async function saveMacro(){
  if(!v('mn'))return alert('Name required');
  if(!edToks.length)return alert('No steps');
  await fetch('/api/macdef?name='+encodeURIComponent(v('mn'))+'&group='+encodeURIComponent(v('mg'))
    +'&script='+encodeURIComponent(edToks.join('; ')),{method:'POST'});
  loadMacros();
}
async function runEditor(){await fetch('/api/macro?script='+encodeURIComponent(edToks.join('; ')),{method:'POST'});}

async function loadMacros(){
  let m;try{m=await (await fetch('/api/macros')).json();}catch(e){return;}
  window._macs=m;
  const g={}; for(const x of m){(g[x.group||'ungrouped']=g[x.group||'ungrouped']||[]).push(x);}
  const names=Object.keys(g).sort((a,b)=>a==='built-in'?1:b==='built-in'?-1:a.localeCompare(b));
  $('macs').innerHTML=names.map(k=>`<h2>${esc(k)}</h2>`+g[k].map(x=>`
    <div class=mac><div style="display:flex;gap:8px;align-items:center">
      <button class=pri style="flex:1;text-align:left" onclick="go('/api/macro?name=${encodeURIComponent(x.name)}')">${esc(x.name)}</button>
      <button class="${/(^|;)\s*fast(\s|;|$)/.test(x.script)?'pri':''}"
        onclick="toggleFast('${esc(x.name).replace(/'/g,"\\'")}')"
        title="Run without the recorded pauses">Fast</button>
      <button onclick="editMacro('${esc(x.name).replace(/'/g,"\\'")}')">Edit</button>
      ${x.user?`<button class=dg onclick="delMacro('${esc(x.name).replace(/'/g,"\\'")}')">Delete</button>`:''}
    </div><code>${esc(x.script)}</code></div>`).join('')).join('');
  $('grouplist').innerHTML=names.filter(k=>k!=='built-in'&&k!=='ungrouped')
    .map(k=>`<option value="${esc(k)}">`).join('');
}
/* "fast" is a marker token, not a step — so toggling it is a text edit on the
   saved script, and the recorded timing stays in place for when it is needed. */
async function toggleFast(n){
  const m=(window._macs||[]).find(x=>x.name===n); if(!m)return;
  const toks=m.script.split(';').map(t=>t.trim()).filter(Boolean);
  const i=toks.findIndex(t=>t.toLowerCase()==='fast');
  if(i>=0) toks.splice(i,1); else toks.unshift('fast');
  await fetch('/api/macdef?name='+encodeURIComponent(m.name)
    +'&group='+encodeURIComponent(m.group==='built-in'?'':m.group)
    +'&script='+encodeURIComponent(toks.join('; ')),{method:'POST'});
  loadMacros();
}
async function delMacro(n){
  if(!confirm('Delete "'+n+'"?'))return;
  await fetch('/api/macdel?name='+encodeURIComponent(n),{method:'POST'});loadMacros();
}

/* The hub's key map is data, not firmware. Read it live so what a button does
   is inspectable and changeable without a reflash. */
let ecpKeyName=null;
async function loadRouting(){
  let m;try{m=await (await fetch('/api/ecpmap')).json();}catch(e){return;}
  window._ecp=m;
  $('routing').innerHTML='<tr><th>Hub button</th><th>Runs</th><th></th></tr>'
    +m.map(r=>`<tr onclick="ecpOpen('${esc(r.key)}')" style=cursor:pointer>
        <td>${esc(r.key)}</td>
        <td><code>${esc(r.action)}</code></td>
        <td class=val>${r.custom?'overridden':''}</td></tr>`).join('');
}
async function ecpOpen(key){
  ecpKeyName=key;
  const r=(window._ecp||[]).find(x=>x.key===key); if(!r)return;
  $('ecpTitle').textContent=key;
  $('ecpDefNote').innerHTML='Default: <code>'+esc(r.def)+'</code>'
    +(r.custom?' — currently overridden.':'');

  let macs=window._macs;
  if(!macs){try{macs=await (await fetch('/api/macros')).json();window._macs=macs;}catch(e){macs=[];}}
  let html='';
  if(macs.length) html+='<optgroup label="Macros">'+macs.map(x=>
    `<option value="m:${esc(x.name)}">${esc(x.name)}</option>`).join('')+'</optgroup>';
  for(const [g,items] of BUILTIN_ACTIONS)
    html+=`<optgroup label="${esc(g)}">`+items.map(([a,l])=>
      `<option value="${esc(a)}">${esc(l)}</option>`).join('')+'</optgroup>';
  html+='<optgroup label="Other"><option value="__custom">Custom script…</option></optgroup>';
  $('ecpAction').innerHTML=html;
  const known=[...$('ecpAction').options].some(o=>o.value===r.action);
  $('ecpAction').value=known?r.action:'__custom';
  $('ecpCustom').value=known?'':r.action;
  ecpChanged();
  $('ecpCard').style.display='';
  $('ecpCard').scrollIntoView({behavior:'smooth',block:'center'});
}
function ecpChanged(){
  $('ecpCustomRow').style.display=$('ecpAction').value==='__custom'?'':'none';
}
function ecpClose(){$('ecpCard').style.display='none';ecpKeyName=null;}
async function ecpSave(){
  let a=$('ecpAction').value;
  if(a==='__custom')a=$('ecpCustom').value.trim();
  if(!a)return alert('Pick or type an action.');
  await fetch('/api/ecpset?key='+encodeURIComponent(ecpKeyName)
    +'&action='+encodeURIComponent(a),{method:'POST'});
  ecpClose();loadRouting();
}
async function ecpRestore(){
  await fetch('/api/ecpset?key='+encodeURIComponent(ecpKeyName)+'&action=',{method:'POST'});
  ecpClose();loadRouting();
}
async function ecpReset(){
  if(!confirm('Reset every hub button to its default?'))return;
  await fetch('/api/ecpreset',{method:'POST'});loadRouting();
}

/* Offered in both the button editor and the hub-key editor. */
const BUILTIN_ACTIONS=[
  ['Power',[['p:on','Power on'],['p:off','Power off'],['p:toggle','Power toggle'],
            ['s:wake','Wake (serial, discrete)'],
            ['s:power; d900; s:ok','Power off, raw — bypasses the state machine']]],
  ['Navigation',[['k:up','Up'],['k:down','Down'],['k:left','Left'],['k:right','Right'],
                 ['k:ok','OK'],['k:back','Back'],['k:menu','Menu'],['k:home','Home']]],
  ['Sound & lens',[['k:volup','Volume +'],['k:voldn','Volume −'],['s:mute','Mute'],
                   ['h:focus+','Focus +'],['h:focus-','Focus −'],['s:autofocus','Autofocus']]],
  ['Inputs',[['s:hdmi1','HDMI1'],['s:hdmi2','HDMI2'],['s:hdmi3','HDMI3'],['s:usbsrc','USB']]],
  ['Picture',[['s:filmmaker','Filmmaker'],['s:movie','Movie'],['s:vivid','Vivid'],
              ['s:blank','Blank'],['s:unblank','Unblank']]]
];

async function saveWifi(){
  await fetch('/api/wifi?ssid='+encodeURIComponent(v('ss'))+'&pass='+encodeURIComponent(v('pw')),{method:'POST'});
  alert('Saved. The bridge is rebooting — reconnect to your own network.');
}

async function refresh(){
  let s;try{s=await (await fetch('/api/status')).json();}catch(e){
    $('hdr').innerHTML='<span class=dotp></span>offline';return;}
  window._st=s;
  const on=s.power==='awake'||s.power.startsWith('on');
  const off=s.power==='asleep'||s.power.startsWith('off');
  $('hdr').innerHTML=`<span class="dotp ${on?'on':off?'off':'unk'}"></span>`
    +`${esc(s.power)} · ${esc(s.ip)} · ${s.uptime}s`
    +(s.macro?` · <b>${esc(s.macro)}</b>`:'');
  if(curTab==='remote'&&curSeg!=='mine')drawActs();
  if(curTab!=='settings')return;

  $('chhid').className=s.keychan==='hid'?'on':'';
  $('chser').className=s.keychan==='serial'?'on':'';
  $('chser').disabled=!s.linkalive;
  $('chnote').innerHTML=s.linkalive
    ? "Both channels work on this projector. Serial does not depend on the ESP32's "
      +'native USB port, so it survives reflashing; HID reaches keys serial has no '
      +'equivalent for. Explicit <code>h:</code>/<code>s:</code> macro steps ignore this.'
    : '<b class=warn>Serial unavailable.</b> No adapter has returned a valid frame, so '
      +'that option is disabled — selecting it would leave every key silently doing nothing.';
  $('pmAssume').className=s.powermode==='assume'?'on':'';
  $('pmObey').className=s.powermode==='obey'?'on':'';
  $('pmnote').textContent=s.powermode==='obey'
    ? 'Every press acts, with a 4s debounce so one press is one toggle. Recommended: a hub tracks its own state, and nothing here can verify ours.'
    : 'Presses are filtered against the state below. Genuinely discrete while that belief is right — but it cannot be verified, and when it drifts the button silently does nothing until you resync.';
  $('obsnote').innerHTML=s.powerobserved
    ? 'This projector reports its power state, so the above is measured.'
    : '<b>Assumed, not measured.</b> Nothing on this projector reports power — the '
      +'temperature probe answers identically in standby — so this tracks what the '
      +'bridge last did. Using the physical remote desyncs it.';
  $('wifiCard').style.display=s.ap?'':'none';
  const rows=[['Power',s.power],['Reading',s.powerobserved?'measured':'assumed'],
    ['Temperature',s.temp],['Channel',s.keychan],['Link',s.link],
    ['Serial link',s.linkalive?'alive':'never received a valid frame'],
    ['Native CDC',s.cdc?'open':'not open'],['Frames sent',s.tx],['Bytes received',s.rx],
    ['Last reply',s.since<0?'never':s.since+' ms ago'],['Last frame',s.lastrx],
    ['Wi-Fi',(s.ap?'setup AP ':'')+esc(s.ssid)+' · '+esc(s.ip)],
    ['Firmware',esc(s.fw)],['Free heap',s.heap]];
  $('st').innerHTML=rows.map(([k,x])=>`<tr><td>${k}</td><td class=val>${x}</td></tr>`).join('');
  $('log').textContent=await (await fetch('/api/log')).text();
  $('log').scrollTop=$('log').scrollHeight;
}
drawActs();refresh();setInterval(()=>{refresh();if(curTab==='macros'&&recOn)pollRec();},2500);
</script>
)HTML";
