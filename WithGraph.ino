#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "DHT.h"

// --- WiFi nastavenie ---
const char* ssid = "ssid";
const char* password = "password";

// --- DHT11 nastavenie ---
#define DHTPIN D2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// --- Web server ---
ESP8266WebServer server(80);

// --- Funkcia farby teploty ---
String colorTemperature(float t) {
  if (t < 20) return "blue";
  else if (t < 25) return "green";
  else if (t <= 27) return "orange";
  else return "red";
}

// --- Funkcia farby vlhkosti ---
String colorHumidity(float h) {
  if (h < 30) return "orange";
  else if (h <= 60) return "green";
  else return "blue";
}

// --- Funkcia farby WiFi signĂˇlu ---
String colorRSSI(int rssi) {
  if (rssi <= -80) return "red";       // slabĂ˝ signĂˇl
  else if (rssi <= -60) return "orange"; // dobrĂ˝ signĂˇl
  else return "green";                  // vĂ˝bornĂ˝ signĂˇl
}

// --- Generovanie web strĂˇnky ---
String generateHTML(float temperature, float humidity, int rssi) {
  String html = "<!DOCTYPE html><html lang='sk'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>DHT11 ESP8266</title>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"; // Chart.js
  html += "<style>";
  html += "body {font-family: Arial, sans-serif; text-align: center; background-color: #f0f0f0;}";
  html += "table {margin: auto; border-collapse: collapse; width: 60%; max-width: 400px;}";
  html += "th, td {padding: 15px; border: 1px solid #333;}";
  html += "th {background-color: #4CAF50; color: white;}";
  html += "@media(max-width:600px){table{width:90%;}}";
  html += ".progress-container {width: 60%; background-color: #ddd; margin: 20px auto; height: 20px; border-radius: 10px;}";
  html += ".progress-bar {width: 0%; height: 100%; background-color: #4CAF50; border-radius: 10px;}";
  html += "</style>";
  html += "<script>";
  html += "let width = 0;";
  html += "function move() {";
  html += "let elem = document.getElementById('progress');";
  html += "width = 0;";
  html += "let id = setInterval(frame, 100);";
  html += "function frame() {";
  html += "if(width >= 100) { clearInterval(id); location.reload(); }";
  html += "else { width += 1; elem.style.width = width + '%'; }";
  html += "}}";
  html += "window.onload = move;";
  html += "</script>";
  html += "</head><body>";
  html += "<h2>DHT11 ESP8266 - Teplota, VlhkosĹĄ & WiFi</h2>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Value</th></tr>";
  html += "<tr><td>Teplota</td><td style='color:" + colorTemperature(temperature) + "; font-weight:bold;'>" + String(temperature) + " Â°C</td></tr>";
  html += "<tr><td>VlhkosĹĄ</td><td style='color:" + colorHumidity(humidity) + "; font-weight:bold;'>" + String(humidity) + " %</td></tr>";
  html += "<tr><td>WiFi RSSI</td><td style='color:" + colorRSSI(rssi) + "; font-weight:bold;'>" + String(rssi) + " dBm</td></tr>";
  html += "</table>";
  html += "<div class='progress-container'><div class='progress-bar' id='progress'></div></div>";

  // --- Graf Chart.js ---
  // --- Graf Chart.js s histĂłriou ---
  html += "<canvas id='myChart' width='400' height='200'></canvas>";

  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chartjs-plugin-datalabels'></script>";
  html += "<script>";
  html += "function move() {";
  html += "  let elem = document.getElementById('progress');";
  html += "  let width = 0;";
  html += "  let id = setInterval(function() {";
  html += "    if (width >= 100) { clearInterval(id); location.reload(); }";
  html += "    else { width += 1; elem.style.width = width + '%'; }";
  html += "  }, 100);";
  html += "}";
  
  html += "function initChart() {";
  html += "  let temperature = " + String(temperature) + ";";
  html += "  let humidity = " + String(humidity) + ";";
  html += "  let time = new Date().toLocaleTimeString();";

  html += "  let tempHistory = JSON.parse(sessionStorage.getItem('tempHistory')) || [];";
  html += "  let humHistory = JSON.parse(sessionStorage.getItem('humHistory')) || [];";
  html += "  let timeHistory = JSON.parse(sessionStorage.getItem('timeHistory')) || [];";

  html += "  tempHistory.push(temperature);";
  html += "  humHistory.push(humidity);";
  html += "  timeHistory.push(time);";

  html += "  if (tempHistory.length > 6) tempHistory.shift();";
  html += "  if (humHistory.length > 6) humHistory.shift();";
  html += "  if (timeHistory.length > 6) timeHistory.shift();";

  html += "  sessionStorage.setItem('tempHistory', JSON.stringify(tempHistory));";
  html += "  sessionStorage.setItem('humHistory', JSON.stringify(humHistory));";
  html += "  sessionStorage.setItem('timeHistory', JSON.stringify(timeHistory));";

  html += "  const ctx = document.getElementById('myChart').getContext('2d');";
  html += "  const data = {";
  html += "    labels: timeHistory,";
  html += "    datasets: [";
  html += "      {";
  html += "        label: 'Teplota (Â°C)',";
  html += "        data: tempHistory,";
  html += "        borderColor: 'red',";
  html += "        backgroundColor: 'rgba(255,0,0,0.2)',";
  html += "        tension: 0.3,";
  html += "        datalabels: { color: 'red', anchor: 'end', align: 'top', formatter: Math.round }";
  html += "      },";
  html += "      {";
  html += "        label: 'VlhkosĹĄ (%)',";
  html += "        data: humHistory,";
  html += "        borderColor: 'blue',";
  html += "        backgroundColor: 'rgba(0,0,255,0.2)',";
  html += "        tension: 0.3,";
  html += "        datalabels: { color: 'blue', anchor: 'end', align: 'top', formatter: Math.round }";
  html += "      }";
  html += "    ]";
  html += "  };";
  html += "  const config = {";
  html += "    type: 'line',";
  html += "    data: data,";
  html += "    options: {";
  html += "      animation: false,";
  html += "      responsive: true,";
  html += "      plugins: {";
  html += "        legend: { position: 'top' },";
  html += "        datalabels: {}";
  html += "      },";
  html += "      scales: {";
  html += "        x: { title: { display: true, text: 'ÄŚas' } },";
  html += "        y: { beginAtZero: true }";
  html += "      }";
  html += "    },";
  html += "    plugins: [ChartDataLabels]";
  html += "  };";
  html += "  new Chart(ctx, config);";
  html += "}";
  
  html += "window.onload = () => { move(); initChart(); };";
  html += "</script>";

  html += "</body></html>";
  return html;
}

// --- Handler pre root ---
void handleRoot() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  int rssi = WiFi.RSSI();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Error reading DHT11!");
    server.send(200, "text/html; charset=UTF-8", "<h1>Error reading DHT11!</h1>");
    return;
  }

  Serial.print("Teplota: "); Serial.print(temperature); Serial.print(" Â°C, VlhkosĹĄ: "); Serial.print(humidity); Serial.print(" %, RSSI: "); Serial.println(rssi);

  server.send(200, "text/html; charset=UTF-8", generateHTML(temperature, humidity, rssi));
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  delay(100);

  Serial.println();
  Serial.print("Connecting to WiFi: "); Serial.println(ssid);
  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retry++;
    if (retry > 30) { Serial.println("\nFailed to connect WiFi!"); return; }
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: "); Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.begin();
  Serial.println("Web server started!");
}

void loop() {
  server.handleClient();
}