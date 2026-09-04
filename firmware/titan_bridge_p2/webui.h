/* webui.h — the single-page console, served from :80. Kept in PROGMEM. */
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
input,textarea{background:#0d1017;color:var(--tx);border:1px solid var(--ln);border-radius:7px;padding:8px 10px;font:inherit;width:100%}
textarea{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;min-height:56px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.row>input{flex:1;min-width:180px}
table{width:100%;border-collapse:collapse;font-size:13px}
td{padding:4px 8px 4px 0;vertical-align:top}
td.k{color:var(--dim);white-space:nowrap;width:34%}
pre{background:#0b0e14;border:1px solid var(--ln);border-radius:7px;padding:10px;overflow:auto;max-height:280px;font-size:11.5px;line-height:1.45;margin:0}
.pill{display:inline-block;padding:1px 8px;border-radius:99px;font-size:11px;border:1px solid var(--ln)}
.awake{color:var(--ok);border-color:#1e4d3a}.asleep{color:var(--dim)}.unknown{color:var(--wn);border-color:#4d3a1e}
.mac{border-top:1px solid var(--ln);padding:8px 0}
.mac:first-child{border-top:0}
.mac code{color:var(--dim);font-size:11.5px;word-break:break-all}
.nav{display:grid;grid-template-columns:repeat(3,52px);gap:6px;justify-content:center}
.nav button{padding:10px 0}
.hide{display:none}
</style>
<header>
  <h1>Titan Bridge</h1>
  <span class=sub id=hdr>connecting…</span>
</header>
<main>

<div class=card id=wifiCard style="display:none">
  <h2>Wi-Fi setup</h2>
  <div class=row><input id=ss placeholder="network name"><input id=pw type=password placeholder="password"></div>
  <div class=row style=margin-top:8px><button class=pri onclick=saveWifi()>Save &amp; reboot</button>
  <span class=sub>The bridge will rejoin on your network and appear at titan-bridge.local</span></div>
</div>

<div class=card>
  <h2>Status</h2>
  <table id=st></table>
</div>

<div class=card>
  <h2>Power</h2>
  <div class=grid>
    <button class=pri onclick="go('/api/power?state=on')">On</button>
    <button onclick="go('/api/power?state=off')">Off</button>
    <button onclick="go('/api/power?state=toggle')">Toggle</button>
    <button onclick="go('/api/cmd?name=temp')">Probe</button>
  </div>
</div>

<div class=card>
  <h2>Source &amp; picture</h2>
  <div class=grid id=quick></div>
</div>

<div class=card>
  <h2>Navigation</h2>
  <div class=nav>
    <span></span><button onclick="go('/api/cmd?name=up')">▲</button><span></span>
    <button onclick="go('/api/cmd?name=left')">◀</button>
    <button onclick="go('/api/cmd?name=ok')">OK</button>
    <button onclick="go('/api/cmd?name=right')">▶</button>
    <span></span><button onclick="go('/api/cmd?name=down')">▼</button><span></span>
  </div>
  <div class=grid style=margin-top:10px;justify-content:center>
    <button onclick="go('/api/cmd?name=back')">Back</button>
    <button onclick="go('/api/cmd?name=home')">Home</button>
    <button onclick="go('/api/cmd?name=setting')">Settings</button>
    <button onclick="go('/api/hid?key=menu')" title="HID only — no serial equivalent">Menu (HID)</button>
    <button onclick="go('/api/cmd?name=autofocus')">Autofocus</button>
  </div>
</div>

<div class=card>
  <h2>Macros</h2>
  <div id=macs></div>
  <div class=row style=margin-top:12px><input id=mn placeholder="name" style=max-width:160px></div>
  <div class=row style=margin-top:6px><textarea id=msc placeholder="anchor; d500; s:down*3; s:ok; s:back*3"></textarea></div>
  <div class=row style=margin-top:6px>
    <button onclick=runScript()>Run once</button>
    <button class=pri onclick=saveMacro()>Save</button>
    <button onclick="go('/api/macabort')">Abort running</button>
  </div>
</div>

<div class=card>
  <h2>Raw frame</h2>
  <div class=row>
    <input id=raw placeholder="2A2A 02 01 01 04">
    <button onclick="go('/api/raw?hex='+encodeURIComponent(v('raw')))">Send</button>
  </div>
  <p class=sub style="margin:8px 0 0">Checksum is not recalculated — bytes go out exactly as typed.</p>
</div>

<div class=card>
  <h2>Infrared</h2>
  <div class=sub id=irlast>waiting for a code…</div>
  <div id=irmaps style=margin-top:8px></div>
  <div class=row style=margin-top:8px>
    <input id=irc placeholder="21DE:4D" style=max-width:140px>
    <input id=ira placeholder="m:movie   or   s:hdmi1">
    <button onclick="go('/api/irmap?code='+encodeURIComponent(v('irc'))+'&action='+encodeURIComponent(v('ira')))">Bind</button>
  </div>
</div>

<div class=card>
  <h2>Log</h2>
  <pre id=log>…</pre>
</div>

<div class=card>
  <h2>Maintenance</h2>
  <div class=grid>
    <button onclick="go('/api/reboot')">Reboot</button>
    <button class=dg onclick="if(confirm('Forget Wi-Fi and reboot into setup mode?'))go('/api/forget')">Forget Wi-Fi</button>
  </div>
</div>

</main>
<script>
const $=i=>document.getElementById(i), v=i=>$(i).value.trim();
const QUICK=[['HDMI1','hdmi1'],['HDMI2','hdmi2'],['HDMI3?','hdmi3'],['USB','usbsrc'],
 ['Filmmaker','filmmaker'],['Movie','movie'],['IMAX','imax'],['Vivid','vivid'],
 ['Perf','perf'],['Sport','sport'],['TV','tvmode'],
 ['Bright 3','b3'],['Bright 7','b7'],['Bright 10','b10'],
 ['Blank','blank'],['Unblank','unblank'],['Vol +','volup'],['Vol −','voldn'],['Mute','mute'],
 ['HRR off','hrroff'],['HRR basic','hrrbasic'],['HRR max','hrrmax']];
$('quick').innerHTML=QUICK.map(([l,c])=>`<button onclick="go('/api/cmd?name=${c}')">${l}</button>`).join('');

async function go(u){try{await fetch(u,{method:'POST'});}catch(e){}refresh();}
async function runScript(){await fetch('/api/macro?script='+encodeURIComponent(v('msc')),{method:'POST'});refresh();}
async function saveMacro(){if(!v('mn'))return alert('name required');
  await fetch('/api/macdef?name='+encodeURIComponent(v('mn'))+'&script='+encodeURIComponent(v('msc')),{method:'POST'});loadMacros();}
async function saveWifi(){await fetch('/api/wifi?ssid='+encodeURIComponent(v('ss'))+'&pass='+encodeURIComponent(v('pw')),{method:'POST'});
  alert('Saved. The bridge is rebooting — reconnect to your own network.');}

function esc(s){return s.replace(/[<>&"]/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]))}

async function loadMacros(){
  const m=await (await fetch('/api/macros')).json();
  $('macs').innerHTML=m.map(x=>`<div class=mac>
    <div class=row><button class=pri onclick="go('/api/macro?name=${encodeURIComponent(x.name)}')">${esc(x.name)}</button>
    <span class=sub>${esc(x.desc)}</span>
    <span style=margin-left:auto>
      <button onclick="edit('${encodeURIComponent(x.name)}')">Edit</button>
      ${x.user?`<button class=dg onclick="go('/api/macdel?name='+encodeURIComponent('${x.name}')).then(loadMacros)">Delete</button>`:''}
    </span></div>
    <code>${esc(x.script)}</code></div>`).join('');
  window._macs=m;
}
function edit(n){const x=window._macs.find(a=>a.name===decodeURIComponent(n));if(!x)return;
  $('mn').value=x.name;$('msc').value=x.script;window.scrollTo({top:$('msc').offsetTop-80,behavior:'smooth'});}

async function loadIr(){
  const m=await (await fetch('/api/irmaps')).json();
  $('irmaps').innerHTML=m.length?m.map(x=>`<div class=row><code>${esc(x.code)}</code> → <code>${esc(x.action)}</code>
    <button class=dg onclick="go('/api/irdel?code='+encodeURIComponent('${x.code}')).then(loadIr)">×</button></div>`).join(''):'<span class=sub>no bindings yet</span>';
}

async function refresh(){
  let s;try{s=await (await fetch('/api/status')).json();}catch(e){$('hdr').textContent='offline';return;}
  $('hdr').innerHTML=`<span class="pill ${s.power}">${s.power}</span> &nbsp;${esc(s.ip)} &nbsp;· uptime ${s.uptime}s`
    +(s.busy?` &nbsp;· <b>${esc(s.busy)}</b>`:'')+(s.macro?` &nbsp;· macro <b>${esc(s.macro)}</b>`:'');
  $('wifiCard').style.display=s.ap?'block':'none';
  const rows=[['Power state',s.power],['Temperature',s.temp],['Link',s.link],
    ['Native CDC port',s.cdc?'open (host bound it)':'not open'],
    ['Frames sent',s.tx],['Bytes received',s.rx],
    ['Last reply',s.since<0?'never':s.since+' ms ago'],['Last frame',s.lastrx],
    ['Wi-Fi',(s.ap?'setup AP ':'')+esc(s.ssid)+' · '+esc(s.ip)],['Firmware',esc(s.fw)]];
  $('st').innerHTML=rows.map(([k,x])=>`<tr><td class=k>${k}</td><td>${x}</td></tr>`).join('');
  $('log').textContent=await (await fetch('/api/log')).text();
  $('log').scrollTop=$('log').scrollHeight;
  if(s.irlast)$('irlast').textContent='last IR code seen: '+s.irlast;
}
loadMacros();loadIr();refresh();setInterval(refresh,2500);
</script>
)HTML";
