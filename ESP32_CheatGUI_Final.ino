/* =====================================================================================
NEON SIM — Waveshare ESP32-S3 1.47" (72×320) LCD + Cyberpunk PWA (Single .ino)
• Local SoftAP-hosted PWA with custom neon SVG glyphs (web-only), NOT real emoji.
• NiceHash-inspired cosmetic simulator: global/per-worker hashrate, shares, temps, power, efficiency, earnings, latency, uptime, clock, balance chart, fleet view.
• Plans: Standard (default) vs Premium (unlocks analytics/fleet/export+), with frosted lock overlays in Standard.
• Profile page: avatar upload from phone (stored in SPIFFS), editable display name, plan badge.
• ESP32 LCD: boot fake CMD installer, rotating 3D wireframe cube, neon bars, scrolling stats ticker.
• Optional WS2812 RGB LED pulse synced to "hashrate".
• Strictly offline/ethical: no wallets, no chains, no network calls.

Hardware: Waveshare ESP32-S3 1.47" LCD Display Dev Board (72×320, 262K colors)
Display driver expected: ST7789 via TFT_eSPI (configure your User_Setup.h accordingly)
===================================================================================== */

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
TFT_eSprite canvas = TFT_eSprite(&tft);
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
const char INDEX_HTML[] PROGMEM =
"<!doctype html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"  <title>NEON SIM</title>\n"
"  <link rel=\"manifest\" href=\"/manifest.json\">\n"
"  <meta name=\"theme-color\" content=\"#00e5ff\">\n"
"  <style>\n"
"    :root { --n1:#00e5ff; --n2:#ff00e5; --bg:#040614; --fg:#eaf8ff; --card:#0c102a; --bad:#ff4d4d; }\n"
"    body { font-family:system-ui,-apple-system,sans-serif; background:var(--bg); color:var(--fg); margin:0; display:flex; padding: 8px; }\n"
"    main { flex:1; }\n"
"    aside.rightbar { width:280px; padding-left:12px; border-left:1px solid rgba(255,255,255,.05); }\n"
"    .row { display:flex; gap:10px; align-items:center; }\n"
"    .k { font-size:.8rem; opacity:.7; text-transform:uppercase; }\n"
"    .v { font-size:1.4rem; font-weight:500; }\n"
"    .stat { flex:1; }\n"
"    .card { background:var(--card); border-radius:16px; padding:12px; margin-bottom: 10px; }\n"
"    .list { display:grid; grid-template-columns:repeat(auto-fill,minmax(220px,1fr)); gap:10px; }\n"
"    .btn { padding:10px 14px; border:0; border-radius:10px; background:linear-gradient(45deg,var(--n1),var(--n2)); color:white; font-weight:500; cursor:pointer; }\n"
"    .btn.alt { background:rgba(255,255,255,.1); }\n"
"    .btn.bad { background:#442233; color:var(--bad); border:1px solid var(--bad); }\n"
"    .t { font-family:monospace; white-space:pre; background:black; border:1px solid #333; padding:8px; border-radius:8px; height:180px; overflow-y:scroll; }\n"
"    nav { display:flex; flex-direction: column; gap:10px; margin-right:12px; }\n"
"    nav a { color:var(--fg); text-decoration:none; padding:8px 12px; border-radius:10px; background:rgba(255,255,255,.05); }\n"
"    nav a.active { background:var(--n1); color:black; }\n"
"    .locked { position:relative; }\n"
"    .locked .lock { position:absolute; inset:0; backdrop-filter:blur(4px); -webkit-backdrop-filter:blur(4px); display:flex; align-items:center; justify-content:center; z-index:1; border-radius: 12px;}\n"
"    .lock svg { width:48px; height:48px; }\n"
"    .small { font-size:.8rem; opacity:.7; }\n"
"    footer { text-align:center; font-size: .8rem; opacity: .5; padding-top: 12px; }\n"
"    input, select { font-family:inherit; font-size:inherit; color: inherit; background:rgba(255,255,255,.03); border:1px solid rgba(255,255,255,.1); padding:8px; border-radius:10px; }\n"
"    .offline { opacity:0.4; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"<nav>\n"
"  <a href=\"#\" id=\"nav-dash\" class=\"active\">Dashboard</a>\n"
"  <a href=\"#\" id=\"nav-analytics\">Analytics</a>\n"
"  <a href=\"#\" id=\"nav-profile\">Profile</a>\n"
"  <a href=\"#\" id=\"nav-terminal\">Terminal</a>\n"
"  <a href=\"#\" id=\"nav-settings\">Settings</a>\n"
"</nav>\n"
"<main>\n"
"  <!-- DASHBOARD -->\n"
"  <section id=\"tab-dash\">\n"
"    <div class=\"row\">\n"
"      <div class=\"stat card\"><div class=\"k\">Uptime</div><div class=\"v\" id=\"vUptime\">--:--:--</div></div>\n"
"      <div class=\"stat card\"><div class=\"k\">Total Hashrate</div><div class=\"v\" id=\"vHash\">-- MH/s</div></div>\n"
"      <div class=\"stat card\"><div class=\"k\">Power</div><div class=\"v\" id=\"vPower\">-- W</div></div>\n"
"      <div class=\"stat card\"><div class=\"k\">Temp</div><div class=\"v\" id=\"vTemp\">-- &deg;C</div></div>\n"
"    </div>\n"
"    <div class=\"card\">\n"
"      <div class=\"k\">Balance</div><div class=\"v\" id=\"vBal\">0.000000 SIM</div>\n"
"    </div>\n"
"    <div class=\"row\" style=\"margin-top:12px\">\n"
"      <div class=\"card\" style=\"flex:1\"><canvas id=\"chartBalance\" height=\"140\"></canvas></div>\n"
"      <div class=\"card\" style=\"width:280px\"><canvas id=\"chartLatency\" height=\"140\"></canvas></div>\n"
"    </div>\n"
"\n"
"    <div style=\"margin-top:12px\">\n"
"      <div class=\"k\">Workers</div>\n"
"      <div id=\"workers\" class=\"list\"></div>\n"
"    </div>\n"
"  </section>\n"
"\n"
"  <!-- ANALYTICS -->\n"
"  <section id=\"tab-analytics\" style=\"display:none\">\n"
"    <div class=\"k\">Advanced Analytics</div>\n"
"    <div id=\"panelAnalytics\" class=\"locked card\">\n"
"      <div class=\"lock\"><svg class=\"neon-ico\" viewBox=\"0 0 24 24\"><path d=\"M6 10h12v10H6z\" fill=\"#fff\" fill-opacity=\".1\"/><path d=\"M8 10V7a4 4 0 1 1 8 0v3\" fill=\"none\" stroke=\"#fff\"/></svg></div>\n"
"      <div class=\"row\" style=\"margin-top:6px\">\n"
"        <div style=\"flex:1\"><canvas id=\"chartHash\" height=\"160\"></canvas></div>\n"
"        <div style=\"flex:1\"><canvas id=\"chartPower\" height=\"160\"></canvas></div>\n"
"      </div>\n"
"      <div class=\"row\" style=\"margin-top:12px\">\n"
"        <div class=\"stat\"><div class=\"k\">Shares (A/R)</div><div class=\"v\" id=\"sar\">-- / --</div></div>\n"
"        <div class=\"stat\"><div class=\"k\">Efficiency</div><div class=\"v\" id=\"seff\">-- %</div></div>\n"
"        <div class=\"stat\"><div class=\"k\">Avg Latency</div><div class=\"v\" id=\"slat\">-- ms</div></div>\n"
"      </div>\n"
"    </div>\n"
"    <div class=\"small\" style=\"margin-top:8px\">Upgrade to Premium to unlock analytics.</div>\n"
"  </section>\n"
"\n"
"  <!-- PROFILE -->\n"
"  <section id=\"tab-profile\" style=\"display:none\">\n"
"    <div class=\"row\" style=\"align-items:flex-start\">\n"
"      <div class=\"card\" style=\"flex:1\">\n"
"        <div class=\"row\">\n"
"          <div style=\"width:96px;height:96px;border-radius:14px;background:linear-gradient(135deg,var(--n1),var(--n2));overflow:hidden\"><img id=\"avatarImg\" src=\"/avatar\" style=\"width:100%;height:100%;object-fit:cover\" onerror=\"this.src='/default_avatar'\"></div>\n"
"          <div style=\"flex:1\">\n"
"            <div class=\"k\">Display name</div>\n"
"            <input id=\"nameInput\" style=\"width:100%;\" value=\"Neon Operator\">\n"
"            <div class=\"row\" style=\"margin-top:8px\">\n"
"              <div id=\"planBadge\">STANDARD</div>\n"
"              <select id=\"planSel\">\n"
"                <option value=\"Standard\">Standard</option>\n"
"                <option value=\"Premium\">Premium</option>\n"
"              </select>\n"
"              <button class=\"btn\" id=\"btnSaveProf\">Save</button>\n"
"            </div>\n"
"          </div>\n"
"        </div>\n"
"        <div class=\"k\" style=\"margin-top:10px\">Upload avatar</div>\n"
"        <form id=\"avatarForm\" enctype=\"multipart/form-data\" method=\"post\" action=\"/upload\">\n"
"          <input type=\"file\" name=\"avatar\" accept=\"image/*\" style=\"width:100%;margin-top:6px\">\n"
"          <div class=\"row\" style=\"margin-top:8px\">\n"
"            <button class=\"btn\" type=\"submit\">Upload</button>\n"
"            <button class=\"btn bad\" type=\"button\" id=\"btnRemoveAvatar\">Remove</button>\n"
"          </div>\n"
"        </form>\n"
"      </div>\n"
"\n"
"      <div class=\"card\" style=\"width:320px\">\n"
"        <div class=\"k\">Premium Toolkit</div>\n"
"        <div id=\"panelPremium\" class=\"locked\" style=\"margin-top:6px;padding:8px;border-radius:12px\">\n"
"          <div class=\"lock\"><svg class=\"neon-ico\" viewBox=\"0 0 24 24\"><path d=\"M6 10h12v10H6z\" fill=\"#fff\" fill-opacity=\".1\"/><path d=\"M8 10V7a4 4 0 1 1 8 0v3\" fill=\"none\" stroke=\"#fff\"/></svg></div>\n"
"          <ul style=\"margin:0;padding-left:16px\">\n"
"            <li>CSV export</li>\n"
"            <li>Per-worker tuning</li>\n"
"            <li>Predictive earnings</li>\n"
"            <li>Fleet overview</li>\n"
"          </ul>\n"
"        </div>\n"
"      </div>\n"
"    </div>\n"
"  </section>\n"
"\n"
"  <!-- TERMINAL -->\n"
"  <section id=\"tab-terminal\" style=\"display:none\">\n"
"    <div class=\"k\">Fake Installer Console</div>\n"
"    <div id=\"term\" class=\"t\" role=\"log\" aria-live=\"polite\"></div>\n"
"    <div class=\"row\" style=\"margin-top:8px\">\n"
"      <button class=\"btn\" id=\"btnInstall\">Run installer</button>\n"
"      <button class=\"btn alt\" id=\"btnRGB\">Toggle RGB Sync</button>\n"
"    </div>\n"
"  </section>\n"
"\n"
"  <!-- SETTINGS -->\n"
"  <section id=\"tab-settings\" style=\"display:none\">\n"
"    <div class=\"row\">\n"
"      <div class=\"stat card\"><div class=\"k\">Plan</div><div class=\"v\" id=\"vPlan\">Standard</div></div>\n"
"      <div class=\"stat card\"><div class=\"k\">RGB Sync</div><div class=\"v\" id=\"vRGB\">On</div></div>\n"
"      <div class=\"stat card\"><div class=\"k\">Latency</div><div class=\"v\" id=\"vLat\">-- ms</div></div>\n"
"    </div>\n"
"    <div style=\"margin-top:10px\">\n"
"      <button class=\"btn\" id=\"btnTheme\">Cycle Theme (Not Impl)</button>\n"
"    </div>\n"
"  </section>\n"
"</main>\n"
"\n"
"<!-- RIGHT BAR -->\n"
"<aside class=\"rightbar\">\n"
"  <div class=\"card\">\n"
"    <div class=\"k\">Pool / Shares</div>\n"
"    <div class=\"row\" style=\"margin-top:6px\">\n"
"      <div class=\"stat\"><div class=\"k\">Accepted</div><div class=\"v\" id=\"vAcc\">--</div></div>\n"
"      <div class=\"stat\"><div class=\"k\">Rejected</div><div class=\"v\" id=\"vRej\">--</div></div>\n"
"      <div class=\"stat\"><div class=\"k\">Efficiency</div><div class=\"v\" id=\"vEff\">-- %</div></div>\n"
"    </div>\n"
"  </div>\n"
"\n"
"  <div class=\"card\">\n"
"    <div class=\"k\">Fleet (Premium)</div>\n"
"    <div id=\"fleet\" class=\"list\"></div>\n"
"  </div>\n"
"\n"
"  <div class=\"card\">\n"
"    <div class=\"k\">About</div>\n"
"    <div class=\"small\" style=\"margin-top:6px\">This simulator is local and harmless. No wallet or chain access. Install to Home Screen to run as an app.</div>\n"
"  </div>\n"
"</aside>\n"
"<footer>Device: 192.168.4.1 - All data synthetic - (c) NeonSim</footer>\n"
"<script>\n"
"/*!\n"
" * Chart.js v4.5.0\n"
" * https://www.chartjs.org\n"
" * (c) 2025 Chart.js Contributors\n"
" * Released under the MIT License\n"
" */\n"
"!function(t,e){\"object\"==typeof exports&&\"undefined\"!=typeof module?module.exports=e():\"function\"==typeof define&&define.amd?define(e):(t=\"undefined\"!=typeof globalThis?globalThis:t||self).Chart=e()}(this,(function(){\"use strict\";var t=Object.freeze({__proto__:null,get Colors(){return Jo},get Decimation(){return ta},get Filler(){return ba},get Legend(){return Ma},get SubTitle(){return Pa},get Title(){return ka},get Tooltip(){return Na}});function e(){}const i=(()=>{let t=0;return()=>t++})();function s(t){return null==t}function n(t){if(Array.isArray&&Array.isArray(t))return!0;const e=Object.prototype.toString.call(t);return\"[object\"===e.slice(0,7)&&\"Array]\"===e.slice(-6)}function o(t){return null!==t&&\"[object Object]\"===Object.prototype.toString.call(t)}function a(t){return(\"number\"==typeof t||t instanceof Number)&&isFinite(+t)}function r(t,e){return a(t)?t:e}function l(t,e){return void 0===t?e:t}const h=(t,e)=>\"string\"==typeof t&&t.endsWith(\"%\")?parseFloat(t)/100:+t/e,c=(t,e)=>\"string\"==typeof t&&t.endsWith(\"%\")?parseFloat(t)/100*e:+t;function d(t,e,i){if(t&&\"function\"==typeof t.call)return t.apply(i,e)}function u(t,e,i,s){let a,r,l;if(n(t))if(r=t.length,s)for(a=r-1;a>=0;a--)e.call(i,t[a],a);else for(a=0;a<r;a++)e.call(i,t[a],a);else if(o(t))for(l=Object.keys(t),r=l.length,a=0;a<r;a++)e.call(i,t[l[a]],l[a])}function f(t,e){let i,s,n,o;if(!t||!e||t.length!==e.length)return!1;for(i=0,s=t.length;i<s;++i)if(n=t[i],o=e[i],n.datasetIndex!==o.datasetIndex||n.index!==o.index)return!1;return!0}function g(t){if(n(t))return t.map(g);if(o(t)){const e=Object.create(null),i=Object.keys(t),s=i.length;let n=0;for(;n<s;++n)e[i[n]]=g(t[i[n]]);return e}return t}function p(t){return-1===[\"__proto__\",\"prototype\",\"constructor\"].indexOf(t)}function m(t,e,i,s){if(!p(t))return;const n=e[t],a=i[t];o(n)&&o(a)?x(n,a,s):e[t]=g(a)}function x(t,e,i){const s=n(e)?e:[e],a=s.length;if(!o(t))return t;const r=(i=i||{}).merger||m;let l;for(let e=0;e<a;++e){if(l=s[e],!o(l))continue;const n=Object.keys(l);for(let e=0,s=n.length;e<s;++e)r(n[e],t,l,i)}return t}function b(t,e){return x(t,e,{merger:_})}function _(t,e,i){if(!p(t))return;const s=e[t],n=i[t];o(s)&&o(n)?b(s,n):Object.prototype.hasOwnProperty.call(e,t)||(e[t]=g(n))}const y={\"\":t=>t,x:t=>t.x,y:t=>t.y};function v(t){const e=t.split(\".\"),i=[];let s=\"\";for(const t of e)s+=t,s.endsWith(\"\\\\\")?s=s.slice(0,-1)+\".\":(i.push(s),s=\"\");return i}function M(t,e){const i=y[e]||(y[e]=function(t){const e=v(t);return t=>{for(const i of e){if(\"\"===i)break;t=t&&t[i]}return t}}(e));return i(t)}function w(t){return t.charAt(0).toUpperCase()+t.slice(1)}const k=t=>void 0!==t,S=t=>\"function\"==typeof t,P=(t,e)=>{if(t.size!==e.size)return!1;for(const i of t)if(!e.has(i))return!1;return!0};function D(t){return\"mouseup\"===t.type||\"click\"===t.type||\"contextmenu\"===t.type}const C=Math.PI,O=2*C,A=O+C,T=Number.POSITIVE_INFINITY,L=C/180,E=C/2,R=C/4,I=2*C/3,z=Math.log10,F=Math.sign;function V(t,e,i){return Math.abs(t-e)<i}function B(t){const e=Math.round(t);t=V(t,e,t/1e3)?e:t;const i=Math.pow(10,Math.floor(z(t))),s=t/i;return(s<=1?1:s<=2?2:s<=5?5:10)*i}function W(t){const e=[],i=Math.sqrt(t);let s;for(s=1;s<i;s++)t%s==0&&(e.push(s),e.push(t/s));return i===(0|i)&&e.push(i),e.sort(((t,e)=>t-e)).pop(),e}function N(t){return!function(t){return\"symbol\"==typeof t||\"object\"==typeof t&&null!==t&&!(Symbol.toPrimitive in t||\"toString\"in t||\"valueOf\"in t)}(t)&&!isNaN(parseFloat(t))&&isFinite(t)}function H(t,e){const i=Math.round(t);return i-e<=t&&i+e>=t}function j(t,e,i){let s,n,o;for(s=0,n=t.length;s<n;s++)o=t[s][i],isNaN(o)||(e.min=Math.min(e.min,o),e.max=Math.max(e.max,o))}function $(t){return t*(C/180)}function Y(t){return t*(180/C)}function U(t){if(!a(t))return;let e=1,i=0;for(;Math.round(t*e)/e!==t;)e*=10,i++;return i}function X(t,e){const i=e.x-t.x,s=e.y-t.y,n=Math.sqrt(i*i+s*s);let o=Math.atan2(s,i);return o<-.5*C&&(o+=O),{angle:o,distance:n}}function q(t,e){return Math.sqrt(Math.pow(e.x-t.x,2)+Math.pow(e.y-t.y,2))}function K(t,e){return(t-e+A)%O-C}function G(t){return(t%O+O)%O}function J(t,e,i,s){const n=G(t),o=G(e),a=G(i),r=G(o-n),l=G(a-n),h=G(n-o),c=G(n-a);return n===o||n===a||s&&o===a||r>l&&h<c}function Z(t,e,i){return Math.max(e,Math.min(i,t))}function Q(t){return Z(t,-32768,32767)}function tt(t,e,i,s=1e-6){return t>=Math.min(e,i)-s&&t<=Math.max(e,i)+s}function et(t,e,i){i=i||(i=>t[i]<e);let s,n=t.length-1,o=0;for(;n-o>1;)s=o+n>>1,i(s)?o=s:n=s;return{lo:o,hi:n}}const it=(t,e,i,s)=>et(t,i,s?s=>{const n=t[s][e];return n<i||n===i&&t[s+1][e]===i}:s=>t[s][e]<i),st=(t,e,i)=>et(t,i,(s=>t[s][e]>=i));function nt(t,e,i){let s=0,n=t.length;for(;s<n&&t[s]<e;)s++;for(;n>s&&t[n-1]>i;)n--;return s>0||n<t.length?t.slice(s,n):t}const ot=[\"push\",\"pop\",\"shift\",\"splice\",\"unshift\"];function at(t,e){t._chartjs?t._chartjs.listeners.push(e):(Object.defineProperty(t,\"_chartjs\",{configurable:!0,enumerable:!1,value:{listeners:[e]}}),ot.forEach((e=>{const i=\"_onData\"+w(e),s=t[e];Object.defineProperty(t,e,{configurable:!0,enumerable:!1,value(...e){const n=s.apply(this,e);return t._chartjs.listeners.forEach((t=>{\"function\"==typeof t[i]&&t[i](...e)})),n}})})))}function rt(t,e){const i=t._chartjs;if(!i)return;const s=i.listeners,n=s.indexOf(e);-1!==n&&s.splice(n,1),s.length>0||(ot.forEach((e=>{delete t[e]})),delete t._chartjs)}function lt(t){const e=new Set(t);return e.size===t.length?t:Array.from(e)}const ht=\"undefined\"==typeof window?function(t){return t()}:window.requestAnimationFrame;function ct(t,e){let i=[],s=!1;return function(...n){i=n,s||(s=!0,ht.call(window,(()=>{s=!1,t.apply(e,i)})))}}function dt(t,e){let i;return function(...s){return e?(clearTimeout(i),i=setTimeout(t,e,s)):t.apply(this,s),e}}const ut=t=>\"start\"===t?\"left\":\"end\"===t?\"right\":\"center\",ft=(t,e,i)=>\"start\"===t?e:\"end\"===t?i:(e+i)/2,gt=(t,e,i,s)=>t===(s?\"left\":\"right\")?i:\"center\"===t?(e+i)/2:e;function pt(t,e,i){const n=e.length;let o=0,a=n;if(t._sorted){const{iScale:r,vScale:l,_parsed:h}=t,c=t.dataset&&t.dataset.options?t.dataset.options.spanGaps:null,d=r.axis,{min:u,max:f,minDefined:g,maxDefined:p}=r.getUserBounds();if(g){if(o=Math.min(it(h,d,u).lo,i?n:it(e,d,r.getPixelForValue(u)).lo),c){const t=h.slice(0,o+1).reverse().findIndex((t=>!s(t[l.axis])));o-=Math.max(0,t)}o=Z(o,0,n-1)}if(p){let t=Math.max(it(h,r.axis,f,!0).hi+1,i?0:it(e,d,r.getPixelForValue(f),!0).hi+1);if(c){const e=h.slice(t-1).findIndex((t=>!s(t[l.axis])));t+=Math.max(0,e)}a=Z(t,o,n)-o}else a=n-o}return{start:o,count:a}}function mt(t){const{xScale:e,yScale:i,_scaleRanges:s}=t,n={xmin:e.min,xmax:e.max,ymin:i.min,ymax:i.max};if(!s)return t._scaleRanges=n,!0;const o=s.xmin!==e.min||s.xmax!==e.max||s.ymin!==i.min||s.ymax!==i.max;return Object.assign(s,n),o}class xt{constructor(){this._request=null,this._charts=new Map,this._running=!1,this._lastDate=void 0}_notify(t,e,i,s){const n=e.listeners[s],o=e.duration;n.forEach((s=>s({chart:t,initial:e.initial,numSteps:o,currentStep:Math.min(i-e.start,o)})))}_refresh(){this._request||(this._running=!0,this._request=ht.call(window,(()=>{this._update(),this._request=null,this._running&&this._refresh()})))}_update(t=Date.now()){let e=0;this._charts.forEach(((i,s)=>{if(!i.running||!i.items.length)return;const n=i.items;let o,a=n.length-1,r=!1;for(;a>=0;--a)o=n[a],o._active?(o._total>i.duration&&(i.duration=o._total),o.tick(t),r=!0):(n[a]=n[n.length-1],n.pop());r&&(s.draw(),this._notify(s,i,t,\"progress\")),n.length||(i.running=!1,this._notify(s,i,t,\"complete\"),i.initial=!1),e+=n.length})),this._lastDate=t,0===e&&(this._running=!1)}_getAnims(t){const e=this._charts;let i=e.get(t);return i||(i={running:!1,initial:!0,items:[],listeners:{complete:[],progress:[]}},e.set(t,i)),i}listen(t,e,i){this._getAnims(t).listeners[e].push(i)}add(t,e){e&&e.length&&this._getAnims(t).items.push(...e)}has(t){return this._getAnims(t).items.length>0}start(t){const e=this._charts.get(t);e&&(e.running=!0,e.start=Date.now(),e.duration=e.items.reduce(((t,e)=>Math.max(t,e._duration)),0),this._refresh())}running(t){if(!this._running)return!1;const e=this._charts.get(t);return!!(e&&e.running&&e.items.length)}stop(t){const e=this._charts.get(t);if(!e||!e.items.length)return;const i=e.items;let s=i.length-1;for(;s>=0;--s)i[s].cancel();e.items=[],this._notify(t,e,Date.now(),\"complete\")}remove(t){return this._charts.delete(t)}}var bt=new xt;\n"
"/*!\n"
" * @kurkle/color v0.3.2\n"
" * https://github.com/kurkle/color#readme\n"
" * (c) 2023 Jukka Kurkela\n"
" * Released under the MIT License\n"
" */function _t(t){return t+.5|0}const yt=(t,e,i)=>Math.max(Math.min(t,i),e);function vt(t){return yt(_t(2.55*t),0,255)}function Mt(t){return yt(_t(255*t),0,255)}function wt(t){return yt(_t(t/2.55)/100,0,1)}function kt(t){return yt(_t(100*t),0,100)}const St={0:0,1:1,2:2,3:3,4:4,5:5,6:6,7:7,8:8,9:9,A:10,B:11,C:12,D:13,E:14,F:15,a:10,b:11,c:12,d:13,e:14,f:15},Pt=[...\"0123456789ABCDEF\"],Dt=t=>Pt[15&t],Ct=t=>Pt[(240&t)>>4]+Pt[15&t],Ot=t=>(240&t)>>4==(15&t);function At(t){var e=(t=>Ot(t.r)&&Ot(t.g)&&Ot(t.b)&&Ot(t.a))(t)?Dt:Ct;return t?\"#\"+e(t.r)+e(t.g)+e(t.b)+((t,e)=>t<255?e(t):\"\")(t.a,e):void 0}const Tt=/^(hsla?|hwb|hsv)\\(\\s*([-+.e\\d]+)(?:deg)?[\\s,]+([-+.e\\d]+)%[\\s,]+([-+.e\\d]+)%(?:[\\s,]+([-+.e\\d]+)(%)?)?\\s*\\)$/;function Lt(t,e,i){const s=e*Math.min(i,1-i),n=(e,n=(e+t/30)%12)=>i-s*Math.max(Math.min(n-3,9-n,1),-1);return[n(0),n(8),n(4)]}function Et(t,e,i){const s=(s,n=(s+t/60)%6)=>i-i*e*Math.max(Math.min(n,4-n,1),0);return[s(5),s(3),s(1)]}function Rt(t,e,i){const s=Lt(t,1,.5);let n;for(e+i>1&&(n=1/(e+i),e*=n,i*=n),n=0;n<3;n++)s[n]*=1-e-i,s[n]+=e;return s}function It(t){const e=t.r/255,i=t.g/255,s=t.b/255,n=Math.max(e,i,s),o=Math.min(e,i,s),a=(n+o)/2;let r,l,h;return n!==o&&(h=n-o,l=a>.5?h/(2-n-o):h/(n+o),r=function(t,e,i,s,n){return t===n?(e-i)/s+(e<i?6:0):e===n?(i-t)/s+2:(t-e)/s+4}(e,i,s,h,n),r=60*r+.5),[0|r,l||0,a]}function zt(t,e,i,s){return(Array.isArray(e)?t(e[0],e[1],e[2]):t(e,i,s)).map(Mt)}function Ft(t,e,i){return zt(Lt,t,e,i)}function Vt(t){return(t%360+360)%360}function Bt(t){const e=Tt.exec(t);let i,s=255;if(!e)return;e[5]!==i&&(s=e[6]?vt(+e[5]):Mt(+e[5]));const n=Vt(+e[2]),o=+e[3]/100,a=+e[4]/100;return i=\"hwb\"===e[1]?function(t,e,i){return zt(Rt,t,e,i)}(n,o,a):\"hsv\"===e[1]?function(t,e,i){return zt(Et,t,e,i)}(n,o,a):Ft(n,o,a),{r:i[0],g:i[1],b:i[2],a:s}}const Wt={x:\"dark\",Z:\"light\",Y:\"re\",X:\"blu\",W:\"gr\",V:\"medium\",U:\"slate\",A:\"ee\",T:\"ol\",S:\"or\",B:\"ra\",C:\"lateg\",D:\"ights\",R:\"in\",Q:\"turquois\",E:\"hi\",P:\"ro\",O:\"al\",N:\"le\",M:\"de\",L:\"yello\",F:\"en\",K:\"ch\",G:\"arks\",H:\"ea\",I:\"ightg\",J:\"wh\"},Nt={OiceXe:\"f0f8ff\",antiquewEte:\"faebd7\",aqua:\"ffff\",aquamarRe:\"7fffd4\",azuY:\"f0ffff\",beige:\"f5f5dc\",bisque:\"ffe4c4\",black:\"0\",blanKedOmond:\"ffebcd\",Xe:\"ff\",XeviTet:\"8a2be2\",bPwn:\"a52a2a\",burlywood:\"deb887\",caMtXe:\"5f9ea0\",KartYuse:\"7fff00\",KocTate:\"d2691e\",cSO:\"ff7f50\",cSnflowerXe:\"6495ed\",cSnsilk:\"fff8dc\",crimson:\"dc143c\",cyan:\"ffff\",xXe:\"8b\",xcyan:\"8b8b\",xgTMnPd:\"b8860b\",xWay:\"a9a9a9\",xgYF:\"6400\",xgYy:\"a9a9a9\",xkhaki:\"bdb76b\",xmagFta:\"8b008b\",xTivegYF:\"556b2f\",xSange:\"ff8c00\",xScEd:\"9932cc\",xYd:\"8b0000\",xsOmon:\"e9967a\",xsHgYF:\"8fbc8f\",xUXe:\"483d8b\",xUWay:\"2f4f4f\",xUgYy:\"2f4f4f\",xQe:\"ced1\",xviTet:\"9400d3\",dAppRk:\"ff1493\",dApskyXe:\"bfff\",dimWay:\"696969\",dimgYy:\"696969\",dodgerXe:\"1e90ff\",fiYbrick:\"b22222\",flSOwEte:\"fffaf0\",foYstWAn:\"228b22\",fuKsia:\"ff00ff\",gaRsbSo:\"dcdcdc\",ghostwEte:\"f8f8ff\",gTd:\"ffd700\",gTMnPd:\"daa520\",Way:\"808080\",gYF:\"8000\",gYFLw:\"adff2f\",gYy:\"808080\",honeyMw:\"f0fff0\",hotpRk:\"ff69b4\",RdianYd:\"cd5c5c\",Rdigo:\"4b0082\",ivSy:\"fffff0\",khaki:\"f0e68c\",lavFMr:\"e6e6fa\",lavFMrXsh:\"fff0f5\",lawngYF:\"7cfc00\",NmoncEffon:\"fffacd\",ZXe:\"add8e6\",ZcSO:\"f08080\",Zcyan:\"e0ffff\",ZgTMnPdLw:\"fafad2\",ZWay:\"d3d3d3\",ZgYF:\"90ee90\",ZgYy:\"d3d3d3\",ZpRk:\"ffb6c1\",ZsOmon:\"ffa07a\",ZsHgYF:\"20b2aa\",ZskyXe:\"87cefa\",ZUWay:\"778899\",ZUgYy:\"778899\",ZstAlXe:\"b0c4de\",ZLw:\"ffffe0\",lime:\"ff00\",limegYF:\"32cd32\",lRF:\"faf0e6\",magFta:\"ff00ff\",maPon:\"800000\",VaquamarRe:\"66cdaa\",VXe:\"cd\",VScEd:\"ba55d3\",VpurpN:\"9370db\",VsHgYF:\"3cb371\",VUXe:\"7b68ee\",VsprRggYF:\"fa9a\",VQe:\"48d1cc\",VviTetYd:\"c71585\",midnightXe:\"191970\",mRtcYam:\"f5fffa\",mistyPse:\"ffe4e1\",moccasR:\"ffe4b5\",navajowEte:\"ffdead\",navy:\"80\",Tdlace:\"fdf5e6\",Tive:\"808000\",TivedBb:\"6b8e23\",Sange:\"ffa500\",SangeYd:\"ff4500\",ScEd:\"da70d6\",pOegTMnPd:\"eee8aa\",pOegYF:\"98fb98\",pOeQe:\"afeeee\",pOeviTetYd:\"db7093\",papayawEp:\"ffefd5\",pHKpuff:\"ffdab9\",peru:\"cd853f\",pRk:\"ffc0cb\",plum:\"dda0dd\",powMrXe:\"b0e0e6\",purpN:\"800080\",YbeccapurpN:\"663399\",Yd:\"ff0000\",Psybrown:\"bc8f8f\",PyOXe:\"4169e1\",saddNbPwn:\"8b4513\",sOmon:\"fa8072\",sandybPwn:\"f4a460\",sHgYF:\"2e8b57\",sHshell:\"fff5ee\",siFna:\"a0522d\",silver:\"c0c0c0\",skyXe:\"87ceeb\",UXe:\"6a5acd\",UWay:\"708090\",UgYy:\"708090\",snow:\"fffafa\",sprRggYF:\"ff7f\",stAlXe:\"4682b4\",tan:\"d2b48c\",teO:\"8080\",tEstN:\"d8bfd8\",tomato:\"ff6347\",Qe:\"40e0d0\",viTet:\"ee82ee\",JHt:\"f5deb3\",wEte:\"ffffff\",wEtesmoke:\"f5f5f5\",Lw:\"ffff00\",LwgYF:\"9acd32\"};let Ht;function jt(t){Ht||(Ht=function(){const t={},e=Object.keys(Nt),i=Object.keys(Wt);let s,n,o,a,r;for(s=0;s<e.length;s++){for(a=r=e[s],n=0;n<i.length;n++)o=i[n],r=r.replace(o,Wt[o]);o=parseInt(Nt[a],16),t[r]=[o>>16&255,o>>8&255,255&o]}return t}(),Ht.transparent=[0,0,0,0]);const e=Ht[t.toLowerCase()];return e&&{r:e[0],g:e[1],b:e[2],a:4===e.length?e[3]:255}}const $t=/^rgba?\\(\\s*([-+.\\d]+)(%)?[\\s,]+([-+.e\\d]+)(%)?[\\s,]+([-+.e\\d]+)(%)?(?:[\\s,/]+([-+.e\\d]+)(%)?)?\\s*\\)$/;const Yt=t=>t<=.0031308?12.92*t:1.055*Math.pow(t,1/2.4)-.055,Ut=t=>t<=.04045?t/12.92:Math.pow((t+.055)/1.055,2.4);function Xt(t,e,i){if(t){let s=It(t);s[e]=Math.max(0,Math.min(s[e]+s[e]*i,0===e?360:1)),s=Ft(s),t.r=s[0],t.g=s[1],t.b=s[2]}}function qt(t,e){return t?Object.assign(e||{},t):t}function Kt(t){var e={r:0,g:0,b:0,a:255};return Array.isArray(t)?t.length>=3&&(e={r:t[0],g:t[1],b:t[2],a:255},t.length>3&&(e.a=Mt(t[3]))):(e=qt(t,{r:0,g:0,b:0,a:1})).a=Mt(e.a),e}function Gt(t){return\"r\"===t.charAt(0)?function(t){const e=$t.exec(t);let i,s,n,o=255;if(e){if(e[7]!==i){const t=+e[7];o=e[8]?vt(t):yt(255*t,0,255)}return i=+e[1],s=+e[3],n=+e[5],i=255&(e[2]?vt(i):yt(i,0,255)),s=255&(e[4]?vt(s):yt(s,0,255)),n=255&(e[6]?vt(n):yt(n,0,255)),{r:i,g:s,b:n,a:o}}}(t):Bt(t)}class Jt{constructor(t){if(t instanceof Jt)return t;const e=typeof t;let i;var s,n,o;\"object\"===e?i=Kt(t):\"string\"===e&&(o=(s=t).length,\"#\"===s[0]&&(4===o||5===o?n={r:255&17*St[s[1]],g:255&17*St[s[2]],b:255&17*St[s[3]],a:5===o?17*St[s[4]]:255}:7!==o&&9!==o||(n={r:St[s[1]]<<4|St[s[2]],g:St[s[3]]<<4|St[s[4]],b:St[s[5]]<<4|St[s[6]],a:9===o?St[s[7]]<<4|St[s[8]]:255})),i=n||jt(t)||Gt(t)),this._rgb=i,this._valid=!!i}get valid(){return this._valid}get rgb(){var t=qt(this._rgb);return t&&(t.a=wt(t.a)),t}set rgb(t){this._rgb=Kt(t)}rgbString(){return this._valid?(t=this._rgb)&&(t.a<255?`rgba(${t.r}, ${t.g}, ${t.b}, ${wt(t.a)})`:`rgb(${t.r}, ${t.g}, ${t.b})`):void 0;var t}hexString(){return this._valid?At(this._rgb):void 0}hslString(){return this._valid?function(t){if(!t)return;const e=It(t),i=e[0],s=kt(e[1]),n=kt(e[2]);return t.a<255?`hsla(${i}, ${s}%, ${n}%, ${wt(t.a)})`:`hsl(${i}, ${s}%, ${n}%)`}(this._rgb):void 0}mix(t,e){if(t){const i=this.rgb,s=t.rgb;let n;const o=e===n?.5:e,a=2*o-1,r=i.a-s.a,l=((a*r==-1?a:(a+r)/(1+a*r))+1)/2;n=1-l,i.r=255&l*i.r+n*s.r+.5,i.g=255&l*i.g+n*s.g+.5,i.b=255&l*i.b+n*s.b+.5,i.a=o*i.a+(1-o)*s.a,this.rgb=i}return this}interpolate(t,e){return t&&(this._rgb=function(t,e,i){const s=Ut(wt(t.r)),n=Ut(wt(t.g)),o=Ut(wt(t.b));return{r:Mt(Yt(s+i*(Ut(wt(e.r))-s))),g:Mt(Yt(n+i*(Ut(wt(e.g))-n))),b:Mt(Yt(o+i*(Ut(wt(e.b))-o))),a:t.a+i*(e.a-t.a)}}(this._rgb,t._rgb,e)),this}clone(){return new Jt(this.rgb)}alpha(t){return this._rgb.a=Mt(t),this}clearer(t){return this._rgb.a*=1-t,this}greyscale(){const t=this._rgb,e=_t(.3*t.r+.59*t.g+.11*t.b);return t.r=t.g=t.b=e,this}opaquer(t){return this._rgb.a*=1+t,this}negate(){const t=this._rgb;return t.r=255-t.r,t.g=255-t.g,t.b=255-t.b,this}lighten(t){return Xt(this._rgb,2,t),this}darken(t){return Xt(this._rgb,2,-t),this}saturate(t){return Xt(this._rgb,1,t),this}desaturate(t){return Xt(this._rgb,1,-t),this}rotate(t){return function(t,e){var i=It(t);i[0]=Vt(i[0]+e),i=Ft(i),t.r=i[0],t.g=i[1],t.b=i[2]}(this._rgb,t),this}}function Zt(t){if(t&&\"object\"==typeof t){const e=t.toString();return\"[object CanvasPattern]\"===e||\"[object CanvasGradient]\"===e}return!1}function Qt(t){return Zt(t)?t:new Jt(t)}function te(t){return Zt(t)?t:new Jt(t).saturate(.5).darken(.1).hexString()}\n"
"</script>\n"
"<script>\n"
"const $ = e => document.querySelector(e);\n"
"const $$ = e => document.querySelectorAll(e);\n"
"\n"
"const navLinks = $$('nav a');\n"
"const tabs = $$('main > section');\n"
"\n"
"navLinks.forEach(link => {\n"
"  link.onclick = e => {\n"
"    e.preventDefault();\n"
"    const target = link.id.replace('nav-','');\n"
"    tabs.forEach(tab => tab.style.display = (tab.id === `tab-${target}` ? 'block' : 'none'));\n"
"    navLinks.forEach(l => l.classList.remove('active'));\n"
"    link.classList.add('active');\n"
"  }\n"
"});\n"
"\n"
"let charts = {};\n"
"const chartDefaults = {\n"
"  responsive: true, maintainAspectRatio: false,\n"
"  scales: { x:{display:false}, y:{ ticks:{color:'rgba(255,255,255,0.5)'}, grid:{color:'rgba(255,255,255,0.1)'} } },\n"
"  plugins: { legend: { display: false } }\n"
"};\n"
"\n"
"function createChart(id, type, label, data, color) {\n"
"  const ctx = $(`#${id}`).getContext('2d');\n"
"  return new Chart(ctx, {\n"
"    type: type,\n"
"    data: {\n"
"      labels: data.map((_,i)=>i),\n"
"      datasets: [{ label, data, borderColor: color, borderWidth: 2, pointRadius: 0, fill:true, tension: 0.4,\n"
"        backgroundColor: color.replace(/, ?0\\.\\d+\\)/,',0.2)')\n"
"      }]\n"
"    },\n"
"    options: chartDefaults\n"
"  });\n"
"}\n"
"\n"
"function updateStats(s) {\n"
"  $('#vUptime').textContent = new Date(s.uptime*1000).toISOString().substr(11,8);\n"
"  $('#vHash').textContent = `${s.globalHashrate.toFixed(1)} MH/s`;\n"
"  $('#vPower').textContent = `${s.globalPower.toFixed(0)} W`;\n"
"  $('#vTemp').innerHTML = `${s.deviceTemp.toFixed(0)} &deg;C`;\n"
"  $('#vBal').textContent = `${s.balance.toFixed(6)} SIM`;\n"
"  $('#vAcc').textContent = s.acceptedShares;\n"
"  $('#vRej').textContent = s.rejectedShares;\n"
"  const eff = s.acceptedShares>0 ? (s.acceptedShares/(s.acceptedShares+s.rejectedShares)*100).toFixed(2) : 0;\n"
"  $('#vEff').textContent = `${eff} %`;\n"
"  $('#vPlan').textContent = s.profilePlan;\n"
"  $('#vRGB').textContent = s.rgbSync ? 'On' : 'Off';\n"
"  $('#vLat').textContent = `${s.latencyMs} ms`;\n"
"\n"
"  $('#nameInput').value = s.profileName;\n"
"  $('#planSel').value = s.profilePlan;\n"
"  $('#planBadge').textContent = s.profilePlan.toUpperCase();\n"
"  $('#panelAnalytics').className = s.profilePlan==='Premium' ? 'card' : 'card locked';\n"
"  $('#panelPremium').className = s.profilePlan==='Premium' ? '' : 'locked';\n"
"\n"
"  let workersHTML = '';\n"
"  s.workers.forEach(w => {\n"
"    workersHTML += `<div class=\"card ${w.online?'':'offline'}\">\n"
"      <div class=\"k\">${w.name}</div>\n"
"      <div class=\"row\">\n"
"        <div class=\"stat\"><div class=\"k\">Hash</div><div class=\"v\">${w.hashrate.toFixed(1)}</div></div>\n"
"        <div class=\"stat\"><div class=\"k\">Temp</div><div class=\"v\">${w.temp}&deg;C</div></div>\n"
"        <div class=\"stat\"><div class=\"k\">Power</div><div class=\"v\">${w.power.toFixed(0)}W</div></div>\n"
"      </div>\n"
"    </div>`;\n"
"  });\n"
"  $('#workers').innerHTML = workersHTML;\n"
"  \n"
"  let fleetHTML = '';\n"
"  if (s.profilePlan === 'Premium') {\n"
"    s.fleet.forEach(f => {\n"
"      fleetHTML += `<div class=\"card ${f.online?'':'offline'}\">\n"
"        <div class=\"k\">${f.id}</div>\n"
"        <div class=\"row\">\n"
"          <div class=\"stat\"><div class=\"k\">Hash</div><div class=\"v\">${f.hashrate.toFixed(0)}</div></div>\n"
"          <div class=\"stat\"><div class=\"k\">Power</div><div class=\"v\">${f.power.toFixed(0)}W</div></div>\n"
"        </div>\n"
"      </div>`;\n"
"    });\n"
"  } else {\n"
"    fleetHTML = `<div class=\"locked\" style=\"height:100px; display:flex; align-items:center; justify-content:center;\"><span>Upgrade to see fleet</span></div>`;\n"
"  }\n"
"  $('#fleet').innerHTML = fleetHTML;\n"
"\n"
"  if (!charts.balance) {\n"
"    charts.balance = createChart('chartBalance', 'line', 'Balance', s.balanceSeries, 'rgba(0,229,255,0.7)');\n"
"    charts.latency = createChart('chartLatency', 'bar', 'Latency', [s.latencyMs], 'rgba(255,0,229,0.7)');\n"
"    charts.hash = createChart('chartHash', 'line', 'Hashrate', [], 'rgba(0,229,255,0.7)');\n"
"    charts.power = createChart('chartPower', 'line', 'Power', [], 'rgba(255,100,0,0.7)');\n"
"  } else {\n"
"    charts.balance.data.datasets[0].data = s.balanceSeries;\n"
"    charts.balance.data.labels = s.balanceSeries.map((_,i)=>i);\n"
"    charts.balance.update('none');\n"
"    \n"
"    charts.latency.data.datasets[0].data.push(s.latencyMs);\n"
"    if(charts.latency.data.datasets[0].data.length > 30) charts.latency.data.datasets[0].data.shift();\n"
"    charts.latency.update('none');\n"
"\n"
"    if(s.profilePlan === 'Premium') {\n"
"      charts.hash.data.datasets[0].data.push(s.globalHashrate);\n"
"      if(charts.hash.data.datasets[0].data.length > 60) charts.hash.data.datasets[0].data.shift();\n"
"      charts.hash.update('none');\n"
"      charts.power.data.datasets[0].data.push(s.globalPower);\n"
"      if(charts.power.data.datasets[0].data.length > 60) charts.power.data.datasets[0].data.shift();\n"
"      charts.power.update('none');\n"
"    }\n"
"  }\n"
"}\n"
"\n"
"setInterval(() => { fetch('/api/stats').then(r=>r.json()).then(updateStats); }, 1200);\n"
"fetch('/api/stats').then(r=>r.json()).then(s => { updateStats(s); $('#avatarImg').src = '/avatar?' + Date.now(); });\n"
"\n"
"$('#btnSaveProf').onclick = () => {\n"
"  fetch('/api/profile', {\n"
"    method: 'POST',\n"
"    headers: {'Content-Type':'application/json'},\n"
"    body: JSON.stringify({name: $('#nameInput').value, plan: $('#planSel').value})\n"
"  }).then(() => fetch('/api/stats').then(r=>r.json()).then(updateStats));\n"
"};\n"
"\n"
"$('#btnRemoveAvatar').onclick = () => {\n"
"  if (confirm('Remove avatar?')) {\n"
"    fetch('/removeAvatar', {method:'POST'}).then(() => { $('#avatarImg').src = '/default_avatar?' + Date.now(); });\n"
"  }\n"
"};\n"
"\n"
"$('#avatarForm').onsubmit = function(e) {\n"
"  e.preventDefault();\n"
"  var formData = new FormData(this);\n"
"  fetch('/upload', { method: 'POST', body: formData })\n"
"    .then(r => r.text()).then(t => {\n"
"      console.log(t);\n"
"      $('#avatarImg').src = '/avatar?' + Date.now();\n"
"    });\n"
"};\n"
"\n"
"$('#btnInstall').onclick = () => {\n"
"  const term = $('#term');\n"
"  term.innerHTML = '';\n"
"  const steps = [\"Initializing...\", \"Scanning shaders...\", \"Linking neon kernels...\", \"Calibrating grid...\", \"Seeding datasets...\", \"Complete.\"];\n"
"  let i = 0;\n"
"  function nextStep() {\n"
"    if (i < steps.length) {\n"
"      term.innerHTML += `> ${steps[i]}\\n`;\n"
"      term.scrollTop = term.scrollHeight;\n"
"      i++;\n"
"      setTimeout(nextStep, Math.random() * 300 + 100);\n"
"    }\n"
"  }\n"
"  nextStep();\n"
"};\n"
"\n"
"$('#btnRGB').onclick = () => { fetch('/api/rgb', {method:'POST'}); };\n"
"\n"
"if ('serviceWorker' in navigator) { navigator.serviceWorker.register('/sw.js'); }\n"
"</script>\n"
"</body>\n"
"</html>\n"
;

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
void drawCube(TFT_eSprite* spr, float ang){
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
  uint16_t col=spr->color565(0,220,255);
  for(int e=0;e<12;e++){
    int a=cubeEdges[e][0], b=cubeEdges[e][1];
    int x1=cx + pr[a].x*s, y1=cy + pr[a].y*s;
    int x2=cx + pr[b].x*s, y2=cy + pr[b].y*s;
    spr->drawLine(x1,y1,x2,y2,col);
  }
}

void drawBars(TFT_eSprite* spr){
  int baseY=HEIGHT-12;
  for(int i=0;i<6;i++){
    int h= 4 + (i*3) + (millis()/7 % 14);
    uint16_t c=spr->color565(0,120+20*i,255);
    spr->fillRect(6+i*12, baseY-h, 8, h, c);
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
  canvas.createSprite(WIDTH, HEIGHT);
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
    canvas.fillSprite(TFT_BLACK);
    // header
    canvas.setTextColor(canvas.color565(0,229,255));
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("NEON SIM", 6, 2, 2);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(String(globalHashrate,0)+"MH "+String(globalPower,0)+"W "+String(deviceTemp,0)+"C", 140, 2, 2);

    // 3D cube
    cubeAngle += 0.08f;
    drawCube(&canvas, cubeAngle);

    // neon bars
    drawBars(&canvas);

    // ticker text
    canvas.setTextColor(TFT_WHITE);
    // Update ticker lines dynamically
    tickerLines[0]="HASH "+String(globalHashrate,1)+" MH/s";
    tickerLines[1]="PWR "+String(globalPower,0)+" W";
    tickerLines[2]="BAL "+String(curBalance(),6)+" SIM";
    tickerLines[3]="ACC "+String(acceptedShares)+"/"+String(rejectedShares);
    canvas.drawString(tickerLines[(tickerPos/20)%4], 6, HEIGHT-26, 2);
    tickerPos++;

    canvas.pushSprite(0, 0);
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
