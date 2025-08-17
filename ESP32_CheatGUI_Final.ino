/* =====================================================================================
NEON SIM — Hardware Simulator
- Waveshare ESP32-S3 1.47" (72x320) LCD
- Cyberpunk PWA (Single .ino File)
===================================================================================== */

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "SPIFFS.h"

// ---------- Optional RGB "Colorful LED" (WS2812) ----------
#define USE_NEOPIXEL 1
#define NEOPIXEL_PIN 48
#define NEOPIXEL_COUNT 1
#if USE_NEOPIXEL
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel rgb(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
#endif

// ---------- Globals ----------
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);
WebServer server(80);

const char* AP_SSID = "SIM-NET";
const char* AP_PASS = "simdemo123";
IPAddress apIP(192, 168, 4, 1);

unsigned long bootMs = 0;
unsigned long lastSimMs = 0;
unsigned long lastLCDMs = 0;
unsigned long lastRGBMs = 0;
int WIDTH = 0, HEIGHT = 0;
float cubeAngle = 0.0f;

// Simulation
bool simRunning = true;
bool isPremium = false;
bool rgbSync = true;
String profileName = "Neon Operator";
String profilePlan = "Standard";
String avatarPath = "/avatar.jpg";
double totalExtracted = 0.0;
float globalHashrate = 120.0;
float globalPower = 230.0;
int acceptedShares = 1324;
int rejectedShares = 11;
int latencyMs = 34;
float deviceTemp = 48.0;

struct Worker { String name; float hashrate; int gpuLoad; int temp; float power; float efficiency; bool online; };
Worker workers[8];
int workerCount = 5;

// ---------- Web UI Assets ----------
const char INDEX_HTML[] PROGMEM =
"<!DOCTYPE html>"
"<html lang=\"en\">"
"<head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>NEON SIM</title>"
    "<style>"
        "body{background-color:#040614;color:#eaf8ff;font-family:sans-serif;margin:1em;}"
        "h2,h3{color:#00e5ff;border-bottom:1px solid #00e5ff;padding-bottom:5px;}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:1em;}"
        ".card{background-color:#0c102a;padding:1em;border-radius:10px;}"
        ".stat{flex:1;}"
        "input,select,button{width:100%;padding:10px;margin-top:5px;border-radius:5px;border:1px solid #00e5ff;background-color:#0c102a;color:#eaf8ff;}"
        "button{background-color:#00e5ff;color:black;font-weight:bold;cursor:pointer;}"
    "</style>"
"</head>"
"<body>"
    "<h2>NEON SIM Dashboard</h2>"
    "<div class=\"grid\">"
        "<div class=\"card\"><h3>Status</h3><div id=\"vStatus\">--</div></div>"
        "<div class=\"card\"><h3>Hashrate</h3><div id=\"vHash\">-- MH/s</div></div>"
        "<div class=\"card\"><h3>Power</h3><div id=\"vPower\">-- W</div></div>"
        "<div class=\"card\"><h3>Uptime</h3><div id=\"vUptime\">--</div></div>"
    "</div>"
    "<h3>Workers</h3>"
    "<div class=\"grid\" id=\"workers\"></div>"
    "<h3>Profile</h3>"
    "<div class=\"card\">"
        "<img id=\"avatarImg\" src=\"/avatar\" width=\"96\" height=\"96\" style=\"border-radius:10px;\" onerror=\"this.style.display='none'\">"
        "<p>Name: <input id=\"nameInput\" value=\"Neon Operator\"></p>"
        "<p>Plan: <select id=\"planSel\"><option>Standard</option><option>Premium</option></select></p>"
        "<button onclick=\"saveProfile()\">Save Profile</button>"
        "<hr>"
        "<form id=\"avatarForm\" enctype=\"multipart/form-data\" method=\"post\" action=\"/upload\">"
          "<input type=\"file\" name=\"avatar\" accept=\"image/*\">"
          "<button type=\"submit\">Upload Avatar</button>"
        "</form>"
    "</div>"
    "<script>"
        "function update(){"
            "fetch('/api/stats').then(r=>r.json()).then(s=>{"
                "document.getElementById('vStatus').innerText=s.running?'Running':'Paused';"
                "document.getElementById('vHash').innerText=`${s.globalHashrate.toFixed(1)} MH/s`;"
                "document.getElementById('vPower').innerText=`${s.globalPower.toFixed(0)} W`;"
                "document.getElementById('vUptime').innerText=new Date(s.uptime*1000).toISOString().substr(11,8);"
                "document.getElementById('nameInput').value=s.profileName;"
                "document.getElementById('planSel').value=s.profilePlan;"
                "let w='';"
                "s.workers.forEach(o=>w+=`<div class=\"card ${o.online?'':'offline'}\"><strong>${o.name}</strong><br>${o.hashrate.toFixed(1)} MH/s<br>${o.temp}&deg;C</div>`);"
                "document.getElementById('workers').innerHTML=w;"
            "});"
        "}"
        "function saveProfile(){"
            "fetch('/api/profile',{method:'POST',body:JSON.stringify({name:document.getElementById('nameInput').value,plan:document.getElementById('planSel').value})});"
        "}"
        "document.getElementById('avatarForm').onsubmit=function(e){e.preventDefault();var f=new FormData(this);fetch('/upload',{method:'POST',body:f}).then(()=>setTimeout(()=>document.getElementById('avatarImg').src='/avatar?'+Date.now(),1000));};"
        "setInterval(update,1500);"
        "window.onload=()=>{"
            "update();"
            "document.getElementById('avatarImg').src='/avatar?'+Date.now();"
        "};"
    "</script>"
"</body>"
"</html>";

// ---------- Function Prototypes ----------
String jsonStats();

// ---------- HTTP Handlers ----------
void hRoot() { server.send_P(200, "text/html", INDEX_HTML); }
void hStats() { server.send(200, "application/json", jsonStats()); }
void hAvatar() {
  if (SPIFFS.exists(avatarPath)) {
    File f = SPIFFS.open(avatarPath, "r");
    server.streamFile(f, "image/jpeg");
    f.close();
  } else {
    server.send(404, "text/plain", "Not found");
  }
}
void hProfile() {
  if (server.hasArg("plain") == false) return;
  String body = server.arg("plain");
  // Simple JSON parsing
  int n1 = body.indexOf("\"name\"");
  int p1 = body.indexOf("\"plan\"");
  if (n1 >= 0) {
    int q1 = body.indexOf('"', body.indexOf(':', n1) + 1);
    int q2 = body.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) profileName = body.substring(q1 + 1, q2);
  }
  if (p1 >= 0) {
    int q1 = body.indexOf('"', body.indexOf(':', p1) + 1);
    int q2 = body.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) profilePlan = body.substring(q1 + 1, q2);
  }
  server.send(200, "text/plain", "ok");
}
void hUploadStream() {
  HTTPUpload& upload = server.upload();
  static File f;
  if (upload.status == UPLOAD_FILE_START) {
    if (SPIFFS.exists(avatarPath)) SPIFFS.remove(avatarPath);
    f = SPIFFS.open(avatarPath, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (f) f.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (f) f.close();
  }
}
void hUploadFinish() { server.send(200, "text/plain", "OK"); }

// ---------- JSON Builder Implementation ----------
String jsonStats() {
  String s = "{";
  s += "\"uptime\":" + String((millis() - bootMs) / 1000) + ",";
  s += "\"running\":" + (simRunning ? String("true") : String("false")) + ",";
  s += "\"rgbSync\":" + (rgbSync ? String("true") : String("false")) + ",";
  s += "\"profileName\":\"" + profileName + "\",";
  s += "\"profilePlan\":\"" + profilePlan + "\",";
  s += "\"globalHashrate\":" + String(globalHashrate, 2) + ",";
  s += "\"globalPower\":" + String(globalPower, 2) + ",";
  s += "\"acceptedShares\":" + String(acceptedShares) + ",";
  s += "\"rejectedShares\":" + String(rejectedShares) + ",";
  s += "\"latencyMs\":" + String(latencyMs) + ",";
  s += "\"deviceTemp\":" + String(deviceTemp, 1) + ",";
  s += "\"workers\":[";
  for (int i = 0; i < workerCount; i++) {
    s += "{\"name\":\"" + workers[i].name + "\",\"hashrate\":" + String(workers[i].hashrate, 2) + ",\"temp\":" + String(workers[i].temp) + ",\"online\":" + (workers[i].online ? "true" : "false") + "}";
    if (i < workerCount - 1) s += ",";
  }
  s += "]";
  s += "}";
  return s;
}

// ---------- LCD & 3D Drawing ----------
void drawCube(TFT_eSprite* spr, float ang) {
  int cx = WIDTH / 2, cy = HEIGHT / 2 + 4;
  float s = 26;
  struct V3 { float x, y, z; };
  V3 cubePts[8] = { {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}, {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} };
  int cubeEdges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};

  auto rot = [&](V3 p) {
    float sa=sin(ang), ca=cos(ang), sb=sin(ang*0.7f), cb=cos(ang*0.7f);
    float x1 = p.x*ca - p.z*sa, z1 = p.x*sa + p.z*ca;
    float y2 = p.y*cb - z1*sb, z2 = p.y*sb + z1*cb;
    return V3{x1, y2, z2};
  };
  V3 pr[8];
  for(int i=0;i<8;i++){
    V3 r = rot(cubePts[i]);
    float d = 3.0f / (r.z + 4.5f);
    pr[i] = {r.x * d, r.y * d, r.z};
  }
  uint16_t col = spr->color565(0,220,255);
  for(int e=0;e<12;e++){
    int a=cubeEdges[e][0], b=cubeEdges[e][1];
    int x1=cx + pr[a].x*s, y1=cy + pr[a].y*s;
    int x2=cx + pr[b].x*s, y2=cy + pr[b].y*s;
    spr->drawLine(x1,y1,x2,y2,col);
  }
}

// ---------- Main Setup ----------
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(1);
  WIDTH = tft.width();
  HEIGHT = tft.height();
  canvas.createSprite(WIDTH, HEIGHT);
  tft.fillScreen(TFT_BLACK);

  if (!SPIFFS.begin(true)) {
    tft.drawString("SPIFFS Mount Failed", 0, 0);
    while(1);
  }

#if USE_NEOPIXEL
  rgb.begin();
  rgb.setBrightness(20);
  rgb.clear();
  rgb.show();
#endif

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", HTTP_GET, hRoot);
  server.on("/api/stats", HTTP_GET, hStats);
  server.on("/api/profile", HTTP_POST, hProfile);
  server.on("/avatar", HTTP_GET, hAvatar);
  server.on("/upload", HTTP_POST, hUploadFinish, hUploadStream);
  server.begin();

  bootMs = millis();
}

// ---------- Main Loop ----------
void loop() {
  server.handleClient();

  if (millis() - lastSimMs > 1000) {
    lastSimMs = millis();
    for (int i = 0; i < workerCount; i++) {
        workers[i].name = "w-" + String(i);
        workers[i].online = random(0, 100) > 10;
        if(workers[i].online) {
            workers[i].hashrate = random(3500, 14500) / 100.0;
            workers[i].temp = random(42, 86);
        }
    }
    float H=0;
    for(int i=0;i<workerCount;i++){ if(workers[i].online){ H+=workers[i].hashrate; }}
    globalHashrate = H;
    latencyMs = constrain(latencyMs + random(-4,5), 16, 120);
  }

  if (millis() - lastLCDMs > 80) {
    lastLCDMs = millis();
    canvas.fillSprite(TFT_BLACK);

    canvas.setTextColor(TFT_CYAN);
    canvas.drawString("NEON SIM", 6, 2, 2);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(String(globalHashrate,0)+"MH/s", 140, 2, 2);

    cubeAngle += 0.08f;
    drawCube(&canvas, cubeAngle);

    canvas.pushSprite(0, 0);
  }

#if USE_NEOPIXEL
  if (rgbSync && millis() - lastRGBMs > 50) {
    lastRGBMs = millis();
    uint8_t lumen = (uint8_t)(20 + (int)(10.0 * abs(sin(millis() / 200.0))));
    uint8_t b = (uint8_t)min(255.0f, (globalHashrate / 2.0f));
    rgb.setPixelColor(0, rgb.Color(lumen, lumen, b));
    rgb.show();
  }
#endif
}
