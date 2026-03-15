#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "DHT.h"

// ===== WiFi =====
const char* ssid     = "ssid";
const char* password = "pass";

// ===== DHT11 =====
#define DHTPIN  D2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
float lastTemp = 0, lastHum = 0;
unsigned long lastDHTread = 0;

// ===== Batéria + CE pin =====
#define CE_PIN  D1
#define ADC_PIN A0
const float V_MAX = 4.00;
const float V_MIN = 3.60;
const float R1    = 330.0;
const float R2    = 100.0;
bool  isCharging  = false;
float batteryVolt = 0;
int   batteryPct  = 0;

float getBatteryVoltage() {
  long sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += analogRead(ADC_PIN);
    delay(5);
  }
  float vADC = ((sum / 5.0) / 1023.0) * 1.0;
  return vADC * ((R1 + R2) / R2);
}

int voltageToPct(float v) {
  if (v >= 4.20) return 100;
  if (v >= 4.00) return map((int)(v * 100), 400, 420, 85, 100);
  if (v >= 3.75) return map((int)(v * 100), 375, 400, 50, 85);
  if (v >= 3.60) return map((int)(v * 100), 360, 375, 30, 50);
  if (v >= 3.00) return map((int)(v * 100), 300, 360, 0, 30);
  return 0;
}

void manageBattery() {
  batteryVolt = getBatteryVoltage();
  batteryPct  = voltageToPct(batteryVolt);
  if (batteryVolt < 2.5 || batteryVolt > 4.3) {
    Serial.printf("Nereálne napätie: %.2fV — preskakujem\n", batteryVolt);
    digitalWrite(CE_PIN, LOW);
    isCharging = false;
    return;
  }
  if (!isCharging && batteryVolt <= V_MIN) {
    digitalWrite(CE_PIN, HIGH);
    isCharging = true;
    Serial.println(">>> NABÍJANIE ZAPNUTÉ");
  }
  if (isCharging && batteryVolt >= V_MAX) {
    digitalWrite(CE_PIN, LOW);
    isCharging = false;
    Serial.println(">>> NABÍJANIE VYPNUTÉ");
  }
}

// ===== Web server =====
ESP8266WebServer server(80);
bool serverStarted = false;

// ===== LED =====
#define LED_PIN LED_BUILTIN
unsigned long prevBlinkMs = 0;
const unsigned long blinkIntervalMs = 200;
bool ledState = false;

// ===== Timery =====
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectIntervalMs = 5000;
unsigned long lastPing   = 0;
unsigned long lastStatus = 0;

// ---------- Farby ----------
String colorTemp(float t) {
  if (t < 20)       return "#3b82f6";
  else if (t < 25)  return "#22c55e";
  else if (t <= 27) return "#f97316";
  else              return "#ef4444";
}
String colorHum(float h) {
  if (h < 30)       return "#f97316";
  else if (h <= 60) return "#22c55e";
  else              return "#3b82f6";
}
String colorRSSI(int r) {
  if (r <= -80)      return "#ef4444";
  else if (r <= -60) return "#f97316";
  else               return "#22c55e";
}
String colorBat(int p) {
  if (p < 20)  return "#ef4444";
  else if (p < 50) return "#f97316";
  else         return "#22c55e";
}

// ---------- HTML ----------
String generateHTML(float temperature, float humidity, int rssi) {
  String html = F("<!DOCTYPE html><html lang='sk'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1.0,maximum-scale=1.0'>"
    "<title>ESP8266 Stanica</title>"
    "<style>"

    // Reset + základné
    "*{box-sizing:border-box;margin:0;padding:0}"
    "html{font-size:16px}"
    "body{font-family:Arial,sans-serif;background:#0f172a;color:#e2e8f0;"
         "min-height:100vh;padding:12px}"

    // Wrapper
    ".wrap{max-width:520px;margin:0 auto}"

    // Hlavička
    ".header{text-align:center;padding:16px 0 20px}"
    ".header h1{color:#38bdf8;font-size:clamp(1.1em,4vw,1.5em);margin-bottom:4px}"
    ".header p{color:#475569;font-size:clamp(0.7em,2.5vw,0.85em)}"

    // Karty — grid 2x2 na PC, 2x2 na mobile (menšie)
    ".cards{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:14px}"

    ".card{background:#1e293b;border-radius:12px;padding:14px 12px;"
          "border:1px solid #1e3a5f;display:flex;flex-direction:column;gap:6px}"
    ".card-icon{font-size:clamp(1.4em,5vw,1.8em)}"
    ".card-label{color:#64748b;font-size:clamp(0.65em,2.2vw,0.78em);text-transform:uppercase;letter-spacing:.05em}"
    ".card-value{font-size:clamp(1.2em,4.5vw,1.6em);font-weight:bold;line-height:1}"
    ".card-sub{color:#64748b;font-size:clamp(0.6em,2vw,0.72em)}"

    // Batéria progress v karte
    ".bat-bar-wrap{width:100%;background:#0f172a;border-radius:4px;height:6px;margin-top:4px;overflow:hidden}"
    ".bat-bar-fill{height:100%;border-radius:4px;transition:width .5s}"

    // Nabíjanie karta — celá šírka
    ".card-full{grid-column:1/-1}"

    // Refresh progress
    ".prog-wrap{width:100%;background:#1e293b;border-radius:4px;height:4px;margin-bottom:14px;overflow:hidden}"
    ".prog-fill{width:0%;height:100%;background:#38bdf8;border-radius:4px}"

    // Graf wrapper
    ".chart-wrap{background:#1e293b;border-radius:12px;padding:12px;"
                "border:1px solid #1e3a5f;margin-bottom:14px}"
    ".chart-title{color:#64748b;font-size:0.78em;text-transform:uppercase;"
                 "letter-spacing:.05em;margin-bottom:8px;text-align:center}"
    "canvas{width:100%!important;height:auto!important;display:block}"

    // Footer
    ".footer{text-align:center;color:#334155;font-size:0.7em;padding:8px 0}"

    // Responzívnosť — malé telefóny
    "@media(max-width:360px){"
      ".cards{grid-template-columns:1fr 1fr;gap:8px}"
      ".card{padding:10px 8px}"
    "}"

    "</style></head><body>"
    "<div class='wrap'>"
  );

  // Hlavička
  html += F("<div class='header'>"
    "<h1>⚡ ESP8266 Meracia Stanica</h1>"
    "<p>Teplota · Vlhkosť · Batéria · WiFi</p>"
    "</div>");

  // Refresh progress bar
  html += F("<div class='prog-wrap'><div class='prog-fill' id='prog'></div></div>");

  // Karty
  html += F("<div class='cards'>");

  // Teplota
  html += "<div class='card'>"
    "<span class='card-icon'>🌡</span>"
    "<span class='card-label'>Teplota</span>"
    "<span class='card-value' style='color:" + colorTemp(temperature) + "'>" + String(temperature, 1) + " °C</span>"
    "<span class='card-sub'>DHT11 senzor</span>"
    "</div>";

  // Vlhkosť
  html += "<div class='card'>"
    "<span class='card-icon'>💧</span>"
    "<span class='card-label'>Vlhkosť</span>"
    "<span class='card-value' style='color:" + colorHum(humidity) + "'>" + String(humidity, 1) + " %</span>"
    "<span class='card-sub'>Relatívna vlhkosť</span>"
    "</div>";

  // WiFi
  html += "<div class='card'>"
    "<span class='card-icon'>📶</span>"
    "<span class='card-label'>WiFi signál</span>"
    "<span class='card-value' style='color:" + colorRSSI(rssi) + "'>" + String(rssi) + " dBm</span>"
    "<span class='card-sub'>" + String(rssi > -60 ? "Silný signál" : rssi > -80 ? "Stredný signál" : "Slabý signál") + "</span>"
    "</div>";

  // Batéria
  html += "<div class='card'>"
    "<span class='card-icon'>🔋</span>"
    "<span class='card-label'>Batéria</span>"
    "<span class='card-value' style='color:" + colorBat(batteryPct) + "'>" + String(batteryPct) + " %</span>"
    "<span class='card-sub'>" + String(batteryVolt, 2) + " V</span>"
    "<div class='bat-bar-wrap'>"
    "<div class='bat-bar-fill' style='width:" + String(batteryPct) + "%;background:" + colorBat(batteryPct) + "'></div>"
    "</div>"
    "</div>";

  // Nabíjanie — celá šírka
  html += "<div class='card card-full'>"
    "<div style='display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px'>"
    "<div style='display:flex;align-items:center;gap:10px'>"
    "<span class='card-icon'>☀️</span>"
    "<div><span class='card-label' style='display:block'>Solárne nabíjanie</span>"
    "<span class='card-value' style='font-size:1.1em;color:" + String(isCharging ? "#fbbf24" : "#475569") + "'>"
    + String(isCharging ? "⚡ Nabíja sa" : "— Nenabíja") + "</span></div></div>"
    "<div style='text-align:right'>"
    "<span class='card-label' style='display:block'>Cieľ 85% / Min 30%</span>"
    "<span style='font-size:0.8em;color:#475569'>" + String(batteryVolt, 2) + "V / " + String(V_MAX) + "V max</span>"
    "</div></div></div>";

  html += F("</div>"); // end cards

  // Graf
  html += F("<div class='chart-wrap'>"
    "<div class='chart-title'>📈 História meraní</div>"
    "<canvas id='chart' height='200'></canvas>"
    "</div>");

  // Footer
  html += F("<div class='footer'>ESP8266 · DHT11 · TP4056 · 18650 · Refresh každých 10s</div>"
    "</div>"); // end wrap

  // JavaScript
  html += "<script>";
  html += "const T_NEW=" + String(temperature, 1) + ";";
  html += "const H_NEW=" + String(humidity, 1) + ";";
  html += "const B_NEW=" + String(batteryPct) + ";";

  html += F(R"(
const MAX_PTS = 12;
function store(key, val) {
  let arr = JSON.parse(sessionStorage.getItem(key) || '[]');
  arr.push(val);
  if (arr.length > MAX_PTS) arr.shift();
  sessionStorage.setItem(key, JSON.stringify(arr));
  return arr;
}

let now = new Date().toLocaleTimeString('sk', {hour:'2-digit', minute:'2-digit', second:'2-digit'});
let tArr = store('tH', T_NEW);
let hArr = store('hH', H_NEW);
let bArr = store('bH', B_NEW);
let xArr = store('xH', now);

function drawChart() {
  const canvas = document.getElementById('chart');
  // Nastav canvas rozmer podľa skutočnej šírky
  canvas.width  = canvas.offsetWidth || 460;
  canvas.height = 200;
  const ctx = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height;
  const PAD = { top:32, right:16, bottom:36, left:40 };
  const gW = W - PAD.left - PAD.right;
  const gH = H - PAD.top  - PAD.bottom;

  ctx.clearRect(0, 0, W, H);
  ctx.fillStyle = '#1e293b';
  ctx.fillRect(0, 0, W, H);

  const n = xArr.length;
  if (n < 2) {
    ctx.fillStyle = '#475569';
    ctx.font = '13px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('Čakám na viac dát (min. 2 merania)...', W/2, H/2);
    return;
  }

  // Mriežka
  for (let i = 0; i <= 5; i++) {
    let y = PAD.top + (gH / 5) * i;
    ctx.strokeStyle = '#1e3a5f';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(PAD.left, y);
    ctx.lineTo(PAD.left + gW, y);
    ctx.stroke();
    ctx.fillStyle = '#334155';
    ctx.font = '9px Arial';
    ctx.textAlign = 'right';
    ctx.fillText(String(100 - i * 20), PAD.left - 4, y + 3);
  }

  // X labely
  ctx.fillStyle = '#334155';
  ctx.font = '8px Arial';
  ctx.textAlign = 'center';
  let step = Math.max(1, Math.floor(n / 5));
  for (let i = 0; i < n; i += step) {
    let x = PAD.left + (i / (n - 1)) * gW;
    ctx.fillText(xArr[i], x, H - PAD.bottom + 12);
  }

  const datasets = [
    { data: tArr, color: '#ef4444', label: '°C', yMin: -10, yMax: 50 },
    { data: hArr, color: '#3b82f6', label: '%H', yMin: 0,   yMax: 100 },
    { data: bArr, color: '#22c55e', label: '%B', yMin: 0,   yMax: 100 }
  ];

  datasets.forEach(ds => {
    if (!ds.data || ds.data.length < 2) return;

    // Plocha pod čiarou
    ctx.beginPath();
    ds.data.forEach((val, i) => {
      let x = PAD.left + (i / (n-1)) * gW;
      let norm = (val - ds.yMin) / (ds.yMax - ds.yMin);
      let y = PAD.top + gH - norm * gH;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.lineTo(PAD.left + ((ds.data.length-1)/(n-1)) * gW, PAD.top + gH);
    ctx.lineTo(PAD.left, PAD.top + gH);
    ctx.closePath();
    ctx.fillStyle = ds.color + '18';
    ctx.fill();

    // Čiara
    ctx.beginPath();
    ctx.strokeStyle = ds.color;
    ctx.lineWidth = 2;
    ctx.lineJoin = 'round';
    ds.data.forEach((val, i) => {
      let x = PAD.left + (i / (n-1)) * gW;
      let norm = (val - ds.yMin) / (ds.yMax - ds.yMin);
      let y = PAD.top + gH - norm * gH;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.stroke();

    // Body + posledná hodnota
    ds.data.forEach((val, i) => {
      let x = PAD.left + (i / (n-1)) * gW;
      let norm = (val - ds.yMin) / (ds.yMax - ds.yMin);
      let y = PAD.top + gH - norm * gH;
      ctx.beginPath();
      ctx.arc(x, y, i === ds.data.length-1 ? 4 : 2.5, 0, 2*Math.PI);
      ctx.fillStyle = ds.color;
      ctx.fill();
      if (i === ds.data.length - 1) {
        ctx.fillStyle = ds.color;
        ctx.font = 'bold 10px Arial';
        ctx.textAlign = 'center';
        ctx.fillText(val.toFixed(1) + ' ' + ds.label, x, y - 9);
      }
    });
  });

  // Legenda
  const leg = [
    { color:'#ef4444', text:'Teplota (°C)' },
    { color:'#3b82f6', text:'Vlhkosť (%)' },
    { color:'#22c55e', text:'Batéria (%)' }
  ];
  leg.forEach((l, i) => {
    let lx = PAD.left + i * (gW / 3) + 4;
    let ly = 13;
    ctx.fillStyle = l.color;
    ctx.fillRect(lx, ly - 3, 14, 3);
    ctx.font = '9px Arial';
    ctx.textAlign = 'left';
    ctx.fillStyle = '#94a3b8';
    ctx.fillText(l.text, lx + 17, ly);
  });
}

// Refresh progress (10 sekúnd)
function startProgress() {
  let el = document.getElementById('prog');
  let w = 0;
  let id = setInterval(() => {
    if (w >= 100) { clearInterval(id); location.reload(); }
    else { w++; el.style.width = w + '%'; }
  }, 100);
}

window.onload = () => {
  drawChart();
  startProgress();
  // Prekreslí graf ak sa zmení veľkosť okna
  window.addEventListener('resize', drawChart);
};
)");

  html += "</script></body></html>";
  return html;
}

// ---------- HTTP handler ----------
void handleRoot() {
  int rssi = WiFi.RSSI();
  if (lastTemp == 0 && lastHum == 0) {
    server.send(200, "text/html; charset=UTF-8",
      "<html><body style='background:#0f172a;color:#38bdf8;"
      "text-align:center;padding:40px;font-family:Arial'>"
      "<h2>⏳ Čakám na DHT11...</h2>"
      "<p style='color:#475569;margin-top:8px'>Prosím počkaj 5 sekúnd a obnov stránku</p>"
      "</body></html>");
    return;
  }
  Serial.printf("T: %.1f°C  H: %.1f%%  Bat: %.2fV (%d%%)  RSSI: %d dBm\n",
    lastTemp, lastHum, batteryVolt, batteryPct, rssi);
  server.send(200, "text/html; charset=UTF-8", generateHTML(lastTemp, lastHum, rssi));
}

void startServerIfNeeded() {
  if (!serverStarted && WiFi.status() == WL_CONNECTED) {
    server.on("/", handleRoot);
    server.begin();
    serverStarted = true;
    Serial.print("✅ Web server spustený! IP: ");
    Serial.println(WiFi.localIP());
  }
}

void readDHT() {
  if ((unsigned long)(millis() - lastDHTread) >= 3000) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      lastHum = h;
      lastTemp = t;
    }
    lastDHTread = millis();
  }
}

void manageWifiAndLed() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW);
    startServerIfNeeded();
  } else {
    unsigned long now = millis();
    if ((unsigned long)(now - prevBlinkMs) >= blinkIntervalMs) {
      prevBlinkMs = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
    if ((unsigned long)(now - lastReconnectAttempt) >= reconnectIntervalMs) {
      lastReconnectAttempt = now;
      WiFi.reconnect();
      Serial.println("⚠ WiFi reconnect...");
      serverStarted = false;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n\n===== ESP8266 ŠTART =====");

  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(CE_PIN, OUTPUT);
  digitalWrite(CE_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(true);
  delay(200);
  WiFi.begin(ssid, password);

  Serial.printf("Connecting to: %s\n", ssid);
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 30) {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Pripojené!");
    Serial.print("IP adresa: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⚠ Nepripojené — skúsim neskôr...");
  }
}

void loop() {
  readDHT();
  manageBattery();
  manageWifiAndLed();
  if (serverStarted) server.handleClient();

  // Keepalive
  if ((unsigned long)(millis() - lastPing) >= 30000) {
    lastPing = millis();
    if (WiFi.status() == WL_CONNECTED) WiFi.RSSI();
  }

  // Status výpis každých 10 sekúnd
  if ((unsigned long)(millis() - lastStatus) >= 10000) {
    lastStatus = millis();
    Serial.println("─────────────────────────");
    Serial.printf("WiFi:    %s\n", WiFi.status() == WL_CONNECTED ? "PRIPOJENÉ" : "ODPOJENÉ");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("IP:      %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("RSSI:    %d dBm\n", WiFi.RSSI());
    }
    Serial.printf("Teplota: %.1f C\n", lastTemp);
    Serial.printf("Vlhkost: %.1f %%\n", lastHum);
    Serial.printf("Bateria: %.2f V (%d %%)\n", batteryVolt, batteryPct);
    Serial.printf("Nabija:  %s\n", isCharging ? "ANO" : "NIE");
    Serial.printf("Uptime:  %lu s\n", millis() / 1000);
    Serial.println("─────────────────────────");
  }
}
