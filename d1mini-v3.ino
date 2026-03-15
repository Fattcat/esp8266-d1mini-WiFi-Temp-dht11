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
  // Priemeruj 5 meraní pre stabilitu
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

  // Ignoruj nereálne merania (nezapojený delič)
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

// ===== Reconnect =====
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectIntervalMs = 5000;

// ===== Keepalive =====
unsigned long lastPing = 0;

// ---------- Farby ----------
String colorTemperature(float t) {
  if (t < 20)       return "#3b82f6";
  else if (t < 25)  return "#22c55e";
  else if (t <= 27) return "#f97316";
  else              return "#ef4444";
}
String colorHumidity(float h) {
  if (h < 30)       return "#f97316";
  else if (h <= 60) return "#22c55e";
  else              return "#3b82f6";
}
String colorRSSI(int rssi) {
  if (rssi <= -80)      return "#ef4444";
  else if (rssi <= -60) return "#f97316";
  else                  return "#22c55e";
}
String colorBat(int pct) {
  if (pct < 20)  return "#ef4444";
  else if (pct < 50) return "#f97316";
  else           return "#22c55e";
}

// ---------- HTML ----------
String generateHTML(float temperature, float humidity, int rssi) {
  String html = F("<!DOCTYPE html><html lang='sk'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
    "<title>ESP8266 Stanica</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:Arial,sans-serif;background:#1a1a2e;color:#e0e0e0;text-align:center;padding:16px}"
    "h2{color:#38bdf8;margin-bottom:16px;font-size:1.3em}"
    "table{margin:auto;border-collapse:collapse;width:95%;max-width:480px;background:#16213e;border-radius:10px;overflow:hidden;box-shadow:0 4px 12px rgba(0,0,0,.4)}"
    "th,td{padding:13px 16px;border-bottom:1px solid #0f3460;text-align:left}"
    "th{background:#0f3460;color:#38bdf8;font-size:.9em}"
    "td:first-child{color:#94a3b8;font-size:.9em}"
    "td:last-child{font-weight:bold}"
    ".bat-wrap{width:120px;background:#0f3460;border-radius:6px;height:12px;display:inline-block;vertical-align:middle;margin-left:8px;overflow:hidden}"
    ".bat-fill{height:100%;border-radius:6px}"
    ".prog-wrap{width:80%;max-width:400px;background:#0f3460;margin:16px auto;height:6px;border-radius:3px;overflow:hidden}"
    ".prog-fill{width:0%;height:100%;background:#38bdf8;border-radius:3px}"
    "canvas{margin:20px auto;display:block;background:#16213e;border-radius:10px;padding:10px}"
    ".charge{color:#fbbf24}"
    "</style></head><body>");

  html += F("<h2>⚡ ESP8266 Meracia Stanica</h2>");
  html += F("<table><tr><th>Parameter</th><th>Hodnota</th></tr>");

  html += "<tr><td>🌡 Teplota</td><td style='color:" + colorTemperature(temperature) + "'>" + String(temperature, 1) + " °C</td></tr>";
  html += "<tr><td>💧 Vlhkosť</td><td style='color:" + colorHumidity(humidity) + "'>" + String(humidity, 1) + " %</td></tr>";
  html += "<tr><td>📶 Wi-Fi RSSI</td><td style='color:" + colorRSSI(rssi) + "'>" + String(rssi) + " dBm</td></tr>";

  html += "<tr><td>🔋 Batéria</td><td style='color:" + colorBat(batteryPct) + "'>";
  html += String(batteryVolt, 2) + " V (" + String(batteryPct) + "%)";
  html += "<span class='bat-wrap'><span class='bat-fill' style='width:" + String(batteryPct) + "%;background:" + colorBat(batteryPct) + "'></span></span>";
  html += "</td></tr>";

  html += "<tr><td>⚡ Nabíjanie</td><td class='charge'>" + String(isCharging ? "ÁNO — nabíja sa" : "NIE") + "</td></tr>";
  html += F("</table>");

  html += F("<div class='prog-wrap'><div class='prog-fill' id='prog'></div></div>");
  html += F("<canvas id='chart' width='460' height='220'></canvas>");

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
  const c = document.getElementById('chart');
  const ctx = c.getContext('2d');
  const W = c.width, H = c.height;
  const PAD = { top: 30, right: 20, bottom: 40, left: 42 };
  const gW = W - PAD.left - PAD.right;
  const gH = H - PAD.top - PAD.bottom;

  ctx.clearRect(0, 0, W, H);
  ctx.fillStyle = '#16213e';
  ctx.fillRect(0, 0, W, H);

  const datasets = [
    { data: tArr, color: '#ef4444', label: '°C', yMin: -10, yMax: 50 },
    { data: hArr, color: '#3b82f6', label: '%H', yMin: 0,   yMax: 100 },
    { data: bArr, color: '#22c55e', label: '%B', yMin: 0,   yMax: 100 }
  ];

  const n = xArr.length;
  if (n < 2) {
    ctx.fillStyle = '#64748b';
    ctx.font = '13px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('Čakám na viac dát...', W/2, H/2);
    return;
  }

  // Mriežka
  ctx.strokeStyle = '#0f3460';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 5; i++) {
    let y = PAD.top + (gH / 5) * i;
    ctx.beginPath();
    ctx.moveTo(PAD.left, y);
    ctx.lineTo(PAD.left + gW, y);
    ctx.stroke();
    ctx.fillStyle = '#475569';
    ctx.font = '10px Arial';
    ctx.textAlign = 'right';
    ctx.fillText(String(100 - i * 20), PAD.left - 4, y + 3);
  }

  // X osi labely
  ctx.fillStyle = '#475569';
  ctx.font = '9px Arial';
  ctx.textAlign = 'center';
  let step = Math.max(1, Math.floor(n / 4));
  for (let i = 0; i < n; i += step) {
    let x = PAD.left + (i / (n - 1)) * gW;
    ctx.fillText(xArr[i], x, H - PAD.bottom + 14);
  }

  // Čiary + body
  datasets.forEach(ds => {
    if (!ds.data || ds.data.length < 2) return;
    ctx.beginPath();
    ctx.strokeStyle = ds.color;
    ctx.lineWidth = 2.5;
    ctx.lineJoin = 'round';
    ds.data.forEach((val, i) => {
      let x = PAD.left + (i / (n - 1)) * gW;
      let norm = (val - ds.yMin) / (ds.yMax - ds.yMin);
      let y = PAD.top + gH - norm * gH;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.stroke();

    ds.data.forEach((val, i) => {
      let x = PAD.left + (i / (n - 1)) * gW;
      let norm = (val - ds.yMin) / (ds.yMax - ds.yMin);
      let y = PAD.top + gH - norm * gH;
      ctx.beginPath();
      ctx.arc(x, y, 3, 0, 2 * Math.PI);
      ctx.fillStyle = ds.color;
      ctx.fill();
      if (i === ds.data.length - 1) {
        ctx.fillStyle = ds.color;
        ctx.font = 'bold 10px Arial';
        ctx.textAlign = 'center';
        ctx.fillText(val.toFixed(1) + ' ' + ds.label, x, y - 8);
      }
    });
  });

  // Legenda
  const legends = [
    { color: '#ef4444', text: 'Teplota (°C)' },
    { color: '#3b82f6', text: 'Vlhkosť (%)' },
    { color: '#22c55e', text: 'Batéria (%)' }
  ];
  legends.forEach((l, i) => {
    let lx = PAD.left + i * (gW / 3) + 10;
    let ly = PAD.top - 12;
    ctx.fillStyle = l.color;
    ctx.fillRect(lx, ly, 18, 3);
    ctx.font = '10px Arial';
    ctx.textAlign = 'left';
    ctx.fillText(l.text, lx + 22, ly + 4);
  });
}

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
      "<html><body style='background:#1a1a2e;color:#38bdf8;text-align:center;padding:40px'>"
      "<h2>⏳ Čakám na DHT11...</h2></body></html>");
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
    Serial.print("Web server spustený! IP: ");
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
    digitalWrite(LED_PIN, LOW);   // svieti = pripojené
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
      Serial.println("WiFi reconnect...");
      serverStarted = false;
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  pinMode(CE_PIN, OUTPUT);
  digitalWrite(CE_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);   // vypne power save — FIX odpojenia
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(true);                // vymaže staré uložené WiFi
  delay(200);
  WiFi.begin(ssid, password);

  Serial.printf("\nConnecting to: %s\n", ssid);
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

  // Keepalive — udrží WiFi spojenie aktívne
  if ((unsigned long)(millis() - lastPing) >= 30000) {
    lastPing = millis();
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.RSSI();
    }
  }
}
