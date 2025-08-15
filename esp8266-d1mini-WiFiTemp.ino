#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "DHT.h"

// --- WiFi nastavenie ---
const char* ssid = "YourSSID-WiFi";
const char* password = "YourPass123";

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

// --- Generovanie web stránky ---
String generateHTML(float temperature, float humidity) {
  String html = "<!DOCTYPE html><html lang='sk'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>DHT11 ESP8266</title>";
  html += "<style>";
  html += "body {font-family: Arial, sans-serif; text-align: center; background-color: #f0f0f0;}";
  html += "table {margin: auto; border-collapse: collapse; width: 60%; max-width: 400px;}";
  html += "th, td {padding: 15px; border: 1px solid #333;}";
  html += "th {background-color: #4CAF50; color: white;}";
  html += "@media(max-width:600px){table{width:90%;}}";
  // --- Loading bar CSS ---
  html += ".progress-container {width: 60%; background-color: #ddd; margin: 20px auto; height: 20px; border-radius: 10px;}";
  html += ".progress-bar {width: 0%; height: 100%; background-color: #4CAF50; border-radius: 10px;}";
  html += "</style>";
  html += "<script>";
  html += "let width = 0;";
  html += "function move() {";
  html += "let elem = document.getElementById('progress');";
  html += "width = 0;";
  html += "let id = setInterval(frame, 100);"; // update každých 100 ms
  html += "function frame() {";
  html += "if(width >= 100) { clearInterval(id); location.reload(); }";
  html += "else { width += 1; elem.style.width = width + '%'; }";
  html += "}}";
  html += "window.onload = move;";
  html += "</script>";
  html += "</head><body>";
  html += "<h2>DHT11 ESP8266 - Temperature and Humidity</h2>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Value</th></tr>";
  html += "<tr><td>Temperature</td><td style='color:" + colorTemperature(temperature) + "; font-weight:bold;'>" + String(temperature) + " °C</td></tr>";
  html += "<tr><td>Humidity</td><td style='color:" + colorHumidity(humidity) + "; font-weight:bold;'>" + String(humidity) + " %</td></tr>";
  html += "</table>";
  // --- Loading bar ---
  html += "<div class='progress-container'><div class='progress-bar' id='progress'></div></div>";
  html += "</body></html>";
  return html;
}

// --- Handler pre root ---
void handleRoot() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Error reading DHT11!");
    server.send(200, "text/html; charset=UTF-8", "<h1>Error reading DHT11!</h1>");
    return;
  }

  Serial.print("Temperature: "); Serial.print(temperature); Serial.print(" °C, Humidity: "); Serial.print(humidity); Serial.println(" %");

  server.send(200, "text/html; charset=UTF-8", generateHTML(temperature, humidity));
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
