#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "DHT.h"

// ===== WiFi nastavenie =====
const char* ssid = "ssid";
const char* password = "pass";

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

// ===== LDR nastavenie =====
#define LDR_PIN A0
const float R_FIXED = 10000.0; // 10kΩ rezistor

// ---------- Emoji pre svetlo ----------
const char* getSunEmoji(float lux) {
  if (lux < 50) return "🌑";         // veľmi tmavé
  else if (lux < 200) return "🌘";    // tmavé
  else if (lux < 500) return "☁️";   // oblačno
  else if (lux < 2000) return "⛅";  // stredné svetlo
  else if (lux < 5000) return "🌞";  // jasné
  else return "☀️";                   // veľmi jasné
}

// ---------- Funkcia merania LDR ----------
float readLux() {
  int adc = analogRead(LDR_PIN);
  float Vout = adc * 3.3 / 1023.0;
  float R_LDR = R_FIXED * (3.3 / Vout - 1.0);
  float lux = 500.0 * pow(R_LDR / 1000.0, -1.4); // empirická konverzia
  return lux;
}

// ---------- HTML uložené v PROGMEM ----------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang='sk'>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>DHT11 + LDR ESP8266</title>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<script src='https://cdn.jsdelivr.net/npm/chartjs-plugin-datalabels'></script>
<style>
body{font-family:Arial,sans-serif;text-align:center;background:#f0f0f0;}
table{margin:auto;border-collapse:collapse;width:60%;max-width:400px;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.1);}
th,td{padding:15px;border:1px solid #333}
th{background:#4CAF50;color:#fff}
@media(max-width:600px){table{width:90%}}
.progress-container{width:60%;background:#ddd;margin:20px auto;height:20px;border-radius:10px;overflow:hidden}
.progress-bar{width:0%;height:100%;background:#4CAF50}
</style>
</head>
<body>
<h2>DHT11 + LDR ESP8266</h2>
<table>
<tr><th>Parameter</th><th>Hodnota</th></tr>
<tr><td>Teplota</td><td id="temp">-- °C</td></tr>
<tr><td>Vlhkosť</td><td id="hum">-- %</td></tr>
<tr><td>Wi-Fi RSSI</td><td id="rssi">-- dBm</td></tr>
<tr><td>Sila slnka</td><td id="sun">--</td></tr>
</table>
<canvas id="myChart" width="400" height="200"></canvas>
<script>
let tempHistory = [];
let humHistory = [];
let sunHistory = [];
let timeHistory = [];

function fetchData() {
  fetch('/data').then(response=>response.json()).then(data=>{
    document.getElementById('temp').innerText = data.temperature.toFixed(1)+' °C';
    document.getElementById('hum').innerText = data.humidity.toFixed(1)+' %';
    document.getElementById('rssi').innerText = data.rssi+' dBm';
    document.getElementById('sun').innerText = data.sunEmoji;

    let now = new Date().toLocaleTimeString();
    tempHistory.push(data.temperature);
    humHistory.push(data.humidity);
    sunHistory.push(data.sunEmoji);
    timeHistory.push(now);
    if(tempHistory.length>6){tempHistory.shift(); humHistory.shift(); sunHistory.shift(); timeHistory.shift();}
    updateChart();
  });
}
function updateChart(){
  const ctx = document.getElementById('myChart').getContext('2d');
  if(window.myChart) window.myChart.destroy();
  window.myChart = new Chart(ctx,{
    type:'line',
    data:{
      labels: timeHistory,
      datasets:[
        {label:'Teplota (°C)',data:tempHistory,borderColor:'red',backgroundColor:'rgba(255,0,0,0.2)',tension:0.3},
        {label:'Vlhkosť (%)',data:humHistory,borderColor:'blue',backgroundColor:'rgba(0,0,255,0.2)',tension:0.3},
        {label:'Slnko',data:sunHistory,borderColor:'orange',backgroundColor:'rgba(255,165,0,0.2)',tension:0.3}
      ]
    },
    options:{
      plugins:{legend:{position:'top'},datalabels:{display:false}},
      responsive:true,
      animation:false,
      scales:{y:{beginAtZero:false}}
    }
  });
}
setInterval(fetchData,3000);
window.onload=fetchData;
</script>
</body>
</html>
)rawliteral";

// ---------- JSON handler pre AJAX ----------
void handleData() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int rssi = WiFi.RSSI();
  float lux = readLux();
  const char* emoji = getSunEmoji(lux);

  char buffer[128]; // statický buffer
  snprintf(buffer, sizeof(buffer),
           "{\"temperature\":%.1f,\"humidity\":%.1f,\"rssi\":%d,\"sunEmoji\":\"%s\"}",
           t, h, rssi, emoji);
  server.send(200,"application/json",buffer);
}


// ---------- HTTP handler ----------
void handleRoot() {
  server.send_P(200,"text/html",index_html);
}

// ---------- Spustenie servera ----------
void startServerIfNeeded() {
  if(!serverStarted && WiFi.status()==WL_CONNECTED){
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.begin();
    serverStarted = true;
    Serial.println(F("HTTP server beží"));
    Serial.print(F("IP adresa: ")); Serial.println(WiFi.localIP());
  }
}

// ---------- Wi-Fi & LED manažment ----------
void manageWifiAndLed() {
  if(WiFi.status()==WL_CONNECTED){
    digitalWrite(LED_PIN,HIGH);
    startServerIfNeeded();
  } else {
    unsigned long now=millis();
    if(now-prevBlinkMs>=blinkIntervalMs){
      prevBlinkMs=now;
      ledState=!ledState;
      digitalWrite(LED_PIN,ledState?LOW:HIGH);
    }
    WiFi.reconnect();
    serverStarted=false;
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(LED_PIN,OUTPUT);
  digitalWrite(LED_PIN,HIGH);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  Serial.print("Pripájanie na WiFi: "); Serial.println(ssid);
  WiFi.begin(ssid,password);
}

// ---------- Loop ----------
void loop() {
  manageWifiAndLed();
  if(serverStarted) server.handleClient();
}
