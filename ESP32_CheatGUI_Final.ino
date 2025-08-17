/* ===================================================================================== NEON SIM — Waveshare ESP32-S3 1.47" (72×320) LCD + Cyberpunk PWA (Single .ino)
• Local SoftAP-hosted PWA with custom neon SVG glyphs (web-only), NOT real emoji. • NiceHash-inspired cosmetic simulator: global/per-worker hashrate, shares, temps, power, efficiency, earnings, latency, uptime, clock, balance chart, fleet view. • Plans: Standard (default) vs Premium (unlocks analytics/fleet/export+), with frosted lock overlays in Standard. • Profile page: avatar upload from phone (stored in SPIFFS), editable display name, plan badge. • ESP32 LCD: boot fake CMD installer, rotating 3D wireframe cube, neon bars, scrolling stats ticker. • Optional WS2812 RGB LED pulse synced to "hashrate". • Strictly offline/ethical: no wallets, no chains, no network calls.

Hardware: Waveshare ESP32-S3 1.47" LCD Display Dev Board (72×320, 262K colors) Display driver expected: ST7789 via TFT_eSPI (configure your User_Setup.h accordingly) ===================================================================================== */

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "SPIFFS.h"

// ---------- Optional RGB "Colorful LED" (WS2812) ----------
#define USE_NEOPIXEL 1 // set 0 to disable if you don't have the lib or LED
#define NEOPIXEL_PIN 48 // adjust for your Waveshare board if different
#define NEOPIXEL_COUNT 1
#if USE_NEOPIXEL
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel rgb(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
#endif

// ---------- Globals ----------
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);

const char* AP_SSID = "SIM-DEMO";
const char* AP_PASS = "simdemo123";
IPAddress apIP(192,168,4,1);

unsigned long bootMs = 0;
unsigned long lastSimMs = 0;
unsigned long lastLCDMs = 0;
unsigned long lastTickerMs = 0;
unsigned long lastRGBMs = 0;
int WIDTH=0, HEIGHT=0; // expected 320x72 after rotation

// Simulation
bool simRunning = true;
bool isPremium = false;
bool rgbSync = true;
String profileName = "Neon Operator";
String profilePlan = "Standard";
String avatarPath = "/avatar.jpg";
double totalExtracted = 0.0;
float globalHashrate = 120.0; // MH/s
float globalPower = 230.0; // W
int acceptedShares = 1324;
int rejectedShares = 11;
int latencyMs = 34; // cosmetic net latency
float deviceTemp = 48.0; // deg C

// Workers
struct Worker {
  String name;
  float hashrate;
  int gpuLoad;
  int temp;
  float power;
  float efficiency;
  bool online;
};
Worker workers[8];
int workerCount = 5;

// Fleet (Premium sample)
struct FleetNode {
  String id;
  float hashrate;
  float power;
  bool online;
};
FleetNode fleet[6];
int fleetCount = 4;

// Balance chart samples (web uses it; we keep a short buffer)
const int BALN = 120;
double balSeries[BALN];
int balHead = 0;

// LCD ticker
String tickerLines[6];
int tickerPos = 0;

// 3D cube state
float cubeAngle = 0.0f;

// ---------- Utilities ----------
String fmt2(float v, int d=2){ char b[24]; dtostrf(v,0,d,b); return String(b); }
String fmt6(double v){ char b[32]; dtostrf(v,0,6,b); return String(b); }
int rnd(int a,int b){ return random(a,b+1); }

// ---------- Synthetic generation ----------
void regenWorkers(){
  String base[8] = {"neon-01","neon-02","neon-03","neon-04","neon-05","neon-06","neon-07","neon-08"};
  for(int i=0;i<workerCount;i++){
    workers[i].name = base[i];
    workers[i].hashrate = random(3500,14500)/100.0; // 35..145
    workers[i].gpuLoad = random(35,99);
    workers[i].temp = random(42,86);
    workers[i].power = random(50,180);
    workers[i].efficiency = workers[i].hashrate / max(1.0f, workers[i].power);
    workers[i].online = random(0,100) > 6;
  }
}
void regenFleet(){
  for(int i=0;i<fleetCount;i++){
    fleet[i].id = "rig-"+String(i+1);
    fleet[i].hashrate = random(1500,8000)/10.0; // 150..800 MH/s
    fleet[i].power = random(200,950);
    fleet[i].online = random(0,100) > 10;
  }
}
void pushBalance(double v){
  balSeries[balHead % BALN] = v;
  balHead++;
}
double curBalance(){
  // integrate totalExtracted cosmetic to show as "balance"
  return totalExtracted;
}

// ---------- JSON builders ----------
String jsonStats(){
  String s="{";
  s += "\"uptime\":"+String((millis()-bootMs)/1000)+",";
  s += "\"running\":"+(simRunning?String("true"):String("false"))+",";
  s += "\"rgbSync\":"+(rgbSync?String("true"):String("false"))+",";
  s += "\"profileName\":\""+profileName+"\",";
  s += "\"profilePlan\":\""+profilePlan+"\",";
  s += "\"globalHashrate\":"+String(globalHashrate,2)+",";
  s += "\"globalPower\":"+String(globalPower,2)+",";
  s += "\"acceptedShares\":"+String(acceptedShares)+",";
  s += "\"rejectedShares\":"+String(rejectedShares)+",";
  s += "\"latencyMs\":"+String(latencyMs)+",";
  s += "\"deviceTemp\":"+String(deviceTemp,1)+",";
  s += "\"totalExtracted\":"+String(totalExtracted,6)+",";
  s += "\"balance\":"+String(curBalance(),6)+",";
  s += "\"workers\":[";
  for(int i=0;i<workerCount;i++){
    s+="{\"name\":\""+workers[i].name+"\",\"hashrate\":"+String(workers[i].hashrate,2)+",\"gpuLoad\":"+String(workers[i].gpuLoad)+",\"temp\":"+String(workers[i].temp)+",\"power\":"+String(workers[i].power,1)+",\"eff\":"+String(workers[i].efficiency,3)+",\"online\":"+(workers[i].online?"true":"false")+"}";
    if(i<workerCount-1) s+=",";
  }
  s+="],";
  s += "\"fleet\":[";
  for(int i=0;i<fleetCount;i++){
    s+="{\"id\":\""+fleet[i].id+"\",\"hashrate\":"+String(fleet[i].hashrate,1)+",\"power\":"+String(fleet[i].power,1)+",\"online\":"+(fleet[i].online?"true":"false")+"}";
    if(i<fleetCount-1) s+=",";
  }
  s+="],";
  // balance series
  s += "\"balanceSeries\":[";
  int n=min(balHead,BALN);
  for(int i=0;i<n;i++){
    int idx=(balHead - n + i + BALN)%BALN;
    s += String(balSeries[idx],6);
    if(i<n-1) s+=",";
  }
  s += "]";
  s+="}";
  return s;
}

// ---------- HTTP assets (ALL INLINE; SINGLE FILE) ----------
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>NEON SIM</title>
  <link rel="manifest" href="/manifest.json">
  <meta name="theme-color" content="#00e5ff">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    :root { --n1:#00e5ff; --n2:#ff00e5; --bg:#040614; --fg:#eaf8ff; --card:#0c102a; --bad:#ff4d4d; }
    body { font-family:system-ui,-apple-system,sans-serif; background:var(--bg); color:var(--fg); margin:0; display:flex; padding: 8px; }
    main { flex:1; }
    aside.rightbar { width:280px; padding-left:12px; border-left:1px solid rgba(255,255,255,.05); }
    .row { display:flex; gap:10px; align-items:center; }
    .k { font-size:.8rem; opacity:.7; text-transform:uppercase; }
    .v { font-size:1.4rem; font-weight:500; }
    .stat { flex:1; }
    .card { background:var(--card); border-radius:16px; padding:12px; margin-bottom: 10px; }
    .list { display:grid; grid-template-columns:repeat(auto-fill,minmax(220px,1fr)); gap:10px; }
    .btn { padding:10px 14px; border:0; border-radius:10px; background:linear-gradient(45deg,var(--n1),var(--n2)); color:white; font-weight:500; cursor:pointer; }
    .btn.alt { background:rgba(255,255,255,.1); }
    .btn.bad { background:#442233; color:var(--bad); border:1px solid var(--bad); }
    .t { font-family:monospace; white-space:pre; background:black; border:1px solid #333; padding:8px; border-radius:8px; height:180px; overflow-y:scroll; }
    nav { display:flex; flex-direction: column; gap:10px; margin-right:12px; }
    nav a { color:var(--fg); text-decoration:none; padding:8px 12px; border-radius:10px; background:rgba(255,255,255,.05); }
    nav a.active { background:var(--n1); color:black; }
    .locked { position:relative; }
    .locked .lock { position:absolute; inset:0; backdrop-filter:blur(4px); -webkit-backdrop-filter:blur(4px); display:flex; align-items:center; justify-content:center; z-index:1; border-radius: 12px;}
    .lock svg { width:48px; height:48px; }
    .small { font-size:.8rem; opacity:.7; }
    footer { text-align:center; font-size: .8rem; opacity: .5; padding-top: 12px; }
    input, select { font-family:inherit; font-size:inherit; color: inherit; background:rgba(255,255,255,.03); border:1px solid rgba(255,255,255,.1); padding:8px; border-radius:10px; }
    .offline { opacity:0.4; }
  </style>
</head>
<body>
<nav>
  <a href="#" id="nav-dash" class="active">Dashboard</a>
  <a href="#" id="nav-analytics">Analytics</a>
  <a href="#" id="nav-profile">Profile</a>
  <a href="#" id="nav-terminal">Terminal</a>
  <a href="#" id="nav-settings">Settings</a>
</nav>
<main>
  <!-- DASHBOARD -->
  <section id="tab-dash">
    <div class="row">
      <div class="stat card"><div class="k">Uptime</div><div class="v" id="vUptime">--:--:--</div></div>
      <div class="stat card"><div class="k">Total Hashrate</div><div class="v" id="vHash">-- MH/s</div></div>
      <div class="stat card"><div class="k">Power</div><div class="v" id="vPower">-- W</div></div>
      <div class="stat card"><div class="k">Temp</div><div class="v" id="vTemp">-- &deg;C</div></div>
    </div>
    <div class="card">
      <div class="k">Balance</div><div class="v" id="vBal">0.000000 SIM</div>
    </div>
    <div class="row" style="margin-top:12px">
      <div class="card" style="flex:1"><canvas id="chartBalance" height="140"></canvas></div>
      <div class="card" style="width:280px"><canvas id="chartLatency" height="140"></canvas></div>
    </div>

    <div style="margin-top:12px">
      <div class="k">Workers</div>
      <div id="workers" class="list"></div>
    </div>
  </section>

  <!-- ANALYTICS -->
  <section id="tab-analytics" style="display:none">
    <div class="k">Advanced Analytics</div>
    <div id="panelAnalytics" class="locked card">
      <div class="lock"><svg class="neon-ico" viewBox="0 0 24 24"><path d="M6 10h12v10H6z" fill="#fff" fill-opacity=".1"/><path d="M8 10V7a4 4 0 1 1 8 0v3" fill="none" stroke="#fff"/></svg></div>
      <div class="row" style="margin-top:6px">
        <div style="flex:1"><canvas id="chartHash" height="160"></canvas></div>
        <div style="flex:1"><canvas id="chartPower" height="160"></canvas></div>
      </div>
      <div class="row" style="margin-top:12px">
        <div class="stat"><div class="k">Shares (A/R)</div><div class="v" id="sar">-- / --</div></div>
        <div class="stat"><div class="k">Efficiency</div><div class="v" id="seff">-- %</div></div>
        <div class="stat"><div class="k">Avg Latency</div><div class="v" id="slat">-- ms</div></div>
      </div>
    </div>
    <div class="small" style="margin-top:8px">Upgrade to Premium to unlock analytics.</div>
  </section>

  <!-- PROFILE -->
  <section id="tab-profile" style="display:none">
    <div class="row" style="align-items:flex-start">
      <div class="card" style="flex:1">
        <div class="row">
          <div style="width:96px;height:96px;border-radius:14px;background:linear-gradient(135deg,var(--n1),var(--n2));overflow:hidden"><img id="avatarImg" src="/avatar" style="width:100%;height:100%;object-fit:cover" onerror="this.src='/default_avatar'"></div>
          <div style="flex:1">
            <div class="k">Display name</div>
            <input id="nameInput" style="width:100%;" value="Neon Operator">
            <div class="row" style="margin-top:8px">
              <div id="planBadge">STANDARD</div>
              <select id="planSel">
                <option value="Standard">Standard</option>
                <option value="Premium">Premium</option>
              </select>
              <button class="btn" id="btnSaveProf">Save</button>
            </div>
          </div>
        </div>
        <div class="k" style="margin-top:10px">Upload avatar</div>
        <form id="avatarForm" enctype="multipart/form-data" method="post" action="/upload">
          <input type="file" name="avatar" accept="image/*" style="width:100%;margin-top:6px">
          <div class="row" style="margin-top:8px">
            <button class="btn" type="submit">Upload</button>
            <button class="btn bad" type="button" id="btnRemoveAvatar">Remove</button>
          </div>
        </form>
      </div>

      <div class="card" style="width:320px">
        <div class="k">Premium Toolkit</div>
        <div id="panelPremium" class="locked" style="margin-top:6px;padding:8px;border-radius:12px">
          <div class="lock"><svg class="neon-ico" viewBox="0 0 24 24"><path d="M6 10h12v10H6z" fill="#fff" fill-opacity=".1"/><path d="M8 10V7a4 4 0 1 1 8 0v3" fill="none" stroke="#fff"/></svg></div>
          <ul style="margin:0;padding-left:16px">
            <li>CSV export</li>
            <li>Per-worker tuning</li>
            <li>Predictive earnings</li>
            <li>Fleet overview</li>
          </ul>
        </div>
      </div>
    </div>
  </section>

  <!-- TERMINAL -->
  <section id="tab-terminal" style="display:none">
    <div class="k">Fake Installer Console</div>
    <div id="term" class="t" role="log" aria-live="polite"></div>
    <div class="row" style="margin-top:8px">
      <button class="btn" id="btnInstall">Run installer</button>
      <button class="btn alt" id="btnRGB">Toggle RGB Sync</button>
    </div>
  </section>

  <!-- SETTINGS -->
  <section id="tab-settings" style="display:none">
    <div class="row">
      <div class="stat card"><div class="k">Plan</div><div class="v" id="vPlan">Standard</div></div>
      <div class="stat card"><div class="k">RGB Sync</div><div class="v" id="vRGB">On</div></div>
      <div class="stat card"><div class="k">Latency</div><div class="v" id="vLat">-- ms</div></div>
    </div>
    <div style="margin-top:10px">
      <button class="btn" id="btnTheme">Cycle Theme (Not Impl)</button>
    </div>
  </section>
</main>

<!-- RIGHT BAR -->
<aside class="rightbar">
  <div class="card">
    <div class="k">Pool / Shares</div>
    <div class="row" style="margin-top:6px">
      <div class="stat"><div class="k">Accepted</div><div class="v" id="vAcc">--</div></div>
      <div class="stat"><div class="k">Rejected</div><div class="v" id="vRej">--</div></div>
      <div class="stat"><div class="k">Efficiency</div><div class="v" id="vEff">-- %</div></div>
    </div>
  </div>

  <div class="card">
    <div class="k">Fleet (Premium)</div>
    <div id="fleet" class="list"></div>
  </div>

  <div class="card">
    <div class="k">About</div>
    <div class="small" style="margin-top:6px">This simulator is local and harmless. No wallet or chain access. Install to Home Screen to run as an app.</div>
  </div>
</aside>
<footer>Device: 192.168.4.1 • All data synthetic • © NeonSim</footer>
<script>
const $ = e => document.querySelector(e);
const $$ = e => document.querySelectorAll(e);

const navLinks = $$('nav a');
const tabs = $$('main > section');

navLinks.forEach(link => {
  link.onclick = e => {
    e.preventDefault();
    const target = link.id.replace('nav-','');
    tabs.forEach(tab => tab.style.display = (tab.id === `tab-${target}` ? 'block' : 'none'));
    navLinks.forEach(l => l.classList.remove('active'));
    link.classList.add('active');
  }
});

let charts = {};
const chartDefaults = {
  responsive: true, maintainAspectRatio: false,
  scales: { x:{display:false}, y:{ ticks:{color:'rgba(255,255,255,0.5)'}, grid:{color:'rgba(255,255,255,0.1)'} } },
  plugins: { legend: { display: false } }
};

function createChart(id, type, label, data, color) {
  const ctx = $(`#${id}`).getContext('2d');
  return new Chart(ctx, {
    type: type,
    data: {
      labels: data.map((_,i)=>i),
      datasets: [{ label, data, borderColor: color, borderWidth: 2, pointRadius: 0, fill:true, tension: 0.4,
        backgroundColor: color.replace(/, ?0\.\d+\)/,',0.2)')
      }]
    },
    options: chartDefaults
  });
}

function updateStats(s) {
  $('#vUptime').textContent = new Date(s.uptime*1000).toISOString().substr(11,8);
  $('#vHash').textContent = `${s.globalHashrate.toFixed(1)} MH/s`;
  $('#vPower').textContent = `${s.globalPower.toFixed(0)} W`;
  $('#vTemp').textContent = `${s.deviceTemp.toFixed(0)} °C`;
  $('#vBal').textContent = `${s.balance.toFixed(6)} SIM`;
  $('#vAcc').textContent = s.acceptedShares;
  $('#vRej').textContent = s.rejectedShares;
  const eff = s.acceptedShares>0 ? (s.acceptedShares/(s.acceptedShares+s.rejectedShares)*100).toFixed(2) : 0;
  $('#vEff').textContent = `${eff} %`;
  $('#vPlan').textContent = s.profilePlan;
  $('#vRGB').textContent = s.rgbSync ? 'On' : 'Off';
  $('#vLat').textContent = `${s.latencyMs} ms`;

  $('#nameInput').value = s.profileName;
  $('#planSel').value = s.profilePlan;
  $('#planBadge').textContent = s.profilePlan.toUpperCase();
  $('#panelAnalytics').className = s.profilePlan==='Premium' ? 'card' : 'card locked';
  $('#panelPremium').className = s.profilePlan==='Premium' ? '' : 'locked';

  let workersHTML = '';
  s.workers.forEach(w => {
    workersHTML += `<div class="card ${w.online?'':'offline'}">
      <div class="k">${w.name}</div>
      <div class="row">
        <div class="stat"><div class="k">Hash</div><div class="v">${w.hashrate.toFixed(1)}</div></div>
        <div class="stat"><div class="k">Temp</div><div class="v">${w.temp}&deg;C</div></div>
        <div class="stat"><div class="k">Power</div><div class="v">${w.power.toFixed(0)}W</div></div>
      </div>
    </div>`;
  });
  $('#workers').innerHTML = workersHTML;

  let fleetHTML = '';
  if (s.profilePlan === 'Premium') {
    s.fleet.forEach(f => {
      fleetHTML += `<div class="card ${f.online?'':'offline'}">
        <div class="k">${f.id}</div>
        <div class="row">
          <div class="stat"><div class="k">Hash</div><div class="v">${f.hashrate.toFixed(0)}</div></div>
          <div class="stat"><div class="k">Power</div><div class="v">${f.power.toFixed(0)}W</div></div>
        </div>
      </div>`;
    });
  } else {
    fleetHTML = `<div class="locked" style="height:100px; display:flex; align-items:center; justify-content:center;"><span>Upgrade to see fleet</span></div>`;
  }
  $('#fleet').innerHTML = fleetHTML;

  if (!charts.balance) {
    charts.balance = createChart('chartBalance', 'line', 'Balance', s.balanceSeries, 'rgba(0,229,255,0.7)');
    charts.latency = createChart('chartLatency', 'bar', 'Latency', [s.latencyMs], 'rgba(255,0,229,0.7)');
    charts.hash = createChart('chartHash', 'line', 'Hashrate', [], 'rgba(0,229,255,0.7)');
    charts.power = createChart('chartPower', 'line', 'Power', [], 'rgba(255,100,0,0.7)');
  } else {
    charts.balance.data.datasets[0].data = s.balanceSeries;
    charts.balance.data.labels = s.balanceSeries.map((_,i)=>i);
    charts.balance.update('none');

    charts.latency.data.datasets[0].data.push(s.latencyMs);
    if(charts.latency.data.datasets[0].data.length > 30) charts.latency.data.datasets[0].data.shift();
    charts.latency.update('none');

    if(s.profilePlan === 'Premium') {
      charts.hash.data.datasets[0].data.push(s.globalHashrate);
      if(charts.hash.data.datasets[0].data.length > 60) charts.hash.data.datasets[0].data.shift();
      charts.hash.update('none');
      charts.power.data.datasets[0].data.push(s.globalPower);
      if(charts.power.data.datasets[0].data.length > 60) charts.power.data.datasets[0].data.shift();
      charts.power.update('none');
    }
  }
}

setInterval(() => { fetch('/api/stats').then(r=>r.json()).then(updateStats); }, 1200);
fetch('/api/stats').then(r=>r.json()).then(s => { updateStats(s); $('#avatarImg').src = '/avatar?' + Date.now(); });

$('#btnSaveProf').onclick = () => {
  fetch('/api/profile', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({name: $('#nameInput').value, plan: $('#planSel').value})
  }).then(() => fetch('/api/stats').then(r=>r.json()).then(updateStats));
};

$('#btnRemoveAvatar').onclick = () => {
  if (confirm('Remove avatar?')) {
    fetch('/removeAvatar', {method:'POST'}).then(() => { $('#avatarImg').src = '/default_avatar?' + Date.now(); });
  }
};

$('#avatarForm').onsubmit = function(e) {
  e.preventDefault();
  var formData = new FormData(this);
  fetch('/upload', { method: 'POST', body: formData })
    .then(r => r.text()).then(t => {
      console.log(t);
      $('#avatarImg').src = '/avatar?' + Date.now();
    });
};

$('#btnInstall').onclick = () => {
  const term = $('#term');
  term.innerHTML = '';
  const steps = ["Initializing...", "Scanning shaders...", "Linking neon kernels...", "Calibrating grid...", "Seeding datasets...", "Complete."];
  let i = 0;
  function nextStep() {
    if (i < steps.length) {
      term.innerHTML += `> ${steps[i]}\n`;
      term.scrollTop = term.scrollHeight;
      i++;
      setTimeout(nextStep, Math.random() * 300 + 100);
    }
  }
  nextStep();
};

$('#btnRGB').onclick = () => { fetch('/api/rgb', {method:'POST'}); };

if ('serviceWorker' in navigator) { navigator.serviceWorker.register('/sw.js'); }
</script>
</body>
</html>
)HTML";
const char MANIFEST_JSON[] PROGMEM = R"JSON({ "name":"NEON SIM", "short_name":"NEON SIM", "start_url":"/", "display":"standalone", "background_color":"#040614", "theme_color":"#00e5ff", "icons":[ { "src":"data:image/svg+xml;utf8,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 24 24%22 fill=%22%2300e5ff%22><path d=%22M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5-10-5-10 5z%22/></svg>", "sizes":"192x192", "type":"image/svg+xml"} ] })JSON";

const char SW_JS[] PROGMEM = R"JS( // Minimal offline cache const C='neon-sim-v2'; self.addEventListener('install',e=>{e.waitUntil(caches.open(C).then(c=>c.addAll(['/','/manifest.json'])));}); self.addEventListener('fetch',e=>{e.respondWith(caches.match(e.request).then(r=>r||fetch(e.request)));}); )JS";

const char DEFAULT_AVATAR[] PROGMEM = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"></path><circle cx="12" cy="7" r="4"></circle></svg>)SVG";

// ---------- HTTP Handlers ----------
void hRoot(){ server.send_P(200,"text/html",INDEX_HTML); }
void hManifest(){ server.send_P(200,"application/json",MANIFEST_JSON); }
void hSW(){ server.send_P(200,"application/javascript",SW_JS); }
void hStats(){ server.send(200,"application/json",jsonStats()); }
void hToggle(){ simRunning=!simRunning; server.send(200,"text/plain", simRunning?"running":"paused"); }
void hReset(){ totalExtracted=0; acceptedShares=0; rejectedShares=0; for(int i=0;i<BALN;i++) balSeries[i]=0; balHead=0; server.send(200,"text/plain","reset"); }
void hExport(){ server.send(200,"application/json",jsonStats()); }
void hAvatar(){
  if(SPIFFS.exists(avatarPath)){
    File f=SPIFFS.open(avatarPath,"r");
    if(!f){ server.send_P(200,"image/svg+xml",DEFAULT_AVATAR); return; }
    uint8_t hdr[3]; f.read(hdr,3); f.seek(0);
    String ct="image/jpeg";
    if(hdr[0]==0x89 && hdr[1]==0x50) ct="image/png";
    server.setContentLength(f.size());
    server.sendHeader("Content-Type",ct);
    WiFiClient c=server.client();
    uint8_t buf[128];
    while(f.available()){ size_t r=f.read(buf,sizeof(buf)); c.write(buf,r);}
    f.close();
  } else server.send_P(200,"image/svg+xml",DEFAULT_AVATAR);
}
void hDefaultAvatar(){ server.send_P(200,"image/svg+xml",DEFAULT_AVATAR); }
void hProfile(){
  if(server.method()!=HTTP_POST){ server.send(405,"text/plain","use POST"); return; }
  String body=server.arg("plain"); // Use arg("plain") for JSON body
  int n1=body.indexOf("\"name\"");
  int p1=body.indexOf("\"plan\"");
  if(n1>=0){
    int q1=body.indexOf('"', body.indexOf(':',n1)+1);
    int q2=body.indexOf('"', q1+1);
    if(q1>=0&&q2>q1) profileName = body.substring(q1+1,q2);
  }
  if(p1>=0){
    int q1=body.indexOf('"', body.indexOf(':',p1)+1);
    int q2=body.indexOf('"', q1+1);
    if(q1>=0&&q2>q1){
      profilePlan = body.substring(q1+1,q2);
      isPremium = (profilePlan=="Premium");
    }
  }
  server.send(200,"text/plain","ok");
}
void hRGB(){ rgbSync=!rgbSync; server.send(200,"text/plain", rgbSync?"on":"off"); }

// Upload multipart
void hUploadFinish(){ server.send(200,"text/plain","ok"); }
void hUploadStream(){
  HTTPUpload& up=server.upload();
  static File f;
  if(up.status==UPLOAD_FILE_START){
    if(SPIFFS.exists(avatarPath)) SPIFFS.remove(avatarPath);
    f = SPIFFS.open(avatarPath, FILE_WRITE);
  } else if(up.status==UPLOAD_FILE_WRITE){
    if(f) f.write(up.buf, up.currentSize);
  } else if(up.status==UPLOAD_FILE_END){
    if(f) f.close();
  }
}
void hRemoveAvatar(){
  if(SPIFFS.exists(avatarPath)) SPIFFS.remove(avatarPath);
  server.send(200,"text/plain","removed");
}

// ---------- LCD & 3D ----------
void lcdInstaller(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("NEON SIM Installer", 6, 4, 2);
  int y=22;
  const char* steps[]={"Initializing...","Scanning shaders...","Linking neon kernels...","Calibrating grid...","Seeding datasets...","Complete"};
  for(int i=0;i<6;i++){
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String("> ")+steps[i], 6, y, 2);
    y+=12;
    // progress bar
    int ww = map(i+1,0,6,0,WIDTH-12);
    tft.drawRect(6, HEIGHT-18, WIDTH-12, 10, tft.color565(0,200,255));
    tft.fillRect(7, HEIGHT-17, ww-2, 8, tft.color565(0,80+25*i,255));
    delay(350);
  }
  delay(400);
}

// simple 3D cube projected to 2D
struct V3{ float x,y,z; };
V3 cubePts[8]={ {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}, {-1,-1, 1},{1,-1, 1},{1,1, 1},{-1,1, 1} };
int cubeEdges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
void drawCube(float ang){
  int cx=WIDTH/2, cy=HEIGHT/2+4;
  float s=26;
  auto rot=[&](V3 p){
    float sa=sin(ang), ca=cos(ang);
    float sb=sin(ang*0.7f), cb=cos(ang*0.7f);
    // Rotate around Y axis
    float x1 = p.x*ca - p.z*sa;
    float z1 = p.x*sa + p.z*ca;
    // Rotate around X axis
    float y2 = p.y*cb - z1*sb;
    float z2 = p.y*sb + z1*cb;
    return V3{x1, y2, z2};
  };
  V3 pr[8];
  for(int i=0;i<8;i++){
    V3 r = rot(cubePts[i]);
    float d = 3.0f / (r.z + 4.5f);
    pr[i] = {r.x * d, r.y * d, r.z};
  }
  uint16_t col=tft.color565(0,220,255);
  for(int e=0;e<12;e++){
    int a=cubeEdges[e][0], b=cubeEdges[e][1];
    int x1=cx + pr[a].x*s, y1=cy + pr[a].y*s;
    int x2=cx + pr[b].x*s, y2=cy + pr[b].y*s;
    tft.drawLine(x1,y1,x2,y2,col);
  }
}

void drawBars(){
  int baseY=HEIGHT-12;
  for(int i=0;i<6;i++){
    int h= 4 + (i%3) + (millis()/7 % 14);
    uint16_t c=tft.color565(0,120+20*i,255);
    tft.fillRect(6+i*12, baseY-h, 8, h, c);
  }
}

/*
void drawTicker(){ // scroll 1 px
  // This function is not currently used and causes a compilation error
  // because tft.scroll() may not be supported on all hardware/configurations.
  // The main loop handles ticker display with a full redraw.
  // tft.scroll(-1);
}
*/

// ---------- Setup & Loop ----------
void setup(){
  Serial.begin(115200);
  randomSeed(esp_random());

  // Display
  tft.init();
  tft.setRotation(1); // 320x72 wide
  WIDTH=tft.width();
  HEIGHT=tft.height();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.drawCentreString("Booting NEON SIM...", WIDTH/2, HEIGHT/2-6, 2);

  // SPIFFS
  SPIFFS.begin(true);

  // RGB LED
#if USE_NEOPIXEL
  rgb.begin();
  rgb.setBrightness(20);
  rgb.clear();
  rgb.show();
#endif

  // SoftAP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Data init
  regenWorkers();
  regenFleet();
  for(int i=0;i<BALN;i++) balSeries[i]=0;
  pushBalance(0.0);
  tickerLines[0]="HASH "+String(globalHashrate,1)+" MH/s";
  tickerLines[1]="PWR "+String(globalPower,0)+" W";
  tickerLines[2]="TEMP "+String(deviceTemp,1)+"C";
  tickerLines[3]="BAL "+String(curBalance(),6)+" SIM";
  tickerLines[4]="ACC "+String(acceptedShares)+" / REJ "+String(rejectedShares);
  tickerLines[5]="LAT "+String(latencyMs)+"ms";

  // Web routes
  server.on("/", HTTP_GET, hRoot);
  server.on("/manifest.json", HTTP_GET, hManifest);
  server.on("/sw.js", HTTP_GET, hSW);
  server.on("/api/stats", HTTP_GET, hStats);
  server.on("/api/toggle", HTTP_POST, hToggle);
  server.on("/api/reset", HTTP_POST, hReset);
  server.on("/api/export", HTTP_GET, hExport);
  server.on("/avatar", HTTP_GET, hAvatar);
  server.on("/default_avatar", HTTP_GET, hDefaultAvatar);
  server.on("/api/profile", HTTP_POST, hProfile);
  server.on("/api/rgb", HTTP_POST, hRGB);
  server.on("/upload", HTTP_POST, hUploadFinish, hUploadStream);
  server.on("/removeAvatar", HTTP_POST, hRemoveAvatar);
  server.begin();

  // LCD installer + first frame
  lcdInstaller();
  tft.fillScreen(TFT_BLACK);
  bootMs = millis();
  lastSimMs = lastLCDMs = lastTickerMs = lastRGBMs = millis();
}

void loop(){
  server.handleClient();

  // Simulation ~1s
  if(millis()-lastSimMs > 1000){
    lastSimMs = millis();
    if (simRunning) {
      // Worker jitter
      for(int i=0;i<workerCount;i++){
        if(workers[i].online){
          workers[i].hashrate = max(5.0f, workers[i].hashrate + random(-20,20)/10.0f);
          workers[i].gpuLoad = constrain(workers[i].gpuLoad + random(-6,6), 10, 100);
          workers[i].temp = constrain(workers[i].temp + random(-2,2), 35, 92);
          workers[i].power = max(10.0f, workers[i].power + random(-8,8)/2.0f);
          workers[i].efficiency = workers[i].hashrate / max(1.0f, workers[i].power);
        } else if(random(0,100)<12) workers[i].online = true;
      }
      // Global recompute
      float H=0, P=0;
      for(int i=0;i<workerCount;i++){
        if(workers[i].online){
          H+=workers[i].hashrate;
          P+=workers[i].power;
        }
      }
      globalHashrate = H;
      globalPower = P;
      acceptedShares += random(0,4);
      if (random(0,100) < 5) rejectedShares += 1; // Lower rejection rate
      latencyMs = constrain(latencyMs + random(-4,5), 16, 120);
      deviceTemp = constrain(deviceTemp + random(-1,2), 36, 78);
      if(random(0,100)<55){
        double t=random(1,90)/100000.0;
        totalExtracted+=t;
      }
      // toggle occasional worker/drop
      if(random(0,100)<8){
        int r=random(0,workerCount);
        workers[r].online = !workers[r].online;
      }
    }
    pushBalance(curBalance());
  }

  // LCD update ~80ms
  if(millis()-lastLCDMs > 80){
    lastLCDMs = millis();
    tft.fillScreen(TFT_BLACK);
    // header
    tft.setTextColor(tft.color565(0,229,255));
    tft.setTextDatum(TL_DATUM);
    tft.drawString("NEON SIM", 6, 2, 2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(String(globalHashrate,0)+"MH "+String(globalPower,0)+"W "+String(deviceTemp,0)+"C", 140, 2, 2);

    // 3D cube
    cubeAngle += 0.08f;
    drawCube(cubeAngle);

    // neon bars
    drawBars();

    // ticker text
    tft.setTextColor(TFT_WHITE);
    // Update ticker lines dynamically
    tickerLines[0]="HASH "+String(globalHashrate,1)+" MH/s";
    tickerLines[1]="PWR "+String(globalPower,0)+" W";
    tickerLines[2]="BAL "+String(curBalance(),6)+" SIM";
    tickerLines[3]="ACC "+String(acceptedShares)+"/"+String(rejectedShares);
    tft.drawString(tickerLines[(tickerPos/20)%4], 6, HEIGHT-26, 2);
    tickerPos++;
  }

  // RGB LED pulse ~50ms
#if USE_NEOPIXEL
  if(rgbSync && millis()-lastRGBMs > 50){
    lastRGBMs = millis();
    if (simRunning) {
      uint8_t lumen = (uint8_t) (20 + (int)(10.0 * abs(sin(millis()/200.0))));
      uint8_t b = (uint8_t) min(255.0f, (globalHashrate/2.0f));
      rgb.setPixelColor(0, rgb.Color(lumen, lumen, b));
      rgb.show();
    } else {
      rgb.clear();
      rgb.show();
    }
  }
#endif
}
