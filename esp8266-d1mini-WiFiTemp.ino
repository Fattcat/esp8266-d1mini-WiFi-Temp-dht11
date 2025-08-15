#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "DHT.h"

// --- Nastavenie WiFi ---
const char* ssid = "YourSSID-WiFi";
const char* password = "YourPass123";

// --- Nastavenie DHT11 ---
#define DHTPIN D2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// --- Web server na porte 80 ---
ESP8266WebServer server(80);

// Funkcia pre určenie farby teploty
String colorTemperature(float t) {
  if (t < 20) return "blue";
  else if (t < 25) return "green";
  else if (t <= 27) return "orange";
  else return "red";
}

// Funkcia pre určenie farby vlhkosti
String colorHumidity(float h) {
  if (h < 30) return "orange";
  else if (h <= 60) return "green";
  else return "blue";
}

// Funkcia na generovanie web stránky
String generateHTML(float temperature, float humidity) {
  String html = "<!DOCTYPE html><html lang='sk'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='10'>"; // auto-refresh
  html += "<title>DHT11 ESP8266</title>";
  html += "<style>";
  html += "body {font-family: Arial, sans-serif; text-align: center; background-color: #f0f0f0;}";
  html += "table {margin: auto; border-collapse: collapse; width: 60%; max-width: 400px;}";
  html += "th, td {padding: 15px; border: 1px solid #333;}";
  html += "th {background-color: #4CAF50; color: white;}";
  html += "@media(max-width:600px){table{width:90%;}}";
  html += "</style></head><body>";
  html += "<h2>DHT11 ESP8266 - Temp & Humidity</h2>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Hodnota</th></tr>";

  html += "<tr><td>Teplota</td><td style='color:" + colorTemperature(temperature) + "; font-weight:bold;'>" 
          + String(temperature) + " °C</td></tr>";
  html += "<tr><td>Vlhkosť</td><td style='color:" + colorHumidity(humidity) + "; font-weight:bold;'>" 
          + String(humidity) + " %</td></tr>";

  html += "</table>";
  html += "<p>Web refresh new data every 10 seconds</p>";
  html += "</body></html>";
  return html;
}

// --- Handler pre root ---
void handleRoot() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Eror with reading DHT11! check connection");
    server.send(200, "text/html; charset=UTF-8", "<h1>Eror with reading DHT11! check connection</h1>");
    return;
  }

  Serial.print("Temp: ");
  Serial.print(temperature);
  Serial.print(" °C, Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  server.send(200, "text/html; charset=UTF-8", generateHTML(temperature, humidity));
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  delay(100);

  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retry++;
    if (retry > 30) {
      Serial.println("\nUnable to connecto to WiFi! Check your SSID or Passwd");
      return;
    }
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("IP adresa: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.begin();
  Serial.println("Web server started!");
}

void loop() {
  server.handleClient();
}
