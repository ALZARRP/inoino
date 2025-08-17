/*
 * ESP32-S3 Cyberpunk Crypto Miner Dashboard - V2 (Enhanced)
 *
 * A single-file Arduino sketch to host a web server and drive an LCD,
 * creating a fake crypto mining dashboard experience.
 *
 * ================== V2 CHANGES ==================
 * - Replaced ESPAsyncWebServer with standard WebServer and WebSocketsServer
 *   to fix compilation errors with modern ESP32 cores.
 * - Added "Stretch Goal" features: Peer counter, difficulty, random warnings.
 * - Added an animated progress bar to the web UI.
 * - Improved code structure with a data struct (MinerData).
 * - Optimized LCD drawing routines to reduce flicker.
 * - Enhanced UI on both Web and LCD.
 * ==============================================
 *
 * Hardware:
 * - ESP32-S3
 * - 1.47" LCD (ST7789 driver assumed, 172x320 resolution)
 *
 * Instructions:
 * 1. Install the required libraries in the Arduino IDE Library Manager:
 *    - WebSockets by Markus Sattler
 *    - TFT_eSPI by Bodmer
 * 2. Configure the TFT_eSPI library for your specific ESP32-S3 and ST7789 display
 *    by editing its `User_Setup.h` or creating a custom setup file.
 * 3. Update the `ssid` and `password` variables below.
 * 4. Upload the sketch to your ESP32-S3.
 * 5. Open the Serial Monitor to get the IP address.
 * 6. Access the IP address from your phone's web browser.
 */

// =============================================================================
// LIBRARIES
// =============================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// =============================================================================
// CONFIGURATION
// =============================================================================
// -- WiFi Credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// -- Web Server and WebSockets
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// -- LCD Configuration (TFT_eSPI)
TFT_eSPI tft = TFT_eSPI();

// =============================================================================
// DATA STRUCTURES
// =============================================================================
struct MinerData {
    bool isMining;
    double walletBalance;
    double totalSOL;
    unsigned long validWallets;
    unsigned long invalidWallets;
    double hashrate;
    const char* walletAddress;
    int peers;
    double difficulty;
    String systemWarning;
};

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
MinerData data = {
    true, 1.378, 0.0, 0, 0, 0.0, "0xS3-JULES-AI-V2", 0, 1.0, ""
};

const char* randomWarnings[] = {
    "Quantum fluctuation detected in Core 2.",
    "High network latency. Rerouting peers.",
    "Difficulty adjustment incoming.",
    "System integrity check running...",
    "Unknown energy signature detected."
};

// --- Timers ---
unsigned long lastDataUpdateTime = 0;
unsigned long lastWarningTime = 0;
const long dataUpdateInterval = 1500; // 1.5 seconds
const long warningInterval = 25000; // 25 seconds

// =============================================================================
// WEB APP (HTML, CSS, JS) - V2
// =============================================================================
const char index_html[] PROGMEM = R"raw_string(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no, viewport-fit=cover">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
    <title>SOL Miner v2</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700&family=Roboto+Mono:wght@300;400&display=swap');
        :root {
            --bg-color: #0d041a; --primary-glow: #ff00ff; --secondary-glow: #00ffff;
            --text-color: #e0e0e0; --border-color: #ff00ff80; --box-bg: #1a0a2a80;
            --glitch-color1: #ff00ff; --glitch-color2: #00ffff; --warning-color: #ffd700;
        }
        body, html { margin: 0; padding: 0; width: 100%; height: 100%; background-color: var(--bg-color); color: var(--text-color); font-family: 'Roboto Mono', monospace; overflow: hidden; overscroll-behavior: none; }
        #splash, #installer { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background-color: var(--bg-color); z-index: 100; display: flex; flex-direction: column; justify-content: center; align-items: center; transition: opacity 1s ease-in-out; }
        .logo { font-family: 'Orbitron', sans-serif; font-size: 2.5rem; color: var(--secondary-glow); text-shadow: 0 0 5px var(--secondary-glow), 0 0 10px var(--secondary-glow), 0 0 20px var(--secondary-glow); margin-bottom: 20px; }
        #splash p { font-size: 1.2rem; letter-spacing: 3px; }
        #installer { z-index: 99; background: #000; }
        #log-container { width: 90%; max-width: 600px; height: 80%; border: 1px solid var(--border-color); box-shadow: 0 0 10px var(--border-color); padding: 15px; overflow-y: hidden; font-size: 0.8rem; background: #000000a0; }
        #log-container p { margin: 2px 0; }
        #log-container p.ok { color: var(--secondary-glow); }
        #log-container p.warn { color: yellow; }
        #dashboard { display: none; flex-direction: column; padding: 15px; padding-top: env(safe-area-inset-top, 15px); padding-bottom: env(safe-area-inset-bottom, 15px); height: calc(100% - 30px - env(safe-area-inset-top, 0px) - env(safe-area-inset-bottom, 0px)); overflow-y: auto; -webkit-overflow-scrolling: touch; }
        header { text-align: center; padding-bottom: 15px; border-bottom: 1px solid var(--border-color); margin-bottom: 15px; }
        header h1 { font-family: 'Orbitron', sans-serif; margin: 0; font-size: 1.8rem; color: var(--primary-glow); text-shadow: 0 0 5px var(--primary-glow), 0 0 10px var(--primary-glow); animation: flicker 3s infinite; }
        .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 15px; }
        .stat-box { background: var(--box-bg); border: 1px solid var(--border-color); border-radius: 10px; padding: 15px; box-shadow: inset 0 0 8px var(--border-color), 0 0 8px var(--border-color); text-align: center; }
        .stat-box h2 { margin: 0 0 10px 0; font-size: 0.9rem; color: var(--secondary-glow); font-family: 'Orbitron', sans-serif; }
        .stat-box .value { font-size: 1.5rem; font-weight: bold; color: #fff; text-shadow: 0 0 3px #fff; min-height: 2rem; }
        .value.glitch { animation: glitch 1.5s infinite steps(2, end); }
        .wallet-address { word-break: break-all; font-size: 0.7rem; color: var(--secondary-glow); }
        #history-log { height: 100px; overflow-y: scroll; border: 1px solid var(--border-color); padding: 10px; margin-top: 15px; border-radius: 10px; background: var(--box-bg); font-size: 0.7rem; }
        #history-log p { margin: 0; }
        .button-container { position: fixed; bottom: 0; left: 0; width: 100%; padding: 20px; padding-bottom: calc(20px + env(safe-area-inset-bottom, 0px)); box-sizing: border-box; background: linear-gradient(to top, var(--bg-color) 50%, transparent); }
        #toggle-miner { width: 100%; padding: 15px; font-family: 'Orbitron', sans-serif; font-size: 1.2rem; border-radius: 10px; border: 2px solid; cursor: pointer; transition: all 0.3s ease; }
        #toggle-miner.running { background-color: var(--primary-glow); color: #000; border-color: var(--primary-glow); box-shadow: 0 0 20px var(--primary-glow); }
        #toggle-miner.stopped { background-color: transparent; color: var(--secondary-glow); border-color: var(--secondary-glow); box-shadow: 0 0 10px var(--secondary-glow); }
        .progress-bar { width: 100%; background-color: #333; border-radius: 5px; overflow: hidden; margin-top: 10px; border: 1px solid var(--secondary-glow); }
        .progress-bar-inner { width: 0%; height: 10px; background: linear-gradient(90deg, var(--secondary-glow), var(--primary-glow)); animation: progress-anim 1.5s linear infinite; }
        #warning-banner { display: none; text-align: center; padding: 10px; background: var(--warning-color); color: #000; font-weight: bold; font-family: 'Orbitron'; margin-bottom: 15px; border-radius: 5px; animation: pulse 2s infinite; }
        @keyframes flicker { 0%, 100% { opacity: 1; text-shadow: 0 0 5px var(--primary-glow); } 50% { opacity: 0.8; text-shadow: 0 0 5px var(--primary-glow); } }
        @keyframes glitch { 0% { transform: translate(0); } 25% { transform: translate(2px, -2px); } 50% { transform: translate(-2px, 2px); } 75% { transform: translate(2px, 2px); } 100% { transform: translate(0); } }
        @keyframes progress-anim { 0% { background-position: 200% 0; } 100% { background-position: -200% 0; } }
        @keyframes pulse { 0%, 100% { transform: scale(1); box-shadow: 0 0 5px var(--warning-color); } 50% { transform: scale(1.02); box-shadow: 0 0 15px var(--warning-color); } }
    </style>
</head>
<body>
    <div id="splash"><div class="logo">SOL-CORE</div><p>INITIALIZING V2...</p></div>
    <div id="installer"><div id="log-container"></div></div>
    <div id="dashboard">
        <header><h1 class="glitch">MINER DASHBOARD</h1></header>
        <div id="warning-banner"></div>
        <div class="stats-grid">
            <div class="stat-box"><h2>Wallet Balance</h2><span class="value" id="balance">0.000</span></div>
            <div class="stat-box"><h2>Hashrate (TH/s)</h2><span class="value glitch" id="hashrate">0.00</span></div>
            <div class="stat-box"><h2>Total SOL Mined</h2><span class="value" id="total-sol">0.000</span></div>
            <div class="stat-box"><h2>Valid Wallets ✅</h2><span class="value" id="valid-wallets">0</span></div>
            <div class="stat-box"><h2>Invalid Wallets ❌</h2><span class="value" id="invalid-wallets">0</span></div>
            <div class="stat-box"><h2>Peers Connected</h2><span class="value" id="peers">0</span></div>
            <div class="stat-box"><h2>Difficulty</h2><span class="value" id="difficulty">0.0</span></div>
            <div class="stat-box" style="grid-column: 1 / -1;"><h2>Mining Progress</h2><div class="progress-bar"><div id="progress-bar-inner" class="progress-bar-inner"></div></div></div>
            <div class="stat-box" style="grid-column: 1 / -1;"><h2>Wallet Address</h2><span class="wallet-address" id="wallet-address">...</span></div>
        </div>
        <div id="history-log"><p>[SYSTEM] Log initialized.</p></div>
        <div style="height: 120px;"></div>
    </div>
    <div class="button-container"><button id="toggle-miner" class="running">STOP MINER</button></div>
    <script>
        let ws; let isMinerRunning = true;
        window.addEventListener('load', () => {
            setTimeout(() => {
                document.getElementById('splash').style.opacity = '0';
                setTimeout(() => document.getElementById('splash').style.display = 'none', 1000);
                runInstaller();
            }, 2000);
            const toggleButton = document.getElementById('toggle-miner');
            toggleButton.addEventListener('click', () => {
                isMinerRunning = !isMinerRunning;
                ws.send(isMinerRunning ? 'START' : 'STOP');
                updateButtonState();
            });
            updateButtonState();
        });
        function updateButtonState() {
            const btn = document.getElementById('toggle-miner');
            btn.textContent = isMinerRunning ? 'STOP MINER' : 'START MINER';
            btn.className = isMinerRunning ? 'running' : 'stopped';
        }
        async function runInstaller() {
            const logContainer = document.getElementById('log-container');
            const steps = ["Booting V2 kernel...", "Loading async libraries...", "Syncing with peer network...", "Verifying crypto modules...", "Launching dashboard."];
            for (const step of steps) {
                await new Promise(r => setTimeout(r, Math.random() * 1000 + 500));
                logContainer.innerHTML += `<p class="ok">${step} [DONE]</p>`;
                logContainer.scrollTop = logContainer.scrollHeight;
            }
            setTimeout(() => {
                const installer = document.getElementById('installer');
                installer.style.opacity = '0';
                setTimeout(() => {
                    installer.style.display = 'none';
                    document.getElementById('dashboard').style.display = 'flex';
                    initWebSocket();
                }, 1000);
            }, 1000);
        }
        function initWebSocket() {
            console.log('Starting WebSocket...');
            ws = new WebSocket(`ws://${window.location.hostname}:81/`);
            ws.onopen = () => console.log('WebSocket connection established');
            ws.onclose = () => { console.log('WebSocket connection closed'); setTimeout(initWebSocket, 2000); };
            ws.onmessage = (event) => {
                const data = JSON.parse(event.data);
                updateDashboard(data);
            };
        }
        function updateDashboard(data) {
            document.getElementById('balance').textContent = data.balance.toFixed(4) + ' SOL';
            document.getElementById('hashrate').textContent = data.hashrate.toFixed(2);
            document.getElementById('total-sol').textContent = data.totalSOL.toFixed(6);
            document.getElementById('valid-wallets').textContent = data.valid;
            document.getElementById('invalid-wallets').textContent = data.invalid;
            document.getElementById('peers').textContent = data.peers;
            document.getElementById('difficulty').textContent = data.difficulty.toFixed(4);
            document.getElementById('wallet-address').textContent = data.address;

            const progressBar = document.getElementById('progress-bar-inner');
            progressBar.style.width = `${(data.valid % 100)}%`;

            const warningBanner = document.getElementById('warning-banner');
            if (data.warning && data.warning.length > 0) {
                warningBanner.textContent = "⚠️ " + data.warning + " ⚠️";
                warningBanner.style.display = 'block';
            } else {
                warningBanner.style.display = 'none';
            }
            if(isMinerRunning) addHistoryLog(`Block # ${data.valid} found. Reward: ${data.hashrate/10000}.0 SOL`);
        }
        function addHistoryLog(message) {
            const historyLog = document.getElementById('history-log');
            const p = document.createElement('p');
            p.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
            if (historyLog.children.length > 20) historyLog.removeChild(historyLog.firstChild);
            historyLog.appendChild(p);
            historyLog.scrollTop = historyLog.scrollHeight;
        }
    </script>
</body>
</html>
)raw_string";

// =============================================================================
// WEB SERVER & WEBSOCKETS
// =============================================================================

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
            // Send initial data
            String json = getJsonPayload();
            webSocket.sendTXT(num, json);
            break;
        }
        case WStype_TEXT:
            if (length > 0) {
                if (strcmp((char *)payload, "START") == 0) {
                    data.isMining = true;
                } else if (strcmp((char *)payload, "STOP") == 0) {
                    data.isMining = false;
                }
            }
            break;
    }
}

String getJsonPayload() {
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "{\"balance\":%.4f, \"hashrate\":%.2f, \"totalSOL\":%.6f, \"valid\":%lu, \"invalid\":%lu, \"address\":\"%s\", \"peers\":%d, \"difficulty\":%.4f, \"warning\":\"%s\"}",
             data.walletBalance, data.hashrate, data.totalSOL, data.validWallets, data.invalidWallets, data.walletAddress, data.peers, data.difficulty, data.systemWarning.c_str());
    return String(buffer);
}

void broadcastData() {
    String json = getJsonPayload();
    webSocket.broadcastTXT(json);
}

// =============================================================================
// LCD DRAWING FUNCTIONS
// =============================================================================

void drawLcdLayout() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false);
    uint16_t primaryColor = tft.color565(255, 0, 255);
    uint16_t secondaryColor = tft.color565(0, 255, 255);

    tft.drawRoundRect(5, 5, 162, 70, 8, primaryColor);
    tft.drawRoundRect(5, 80, 162, 70, 8, primaryColor);
    tft.drawRoundRect(5, 155, 162, 70, 8, secondaryColor);
    tft.drawRoundRect(5, 230, 162, 70, 8, secondaryColor);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(secondaryColor, TFT_BLACK);
    tft.drawString("Wallets Found", 86, 20, 2);
    tft.drawString("Hashrate TH/s", 86, 95, 2);
    tft.drawString("Total SOL", 86, 170, 2);
    tft.drawString("Peers", 86, 245, 2);
}

void updateLcdValue(int y, const String& value, int font) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    // Clear the area before drawing to prevent artifacts
    tft.fillRect(10, y - 18, 152, 36, TFT_BLACK);
    tft.drawString(value, 86, y, font);
}

void updateLcdData() {
    updateLcdValue(50, String(data.validWallets), 4);
    updateLcdValue(125, String(data.hashrate, 2), 4);
    updateLcdValue(200, String(data.totalSOL, 4), 4);
    updateLcdValue(275, String(data.peers), 4);
}

void updateLcdWarning() {
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(tft.color565(255, 215, 0), TFT_BLACK); // Gold
    tft.fillRect(0, 305, 172, 15, TFT_BLACK);
    if(data.systemWarning.length() > 0) {
        tft.drawString(data.systemWarning, 86, 320, 1);
    }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);

    // --- LCD Init ---
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(0, 0);
    tft.println("Booting Miner v2...");

    // --- WiFi Connection ---
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    tft.println("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    tft.println("Connected!");
    tft.println(WiFi.localIP());
    delay(2000);

    // --- Web Server & Sockets Init ---
    server.on("/", HTTP_GET, [](){
        server.send_P(200, "text/html", index_html);
    });
    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    // --- Initial LCD Draw ---
    drawLcdLayout();
    updateLcdData();
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
    webSocket.loop();
    server.handleClient();

    unsigned long currentTime = millis();

    // --- Update data periodically ---
    if (data.isMining && (currentTime - lastDataUpdateTime > dataUpdateInterval)) {
        lastDataUpdateTime = currentTime;

        // Update fake data
        data.hashrate = random(8000, 12000) / 100.0;
        data.peers = random(5, 25);
        data.difficulty *= (1 + random(-10, 11) / 10000.0); // Fluctuate by +/- 0.1%
        if (data.difficulty < 1) data.difficulty = 1;

        double solMinedThisTick = data.hashrate / (500000.0 * data.difficulty);
        data.totalSOL += solMinedThisTick;
        data.walletBalance += solMinedThisTick;

        if (random(0, 100) < 95) {
            data.validWallets++;
        } else {
            data.invalidWallets++;
        }

        updateLcdData();
        broadcastData();
    }

    // --- Handle random warnings ---
    if (currentTime - lastWarningTime > warningInterval) {
        lastWarningTime = currentTime;
        if (random(0, 100) < 30) { // 30% chance of a warning
            data.systemWarning = randomWarnings[random(0, 5)];
            updateLcdWarning();
            broadcastData(); // Send update immediately

            // Clear warning after a few seconds
            lastWarningTime = currentTime - (warningInterval - 5000); // Reset timer to clear in 5s
        } else if (data.systemWarning.length() > 0) {
            data.systemWarning = "";
            updateLcdWarning();
            broadcastData();
        }
    }
}
