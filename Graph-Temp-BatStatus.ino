#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "DHT.h"

// ===== WiFi nastavenie =====
const char* ssid     = "ssid";
const char* password = "pass";

// ===== DHT11 nastavenie =====
#define DHTPIN  D2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== Batéria cez delič (A0) =====
// R1 = 100k (BAT+ → A0), R2 = 100k (A0 → GND)
const float CALIB_RAW_TO_V = 0.00765748; // kalibrácia z meraní
const float BATTERY_MAX = 4.20;
const float BATTERY_MIN = 3.30;

int readAverageRaw(int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; ++i) {
    sum += analogRead(A0);
    delay(5);
  }
  return sum / samples;
}

float readBatteryVoltage() {
  int raw = readAverageRaw(10);
  float v = raw * CALIB_RAW_TO_V;
  if (v < 0) v = 0;
  if (v > 6) v = 6;
  return v;
}

int voltageToPercent(float v) {
  if (v >= BATTERY_MAX) return 100;
  if (v <= BATTERY_MIN) return 0;
  return (int)((v - BATTERY_MIN) * 100.0 / (BATTERY_MAX - BATTERY_MIN));
}

String getBatteryColor(int p) {
  if (p >= 80) return "green";
  else if (p >= 60) return "yellow";
  else if (p >= 36) return "orange";
  else return "red";
}

// ===== Web server =====
ESP8266WebServer server(80);
bool serverStarted = false;

// ===== LED =====
#define LED_PIN LED_BUILTIN
unsigned long prevBlinkMs = 0;
const unsigned long blinkIntervalMs = 200;
bool ledState = false;

// ===== Reconnect správanie =====
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectIntervalMs = 5000;

// ===== Uptime =====
unsigned long startMillis;

// ---------- Pomocné funkcie pre farby ----------
String colorTemperature(float t) {
  if (t < 20)       return "blue";
  else if (t < 25)  return "green";
  else if (t <= 27) return "orange";
  else              return "red";
}

String colorHumidity(float h) {
  if (h < 30)       return "orange";
  else if (h <= 60) return "green";
  else              return "blue";
}

String colorRSSI(int rssi) {
  if (rssi <= -80)      return "red";
  else if (rssi <= -60) return "orange";
  else                  return "green";
}

String getUptime() {
  unsigned long seconds = (millis() - startMillis) / 1000;
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;
  char buf[32];
  sprintf(buf, "%dh %dm %ds", hours, minutes, secs);
  return String(buf);
}

// ---------- HTML stránka ----------
String generateHTML(float t, float h, int rssi, float voltage, int percent) {
  String html = "<!DOCTYPE html><html lang='sk'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<title>ESP8266 Battery & DHT11 Monitor</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;text-align:center;background:#f0f0f0;}";
  html += "table{margin:auto;border-collapse:collapse;width:75%;max-width:550px;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.1)}";
  html += "th,td{padding:12px;border:1px solid #333;font-size:18px;}";
  html += "th{background:#4CAF50;color:#fff}";
  html += "@media(max-width:600px){table{width:95%}}";
  html += "</style></head><body>";
  html += "<h2>🌡️ DHT11 + 🔋 Batéria ESP8266 Monitor</h2>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Hodnota</th></tr>";
  html += "<tr><td>Teplota</td><td style='color:" + colorTemperature(t) + ";font-weight:bold;'>" + String(t, 1) + " °C</td></tr>";
  html += "<tr><td>Vlhkosť</td><td style='color:" + colorHumidity(h) + ";font-weight:bold;'>" + String(h, 1) + " %</td></tr>";
  html += "<tr><td>Wi-Fi RSSI</td><td style='color:" + colorRSSI(rssi) + ";font-weight:bold;'>" + String(rssi) + " dBm</td></tr>";
  html += "<tr><td>Napätie batérie</td><td>" + String(voltage, 2) + " V</td></tr>";
  html += "<tr><td>Úroveň nabitia</td><td style='color:" + getBatteryColor(percent) + ";font-weight:bold;'>" + String(percent) + " %</td></tr>";
  if (percent <= 35)
    html += "<tr><td>Upozornenie</td><td style='color:red;font-weight:bold;'>⚠ Nabite batériu!</td></tr>";
  else
    html += "<tr><td>Stav</td><td>OK</td></tr>";
  html += "<tr><td>Uptime</td><td>" + getUptime() + "</td></tr>";
  html += "</table>";
  html += "<p>Aktualizácia každé 3 sekundy</p>";
  html += "</body></html>";
  return html;
}

// ---------- HTTP handler ----------
void handleRoot() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int rssi = WiFi.RSSI();
  float voltage = readBatteryVoltage();
  int percent = voltageToPercent(voltage);

  if (isnan(h) || isnan(t)) {
    Serial.println("Chyba pri čítaní DHT11!");
    server.send(200, "text/html; charset=UTF-8", "<h1>Chyba pri čítaní DHT11!</h1>");
    return;
  }

  Serial.printf("T: %.1f °C, H: %.1f %%, Vbat: %.2f V, %d %% , RSSI: %d dBm\n", t, h, voltage, percent, rssi);
  server.send(200, "text/html; charset=UTF-8", generateHTML(t, h, rssi, voltage, percent));
}

// ---------- Server štart ----------
void startServerIfNeeded() {
  if (!serverStarted && WiFi.status() == WL_CONNECTED) {
    server.on("/", handleRoot);
    server.begin();
    serverStarted = true;
    Serial.println("Web server spustený!");
    Serial.print("IP Adresa: "); Serial.println(WiFi.localIP());
  }
}

// ---------- Wi-Fi & LED ----------
void manageWifiAndLed() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);
    startServerIfNeeded();
  } else {
    unsigned long now = millis();
    if (now - prevBlinkMs >= blinkIntervalMs) {
      prevBlinkMs = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH);
    }
    if (now - lastReconnectAttempt >= reconnectIntervalMs) {
      lastReconnectAttempt = now;
      WiFi.reconnect();
      Serial.println("WiFi reconnect attempt...");
      serverStarted = false;
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  Serial.println();
  Serial.printf("Pripájam sa na WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);
  startMillis = millis();
}

void loop() {
  manageWifiAndLed();
  if (serverStarted) server.handleClient();
}
