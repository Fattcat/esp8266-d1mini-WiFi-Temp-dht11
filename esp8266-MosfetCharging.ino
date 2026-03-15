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

// ===== Batéria + MOSFET =====
// MOSFET (napr. IRLZ44N): Gate na D1
// HIGH = MOSFET otvorený = nabíjanie ZAPNUTÉ
// LOW  = MOSFET zatvorený = nabíjanie VYPNUTÉ
#define MOSFET_PIN D1
#define ADC_PIN    A0

const float V_MAX = 4.00;   // 85% — zastav nabíjanie
const float V_MIN = 3.60;   // 30% — začni nabíjanie
const float R1    = 330.0;
const float R2    = 100.0;

bool  isCharging  = false;
float batteryVolt = 0;
int   batteryPct  = 0;

// ===== Hodinové priemerovanie =====
float  accTemp = 0, accHum = 0, accBat = 0;
int    accCount = 0;
unsigned long lastHourlyStore = 0;
const unsigned long HOURLY_INTERVAL = 3600000UL;

#define MAX_HOURLY 24
float  hTemp[MAX_HOURLY];
float  hHum[MAX_HOURLY];
float  hBat[MAX_HOURLY];
String hLabel[MAX_HOURLY];
int    hCount = 0;
int    hIndex = 0;

// ===== Uptime =====
unsigned long startMillis = 0;

// ===== LED =====
// ESP8266 built-in LED: LOW = zapnutá, HIGH = vypnutá (invertovaná logika)
#define LED_PIN LED_BUILTIN
unsigned long prevBlinkMs    = 0;
const unsigned long blinkIntervalMs = 200;
bool ledState = false;

// ===== Timery =====
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectIntervalMs = 5000;
unsigned long lastPing   = 0;
unsigned long lastStatus = 0;

// ===== Web server =====
ESP8266WebServer server(80);
bool serverStarted = false;

// ──────────────────────────────────────────
//  Batéria
// ──────────────────────────────────────────

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

  // Ochrana pred nereálnymi hodnotami
  if (batteryVolt < 2.5 || batteryVolt > 4.3) {
    Serial.printf("Nereálne napätie: %.2fV — preskakujem\n", batteryVolt);
    digitalWrite(MOSFET_PIN, LOW);
    isCharging = false;
    return;
  }

  // Zapni nabíjanie pri <= 30%
  if (!isCharging && batteryVolt <= V_MIN) {
    digitalWrite(MOSFET_PIN, HIGH);  // MOSFET otvorený → prúd tečie do TP4056
    isCharging = true;
    Serial.println(">>> NABÍJANIE ZAPNUTÉ (MOSFET HIGH)");
  }

  // Vypni nabíjanie pri >= 85%
  if (isCharging && batteryVolt >= V_MAX) {
    digitalWrite(MOSFET_PIN, LOW);   // MOSFET zatvorený → nabíjanie prerušené
    isCharging = false;
    Serial.println(">>> NABÍJANIE VYPNUTÉ (MOSFET LOW)");
  }
}

// ──────────────────────────────────────────
//  Hodinové záznamy
// ──────────────────────────────────────────

void accumulateHourly() {
  if (lastTemp == 0 && lastHum == 0) return;
  accTemp += lastTemp;
  accHum  += lastHum;
  accBat  += batteryPct;
  accCount++;
}

void storeHourlyAverage() {
  if (accCount == 0) return;

  float avgT = accTemp / accCount;
  float avgH = accHum  / accCount;
  float avgB = accBat  / accCount;

  unsigned long totalS = millis() / 1000;
  unsigned long d = totalS / 86400;
  unsigned long h = (totalS % 86400) / 3600;
  String lbl = (d > 0) ? String(d) + "d" + String(h) + "h" : String(h) + "h";

  hTemp[hIndex]  = avgT;
  hHum[hIndex]   = avgH;
  hBat[hIndex]   = avgB;
  hLabel[hIndex] = lbl;

  hIndex = (hIndex + 1) % MAX_HOURLY;
  if (hCount < MAX_HOURLY) hCount++;

  Serial.printf(">>> Hodinový priemer: T=%.1f H=%.1f B=%.0f label=%s\n",
    avgT, avgH, avgB, lbl.c_str());

  accTemp = 0; accHum = 0; accBat = 0; accCount = 0;
}

// ──────────────────────────────────────────
//  WiFi + LED
// ──────────────────────────────────────────

void manageWifiAndLed() {
  if (WiFi.status() == WL_CONNECTED) {
    // Pripojené → LED zhasnutá (HIGH = off na ESP8266)
    digitalWrite(LED_PIN, HIGH);
    startServerIfNeeded();
  } else {
    // Nepripojené → LED bliká každých 200 ms
    unsigned long now = millis();
    if ((unsigned long)(now - prevBlinkMs) >= blinkIntervalMs) {
      prevBlinkMs = now;
      ledState = !ledState;
      // Invertovaná logika: ledState true → LOW (svietiť), false → HIGH (zhasnúť)
      digitalWrite(LED_PIN, ledState ? LOW : HIGH);
    }

    // Pokus o reconnect každých 5 s
    if ((unsigned long)(now - lastReconnectAttempt) >= reconnectIntervalMs) {
      lastReconnectAttempt = now;
      WiFi.reconnect();
      Serial.println("⚠ WiFi reconnect...");
      serverStarted = false;
    }
  }
}

// ──────────────────────────────────────────
//  Farby
// ──────────────────────────────────────────

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
  if (p < 20)      return "#ef4444";
  else if (p < 50) return "#f97316";
  else             return "#22c55e";
}

// ──────────────────────────────────────────
//  Uptime
// ──────────────────────────────────────────

String uptimeString() {
  unsigned long s = millis() / 1000;
  unsigned long d = s / 86400; s %= 86400;
  unsigned long h = s / 3600;  s %= 3600;
  unsigned long m = s / 60;    s %= 60;
  String r = "";
  if (d > 0) r += String(d) + "d ";
  if (h > 0 || d > 0) r += String(h) + "h ";
  r += String(m) + "m " + String(s) + "s";
  return r;
}

// ──────────────────────────────────────────
//  JSON
// ──────────────────────────────────────────

String hourlyJSON() {
  String j = "[";
  int start = (hCount < MAX_HOURLY) ? 0 : hIndex;
  for (int i = 0; i < hCount; i++) {
    int idx = (start + i) % MAX_HOURLY;
    if (i > 0) j += ",";
    j += "{\"t\":" + String(hTemp[idx], 1)
       + ",\"h\":" + String(hHum[idx], 1)
       + ",\"b\":" + String(hBat[idx], 0)
       + ",\"l\":\"" + hLabel[idx] + "\"}";
  }
  j += "]";
  return j;
}

void handleData() {
  int rssi = WiFi.RSSI();
  String json = "{";
  json += "\"t\":"    + String(lastTemp, 1)    + ",";
  json += "\"h\":"    + String(lastHum, 1)     + ",";
  json += "\"rssi\":" + String(rssi)           + ",";
  json += "\"bv\":"   + String(batteryVolt, 2) + ",";
  json += "\"bp\":"   + String(batteryPct)     + ",";
  json += "\"chg\":"  + String(isCharging ? 1 : 0) + ",";
  json += "\"up\":\"" + uptimeString()         + "\",";
  json += "\"tc\":\"" + colorTemp(lastTemp)    + "\",";
  json += "\"hc\":\"" + colorHum(lastHum)      + "\",";
  json += "\"rc\":\"" + colorRSSI(rssi)        + "\",";
  json += "\"bc\":\"" + colorBat(batteryPct)   + "\",";
  json += "\"hourly\":" + hourlyJSON();
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ──────────────────────────────────────────
//  HTML
// ──────────────────────────────────────────

String generateHTML() {
  int rssi = WiFi.RSSI();
  String html = F("<!DOCTYPE html><html lang='sk'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1.0,maximum-scale=1.0'>"
    "<title>ESP8266 Stanica</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:Arial,sans-serif;background:#0f172a;color:#e2e8f0;min-height:100vh;padding:10px}"
    ".wrap{max-width:600px;margin:0 auto}"
    ".header{text-align:center;padding:10px 0 12px}"
    ".header h1{color:#38bdf8;font-size:clamp(1em,3.5vw,1.3em);margin-bottom:2px}"
    ".header p{color:#475569;font-size:clamp(0.65em,2vw,0.78em)}"
    ".cards{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-bottom:10px}"
    ".card{background:#1e293b;border-radius:8px;padding:8px 6px;"
          "border:1px solid #1e3a5f;display:flex;flex-direction:column;"
          "align-items:center;text-align:center;gap:3px}"
    ".card-icon{font-size:clamp(1em,3vw,1.3em)}"
    ".card-label{color:#64748b;font-size:clamp(0.55em,1.6vw,0.68em);"
                "text-transform:uppercase;letter-spacing:.04em;line-height:1.2}"
    ".card-value{font-size:clamp(0.85em,2.8vw,1.1em);font-weight:bold;line-height:1}"
    ".card-sub{color:#475569;font-size:clamp(0.5em,1.5vw,0.62em);line-height:1.2}"
    ".bat-bar-wrap{width:100%;background:#0f172a;border-radius:3px;height:4px;margin-top:2px;overflow:hidden}"
    ".bat-bar-fill{height:100%;border-radius:3px;transition:width .6s}"
    ".card-full{grid-column:1/-1;flex-direction:row;justify-content:space-between;"
               "align-items:center;padding:7px 12px;text-align:left;gap:8px;flex-wrap:wrap}"
    ".card-full .left{display:flex;align-items:center;gap:8px}"
    ".card-full .right{text-align:right}"
    ".card-full .mid{flex:1;text-align:center;border-left:1px solid #1e3a5f;"
                    "border-right:1px solid #1e3a5f;padding:0 10px}"
    ".ajax-bar{width:100%;background:#1e293b;border-radius:3px;height:3px;margin-bottom:10px;overflow:hidden}"
    ".ajax-fill{width:0%;height:100%;background:#38bdf8;border-radius:3px;transition:width .1s linear}"
    ".chart-wrap{background:#1e293b;border-radius:10px;padding:10px;"
                "border:1px solid #1e3a5f;margin-bottom:10px}"
    ".chart-title{color:#64748b;font-size:0.72em;text-transform:uppercase;"
                 "letter-spacing:.05em;margin-bottom:6px;display:flex;"
                 "justify-content:space-between;align-items:center}"
    ".chart-badge{background:#0f172a;border:1px solid #1e3a5f;border-radius:4px;"
                 "padding:2px 7px;font-size:0.85em;color:#38bdf8}"
    "canvas{width:100%!important;height:auto!important;display:block}"
    ".footer{text-align:center;color:#334155;font-size:0.65em;padding:6px 0}"
    "@media(max-width:320px){"
    ".cards{grid-template-columns:repeat(2,1fr)}"
    ".card-full{grid-column:1/-1}"
    ".card-full .mid{border:none;padding:4px 0;width:100%;text-align:left}"
    "}"
    "</style></head><body><div class='wrap'>"
  );

  html += F("<div class='header'>"
    "<h1>⚡ ESP8266 Meracia Stanica</h1>"
    "<p>Teplota · Vlhkosť · Batéria · WiFi</p>"
    "</div>");

  html += F("<div class='ajax-bar'><div class='ajax-fill' id='prog'></div></div>");
  html += F("<div class='cards'>");

  html += "<div class='card'>"
    "<span class='card-icon'>🌡</span>"
    "<span class='card-label'>Teplota</span>"
    "<span class='card-value' id='val-t' style='color:" + colorTemp(lastTemp) + "'>"
    + String(lastTemp, 1) + " °C</span>"
    "<span class='card-sub'>DHT11</span>"
    "</div>";

  html += "<div class='card'>"
    "<span class='card-icon'>💧</span>"
    "<span class='card-label'>Vlhkosť</span>"
    "<span class='card-value' id='val-h' style='color:" + colorHum(lastHum) + "'>"
    + String(lastHum, 1) + " %</span>"
    "<span class='card-sub'>Relatívna</span>"
    "</div>";

  html += "<div class='card'>"
    "<span class='card-icon'>📶</span>"
    "<span class='card-label'>WiFi</span>"
    "<span class='card-value' id='val-r' style='color:" + colorRSSI(rssi) + "'>"
    + String(rssi) + " dBm</span>"
    "<span class='card-sub' id='sub-r'>"
    + String(rssi > -60 ? "Silný" : rssi > -80 ? "Stredný" : "Slabý")
    + "</span></div>";

  html += "<div class='card'>"
    "<span class='card-icon'>🔋</span>"
    "<span class='card-label'>Batéria</span>"
    "<span class='card-value' id='val-bp' style='color:" + colorBat(batteryPct) + "'>"
    + String(batteryPct) + " %</span>"
    "<span class='card-sub' id='val-bv'>" + String(batteryVolt, 2) + " V</span>"
    "<div class='bat-bar-wrap'>"
    "<div class='bat-bar-fill' id='bat-bar' style='width:" + String(batteryPct)
    + "%;background:" + colorBat(batteryPct) + "'></div>"
    "</div></div>";

  html += "<div class='card card-full'>"
    "<div class='left'>"
    "<span class='card-icon'>☀️</span>"
    "<div>"
    "<span class='card-label' style='display:block'>Solárne nabíjanie</span>"
    "<span class='card-value' id='val-chg' style='font-size:1em;color:"
    + String(isCharging ? "#fbbf24" : "#475569") + "'>"
    + String(isCharging ? "⚡ Nabíja sa" : "— Nenabíja") + "</span>"
    "</div></div>"
    "<div class='mid'>"
    "<span class='card-label' style='display:block'>Uptime</span>"
    "<span class='card-value' id='val-up' style='font-size:0.85em;color:#94a3b8'>"
    + uptimeString() + "</span>"
    "</div>"
    "<div class='right'>"
    "<span class='card-label' style='display:block'>Rozsah: 30% – 85%</span>"
    "<span style='font-size:0.72em;color:#475569' id='val-bvmax'>"
    + String(batteryVolt, 2) + "V / max " + String(V_MAX) + "V</span>"
    "</div></div>";

  html += F("</div>");

  html += F("<div class='chart-wrap'>"
    "<div class='chart-title'>"
    "<span>📈 Posledné merania</span>"
    "<span class='chart-badge'>každých 10s · max 12 bodov</span>"
    "</div>"
    "<canvas id='chart' height='180'></canvas>"
    "</div>");

  html += F("<div class='chart-wrap'>"
    "<div class='chart-title'>"
    "<span>📊 Hodinový priemer</span>"
    "<span class='chart-badge'>priemer za hodinu · max 24h</span>"
    "</div>"
    "<canvas id='chartH' height='180'></canvas>"
    "</div>");

  html += F("<div class='footer'>ESP8266 · DHT11 · TP4056 · MOSFET · 18650 · AJAX refresh každých 10s</div>"
    "</div>");

  html += "<script>";
  html += "const INTERVAL=10000;";

  html += F(R"(
const MAX_PTS = 12;
let tArr=[], hArr=[], bArr=[], xArr=[];
let hourlyData = [];

function drawGraph(canvasId, labels, datasets, noDataMsg) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  canvas.width  = canvas.offsetWidth || 460;
  canvas.height = parseInt(canvas.getAttribute('height')) || 180;
  const ctx = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height;
  const PAD = {top:30, right:14, bottom:34, left:38};
  const gW = W - PAD.left - PAD.right;
  const gH = H - PAD.top  - PAD.bottom;

  ctx.clearRect(0,0,W,H);
  ctx.fillStyle='#1e293b';
  ctx.fillRect(0,0,W,H);

  const n = labels.length;
  if (n < 2) {
    ctx.fillStyle='#475569'; ctx.font='12px Arial'; ctx.textAlign='center';
    ctx.fillText(noDataMsg || 'Čakám na dáta...', W/2, H/2);
    return;
  }

  for (let i=0;i<=5;i++){
    let y=PAD.top+(gH/5)*i;
    ctx.strokeStyle='#1e3a5f'; ctx.lineWidth=1;
    ctx.beginPath(); ctx.moveTo(PAD.left,y); ctx.lineTo(PAD.left+gW,y); ctx.stroke();
    ctx.fillStyle='#334155'; ctx.font='9px Arial'; ctx.textAlign='right';
    ctx.fillText(String(100-i*20), PAD.left-4, y+3);
  }

  ctx.fillStyle='#334155'; ctx.font='8px Arial'; ctx.textAlign='center';
  let step = Math.max(1, Math.floor(n/5));
  for(let i=0;i<n;i+=step){
    let x = PAD.left+(i/(n-1))*gW;
    ctx.fillText(labels[i], x, H-PAD.bottom+11);
  }

  datasets.forEach(ds=>{
    if(!ds.data || ds.data.length < 2) return;
    const dn = ds.data.length;
    ctx.beginPath();
    ds.data.forEach((val,i)=>{
      let x=PAD.left+(i/(n-1))*gW;
      let y=PAD.top+gH-((val-ds.yMin)/(ds.yMax-ds.yMin))*gH;
      i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
    });
    ctx.lineTo(PAD.left+((dn-1)/(n-1))*gW, PAD.top+gH);
    ctx.lineTo(PAD.left, PAD.top+gH);
    ctx.closePath();
    ctx.fillStyle=ds.color+'18'; ctx.fill();

    ctx.beginPath(); ctx.strokeStyle=ds.color; ctx.lineWidth=2; ctx.lineJoin='round';
    ds.data.forEach((val,i)=>{
      let x=PAD.left+(i/(n-1))*gW;
      let y=PAD.top+gH-((val-ds.yMin)/(ds.yMax-ds.yMin))*gH;
      i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
    });
    ctx.stroke();

    ds.data.forEach((val,i)=>{
      let x=PAD.left+(i/(n-1))*gW;
      let y=PAD.top+gH-((val-ds.yMin)/(ds.yMax-ds.yMin))*gH;
      ctx.beginPath(); ctx.arc(x,y,i===dn-1?4:2.5,0,2*Math.PI);
      ctx.fillStyle=ds.color; ctx.fill();
      if(i===dn-1){
        ctx.fillStyle=ds.color; ctx.font='bold 10px Arial'; ctx.textAlign='center';
        ctx.fillText(val.toFixed(1)+' '+ds.label, x, y-8);
      }
    });
  });

  datasets.forEach((l,i)=>{
    let lx=PAD.left+i*(gW/datasets.length)+4;
    ctx.fillStyle=l.color; ctx.fillRect(lx,12,12,3);
    ctx.font='9px Arial'; ctx.textAlign='left';
    ctx.fillStyle='#94a3b8'; ctx.fillText(l.label2||l.label, lx+15, 16);
  });
}

function drawShortChart() {
  drawGraph('chart', xArr, [
    {data:tArr, color:'#ef4444', label:'°C',  label2:'Teplota(°C)',  yMin:-10, yMax:50},
    {data:hArr, color:'#3b82f6', label:'%H',  label2:'Vlhkosť(%)',  yMin:0,   yMax:100},
    {data:bArr, color:'#22c55e', label:'%B',  label2:'Batéria(%)',  yMin:0,   yMax:100}
  ], 'Čakám na viac meraní (min. 2)...');
}

function drawHourlyChart() {
  if (!hourlyData || hourlyData.length < 1) {
    const canvas = document.getElementById('chartH');
    if (!canvas) return;
    canvas.width  = canvas.offsetWidth || 460;
    canvas.height = parseInt(canvas.getAttribute('height')) || 180;
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0,0,canvas.width,canvas.height);
    ctx.fillStyle='#1e293b'; ctx.fillRect(0,0,canvas.width,canvas.height);
    ctx.fillStyle='#475569'; ctx.font='12px Arial'; ctx.textAlign='center';
    ctx.fillText('Hodinový graf — prvý záznam za hodinu od štartu', canvas.width/2, canvas.height/2);
    return;
  }
  let labels = hourlyData.map(d=>d.l);
  let tD = hourlyData.map(d=>d.t);
  let hD = hourlyData.map(d=>d.h);
  let bD = hourlyData.map(d=>d.b);
  drawGraph('chartH', labels, [
    {data:tD, color:'#f59e0b', label:'°C', label2:'Ø Teplota(°C)', yMin:-10, yMax:50},
    {data:hD, color:'#60a5fa', label:'%H', label2:'Ø Vlhkosť(%)',  yMin:0,   yMax:100},
    {data:bD, color:'#34d399', label:'%B', label2:'Ø Batéria(%)',  yMin:0,   yMax:100}
  ], 'Hodinový graf — prvý záznam za hodinu od štartu');
}

function updateDOM(d) {
  let el;
  el=document.getElementById('val-t');  if(el){el.textContent=d.t.toFixed(1)+' °C'; el.style.color=d.tc;}
  el=document.getElementById('val-h');  if(el){el.textContent=d.h.toFixed(1)+' %';  el.style.color=d.hc;}
  el=document.getElementById('val-r');  if(el){el.textContent=d.rssi+' dBm';         el.style.color=d.rc;}
  el=document.getElementById('sub-r');  if(el){el.textContent=d.rssi>-60?'Silný':d.rssi>-80?'Stredný':'Slabý';}
  el=document.getElementById('val-bp'); if(el){el.textContent=d.bp+' %';             el.style.color=d.bc;}
  el=document.getElementById('val-bv'); if(el){el.textContent=d.bv.toFixed(2)+' V';}
  el=document.getElementById('bat-bar');if(el){el.style.width=d.bp+'%'; el.style.background=d.bc;}
  el=document.getElementById('val-chg');if(el){el.textContent=d.chg?'⚡ Nabíja sa':'— Nenabíja'; el.style.color=d.chg?'#fbbf24':'#475569';}
  el=document.getElementById('val-up'); if(el){el.textContent=d.up;}
  el=document.getElementById('val-bvmax');if(el){el.textContent=d.bv.toFixed(2)+'V / max 4.0V';}
}

function fetchData() {
  fetch('/data')
    .then(r=>r.json())
    .then(d=>{
      updateDOM(d);
      let now=new Date().toLocaleTimeString('sk',{hour:'2-digit',minute:'2-digit',second:'2-digit'});
      tArr.push(d.t);  if(tArr.length>MAX_PTS) tArr.shift();
      hArr.push(d.h);  if(hArr.length>MAX_PTS) hArr.shift();
      bArr.push(d.bp); if(bArr.length>MAX_PTS) bArr.shift();
      xArr.push(now);  if(xArr.length>MAX_PTS) xArr.shift();
      drawShortChart();
      hourlyData = d.hourly || [];
      drawHourlyChart();
    })
    .catch(e=>console.warn('Fetch chyba:', e));
}

let progW = 0;
function startProgress() {
  let el = document.getElementById('prog');
  setInterval(()=>{
    progW += 100/(INTERVAL/100);
    if(progW>100) progW=0;
    el.style.width=progW+'%';
  },100);
}

window.onload = () => {
  drawShortChart();
  drawHourlyChart();
  startProgress();
  fetchData();
  setInterval(()=>{ progW=0; fetchData(); }, INTERVAL);
  window.addEventListener('resize',()=>{ drawShortChart(); drawHourlyChart(); });
};
)");

  html += "</script></body></html>";
  return html;
}

// ──────────────────────────────────────────
//  HTTP handlery
// ──────────────────────────────────────────

void handleRoot() {
  if (lastTemp == 0 && lastHum == 0) {
    server.send(200, "text/html; charset=UTF-8",
      "<html><body style='background:#0f172a;color:#38bdf8;"
      "text-align:center;padding:40px;font-family:Arial'>"
      "<h2>⏳ Čakám na DHT11...</h2>"
      "<p style='color:#475569;margin-top:8px'>Prosím počkaj 5 sekúnd a obnov stránku</p>"
      "</body></html>");
    return;
  }
  server.send(200, "text/html; charset=UTF-8", generateHTML());
}

void startServerIfNeeded() {
  if (!serverStarted && WiFi.status() == WL_CONNECTED) {
    server.on("/",     handleRoot);
    server.on("/data", handleData);
    server.begin();
    serverStarted = true;
    Serial.print("✅ Web server spustený! IP: ");
    Serial.println(WiFi.localIP());
  }
}

// ──────────────────────────────────────────
//  DHT čítanie
// ──────────────────────────────────────────

void readDHT() {
  if ((unsigned long)(millis() - lastDHTread) >= 3000) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      lastHum  = h;
      lastTemp = t;
    }
    lastDHTread = millis();
    accumulateHourly();
  }
}

// ──────────────────────────────────────────
//  Setup
// ──────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n\n===== ESP8266 ŠTART =====");

  startMillis      = millis();
  lastHourlyStore  = millis();

  for (int i = 0; i < MAX_HOURLY; i++) {
    hTemp[i] = 0; hHum[i] = 0; hBat[i] = 0; hLabel[i] = "";
  }

  dht.begin();

  // LED — začni blikať (ešte nie sme pripojení)
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);   // HIGH = zhasnutá (zaciatok, potom loop preberie)

  // MOSFET — vypnutý pri štarte
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(true);
  delay(200);
  WiFi.begin(ssid, password);

  Serial.printf("Connecting to: %s\n", ssid);

  // Čakáme na pripojenie — LED bliká manuálne aj tu
  unsigned long t0 = millis();
  bool ls = false;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    // Blikanie LED každých 200 ms aj počas blocking čakania
    if ((unsigned long)(millis() - prevBlinkMs) >= blinkIntervalMs) {
      prevBlinkMs = millis();
      ls = !ls;
      digitalWrite(LED_PIN, ls ? LOW : HIGH);
    }
    delay(50);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);   // Pripojené → LED ZO
    Serial.println("\n✅ Pripojené!");
    Serial.print("IP adresa: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⚠ Nepripojené — skúsim neskôr...");
    // LED ostane blikať cez loop()
  }
}

// ──────────────────────────────────────────
//  Loop
// ──────────────────────────────────────────

void loop() {
  readDHT();
  manageBattery();
  manageWifiAndLed();

  if (serverStarted) server.handleClient();

  // Hodinové ukladanie
  if ((unsigned long)(millis() - lastHourlyStore) >= HOURLY_INTERVAL) {
    lastHourlyStore = millis();
    storeHourlyAverage();
  }

  // Keepalive
  if ((unsigned long)(millis() - lastPing) >= 30000) {
    lastPing = millis();
    if (WiFi.status() == WL_CONNECTED) WiFi.RSSI();
  }

  // Serial monitor
  if ((unsigned long)(millis() - lastStatus) >= 10000) {
    lastStatus = millis();
    Serial.println("─────────────────────────");
    Serial.printf("WiFi:    %s\n", WiFi.status() == WL_CONNECTED ? "PRIPOJENÉ" : "ODPOJENÉ");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("IP:      %s\n",   WiFi.localIP().toString().c_str());
      Serial.printf("RSSI:    %d dBm\n", WiFi.RSSI());
    }
    Serial.printf("Teplota: %.1f C\n",        lastTemp);
    Serial.printf("Vlhkost: %.1f %%\n",        lastHum);
    Serial.printf("Bateria: %.2f V (%d %%)\n", batteryVolt, batteryPct);
    Serial.printf("Nabija:  %s\n",              isCharging ? "ANO" : "NIE");
    Serial.printf("MOSFET:  %s\n",              isCharging ? "HIGH (otvorený)" : "LOW (zatvorený)");
    Serial.printf("Uptime:  %s\n",              uptimeString().c_str());
    Serial.printf("Hodinov. zaznamov: %d\n",    hCount);
    Serial.printf("Acc. vzorky:       %d\n",    accCount);
    Serial.println("─────────────────────────");
  }
}
