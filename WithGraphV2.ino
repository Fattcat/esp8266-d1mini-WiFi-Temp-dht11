#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "DHT.h"

// ===== WiFi nastavenie =====
const char* ssid     = "ssid";
const char* password = "password";

// ===== DHT11 nastavenie =====
#define DHTPIN  D2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== Web server =====
ESP8266WebServer server(80);
bool serverStarted = false;

// ===== LED (vstavaná) =====
// Na D1 mini je LED na D4 (GPIO2) a je aktívna v logickej úrovni LOW.
#define LED_PIN LED_BUILTIN
unsigned long prevBlinkMs = 0;
const unsigned long blinkIntervalMs = 200; // rýchle blikanie
bool ledState = false; // len logická premenná pre toggle

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
  if (rssi <= -80)      return "red";    // slabý
  else if (rssi <= -60) return "orange"; // dobrý
  else                  return "green";  // výborný
}

// ---------- HTML stránka ----------
String generateHTML(float temperature, float humidity, int rssi) {
  String html = "<!DOCTYPE html><html lang='sk'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>DHT11 ESP8266</title>";
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

  html += "<h2>DHT11 ESP8266 - Teplota, Vlhkosť & Wi-Fi</h2>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Hodnota</th></tr>";
  html += "<tr><td>Teplota</td><td style='color:" + colorTemperature(temperature) + ";font-weight:bold;'>" + String(temperature, 1) + " °C</td></tr>";
  html += "<tr><td>Vlhkosť</td><td style='color:" + colorHumidity(humidity) + ";font-weight:bold;'>" + String(humidity, 1) + " %</td></tr>";
  html += "<tr><td>Wi-Fi RSSI</td><td style='color:" + colorRSSI(rssi) + ";font-weight:bold;'>" + String(rssi) + " dBm</td></tr>";
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
  html += "  let time=new Date().toLocaleTimeString();";
  html += "  let tempHistory=JSON.parse(sessionStorage.getItem('tempHistory'))||[];";
  html += "  let humHistory=JSON.parse(sessionStorage.getItem('humHistory'))||[];";
  html += "  let timeHistory=JSON.parse(sessionStorage.getItem('timeHistory'))||[];";
  html += "  tempHistory.push(temperature); humHistory.push(humidity); timeHistory.push(time);";
  html += "  if(tempHistory.length>6)tempHistory.shift();";
  html += "  if(humHistory.length>6)humHistory.shift();";
  html += "  if(timeHistory.length>6)timeHistory.shift();";
  html += "  sessionStorage.setItem('tempHistory',JSON.stringify(tempHistory));";
  html += "  sessionStorage.setItem('humHistory',JSON.stringify(humHistory));";
  html += "  sessionStorage.setItem('timeHistory',JSON.stringify(timeHistory));";
  html += "  const ctx=document.getElementById('myChart').getContext('2d');";
  html += "  const data={labels:timeHistory,datasets:[";
  html += "    {label:'Teplota (°C)',data:tempHistory,borderColor:'red',backgroundColor:'rgba(255,0,0,0.2)',tension:0.3,datalabels:{color:'red',anchor:'end',align:'top',formatter:Math.round}},";
  html += "    {label:'Vlhkosť (%)',data:humHistory,borderColor:'blue',backgroundColor:'rgba(0,0,255,0.2)',tension:0.3,datalabels:{color:'blue',anchor:'end',align:'top',formatter:Math.round}}";
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

  if (isnan(h) || isnan(t)) {
    Serial.println("Error reading DHT11!");
    server.send(200, "text/html; charset=UTF-8", "<h1>Error reading DHT11!</h1>");
    return;
  }

  Serial.print("Teplota: "); Serial.print(t, 1); Serial.print(" °C, Vlhkosť: ");
  Serial.print(h, 1); Serial.print(" %, RSSI: "); Serial.println(rssi);

  server.send(200, "text/html; charset=UTF-8", generateHTML(t, h, rssi));
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
    // pripojené: LED zhasnutá (aktívna LOW => HIGH = vypnutá)
    digitalWrite(LED_PIN, HIGH);
    startServerIfNeeded();
  } else {
    // ne/prerušene pripojenie: rýchle blikanie + periodický reconnect
    unsigned long now = millis();

    if (now - prevBlinkMs >= blinkIntervalMs) {
      prevBlinkMs = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH); // LOW = svieti, HIGH = zhasnutá
    }

    if (now - lastReconnectAttempt >= reconnectIntervalMs) {
      lastReconnectAttempt = now;
      // Pokus o znovupripojenie (neblokujúci)
      WiFi.reconnect(); // využije posledné credentials
      // Ak by bolo potrebné tvrdé začatie:
      // WiFi.begin(ssid, password);
      Serial.println("WiFi reconnect attempt...");
      serverStarted = false; // ak stratíme IP, po získaní ju znova spustíme
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // zhasnutá na štarte

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);      // nechceme zapisovať do flash pri každej zmene
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  Serial.println();
  Serial.print("Connecting to WiFi: "); Serial.println(ssid);
  WiFi.begin(ssid, password);
  // Nečakáme blokujúco — stav rieši manageWifiAndLed()
}

void loop() {
  manageWifiAndLed();
  if (serverStarted) {
    server.handleClient();
  }
}