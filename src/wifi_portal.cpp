#include "wifi_portal.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "constants.h"

extern AsyncWebServer server;
extern void ledSet(uint8_t r, uint8_t g, uint8_t b);

volatile bool     req_restart = false;
volatile uint32_t restartAt   = 0;

static char wifiSSID[64] = "";
static char wifiPass[64] = "";

static void wifiLoadCredentials() {
    Preferences prefs;
    prefs.begin("wifi", true);
    prefs.getString("ssid", wifiSSID, sizeof(wifiSSID));
    prefs.getString("pass", wifiPass, sizeof(wifiPass));
    prefs.end();
}

static void wifiSaveCredentials(const char *ssid, const char *pass) {
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
}

void wifiClearCredentials() {
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
    wifiSSID[0] = 0;
    wifiPass[0] = 0;
}

static const char portal_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>433MHz WiFi Setup</title>
<style>*{box-sizing:border-box}body{font-family:sans-serif;background:#111;color:#ddd;max-width:440px;margin:40px auto;padding:0 16px}
h2{color:#0af;margin-bottom:20px}label{display:block;font-size:.9em;color:#888;margin-bottom:3px}
input[type=text],input[type=password],select{display:block;width:100%;padding:8px;margin-bottom:14px;background:#1a1a1a;color:#eee;border:1px solid #444;border-radius:4px;font-size:15px}
.showpw{display:flex;align-items:center;gap:8px;font-size:.85em;color:#666;margin:-10px 0 14px;cursor:pointer}
.showpw input{width:auto;margin:0;cursor:pointer}
.btn{display:block;width:100%;padding:10px;margin:5px 0;border:none;border-radius:4px;font-size:15px;cursor:pointer;color:#fff}
.btn:disabled{opacity:.45;cursor:default}
.scan{background:#444}.test{background:#2a5a2a}.save{background:#0a84ff}
#msg{margin-top:12px;min-height:18px}</style>
</head><body>
<h2>433MHz WiFi Setup</h2>
<button class="btn scan" onclick="doScan()">&#8635; Scan Networks</button>
<select id="sel" onchange="document.getElementById('ssid').value=this.value">
<option value="">&#8212; select network &#8212;</option></select>
<label>SSID</label><input id="ssid" type="text" placeholder="Network name">
<label>Password</label><input id="pass" type="password" placeholder="Password">
<label class="showpw"><input type="checkbox" onchange="document.getElementById('pass').type=this.checked?'text':'password'"> Show password</label>
<button class="btn test" id="testBtn" onclick="doTest()">&#10003; Test Connection</button>
<button class="btn save" onclick="doSave()">Save &amp; Connect</button>
<p id="msg"></p>
<script>
function msg(t,c){var e=document.getElementById('msg');e.textContent=t;e.style.color=c||'#fa0';}
function doScan(){msg('Scanning…','#fa0');
fetch('/scan').then(r=>r.json()).then(n=>{
var s=document.getElementById('sel');
s.innerHTML='<option value="">— select —</option>';
n.forEach(v=>{var o=document.createElement('option');o.value=v;o.textContent=v;s.appendChild(o);});
msg('Found '+n.length+' network'+(n.length!==1?'s':''),'#aaa');}).catch(()=>msg('Scan failed','#f44'));}
function doTest(){
var ssid=document.getElementById('ssid').value.trim();
if(!ssid){msg('Enter SSID','#f44');return;}
var tb=document.getElementById('testBtn');tb.disabled=true;
msg('Testing connection… (up to 10 s)','#fa0');
var fd=new FormData();fd.append('ssid',ssid);fd.append('pass',document.getElementById('pass').value);
fetch('/wifitest',{method:'POST',body:fd}).then(()=>{
var iv=setInterval(()=>{
fetch('/wifiteststatus').then(r=>r.json()).then(d=>{
if(d.state===2){clearInterval(iv);tb.disabled=false;msg('✓ Connected! You can now save.','#0f0');}
else if(d.state===3){clearInterval(iv);tb.disabled=false;msg('✗ Connection failed — check SSID / password','#f44');}
});},700);}).catch(()=>{tb.disabled=false;msg('Test error','#f44');});}
function doSave(){var ssid=document.getElementById('ssid').value.trim();
if(!ssid){msg('Enter SSID','#f44');return;}msg('Saving…','#fa0');
var fd=new FormData();fd.append('ssid',ssid);fd.append('pass',document.getElementById('pass').value);
fetch('/wificfg',{method:'POST',body:fd}).then(r=>r.text()).then(t=>msg(t,'#0af')).catch(()=>msg('Error','#f44'));}
</script></body></html>
)rawliteral";

static void startConfigPortal() {
    Serial.println("Starting WiFi config portal (AP: 433MHz-Setup)");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("433MHz-Setup");
    delay(500);
    Serial.printf("Portal IP: %s\n", WiFi.softAPIP().toString().c_str());
    ledSet(60, 30, 0);   // amber = config portal

    // Scan once on entry; cache results for /scan endpoint
    static char scanJson[512] = "[]";
    int n = WiFi.scanNetworks(false, false);
    String tmp = "[";
    for (int i = 0; i < n; i++) {
        if (i) tmp += ",";
        String s = WiFi.SSID(i);
        s.replace("\"", "\\\"");
        tmp += "\""; tmp += s; tmp += "\"";
    }
    tmp += "]";
    strncpy(scanJson, tmp.c_str(), sizeof(scanJson) - 1);
    WiFi.scanDelete();

    // Test-connection state: 0=idle 1=pending 2=ok 3=fail
    static char         testSSID[64] = "";
    static char         testPass[64] = "";
    static volatile int testState    = 0;

    DNSServer dns;
    dns.start(53, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(200, "text/html", portal_html);
    });
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(200, "application/json", scanJson);
    });
    server.on("/wifitest", HTTP_POST, [](AsyncWebServerRequest *r) {
        if (!r->hasParam("ssid", true)) { r->send(400, "text/plain", "missing ssid"); return; }
        strncpy(testSSID, r->getParam("ssid",true)->value().c_str(), sizeof(testSSID)-1);
        testSSID[sizeof(testSSID)-1] = 0;
        strncpy(testPass, r->hasParam("pass",true) ? r->getParam("pass",true)->value().c_str() : "", sizeof(testPass)-1);
        testPass[sizeof(testPass)-1] = 0;
        testState = 1;
        r->send(202, "text/plain", "testing");
    });
    server.on("/wifiteststatus", HTTP_GET, [](AsyncWebServerRequest *r) {
        char buf[24];
        snprintf(buf, sizeof(buf), "{\"state\":%d}", (int)testState);
        r->send(200, "application/json", buf);
    });
    server.on("/wificfg", HTTP_POST, [](AsyncWebServerRequest *r) {
        if (!r->hasParam("ssid", true)) { r->send(400, "text/plain", "missing ssid"); return; }
        String ssid = r->getParam("ssid", true)->value();
        String pass = r->hasParam("pass", true) ? r->getParam("pass", true)->value() : "";
        wifiSaveCredentials(ssid.c_str(), pass.c_str());
        r->send(200, "text/plain", "Saved! Rebooting in 2 seconds\xe2\x80\xa6");
        req_restart = true;
        restartAt = millis() + 2000;
    });
    server.onNotFound([](AsyncWebServerRequest *r) {
        r->redirect("http://192.168.4.1/");
    });
    server.begin();

    while (true) {
        dns.processNextRequest();

        if (testState == 1) {
            testState = 0;   // clear pending flag before blocking
            Serial.printf("Testing WiFi SSID: %s\n", testSSID);
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(testSSID, testPass);
            uint32_t t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis()-t0 < 10000) {
                dns.processNextRequest();
                delay(100);
            }
            bool ok = (WiFi.status() == WL_CONNECTED);
            Serial.printf("WiFi test: %s\n", ok ? "OK" : "FAIL");
            testState = ok ? 2 : 3;
            WiFi.disconnect(false);
            delay(200);
            WiFi.mode(WIFI_AP);
            WiFi.softAP("433MHz-Setup");
            delay(200);
        }

        if (req_restart && millis() >= restartAt) {
            Serial.println("Rebooting after WiFi config save");
            delay(100);
            ESP.restart();
        }
        delay(5);
    }
}

void wifiBegin() {
    wifiLoadCredentials();
    if (wifiSSID[0]) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifiSSID, wifiPass);
        Serial.print("WiFi");
        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && millis()-t < 15000) { delay(400); Serial.print("."); }
        Serial.println();
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("No credentials or connect failed — starting config portal");
        startConfigPortal(); // never returns
    }
}
