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
TFT_eSprite spr = TFT_eSprite(&tft);
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
  String name; float hashrate; int gpuLoad; int temp; float power; float efficiency; bool online;
};
Worker workers[8];
int workerCount = 5;

// Fleet (Premium sample)
struct FleetNode {
  String id; float hashrate; float power; bool online;
};
FleetNode fleet[6];
int fleetCount = 4;

// Balance chart samples (web uses it; we keep a short buffer)
const int BALN = 120;
double balSeries[BALN];
int balHead = 0;

// LCD ticker
String tickerLines[6];
int tickerPos = 0; // Unused, but kept for reference
int tickerScrollX = 0;

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
double curBalance(){ // integrate totalExtracted cosmetic to show as "balance"
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
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>NEON SIM</title>
<link rel="manifest" href="/manifest.json">
<style>
:root{--bg:#040614;--n1:#00e5ff;--n2:#ff00e1;--fg:#eaf8ff;--bad:#ff4b4b;--fnt:'SF Mono',Consolas,monospace}
body{background:var(--bg);color:var(--fg);font-family:var(--fnt);font-size:14px;margin:0;display:flex;min-height:100vh}
main{flex:1;padding:10px}
aside{width:260px;background:rgba(255,255,255,.02);border-left:1px solid rgba(255,255,255,.05);padding:10px;font-size:12px}
.row{display:flex;gap:10px;align-items:center}
.card{background:rgba(255,255,255,.03);padding:10px;border-radius:14px;margin-bottom:10px}
.stat{flex:1}
.k{font-size:11px;color:rgba(255,255,255,.5);margin-bottom:2px}
.v{font-size:22px;font-weight:600}
.list .item{padding:8px 6px;border-bottom:1px solid rgba(255,255,255,.04);display:flex;gap:8px}
.list .i-name{flex:1;color:#fff}
.list .i-rate{width:80px;text-align:right}
.list .i-stat{width:50px;text-align:right}
.list .i-on{width:12px;height:12px;border-radius:50%;background:#44ff88}
.list .i-off{width:12px;height:12px;border-radius:50%;background:#ff4b4b}
.btn{background:linear-gradient(90deg,var(--n1),var(--n2));color:#000;padding:8px 12px;border:0;border-radius:10px;font-weight:600;cursor:pointer}
.btn.alt{background:rgba(255,255,255,.1)}
.btn.bad{background:var(--bad)}
.t{height:160px;background:rgba(0,0,0,.3);border:1px solid rgba(255,255,255,.08);border-radius:12px;padding:8px;overflow-y:scroll;font-size:12px}
.t p{margin:0;padding:0;line-height:1.5}
.locked{position:relative;filter:blur(3px);opacity:.7}
.lock{position:absolute;top:0;left:0;right:0;bottom:0;display:flex;align-items:center;justify-content:center;z-index:2}
.neon-ico{width:48px;height:48px}
.badge{padding:4px 8px;border-radius:8px;font-size:11px;font-weight:600}
.badge.std{background:rgba(0,229,255,.2);color:#9ef2ff}
.badge.prm{background:rgba(255,0,225,.2);color:#ff9ef2}
footer{font-size:10px;color:rgba(255,255,255,.3);text-align:center;padding:10px}
</style>
</head>
<body>
<nav class="sidebar">
  <div class="logo">NS</div>
  <div class="menu">
    <a href="#dash" class="active" title="Dashboard"><svg viewBox="0 0 24 24"><path d="M13 3h-2v10h2V3zm4.83 2.17l-1.42 1.42A6.92 6.92 0 0 1 19 12c0 3.87-3.13 7-7 7s-7-3.13-7-7c0-1.7.6-3.28 1.58-4.42L5.17 5.17A8.932 8.932 0 0 0 3 12a9 9 0 1 0 18 0c0-2.48-1-4.73-2.58-6.42z"/></svg></a>
    <a href="#analytics" title="Analytics"><svg viewBox="0 0 24 24"><path d="M12 3C4.9 3 2 5.43 2 8v8c0 2.57 2.9 5 10 5s10-2.43 10-5V8c0-2.57-2.9-5-10-5zm0 2c6.72 0 8 2.01 8 3v.5c-1.44.22-3.13.5-5 .5s-3.56-.28-5-.5V8c0-.99 1.28-3 8-3zm0 13c-6.72 0-8-2.01-8-3v-3.5c1.44-.22 3.13-.5 5-.5s3.56.28 5 .5V16c0 .99-1.28 3-8 3z"/></svg></a>
    <a href="#profile" title="Profile"><svg viewBox="0 0 24 24"><path d="M12 12c2.21 0 4-1.79 4-4s-1.79-4-4-4-4 1.79-4 4 1.79 4 4 4zm0 2c-2.67 0-8 1.34-8 4v2h16v-2c0-2.66-5.33-4-8-4z"/></svg></a>
    <a href="#terminal" title="Terminal"><svg viewBox="0 0 24 24"><path d="M4 11h16v2H4z"/><path d="M6.41 6.41L4.3 8.53 9.77 14l-5.47 5.47 2.12 2.12L14 16.23l5.47 5.47 2.12-2.12L16.23 14l5.47-5.47-2.12-2.12L14 11.77z"/></svg></a>
    <a href="#settings" title="Settings"><svg viewBox="0 0 24 24"><path d="M19.43 12.98c.04-.32.07-.64.07-.98s-.03-.66-.07-.98l2.11-1.65c.19-.15.24-.42.12-.64l-2-3.46c-.12-.22-.39-.3-.61-.22l-2.49 1c-.52-.4-1.08-.73-1.69-.98l-.38-2.65C14.46 2.18 14.25 2 14 2h-4c-.25 0-.46.18-.49.42l-.38 2.65c-.61.25-1.17.59-1.69.98l-2.49-1c-.23-.09-.49 0-.61.22l-2 3.46c-.13.22-.07.49.12.64l2.11 1.65c-.04.32-.07.65-.07.98s.03.66.07.98l-2.11 1.65c-.19.15-.24.42.12.64l2 3.46c.12.22.39.3.61.22l2.49-1c.52.4 1.08.73 1.69.98l.38 2.65c.03.24.24.42.49.42h4c.25 0 .46-.18.49-.42l.38-2.65c.61-.25 1.17-.59 1.69-.98l2.49 1c.23.09.49 0 .61-.22l2-3.46c.12-.22.07-.49-.12-.64l-2.11-1.65zM12 15.5c-1.93 0-3.5-1.57-3.5-3.5s1.57-3.5 3.5-3.5 3.5 1.57 3.5 3.5-1.57 3.5-3.5 3.5z"/></svg></a>
  </div>
</nav>

<main>
  <!-- DASHBOARD -->
  <section id="tab-dash">
    <div class="row" style="margin-bottom:12px">
      <div class="stat"><div class="k">Status</div><div id="status" class="v" style="color:#44ff88">RUNNING</div></div>
      <div class="stat"><div class="k">Uptime</div><div id="uptime" class="v">--:--:--</div></div>
      <div class="stat"><div class="k">Time</div><div id="clock" class="v">--:--:--</div></div>
      <div class="stat"><div class="k">Plan</div><div id="sPlan" class="v">Standard</div></div>
      <div class="stat"><button class="btn" id="btnToggle">Pause</button> <button class="btn bad" id="btnReset">Reset</button></div>
    </div>
    <div class="card">
      <div class="row">
        <div class="stat"><div class="k">Total Hashrate</div><div id="ghr" class="v">-- MH/s</div></div>
        <div class="stat"><div class="k">Power</div><div id="gpw" class="v">-- W</div></div>
        <div class="stat"><div class="k">Temp</div><div id="gtmp" class="v">-- °C</div></div>
        <div class="stat" style="flex:1.5"><div class="k">Balance</div><div id="bal" class="v">0.000000 SIM</div></div>
      </div>
    </div>

    <div class="row" style="margin-top:12px">
      <div class="canvasBox" style="flex:1"><canvas id="chartBalance" height="140"></canvas></div>
      <div class="canvasBox" style="width:280px"><canvas id="chartLatency" height="140"></canvas></div>
    </div>

    <div style="margin-top:12px">
      <div class="k">Workers</div>
      <div id="workers" class="list"></div>
    </div>
  </section>

  <!-- ANALYTICS -->
  <section id="tab-analytics" style="display:none">
    <div class="k">Advanced Analytics</div>
    <div id="panelAnalytics" class="locked">
      <div class="lock"><svg class="neon-ico" viewBox="0 0 24 24"><path d="M6 10h12v10H6z" fill="#fff" fill-opacity=".1"/><path d="M8 10V7a4 4 0 1 1 8 0v3" fill="none" stroke="#fff"/></svg></div>
      <div class="row" style="margin-top:6px">
        <div class="canvasBox" style="flex:1"><canvas id="chartHash" height="160"></canvas></div>
        <div class="canvasBox" style="flex:1"><canvas id="chartPower" height="160"></canvas></div>
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
            <input id="nameInput" style="width:100%;padding:8px;border-radius:10px;border:1px solid rgba(255,255,255,.08);background:rgba(255,255,255,.03);color:#eaf8ff" value="Neon Operator">
            <div class="row" style="margin-top:8px">
              <div class="badge std" id="planBadge">STANDARD PLAN</div>
              <select id="planSel" style="padding:8px;border-radius:10px;background:rgba(255,255,255,.04);color:#eaf8ff">
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
      <div class="stat"><div class="k">Plan</div><div class="v" id="vPlan">Standard</div></div>
      <div class="stat"><div class="k">RGB Sync</div><div class="v" id="vRGB">On</div></div>
      <div class="stat"><div class="k">Latency</div><div class="v" id="vLat">-- ms</div></div>
    </div>
    <div style="margin-top:10px">
      <button class="btn" id="btnTheme">Cycle Theme</button>
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
</body></html>
)HTML";
const char MANIFEST_JSON[] PROGMEM = R"JSON({ "name":"NEON SIM", "short_name":"NEON SIM", "start_url":"/", "display":"standalone", "background_color":"#040614", "theme_color":"#00e5ff", "icons":[ { "src":"data:image/svg+xml;utf8,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Crect width='24' height='24' fill='%2300e5ff'/%3E%3C/svg%3E", "sizes":"192x192", "type":"image/svg+xml"} ] })JSON";

const char SW_JS[] PROGMEM = R"JS( // Minimal offline cache
const C='neon-sim-v2';
self.addEventListener('install',e=>{e.waitUntil(caches.open(C).then(c=>c.addAll(['/','/manifest.json'])));});
self.addEventListener('fetch',e=>{e.respondWith(caches.match(e.request).then(r=>r||fetch(e.request)));});
)JS";

const char DEFAULT_AVATAR[] PROGMEM = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#eaf8ff"><path d="M12 12c2.21 0 4-1.79 4-4s-1.79-4-4-4-4 1.79-4 4 1.79 4 4 4zm0 2c-2.67 0-8 1.34-8 4v2h16v-2c0-2.66-5.33-4-8-4z"/></svg>
)SVG";

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
  String body=server.arg(0); // naive parse for {"name":"..","plan":".."}
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
V3 cubePts[8]={ {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                {-1,-1, 1},{1,-1, 1},{1,1, 1},{-1,1, 1} };
int cubeEdges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
void drawCube(float ang, TFT_eSPI& canvas){
  int cx=WIDTH/2, cy=HEIGHT/2+4; float s=26;
  auto rot=[&](V3 p){
    float sa=sin(ang), ca=cos(ang);
    float sb=sin(ang*0.7), cb=cos(ang*0.7);
    // Rot Y
    float r_x = p.x*ca - p.z*sa;
    float r_z = p.x*sa + p.z*ca;
    // Rot X (using original p.y and rotated z)
    float r_y = p.y*cb - r_z*sb;
    r_z = p.y*sb + r_z*cb;
    return V3{r_x, r_y, r_z};
  };
  V3 pr[8];
  for(int i=0;i<8;i++){
    V3 r=rot(cubePts[i]);
    float d=3/(r.z+4.5f);
    pr[i]={r.x*d,r.y*d,r.z};
  }
  uint16_t col=canvas.color565(0,220,255);
  for(int e=0;e<12;e++){
    int a=cubeEdges[e][0], b=cubeEdges[e][1];
    int x1=cx+pr[a].x*s, y1=cy+pr[a].y*s, x2=cx+pr[b].x*s, y2=cy+pr[b].y*s;
    canvas.drawLine(x1,y1,x2,y2,col);
  }
}

void drawBars(TFT_eSPI& canvas){
  int baseY=HEIGHT-12;
  for(int i=0;i<6;i++){
    int h= 4 + (i*3) + (millis()/7 % 14);
    uint16_t c=canvas.color565(0,120+20*i,255);
    canvas.fillRect(6+i*12, baseY-h, 8, h, c);
  }
}
void drawTicker(){ // scroll 1 px
  tft.scroll(-1); // requires setRotation supports scroll, else just redraw
}

// ---------- Setup & Loop ----------
void setup(){
  Serial.begin(115200);
  randomSeed(esp_random());

  // Display
  tft.init();
  tft.setRotation(1); // 320x72 wide
  WIDTH=tft.width();
  HEIGHT=tft.height();
  spr.createSprite(WIDTH, HEIGHT);
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
  server.on("/upload", HTTP_POST, { hUploadFinish(); }, hUploadStream);
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
      if(workers[i].online){ H+=workers[i].hashrate; P+=workers[i].power; }
    }
    globalHashrate = H;
    globalPower = P;
    acceptedShares += random(0,4);
    rejectedShares += random(0,1);
    latencyMs = constrain(latencyMs + random(-4,5), 16, 120);
    deviceTemp = constrain(deviceTemp + random(-1,2), 36, 78);
    if(simRunning && random(0,100)<55){
      double t=random(1,90)/100000.0;
      totalExtracted+=t;
    }
    pushBalance(curBalance());
    // toggle occasional worker/drop
    if(random(0,100)<8){
      int r=random(0,workerCount);
      workers[r].online = !workers[r].online;
    }
  }

  // LCD update ~80ms
  if(millis()-lastLCDMs > 80){
    lastLCDMs = millis();
    spr.fillSprite(TFT_BLACK);

    // header
    spr.setTextColor(spr.color565(0,229,255));
    spr.setTextDatum(TL_DATUM);
    spr.drawString("NEON SIM", 6, 2, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(String(globalHashrate,0)+"MH "+String(globalPower,0)+"W "+String(deviceTemp,0)+"C", 140, 2, 2);

    // 3D cube
    cubeAngle += 0.08f;
    drawCube(cubeAngle, spr);

    // neon bars
    drawBars(spr);

    // ticker text
    spr.setTextColor(TFT_WHITE);
    spr.drawString(tickerLines[(tickerPos/20)%6], 6, HEIGHT-26, 2);
    tickerPos++;

    spr.pushSprite(0,0);
  }

  // RGB LED pulse ~50ms
#if USE_NEOPIXEL
  if(rgbSync && millis()-lastRGBMs > 50){
    lastRGBMs = millis();
    uint8_t lumen = (uint8_t) (20 + (int)(10.0 * abs(sin(millis()/200.0))));
    uint8_t b = (uint8_t) min(255.0f, (globalHashrate/2.0f));
    rgb.setPixelColor(0, rgb.Color(lumen, lumen, b));
    rgb.show();
  }
#endif
}
