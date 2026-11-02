#pragma once
// Self-contained, offline captive-browser UI; no external assets or WebSocket.
static const char PHONE_PAGE[] PROGMEM=R"HTML(<!doctype html><html lang="de"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>RangeMaster Fernbedienung</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#101722;color:#fff;font:17px system-ui,sans-serif}
main{max-width:520px;margin:auto;padding:20px}h1{font-size:24px}h2{font-size:21px}
button,input{font:inherit;border-radius:10px;padding:16px;border:1px solid #64748b;min-height:54px}
button{background:#26364a;color:#fff;cursor:pointer}button:disabled{opacity:.38;cursor:default}
input{width:100%;background:#172232;color:white;margin:6px 0}label{display:block;margin:15px 0}
nav,.row{display:flex;gap:8px;margin:14px 0}nav button,.row>*{flex:1}
.buttons{display:grid;gap:12px}.red{background:#dc2626}.blue{background:#2563eb}.white{background:white;color:black}
.card{background:#172232;padding:16px;border-radius:12px;margin:16px 0}
[hidden]{display:none!important}small{color:#cbd5e1;display:block;line-height:1.5}
#status{min-height:50px;color:#fde68a}input[type=checkbox]{width:26px;height:26px;vertical-align:middle}
</style><main><h1>RangeMaster · Handy-Modus</h1><p id="status" role="status" aria-live="polite">Verbinden …</p>
<section id="auth"><h2 id="authTitle">Anmelden</h2><form id="login">
<label>PIN<input id="pin" type="password" inputmode="numeric" pattern="[0-9]{4}" minlength="4" maxlength="4" required autocomplete="off"></label>
<label id="repeatLabel" hidden>PIN wiederholen<input id="repeat" type="password" inputmode="numeric" maxlength="4" autocomplete="off"></label>
<button>Anmelden</button></form></section>
<section id="app" hidden><nav><button data-page="control">Steuerung</button><button data-page="test">Maschinentest</button><button data-page="settings">PIN</button></nav>
<section id="control"><label class="card"><input id="fireToggle" type="checkbox"> Schuss freigeben</label>
<div class="buttons"><button data-btn="1" class="red">Start</button><button data-btn="2" class="blue">Wiederholen</button><button data-btn="3" class="white">Skip</button></div>
<label class="card"><input id="scoreToggle" type="checkbox"> Punkte freigeben</label>
<div class="row"><button data-btn="4">0 Punkte</button><button data-btn="5">1 Punkt</button><button data-btn="6">2 Punkte</button></div></section>
<section id="test" hidden><h2>Maschinentest</h2><small>Terminal einschalten und Fernbedienungsempfang aktivieren. Kein laufendes Spiel.
Nur am gewählten Terminal aktivierte Maschinen werden ausgelöst. Sicherheitsbereich freihalten!</small>
<label>Terminal-ID<input id="terminal" placeholder="ESP32-P4-XXXXXXXXXXXX" maxlength="21" autocapitalize="characters" autocomplete="off"></label>
<label><input id="testToggle" type="checkbox"> Testauslösung freigeben</label><div id="machines" class="row" style="flex-wrap:wrap"></div>
<small>Jede Auslösung muss bestätigt werden. „ACK erhalten“ bestätigt nur den Funkempfang am Gateway, nicht die Auslösung der Maschine.</small></section>
<section id="settings" hidden><h2>PIN ändern</h2><form id="change">
<label>Aktuelle PIN<input name="current" type="password" inputmode="numeric" maxlength="4" required autocomplete="off"></label>
<label>Neue PIN<input name="first" type="password" inputmode="numeric" maxlength="4" required autocomplete="off"></label>
<label>Neue PIN wiederholen<input name="second" type="password" inputmode="numeric" maxlength="4" required autocomplete="off"></label>
<button>PIN ändern</button></form></section><p><button id="logout">Abmelden</button></p>
<small>Hotspot ohne Internet. Automatisch aus nach 10 Minuten ohne erfolgreiche Bedienung oder 2 Minuten ohne verbundenes Handy.
Funk-ACK bestätigt ausschließlich den Gateway-Empfang.</small></section></main>
<script>
'use strict';let state={},csrf='',busy=false;
const el=id=>document.getElementById(id),say=t=>el('status').textContent=t;
async function api(path,data){const opts={cache:'no-store',credentials:'same-origin'};
if(data){opts.method='POST';opts.headers={'Content-Type':'application/x-www-form-urlencoded'};opts.body=new URLSearchParams({...data,csrf});}
const r=await fetch(path,opts),j=await r.json();if(!r.ok)throw Error(j.error||'Anfrage abgelehnt');return j;}
function paint(s){state=s;csrf=s.csrf||'';el('auth').hidden=s.auth;el('app').hidden=!s.auth;
el('authTitle').textContent=s.setup?'PIN festlegen':'Anmelden';el('repeatLabel').hidden=!s.setup;
el('fireToggle').checked=!!(s.groups&1);el('scoreToggle').checked=!!(s.groups&2);el('testToggle').checked=!!(s.groups&4);
document.querySelectorAll('[data-btn]').forEach(b=>b.disabled=s.pending||!(s.groups&(Number(b.dataset.btn)<=3?1:2)));
document.querySelectorAll('[data-machine]').forEach(b=>b.disabled=s.pending||!(s.groups&4));
say(s.status||'Bereit');}
async function refresh(){try{paint(await api('/state'));}catch(e){say('Verbindung getrennt: '+e.message);}}
async function action(path,data){if(busy)return;busy=true;try{await api(path,data);await refresh();}catch(e){say(e.message);}finally{busy=false;}}
el('login').onsubmit=e=>{e.preventDefault();action(state.setup?'/setup':'/login',{first:el('pin').value,second:el('repeat').value}).then(()=>{el('pin').value='';el('repeat').value='';});};
el('change').onsubmit=e=>{e.preventDefault();const data=Object.fromEntries(new FormData(e.target));action('/pin',data).then(()=>e.target.reset());};
el('logout').onclick=()=>action('/logout',{});
document.querySelectorAll('[data-page]').forEach(b=>b.onclick=()=>['control','test','settings'].forEach(p=>el(p).hidden=p!==b.dataset.page));
['fireToggle','scoreToggle','testToggle'].forEach(id=>el(id).onchange=()=>action('/groups',{groups:(el('fireToggle').checked?1:0)|(el('scoreToggle').checked?2:0)|(el('testToggle').checked?4:0)}));
document.querySelectorAll('[data-btn]').forEach(b=>b.onclick=()=>action('/button',{button:b.dataset.btn}));
for(const m of 'ABCDEFGH'){const b=document.createElement('button');b.textContent='Maschine '+m;b.dataset.machine=m;
b.onclick=()=>{const target=el('terminal').value.trim();if(!/^ESP32-P4-[0-9a-f]{12}$/i.test(target)){say('Gültige Terminal-ID erforderlich');return;}
if(confirm('Maschine '+m+' am Terminal '+target+' einmal auslösen? Sicherheitsbereich frei?'))action('/test',{machine:m,target});};el('machines').appendChild(b);}
refresh();setInterval(refresh,1000);
</script></html>)HTML";
static_assert(sizeof(PHONE_PAGE)<20000,"Captive page budget exceeded");
