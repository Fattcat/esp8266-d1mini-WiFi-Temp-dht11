#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "DHT.h"

// ===== WiFi nastavenie =====
const char* ssid     = "SSID";
const char* password = "PASSWORD";

// ===== DHT11 nastavenie =====
#define DHTPIN  D2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== Web server =====
ESP8266WebServer server(80);
bool serverStarted = false;

// ===== LED (vstavaná) =====
#define LED_PIN LED_BUILTIN
unsigned long prevBlinkMs = 0;
const unsigned long blinkIntervalMs = 200;
bool ledState = false;

// ===== Reconnect správanie =====
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectIntervalMs = 5000;

// ===== LDR nastavenie =====
#define LDR_PIN A0
const float VCC = 3.3;
const int RLDR_FIXED = 10000; // 10kΩ rezistor na GND

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

// ---------- LDR funkcie ----------
float readLux() {
  int adcValue = analogRead(LDR_PIN);
  float Vout = adcValue * (VCC / 1023.0);
  float R_LDR = (VCC * RLDR_FIXED / Vout) - RLDR_FIXED;
  if (R_LDR < 1) R_LDR = 1;
  // empirická konverzia na lux (orientačná)
  float lux = 500000.0 / R_LDR;
  return lux;
}

String luxLevel(float lux) {
  if (lux < 50) return "🌑 Tma";
  else if (lux < 500) return "🌒 Slabé svetlo";
  else if (lux < 5000) return "⛅ Stredné svetlo";
  else if (lux < 20000) return "☀️ Jasné svetlo";
  else return "🌞 Priame slnko";
}

// ---------- HTML stránka ----------
String generateHTML(float temperature, float humidity, int rssi, float lux) {
  String html = "<!DOCTYPE html><html lang='sk'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>DHT11 ESP8266 + LDR</title>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chartjs-plugin-datalabels'></script>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;text-align:center;background:#f0f0f0;}";
  html += "table{margin:auto;border-collapse:collapse;width:70%;max-width:500px;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.1)}";
  html += "th,td{padding:15px;border:1px solid #333}";
  html += "th{background:#4CAF50;color:#fff}";
  html += "@media(max-width:600px){table{width:95%}}";
  html += ".progress-container{width:70%;background:#ddd;margin:20px auto;height:20px;border-radius:10px;overflow:hidden}";
  html += ".progress-bar{width:0%;height:100%;background:#4CAF50}";
  html += "</style>";
  html += "</head><body>";

  html += "<h2>DHT11 + LDR ESP8266</h2>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Hodnota</th></tr>";
  html += "<tr><td>Teplota</td><td style='color:" + colorTemperature(temperature) + ";font-weight:bold;'>" + String(temperature, 1) + " °C</td></tr>";
  html += "<tr><td>Vlhkosť</td><td style='color:" + colorHumidity(humidity) + ";font-weight:bold;'>" + String(humidity, 1) + " %</td></tr>";
  html += "<tr><td>Wi-Fi RSSI</td><td style='color:" + colorRSSI(rssi) + ";font-weight:bold;'>" + String(rssi) + " dBm</td></tr>";
  html += "<tr><td>Svetlo (LDR)</td><td><b>" + String(lux, 0) + " lx</b> - " + luxLevel(lux) + "</td></tr>";
  html += "</table>";

  html += "<div class='progress-container'><div class='progress-bar' id='progress'></div></div>";
  html += "<canvas id='myChart' width='400' height='200'></canvas>";

  html += "<script>";
  html += "function move(){let elem=document.getElementById('progress');let w=0;let id=setInterval(function(){if(w>=100){clearInterval(id);location.reload();}else{w+=1;elem.style.width=w+'%';}},100);}";
  html += "window.onload=()=>{move();};";
  html += "</script>";

  html += "</body></html>";
  return html;
}

// ---------- HTTP handler ----------
void handleRoot() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int rssi = WiFi.RSSI();
  float lux = readLux();

  if (isnan(h) || isnan(t)) {
    Serial.println("Error reading DHT11!");
    server.send(200, "text/html; charset=UTF-8", "<h1>Error reading DHT11!</h1>");
    return;
  }

  Serial.print("Teplota: "); Serial.print(t, 1);
  Serial.print(" °C, Vlhkosť: "); Serial.print(h, 1);
  Serial.print(" %, RSSI: "); Serial.print(rssi);
  Serial.print(" dBm, Lux: "); Serial.print(lux, 0);
  Serial.println(" lx");

  server.send(200, "text/html; charset=UTF-8", generateHTML(t, h, rssi, lux));
}

// ---------- Spustenie servera až po získaní IP ----------
void startServerIfNeeded() {
  if (!serverStarted && WiFi.status() == WL_CONNECTED) {
    server.on("/", handleRoot);
    server.begin();
    serverStarted = true;
    Serial.println("Web server started!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
  }
}

// ---------- Wi-Fi & LED manažment ----------
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
  Serial.print("Connecting to WiFi: "); Serial.println(ssid);
  WiFi.begin(ssid, password);
}

void loop() {
  manageWifiAndLed();
  if (serverStarted) {
    server.handleClient();
  }
}
