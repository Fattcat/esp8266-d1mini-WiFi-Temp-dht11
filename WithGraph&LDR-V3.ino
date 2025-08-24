#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "DHT.h"

// ===== WiFi nastavenie =====
const char* ssid     = "SSID";
const char* password = "pass";

// ===== DHT11 nastavenie =====
#define DHTPIN  D2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== LDR nastavenie =====
#define LDR_PIN A0

// ===== Web server =====
ESP8266WebServer server(80);
bool serverStarted = false;

// ===== LED (vstavaná) =====
#define LED_PIN LED_BUILTIN
unsigned long prevBlinkMs = 0;
const unsigned long blinkIntervalMs = 200; // rýchle blikanie
bool ledState = false; 

// ===== Reconnect správanie =====
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectIntervalMs = 5000; // pokus o reconnect každých 5 s

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

String colorLux(int lux) {
  if (lux < 200)       return "gray";
  else if (lux < 500)  return "orange";
  else                  return "yellow";
}

// ---------- HTML stránka ----------
String generateHTML(float temperature, float humidity, int rssi, int lux) {
  String html = "<!DOCTYPE html><html lang='sk'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>DHT11 + LDR ESP8266</title>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chartjs-plugin-datalabels'></script>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;text-align:center;background:#f0f0f0;}";
  html += "table{margin:auto;border-collapse:collapse;width:60%;max-width:400px;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.1)}";
  html += "th,td{padding:15px;border:1px solid #333}";
  html += "th{background:#4CAF50;color:#fff}";
  html += "@media(max-width:600px){table{width:90%}}";
  html += ".progress-container{width:60%;background:#ddd;margin:20px auto;height:20px;border-radius:10px;overflow:hidden}";
  html += ".progress-bar{width:0%;height:100%;background:#4CAF50}";
  html += "</style>";
  html += "</head><body>";

  html += "<h2>DHT11 + LDR ESP8266</h2>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Hodnota</th></tr>";
  html += "<tr><td>Teplota</td><td style='color:" + colorTemperature(temperature) + ";font-weight:bold;'>" + String(temperature, 1) + " °C</td></tr>";
  html += "<tr><td>Vlhkosť</td><td style='color:" + colorHumidity(humidity) + ";font-weight:bold;'>" + String(humidity, 1) + " %</td></tr>";
  html += "<tr><td>Wi-Fi RSSI</td><td style='color:" + colorRSSI(rssi) + ";font-weight:bold;'>" + String(rssi) + " dBm</td></tr>";
  html += "<tr><td>Svetlo ☀️</td><td style='color:" + colorLux(lux) + ";font-weight:bold;'>" + String(lux) + " lx</td></tr>";
  html += "</table>";

  html += "<div class='progress-container'><div class='progress-bar' id='progress'></div></div>";
  html += "<canvas id='myChart' width='400' height='200'></canvas>";

  html += "<script>";
  // Progress + auto-refresh
  html += "function move(){let elem=document.getElementById('progress');let w=0;let id=setInterval(function(){if(w>=100){clearInterval(id);location.reload();}else{w+=1;elem.style.width=w+'%';}},100);}";

  // Chart s krátkou históriou v sessionStorage
  html += "function initChart(){";
  html += "  let temperature=" + String(temperature, 1) + ";";
  html += "  let humidity=" + String(humidity, 1) + ";";
  html += "  let lux=" + String(lux) + ";";
  html += "  let time=new Date().toLocaleTimeString();";
  html += "  let tempHistory=JSON.parse(sessionStorage.getItem('tempHistory'))||[];";
  html += "  let humHistory=JSON.parse(sessionStorage.getItem('humHistory'))||[];";
  html += "  let luxHistory=JSON.parse(sessionStorage.getItem('luxHistory'))||[];";
  html += "  let timeHistory=JSON.parse(sessionStorage.getItem('timeHistory'))||[];";
  html += "  tempHistory.push(temperature); humHistory.push(humidity); luxHistory.push(lux); timeHistory.push(time);";
  html += "  if(tempHistory.length>6)tempHistory.shift();";
  html += "  if(humHistory.length>6)humHistory.shift();";
  html += "  if(luxHistory.length>6)luxHistory.shift();";
  html += "  if(timeHistory.length>6)timeHistory.shift();";
  html += "  sessionStorage.setItem('tempHistory',JSON.stringify(tempHistory));";
  html += "  sessionStorage.setItem('humHistory',JSON.stringify(humHistory));";
  html += "  sessionStorage.setItem('luxHistory',JSON.stringify(luxHistory));";
  html += "  sessionStorage.setItem('timeHistory',JSON.stringify(timeHistory));";
  html += "  const ctx=document.getElementById('myChart').getContext('2d');";
  html += "  const data={labels:timeHistory,datasets:[";
  html += "    {label:'Teplota (°C)',data:tempHistory,borderColor:'red',backgroundColor:'rgba(255,0,0,0.2)',tension:0.3,datalabels:{color:'red',anchor:'end',align:'top',formatter:Math.round}},";
  html += "    {label:'Vlhkosť (%)',data:humHistory,borderColor:'blue',backgroundColor:'rgba(0,0,255,0.2)',tension:0.3,datalabels:{color:'blue',anchor:'end',align:'top',formatter:Math.round}},";
  html += "    {label:'Svetlo (lx)',data:luxHistory,borderColor:'orange',backgroundColor:'rgba(255,165,0,0.2)',tension:0.3,datalabels:{color:'orange',anchor:'end',align:'top',formatter:Math.round}}";
  html += "  ]};";
  html += "  const config={type:'line',data:data,options:{animation:false,responsive:true,plugins:{legend:{position:'top'},datalabels:{}},scales:{x:{title:{display:true,text:'Čas'}},y:{beginAtZero:true}}},plugins:[ChartDataLabels]};";
  html += "  new Chart(ctx,config);";
  html += "}";
  html += "window.onload=()=>{move();initChart();};";
  html += "</script>";

  html += "</body></html>";
  return html;
}

// ---------- HTTP handler ----------
void handleRoot() {
  float h = dht.readHumidity();
  float t = dht.readTemperature(); // °C
  int rssi = WiFi.RSSI();
  int ldrValue = analogRead(LDR_PIN); // 0-1023
  int lux = map(ldrValue, 0, 1023, 0, 1000); // približný lux

  if (isnan(h) || isnan(t)) {
    Serial.println("Error reading DHT11!");
    server.send(200, "text/html; charset=UTF-8", "<h1>Error reading DHT11!</h1>");
    return;
  }

  Serial.print("Teplota: "); Serial.print(t, 1); Serial.print(" °C, Vlhkosť: ");
  Serial.print(h, 1); Serial.print(" %, RSSI: "); Serial.print(rssi);
  Serial.print(" dBm, Lux: "); Serial.println(lux);

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
