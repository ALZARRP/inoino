/*
 * ESP32-S3 Cyberpunk Crypto Miner Dashboard
 *
 * A single-file Arduino sketch to host a web server and drive an LCD,
 * creating a fake crypto mining dashboard experience.
 *
 * Hardware:
 * - ESP32-S3
 * - 1.47" LCD (ST7789 driver assumed, 172x320 resolution)
 *
 * Instructions:
 * 1. Install the required libraries in the Arduino IDE:
 *    - ESPAsyncWebServer: https://github.com/me-no-dev/ESPAsyncWebServer
 *    - AsyncTCP: https://github.com/me-no-dev/AsyncTCP
 *    - TFT_eSPI: https://github.com/Bodmer/TFT_eSPI
 * 2. Configure the TFT_eSPI library for your specific ESP32-S3 and ST7789 display
 *    by editing its `User_Setup.h` or creating a custom setup file.
 *    Common for ESP32-S3 is to use the `User_Setup_Select.h` to include a setup for your board.
 * 3. Update the `ssid` and `password` variables below with your WiFi network credentials.
 * 4. Upload the sketch to your ESP32-S3.
 * 5. After it connects to WiFi, open the Serial Monitor to get the IP address.
 * 6. Access the IP address from your phone's web browser.
 */

// =============================================================================
// LIBRARIES
// =============================================================================
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// =============================================================================
// CONFIGURATION
// =============================================================================
// -- WiFi Credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// -- LCD Configuration (TFT_eSPI)
// NOTE: Pin configuration for the ST7789 display is done in the TFT_eSPI library's User_Setup.h file.
// Make sure you have configured it correctly for your ESP32-S3 board.
// For a 1.47" display, the resolution is typically 172x320.
TFT_eSPI tft = TFT_eSPI();

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- Fake Mining Data ---
bool isMining = true;
double walletBalance = 1.378;
double totalSOL = 0.0;
unsigned long validWallets = 0;
unsigned long invalidWallets = 0;
double hashrate = 0.0;
const char* walletAddress = "0xS3-JULES-AI-47C";
const char* logMessages[] = {
  "[CORE] System nominal. Quantum link stable.",
  "[NET] Peer connection established: 192.168.4.1",
  "[MINER] Hash block #734aef found.",
  "[SYS] Coolant pump speed at 98%.",
  "[AI] Predictive analysis complete. No threats.",
  "[SEC] Firewall integrity check passed."
};
int currentLogMessage = 0;

// --- Timers ---
unsigned long lastDataUpdateTime = 0;
unsigned long lastLcdLogTime = 0;
const long dataUpdateInterval = 2000; // 2 seconds
const long lcdLogInterval = 5000; // 5 seconds

// =============================================================================
// WEB APP (HTML, CSS, JS) STORED IN PROGMEM
// =============================================================================
const char index_html[] PROGMEM = R"raw_string(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no, viewport-fit=cover">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
    <title>SOL Miner Interface</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700&family=Roboto+Mono:wght@300;400&display=swap');

        :root {
            --bg-color: #0d041a;
            --primary-glow: #ff00ff;
            --secondary-glow: #00ffff;
            --text-color: #e0e0e0;
            --border-color: #ff00ff80;
            --box-bg: #1a0a2a80;
            --glitch-color1: #ff00ff;
            --glitch-color2: #00ffff;
        }

        body, html {
            margin: 0;
            padding: 0;
            width: 100%;
            height: 100%;
            background-color: var(--bg-color);
            color: var(--text-color);
            font-family: 'Roboto Mono', monospace;
            overflow: hidden;
            overscroll-behavior: none;
        }

        #splash, #installer {
            position: fixed;
            top: 0; left: 0;
            width: 100%; height: 100%;
            background-color: var(--bg-color);
            z-index: 100;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            transition: opacity 1s ease-in-out;
        }

        .logo {
            font-family: 'Orbitron', sans-serif;
            font-size: 2.5rem;
            color: var(--secondary-glow);
            text-shadow: 0 0 5px var(--secondary-glow), 0 0 10px var(--secondary-glow), 0 0 20px var(--secondary-glow);
            margin-bottom: 20px;
        }

        #splash p {
            font-size: 1.2rem;
            letter-spacing: 3px;
        }

        #installer {
            z-index: 99;
            background: #000;
        }

        #log-container {
            width: 90%;
            max-width: 600px;
            height: 80%;
            border: 1px solid var(--border-color);
            box-shadow: 0 0 10px var(--border-color);
            padding: 15px;
            overflow-y: hidden;
            font-size: 0.8rem;
            background: #000000a0;
        }
        #log-container p { margin: 2px 0; }
        #log-container p.ok { color: var(--secondary-glow); }
        #log-container p.warn { color: yellow; }
        #log-container p.err { color: red; }

        #dashboard {
            display: none;
            flex-direction: column;
            padding: 15px;
            padding-top: env(safe-area-inset-top, 15px);
            padding-bottom: env(safe-area-inset-bottom, 15px);
            height: calc(100% - 30px - env(safe-area-inset-top, 0px) - env(safe-area-inset-bottom, 0px));
            overflow-y: auto;
            -webkit-overflow-scrolling: touch;
        }

        header {
            text-align: center;
            padding-bottom: 15px;
            border-bottom: 1px solid var(--border-color);
            margin-bottom: 15px;
        }

        header h1 {
            font-family: 'Orbitron', sans-serif;
            margin: 0;
            font-size: 1.8rem;
            color: var(--primary-glow);
            text-shadow: 0 0 5px var(--primary-glow), 0 0 10px var(--primary-glow);
            animation: flicker 3s infinite;
        }

        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
        }

        .stat-box {
            background: var(--box-bg);
            border: 1px solid var(--border-color);
            border-radius: 10px;
            padding: 15px;
            box-shadow: inset 0 0 8px var(--border-color), 0 0 8px var(--border-color);
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            text-align: center;
        }

        .stat-box h2 {
            margin: 0 0 10px 0;
            font-size: 0.9rem;
            color: var(--secondary-glow);
            font-family: 'Orbitron', sans-serif;
        }

        .stat-box .value {
            font-size: 1.5rem;
            font-weight: bold;
            color: #fff;
            text-shadow: 0 0 3px #fff;
            min-height: 2rem;
        }

        .value.glitch {
            animation: glitch 1.5s infinite steps(2, end);
        }

        .wallet-address {
            word-break: break-all;
            font-size: 0.7rem;
            color: var(--secondary-glow);
        }

        #history-log {
            height: 100px;
            overflow-y: scroll;
            border: 1px solid var(--border-color);
            padding: 10px;
            margin-top: 15px;
            border-radius: 10px;
            background: var(--box-bg);
            font-size: 0.7rem;
        }
        #history-log p { margin: 0; }

        .button-container {
            position: fixed;
            bottom: 0;
            left: 0;
            width: 100%;
            padding: 20px;
            padding-bottom: calc(20px + env(safe-area-inset-bottom, 0px));
            box-sizing: border-box;
            background: linear-gradient(to top, var(--bg-color) 50%, transparent);
        }

        #toggle-miner {
            width: 100%;
            padding: 15px;
            font-family: 'Orbitron', sans-serif;
            font-size: 1.2rem;
            border-radius: 10px;
            border: 2px solid;
            cursor: pointer;
            transition: all 0.3s ease;
        }

        #toggle-miner.running {
            background-color: var(--primary-glow);
            color: #000;
            border-color: var(--primary-glow);
            box-shadow: 0 0 20px var(--primary-glow);
        }

        #toggle-miner.stopped {
            background-color: transparent;
            color: var(--secondary-glow);
            border-color: var(--secondary-glow);
            box-shadow: 0 0 10px var(--secondary-glow);
        }

        @keyframes flicker {
            0%, 100% { opacity: 1; text-shadow: 0 0 5px var(--primary-glow), 0 0 10px var(--primary-glow); }
            50% { opacity: 0.8; text-shadow: 0 0 5px var(--primary-glow); }
        }

        @keyframes glitch {
            0% {
                transform: translate(0);
                text-shadow: 1px 1px var(--glitch-color1), -1px -1px var(--glitch-color2);
            }
            25% {
                transform: translate(2px, -2px);
                text-shadow: -2px 2px var(--glitch-color1), 2px -2px var(--glitch-color2);
            }
            50% {
                transform: translate(-2px, 2px);
                text-shadow: 2px -2px var(--glitch-color1), -2px 2px var(--glitch-color2);
            }
            75% {
                transform: translate(2px, 2px);
                 text-shadow: 1px -1px var(--glitch-color1), -1px 1px var(--glitch-color2);
            }
            100% {
                transform: translate(0);
                text-shadow: -1px 1px var(--glitch-color1), 1px -1px var(--glitch-color2);
            }
        }
    </style>
</head>
<body>

    <div id="splash">
        <div class="logo">SOL-CORE</div>
        <p>INITIALIZING...</p>
    </div>

    <div id="installer">
        <div id="log-container"></div>
    </div>

    <div id="dashboard">
        <header>
            <h1 class="glitch">MINER DASHBOARD</h1>
        </header>

        <div class="stats-grid">
            <div class="stat-box">
                <h2>Wallet Balance</h2>
                <span class="value" id="balance">0.000</span>
            </div>
            <div class="stat-box">
                <h2>Hashrate (TH/s)</h2>
                <span class="value glitch" id="hashrate">0.00</span>
            </div>
            <div class="stat-box">
                <h2>Total SOL Mined</h2>
                <span class="value" id="total-sol">0.000</span>
            </div>
            <div class="stat-box">
                <h2>Valid Wallets ✅</h2>
                <span class="value" id="valid-wallets">0</span>
            </div>
            <div class="stat-box">
                <h2>Invalid Wallets ❌</h2>
                <span class="value" id="invalid-wallets">0</span>
            </div>
            <div class="stat-box">
                <h2>Total Wallets</h2>
                <span class="value" id="total-wallets">0</span>
            </div>
             <div class="stat-box">
                <h2>Transaction Fees</h2>
                <span class="value" id="fees">0.00012 SOL</span>
            </div>
            <div class="stat-box" style="grid-column: 1 / -1;">
                <h2>Wallet Address</h2>
                <span class="wallet-address" id="wallet-address">...</span>
            </div>
        </div>

        <div id="history-log">
            <p>[SYSTEM] Log initialized.</p>
        </div>

        <div style="height: 120px;"></div> <!-- Spacer for button -->
    </div>

    <div class="button-container">
        <button id="toggle-miner" class="running">STOP MINER</button>
    </div>

    <script>
        let ws;
        let isMinerRunning = true;

        const fakeInstallSteps = [
            { text: "Initializing quantum core...", delay: 500, status: "ok" },
            { text: "Loading dependencies... [crypto.lib, net.lib]", delay: 1000, status: "ok" },
            { text: "Establishing secure uplink...", delay: 800, status: "ok" },
            { text: "Syncing with Solana blockchain... (1/3)", delay: 700, status: "warn" },
            { text: "Syncing with Solana blockchain... (2/3)", delay: 900, status: "warn" },
            { text: "Syncing with Solana blockchain... (3/3)", delay: 1200, status: "ok" },
            { text: "Authenticating wallet credentials...", delay: 600, status: "ok" },
            { text: "Firing up mining processors...", delay: 500, status: "ok" },
            { text: "Boot sequence complete. Launching dashboard.", delay: 1500, status: "ok" },
        ];

        window.addEventListener('load', (event) => {
            // --- Splash Screen ---
            setTimeout(() => {
                document.getElementById('splash').style.opacity = '0';
                setTimeout(() => document.getElementById('splash').style.display = 'none', 1000);
                runInstaller();
            }, 2000);

            // --- Button Logic ---
            const toggleButton = document.getElementById('toggle-miner');
            toggleButton.addEventListener('click', () => {
                isMinerRunning = !isMinerRunning;
                ws.send(isMinerRunning ? 'START' : 'STOP');
                updateButtonState();
            });

            updateButtonState();
        });

        function updateButtonState() {
            const toggleButton = document.getElementById('toggle-miner');
            if (isMinerRunning) {
                toggleButton.textContent = 'STOP MINER';
                toggleButton.classList.remove('stopped');
                toggleButton.classList.add('running');
            } else {
                toggleButton.textContent = 'START MINER';
                toggleButton.classList.remove('running');
                toggleButton.classList.add('stopped');
            }
        }

        async function runInstaller() {
            const logContainer = document.getElementById('log-container');
            for (const step of fakeInstallSteps) {
                await new Promise(resolve => setTimeout(resolve, step.delay));
                const p = document.createElement('p');
                p.textContent = `${step.text}${step.status === 'ok' ? ' [DONE]' : ' [...]'}`;
                p.classList.add(step.status);
                logContainer.appendChild(p);
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
            }, 500);
        }

        function initWebSocket() {
            console.log('Starting WebSocket...');
            ws = new WebSocket(`ws://${window.location.hostname}/ws`);

            ws.onopen = () => console.log('WebSocket connection established');
            ws.onclose = () => {
                console.log('WebSocket connection closed');
                setTimeout(initWebSocket, 2000); // Try to reconnect
            };
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
            document.getElementById('total-wallets').textContent = data.valid + data.invalid;
            document.getElementById('wallet-address').textContent = data.address;

            if (data.log) {
                addHistoryLog(data.log);
            }
        }

        function addHistoryLog(message) {
            const historyLog = document.getElementById('history-log');
            const p = document.createElement('p');
            const timestamp = new Date().toLocaleTimeString();
            p.textContent = `[${timestamp}] ${message}`;

            // Keep the log from getting too long
            while (historyLog.children.length > 20) {
                historyLog.removeChild(historyLog.firstChild);
            }

            historyLog.appendChild(p);
            historyLog.scrollTop = historyLog.scrollHeight;
        }

    </script>
</body>
</html>
)raw_string";

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

void notifyClients() {
  if (ws.count() > 0) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "{\"balance\":%.4f, \"hashrate\":%.2f, \"totalSOL\":%f, \"valid\":%lu, \"invalid\":%lu, \"address\":\"%s\", \"log\":\"%s\"}",
             walletBalance, hashrate, totalSOL, validWallets, invalidWallets, walletAddress, logMessages[currentLogMessage]);
    ws.textAll(buffer);
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        if (strcmp((char*)data, "START") == 0) {
            isMining = true;
        } else if (strcmp((char*)data, "STOP") == 0) {
            isMining = false;
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            // Send initial data dump
            notifyClients();
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void initWebSocket() {
    ws.onEvent(onEvent);
    server.addHandler(&ws);
}

// =============================================================================
// LCD DRAWING FUNCTIONS
// =============================================================================

void drawLcdLayout() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false);

    // Define colors
    uint16_t primaryColor = tft.color565(255, 0, 255); // Magenta
    uint16_t secondaryColor = tft.color565(0, 255, 255); // Cyan

    // Draw rounded boxes
    tft.drawRoundRect(5, 5, 162, 70, 8, primaryColor);   // Wallets Mined
    tft.drawRoundRect(5, 80, 162, 70, 8, primaryColor);  // Invalid Wallets
    tft.drawRoundRect(5, 155, 162, 70, 8, secondaryColor); // Total SOL
    tft.drawRoundRect(5, 230, 162, 70, 8, secondaryColor); // Hashrate

    // Draw static text
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(secondaryColor);
    tft.drawString("Wallets Mined", 172/2, 20, 2);
    tft.drawString("Invalid Wallets", 172/2, 95, 2);
    tft.drawString("Total SOL", 172/2, 170, 2);
    tft.drawString("Hashrate", 172/2, 245, 2);
}

void updateLcdData() {
    tft.setTextDatum(MC_DATUM);

    // Wallets Mined
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawChar(0x27, 86, 45, 4); // Unicode checkmark doesn't work, use tick ✅
    tft.drawString(String(validWallets), 86, 50, 4);

    // Invalid Wallets
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("X", 86, 120, 4); // ❌
    tft.drawString(String(invalidWallets), 86, 125, 4);

    // Total SOL
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(totalSOL, 4), 86, 195, 4);

    // Hashrate
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(hashrate, 2), 86, 270, 4);
}

void updateLcdLog() {
    tft.fillRect(0, 305, 172, 15, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(tft.color565(255, 0, 255));
    tft.drawString(logMessages[currentLogMessage], 172/2, 312, 1);
}


// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);

    // --- LCD Init ---
    tft.init();
    tft.setRotation(0); // Adjust if necessary. 0 or 2 for portrait
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(0, 0);
    tft.println("Initializing...");

    // --- WiFi Connection ---
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    tft.println("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        tft.print(".");
    }
    Serial.println("\nWiFi connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    tft.println("\nConnected!");
    tft.println("IP Address:");
    tft.println(WiFi.localIP());
    delay(2000);

    // --- Web Server Init ---
    initWebSocket();
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });
    server.begin();

    // --- Initial LCD Draw ---
    drawLcdLayout();
    updateLcdData();
    updateLcdLog();
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
    ws.cleanupClients();
    unsigned long currentTime = millis();

    if (isMining && (currentTime - lastDataUpdateTime > dataUpdateInterval)) {
        lastDataUpdateTime = currentTime;

        // --- Update Fake Data ---
        hashrate = random(8000, 10000) / 100.0; // e.g., 80.00 - 100.00 TH/s
        double solMinedThisTick = hashrate / 500000.0;
        totalSOL += solMinedThisTick;
        walletBalance += solMinedThisTick;

        if (random(0, 100) < 95) { // 95% chance of valid
            validWallets++;
        } else {
            invalidWallets++;
        }

        // --- Update LCD ---
        updateLcdData();

        // --- Send data to web clients ---
        notifyClients();
    }

    if (currentTime - lastLcdLogTime > lcdLogInterval) {
      lastLcdLogTime = currentTime;
      currentLogMessage = (currentLogMessage + 1) % (sizeof(logMessages) / sizeof(logMessages[0]));
      updateLcdLog();
    }
}
