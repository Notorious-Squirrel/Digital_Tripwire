#include "WebUI.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <FS.h>
#include <WiFi.h>

// ======================================================
// GLOBALS
// ======================================================

WebServer server(80);
DNSServer dnsServer;
bool scanningPaused = false;

// ======================================================
// EXTERNALS FROM MAIN SKETCH
// ======================================================

extern int deviceCount;
extern int knownDeviceCount;
extern int totalLogs;
extern int activeTargetIndex;
extern bool sdReady;
extern bool newDeviceDetected;

enum ScanMode { BLE_MODE, WIFI_MODE };
extern ScanMode currentMode;

struct DeviceInfo {
    char name[24];
    char mac[18];
    int8_t rssi;
    bool isNew;
    unsigned long firstSeen;
    unsigned long lastSeen;
};
extern DeviceInfo devices[];

// ======================================================
// EMBEDDED HTML
// ======================================================

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Tripwire Radar</title>
  <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>&#x1f6e1;</text></svg>">
<style>
  *,*::before,*::after{box-sizing:border-box}
  :root{
    --bg:#0a0e13;--card:#111820;--text:#e2eaf4;--muted:#8899ab;
    --border:#1e2a3a;--input:#0c1219;--inputBorder:#243044;
    --accent:#22d3ee;--accentDim:rgba(34,211,238,.12);
    --btn:#182030;--btnHover:#1f2d42;--btnText:#e2eaf4;
    --primary:#22d3ee;--primaryText:#0a0e13;
    --danger:#fb7185;--dangerDim:rgba(251,113,133,.12);
    --warn:#fbbf24;--warnDim:rgba(251,191,36,.12);
    --ok:#22d3ee;--bad:#fb7185;
    --barBg:#182030;--radius:10px;--shadow:0 4px 24px rgba(0,0,0,.35);
  }
  html{color-scheme:dark}
  body{
    font-family:'Inter',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;
    margin:0;padding:16px;max-width:900px;margin-left:auto;margin-right:auto;
    background:var(--bg);color:var(--text);line-height:1.5;
    -webkit-font-smoothing:antialiased;
  }
  .header{display:flex;align-items:center;gap:12px;margin-bottom:16px;padding-bottom:12px;border-bottom:1px solid var(--border)}
  .header .logo{font-size:28px;line-height:1}
  .header h1{font-size:20px;font-weight:700;margin:0}
  .header .sub{font-size:12px;color:var(--muted);margin:0}
  .card{border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin:12px 0;background:var(--card);box-shadow:var(--shadow)}
  .card h3{margin:0 0 12px 0;font-size:13px;font-weight:600;text-transform:uppercase;letter-spacing:.5px;color:var(--muted)}
  .statusGrid{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:12px}
  .pill{display:inline-flex;align-items:center;gap:5px;border:1px solid var(--border);border-radius:999px;padding:4px 10px;font-size:12px;font-weight:500;background:var(--input);color:var(--text)}
  .pill .dot{width:7px;height:7px;border-radius:50%;background:var(--muted);flex-shrink:0}
  .pill.ok .dot{background:var(--ok)}.pill.bad .dot{background:var(--bad)}.pill.warn .dot{background:var(--warn)}
  .pill.ok{border-color:rgba(34,211,238,.3);background:var(--accentDim)}
  .pill.bad{border-color:rgba(251,113,133,.3);background:var(--dangerDim)}
  .pill.warn{border-color:rgba(251,191,36,.3);background:var(--warnDim)}
  .kv{display:grid;grid-template-columns:1fr 1fr;gap:6px}
  @media(max-width:520px){.kv{grid-template-columns:1fr}}
  .kv>div{display:flex;justify-content:space-between;align-items:center;gap:10px;border:1px solid var(--border);border-radius:6px;padding:8px 10px;background:var(--input)}
  .k{color:var(--muted);font-size:12px}.v{font-weight:600;font-size:13px;color:var(--text);text-align:right}
  .row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
  button{cursor:pointer;padding:8px 14px;border-radius:6px;border:1px solid var(--inputBorder);background:var(--btn);color:var(--btnText);font-size:13px;font-weight:500;transition:all .12s ease;outline:none;width:100%}
  button:hover{background:var(--btnHover);border-color:var(--accent)}
  button:active{transform:translateY(1px)}
  .btn-primary{background:var(--primary);color:var(--primaryText);border-color:var(--primary);font-weight:600}
  .btn-primary:hover{background:color-mix(in srgb,var(--primary) 85%,#fff)}
  .btn-sm{padding:5px 10px;font-size:12px;width:auto}
  .btn-danger{color:var(--danger);border-color:rgba(251,113,133,.3);background:var(--dangerDim)}
  .btn-danger:hover{background:rgba(251,113,133,.2)}
  input{padding:7px 10px;border-radius:6px;border:1px solid var(--inputBorder);width:100%;background:var(--input);color:var(--text);font-size:13px;outline:none}
  input:focus{border-color:var(--accent);box-shadow:0 0 0 2px var(--accentDim)}
  table{width:100%;border-collapse:collapse;font-size:12px}
  th{text-align:left;color:var(--muted);font-weight:500;padding:6px 4px;border-bottom:1px solid var(--border);text-transform:uppercase;font-size:10px;letter-spacing:.4px}
  td{padding:5px 4px;border-bottom:1px solid rgba(30,42,58,.5)}
  .rssi-bar{display:inline-block;height:6px;border-radius:3px;min-width:4px;background:var(--accent);vertical-align:middle;margin-right:6px}
  .rssi-strong{color:var(--accent)}.rssi-mid{color:var(--warn)}.rssi-weak{color:var(--danger)}
  .badge{font-size:10px;font-weight:600;text-transform:uppercase;padding:1px 6px;border-radius:999px;margin-left:4px}
  .badge-new{color:var(--accent);background:var(--accentDim);border:1px solid rgba(34,211,238,.2)}
  .badge-target{color:var(--warn);background:var(--warnDim);border:1px solid rgba(251,191,36,.2)}
  .mt-sm{margin-top:8px}.mt-md{margin-top:12px}
  .ok-text{color:var(--ok)}.bad-text{color:var(--bad)}.muted{color:var(--muted);font-size:12px}
  .file-row{display:flex;align-items:center;gap:8px;padding:6px 0;border-bottom:1px solid var(--border);font-size:12px;flex-wrap:wrap}
  .file-row:last-child{border-bottom:none}
  .file-name{font-weight:500;word-break:break-all}
  .file-size{color:var(--muted);font-size:11px;white-space:nowrap}
  .file-actions{margin-left:auto;display:flex;gap:4px}
  .empty{color:var(--muted);font-style:italic;font-size:13px;text-align:center;padding:20px}
  .scroll{overflow-x:auto}
  a{color:var(--accent);text-decoration:none}
  a:hover{text-decoration:underline}
</style>
</head>
<body>

<div class="header">
  <div class="logo">&#x1f6e1;</div>
  <div>
    <h1>Tripwire Radar</h1>
    <p class="sub">BLE &amp; WiFi Scanner &mdash; <span id="vIp">connecting...</span></p>
  </div>
</div>

<!-- ============ STATUS ============ -->
<div class="card">
  <h3>Status</h3>
  <div class="statusGrid" id="statusPills"></div>
  <div class="kv">
    <div><span class="k">Mode</span><span class="v" id="vMode">—</span></div>
    <div><span class="k">Scan</span><span class="v" id="vScan">—</span></div>
    <div><span class="k">Live Devices</span><span class="v" id="vLive">—</span></div>
    <div><span class="k">Known Devices</span><span class="v" id="vKnown">—</span></div>
    <div><span class="k">Total Logs</span><span class="v" id="vLogs">—</span></div>
    <div><span class="k">SD Card</span><span class="v" id="vSd">—</span></div>
  </div>
  <div id="targetInfo" class="mt-sm" style="display:none;border:1px solid var(--border);border-radius:6px;padding:8px 10px;background:var(--accentDim)">
    <div style="font-size:10px;text-transform:uppercase;color:var(--muted);letter-spacing:.3px">Target Lock</div>
    <div style="font-size:14px;font-weight:600" id="vTargetName">—</div>
    <div style="font-size:11px;color:var(--muted)" id="vTargetMac">—</div>
    <div style="font-size:13px;font-weight:600" id="vTargetRssi">—</div>
  </div>
  <div class="row mt-md">
    <button id="btnMode" onclick="toggleMode()">&#x21bb; Toggle Mode</button>
    <button id="btnScan" onclick="toggleScan()">&#9646;&#9646; Pause</button>
  </div>
</div>

<!-- ============ DEVICES ============ -->
<div class="card">
  <h3>Detected Devices <span id="vDeviceCount" style="color:var(--accent)">0</span></h3>
  <div class="scroll" id="deviceList"><div class="empty">No devices detected yet</div></div>
</div>

<!-- ============ FILES ============ -->
<div class="card">
  <h3>SD Card Files</h3>
  <div style="display:flex;flex-wrap:wrap;gap:6px;margin-bottom:8px">
    <button class="btn-sm" onclick="loadFiles()">&#x21bb; Refresh</button>
    <button class="btn-sm" onclick="location.href='/api/downloadAll'">&#x2b07; Download All</button>
    <button class="btn-sm btn-danger" onclick="deleteAll()">&#x1f5d1; Delete All</button>
  </div>
  <div id="fileList"><div class="empty">Loading...</div></div>
</div>

<script>
let scanning = true;

function $(id){return document.getElementById(id)}
function setPill(id,text,cls){const e=$(id);if(!e)return;e.classList.remove('ok','bad','warn');if(cls)e.classList.add(cls);const d=e.querySelector('.dot');e.textContent='';if(d)e.appendChild(d);e.appendChild(document.createTextNode(' '+text))}

async function loadStatus(){
  try{
    const r=await fetch('/status.json');const j=await r.json();
    setPill('pillMode','Mode: '+(j.mode||'?'),j.mode==='BLE'?'ok':'warn');
    setPill('pillScan','Scan: '+(j.scanning?'ACTIVE':'PAUSED'),j.scanning?'ok':'warn');
    setPill('pillSd','SD: '+(j.sd?'OK':'FAIL'),j.sd?'ok':'bad');
    setPill('pillDevices','Devices: '+(j.live||0),j.live>0?'ok':'warn');
    setText('vMode',j.mode||'—');
    setText('vScan',j.scanning?'Active':'Paused');
    setText('vLive',j.live);
    setText('vKnown',j.known);
    setText('vLogs',j.logs);
    setText('vSd',j.sd?'OK':'NO SD');
    const ti=$('targetInfo');
    if(j.targetName){
      ti.style.display='block';
      setText('vTargetName',j.targetName);
      setText('vTargetMac',j.targetMac);
      setText('vTargetRssi',j.targetRssi+' dBm');
    }else{ti.style.display='none'}
    $('btnScan').textContent=j.scanning?'\u25a0 Pause':'\u25b6 Resume';
    scanning=j.scanning;
    $('vDeviceCount').textContent=j.live||0;
    setText('vIp',j.ip||'—');
  }catch(e){console.error('status',e)}
}

function setText(id,v){const e=$(id);if(e)e.textContent=(v===undefined||v===null)?'—':String(v)}

async function loadDevices(){
  try{
    const r=await fetch('/devices.json');const j=await r.json();
    const el=$('deviceList');
    if(!j.devices||j.devices.length===0){el.innerHTML='<div class="empty">No devices detected</div>';return}
    let h='<table><tr><th>Name</th><th>MAC</th><th>RSSI</th><th>Seen</th></tr>';
    for(const d of j.devices){
      const cls=d.rssi>-55?'rssi-strong':d.rssi>-65?'rssi-mid':'rssi-weak';
      const w=mapRssi(d.rssi);
      const badge=(d.isTarget?'<span class="badge badge-target">TARGET</span>':'')+(d.isNew?'<span class="badge badge-new">NEW</span>':'');
      const ago=Math.floor((Date.now()-d.lastSeen)/1000);
      const agoStr=ago<60?ago+'s':Math.floor(ago/60)+'m';
      h+='<tr><td>'+escapeHtml(d.name||'UNKNOWN')+' '+badge+'</td><td style="font-family:monospace;font-size:11px">'+d.mac+'</td><td class="'+cls+'"><span class="rssi-bar" style="width:'+w+'px;background:'+(d.rssi>-55?'var(--accent)':d.rssi>-65?'var(--warn)':'var(--danger)')+'"></span>'+d.rssi+'</td><td style="color:var(--muted);font-size:11px">'+agoStr+' ago</td></tr>';
    }
    h+='</table>';
    el.innerHTML=h;
  }catch(e){console.error('devices',e)}
}

function mapRssi(r){return Math.min(80,Math.max(4,(r+100)/70*80))}
function escapeHtml(s){const d=document.createElement('div');d.appendChild(document.createTextNode(s));return d.innerHTML}

async function toggleMode(){
  try{await fetch('/api/mode',{method:'POST'});await loadStatus();}catch(e){}
}
async function toggleScan(){
  const act=scanning?'pause':'resume';
  try{await fetch('/api/scan/'+act,{method:'POST'});await loadStatus();}catch(e){}
}

async function loadFiles(){
  const el=$('fileList');
  try{
    const r=await fetch('/api/files');const j=await r.json();
    if(!j.ok||!j.files||j.files.length===0){el.innerHTML='<div class="empty">No files on SD card</div>';return}
    el.innerHTML=j.files.map(f=>{
      const sz=f.size<1024?f.size+' B':(f.size/1024).toFixed(1)+' KB';
      return '<div class="file-row"><span class="file-name"><a href="/api/download?name='+encodeURIComponent(f.name)+'">'+f.name+'</a></span><span class="file-size">'+sz+'</span><span class="file-actions"><button class="btn-sm btn-danger" onclick="delFile(\''+f.name.replace(/'/g,"\\'")+'\')">Delete</button></span></div>';
    }).join('');
  }catch(e){el.innerHTML='<div class="empty">Error loading files</div>'}
}

async function delFile(n){if(!confirm('Delete '+n+'?'))return;try{await fetch('/api/delete?name='+encodeURIComponent(n),{method:'POST'});await loadFiles()}catch(e){}}

async function deleteAll(){
  if(!confirm('Delete ALL log files?'))return;
  try{await fetch('/api/deleteAll',{method:'POST'});await loadFiles()}catch(e){}
}

// Polling
setInterval(()=>{loadStatus();loadDevices()},2000);
loadStatus();loadDevices();loadFiles();
</script>
</body>
</html>
)HTML";

// ======================================================
// HELPERS
// ======================================================

static String formatBytes(size_t b) {
    if (b < 1024) return String(b) + " B";
    if (b < 1048576) return String(b / 1024.0f, 1) + " KB";
    return String(b / 1048576.0f, 1) + " MB";
}

static bool isAllowedPath(const String& path) {
    return path.startsWith("/logs/") || path.startsWith("/uploaded/") ||
           path.startsWith("/logs") || path.startsWith("/known_devices.txt");
}

// ======================================================
// ROUTE HANDLERS
// ======================================================

static void handleRoot() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handleStatus() {
    DynamicJsonDocument doc(1024);

    doc["mode"] = (currentMode == BLE_MODE) ? "BLE" : "WiFi";
    doc["scanning"] = !scanningPaused;
    doc["sd"] = sdReady;
    doc["live"] = deviceCount;
    doc["known"] = knownDeviceCount;
    doc["logs"] = totalLogs;
    doc["newDevice"] = newDeviceDetected;
    doc["ip"] = WiFi.localIP().toString();
    doc["apIp"] = WiFi.softAPIP().toString();
    doc["heap"] = ESP.getFreeHeap();

    if (activeTargetIndex >= 0 && activeTargetIndex < deviceCount) {
        DeviceInfo& t = devices[activeTargetIndex];
        doc["targetName"] = String(t.name);
        doc["targetMac"] = String(t.mac);
        doc["targetRssi"] = t.rssi;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleDevices() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("devices");

    unsigned long now = millis();
    for (int i = 0; i < deviceCount; i++) {
        DeviceInfo& d = devices[i];
        if (now - d.lastSeen > 12000) continue;

        JsonObject o = arr.createNestedObject();
        o["name"] = String(d.name);
        o["mac"] = String(d.mac);
        o["rssi"] = d.rssi;
        o["isNew"] = d.isNew;
        o["isTarget"] = (i == activeTargetIndex);
        o["firstSeen"] = d.firstSeen;
        o["lastSeen"] = d.lastSeen;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleMode() {
    currentMode = (currentMode == BLE_MODE) ? WIFI_MODE : BLE_MODE;
    server.send(200, "text/plain", "OK");
}

static void handleScanPause() {
    scanningPaused = true;
    server.send(200, "text/plain", "OK");
}

static void handleScanResume() {
    scanningPaused = false;
    server.send(200, "text/plain", "OK");
}

static void handleFiles() {
    DynamicJsonDocument doc(3072);
    doc["ok"] = sdReady;

    if (sdReady) {
        JsonArray arr = doc.createNestedArray("files");
        const char* dirs[] = { "/logs", "/uploaded" };

        for (const char* dir : dirs) {
            File root = SD.open(dir);
            if (!root) continue;
            File f = root.openNextFile();
            while (f) {
                if (!f.isDirectory()) {
                    JsonObject o = arr.createNestedObject();
                    String full = String(dir) + "/" + String(f.name());
                    // Remove double slash
                    full.replace("//", "/");
                    o["name"] = full;
                    o["size"] = (uint32_t)f.size();
                }
                f.close();
                f = root.openNextFile();
            }
            root.close();
        }
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleDownload() {
    if (!sdReady) {
        server.send(500, "text/plain", "SD not available");
        return;
    }
    if (!server.hasArg("name")) {
        server.send(400, "text/plain", "Missing name");
        return;
    }
    String name = server.arg("name");
    if (!isAllowedPath(name)) {
        server.send(403, "text/plain", "Forbidden");
        return;
    }
    if (!SD.exists(name)) {
        server.send(404, "text/plain", "Not found");
        return;
    }
    File f = SD.open(name, FILE_READ);
    server.streamFile(f, "text/plain");
    f.close();
}

static void handleDownloadAll() {
    if (!sdReady) { server.send(500, "text/plain", "SD not available"); return; }

    String all;
    const char* dirs[] = { "/logs", "/uploaded" };
    for (const char* dir : dirs) {
        File root = SD.open(dir);
        if (!root) continue;
        File f = root.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                all += "=== " + String(dir) + "/" + String(f.name()) + " ===\r\n";
                while (f.available()) all += (char)f.read();
                all += "\r\n";
            }
            f.close();
            f = root.openNextFile();
        }
        root.close();
    }

    server.sendHeader("Content-Disposition", "attachment; filename=\"tripwire_logs.txt\"");
    server.send(200, "text/plain", all);
}

static void handleDelete() {
    if (!sdReady) { server.send(500, "text/plain", "SD not available"); return; }
    if (!server.hasArg("name")) { server.send(400, "text/plain", "Missing name"); return; }
    String name = server.arg("name");
    if (!isAllowedPath(name)) { server.send(403, "text/plain", "Forbidden"); return; }
    bool ok = SD.remove(name);
    server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
}

static void handleDeleteAll() {
    if (!sdReady) { server.send(500, "application/json", "{\"ok\":false}"); return; }

    uint32_t deleted = 0;
    const char* dirs[] = { "/logs", "/uploaded" };
    for (const char* dir : dirs) {
        File root = SD.open(dir);
        if (!root) continue;
        File f = root.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                String name = String(dir) + "/" + String(f.name());
                f.close();
                if (SD.remove(name)) deleted++;
            } else {
                f.close();
            }
            f = root.openNextFile();
        }
        root.close();
    }

    DynamicJsonDocument doc(128);
    doc["ok"] = true;
    doc["deleted"] = deleted;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ======================================================
// INIT
// ======================================================

void startWebUI() {
    Serial.println("[WEB] Starting WiFi AP...");

    String chipId = String((uint32_t)(ESP.getEfuseMac() >> 24), HEX);
    String ssid = "Tripwire-" + chipId;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid.c_str(), NULL, 1, 0, 1);

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    Serial.printf("[WEB] AP SSID: %s\n", ssid.c_str());
    Serial.printf("[WEB] AP IP:   %s\n", apIP.toString().c_str());

    // DNS captive portal
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);

    Serial.println("[WEB] Registering routes...");

    server.on("/", handleRoot);
    server.on("/index.html", handleRoot);
    server.on("/status.json", handleStatus);
    server.on("/devices.json", handleDevices);
    server.on("/api/mode", HTTP_POST, handleMode);
    server.on("/api/scan/pause", HTTP_POST, handleScanPause);
    server.on("/api/scan/resume", HTTP_POST, handleScanResume);
    server.on("/api/files", handleFiles);
    server.on("/api/download", handleDownload);
    server.on("/api/downloadAll", handleDownloadAll);
    server.on("/api/delete", HTTP_POST, handleDelete);
    server.on("/api/deleteAll", HTTP_POST, handleDeleteAll);

    // Redirect any unknown route to the captive portal
    server.onNotFound([]() {
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
        server.send(302, "text/plain", "");
    });

    server.begin();
    Serial.println("[WEB] Server started");
}

void stopWebUI() {
    Serial.println("[WEB] Stopping AP...");
    server.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WEB] AP stopped");
}

void handleWebUI() {
    dnsServer.processNextRequest();
    server.handleClient();
}
