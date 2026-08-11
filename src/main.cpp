/*
  Icy OS Web OS
  ESP32 WROOM-1 + MicroSD (SPI) + ESPAsyncWebServer + WebSocket
*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_wifi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "mbedtls/md5.h"
#include <Update.h>
#include <time.h>
#include <vector>
#include <set>
#include <string>

#include "WiFiAttack.h"

// --- Default pins / settings ---
#define SD_CS    5
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define DEFAULT_BUZZER 25

// --- Globals ---
AsyncWebServer server(80);
AsyncWebSocket   ws("/ws");

String  apSSID      = "Icy-OS";
String  apPassword  = "Password123";
String  adminPass   = "admin";
int     buzzerPin   = DEFAULT_BUZZER;
int     apChannel   = 1;
String  staSSID     = "";
String  staPassword = "";
String  ntpServer   = "pool.ntp.org";
int     ntpOffset   = 0;
bool    sdReady     = false;
String  currentDir  = "/";

bool    sniffing      = false;
File    pcapFile;
uint32_t sniffStart   = 0;
uint32_t sniffDuration = 0;

unsigned long staConnectStart = 0;
IPAddress localIP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// Forward declarations
String resolvePath(const String& in);
String shellCommand(const String& cmd);
void restoreAP();
void connectSTA();
void startSniff(uint8_t ch, uint32_t seconds);
void stopSniff();

uint64_t sdTotalBytes() {
  if (!sdReady) return 0;
  return SD.totalBytes();
}

uint64_t sdUsedBytes() {
  if (!sdReady) return 0;
  return SD.usedBytes();
}
bool    portalRunning = false;

const char DEFAULT_PORTAL[] =
  "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Free Wi-Fi</title><style>"
  "body{font-family:sans-serif;background:#0f1115;color:#e0e6ed;text-align:center;padding:40px}"
  "input,button{padding:10px 15px;margin:8px 0;border-radius:6px;border:1px solid #36d13c;background:#171a21;color:#e0e6ed}"
  "button{background:#36d13c;color:#000;border:none;cursor:pointer}"
  "</style></head><body>"
  "<h1>Free Guest Wi-Fi</h1>"
  "<p>Enter your details to connect</p>"
  "<form action='/post' method='POST' enctype='application/x-www-form-urlencoded'>"
  "<input name='name' placeholder='Name'><br>"
  "<input name='email' type='email' placeholder='Email'><br>"
  "<button>Connect</button></form>"
  "<p style='font-size:12px;color:#9aa3ad'>This is a benign portal for authorized testing only.</p>"
  "</body></html>";

const char PORTAL_OK[] =
  "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Connected</title><style>"
  "body{font-family:sans-serif;background:#0f1115;color:#e0e6ed;text-align:center;padding:40px}"
  "</style></head><body>"
  "<h1>You are connected</h1>"
  "<p>Enjoy the free Wi-Fi.</p>"
  "</body></html>";

unsigned long bootMs        = 0;
unsigned long lastSysInfo   = 0;
unsigned long initialHeap   = 0;
unsigned long lastScanTick  = 0;
bool          scanPending   = false;

std::set<uint32_t> authenticated;
std::set<uint32_t> scannerSubs;   // clients that want continuous scans

int lastBestRssi = -100;

WiFiAttack wifiAttack;

File fsUploadFile;
bool fsUploadSuccess = false;
int  fsUploadCode    = 200;
String fsUploadResponse;

// --- Recon / wardrive state ---
struct NetworkInfo {
  String ssid;
  String bssid;
  int rssi;
  uint8_t channel;
  String auth;
};
std::vector<NetworkInfo> apCache;
std::set<int> selectedAPs;
bool wardriveMode = false;
File wardriveFile;
bool scanAll = false;

// --- String sanitizer for safe JSON / WebSocket UTF-8 text frames ---
String sanitize(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    unsigned char c = (unsigned char)in[i];
    if (c == 0) continue;
    if (c < 0x20) out += ' ';
    else if (c >= 0x80) out += '?';
    else out += (char)c;
  }
  return out;
}

String urlEncode(const String& in) {
  String out;
  out.reserve(in.length() * 3);
  for (size_t i = 0; i < in.length(); i++) {
    unsigned char c = (unsigned char)in[i];
    if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.') out += (char)c;
    else {
      char hex[8];
      snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  return out;
}

// --- Buzzer ---
void beep(int ms = 100) {
  if (buzzerPin < 0) return;
  ledcWriteTone(0, 2000);
  delay(ms);
  ledcWriteTone(0, 0);
}

// --- Settings persistence ---
void loadSettings() {
  if (!sdReady) return;
  File f = SD.open("/settings.json", FILE_READ);
  if (f && f.size()) {
    DynamicJsonDocument doc(1024);
    DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (!e) {
      apSSID      = "Icy-OS";                    // keep the brand consistent
      apPassword  = "Password123";               // always use the known AP password
      apChannel   = 1;                           // always use channel 1 for best stability
      adminPass   = "admin";                     // keep the web auth token predictable
      buzzerPin   = doc["buzzerGPIO"] | buzzerPin;
      staSSID     = sanitize(doc["staSSID"]    | staSSID);
      staPassword = sanitize(doc["staPassword"] | staPassword);
      ntpServer   = sanitize(doc["ntpServer"]  | ntpServer);
      ntpOffset   = doc["ntpOffset"]   | ntpOffset;
    }
  } else {
    // Create default settings on first boot
    File out = SD.open("/settings.json", FILE_WRITE);
    if (out) {
      DynamicJsonDocument doc(1024);
      doc["ssid"]        = apSSID;
      doc["password"]    = apPassword;
      doc["adminPass"]   = adminPass;
      doc["buzzerGPIO"]  = buzzerPin;
      doc["channel"]     = apChannel;
      doc["staSSID"]     = staSSID;
      doc["staPassword"] = staPassword;
      doc["ntpServer"]   = ntpServer;
      doc["ntpOffset"]   = ntpOffset;
      serializeJson(doc, out);
      out.close();
    }
  }
}

void saveSettings() {
  if (!sdReady) return;
  File out = SD.open("/settings.json", FILE_WRITE);
  if (out) {
    DynamicJsonDocument doc(1024);
    doc["ssid"]        = apSSID;
    doc["password"]    = apPassword;
    doc["adminPass"]   = adminPass;
    doc["buzzerGPIO"]  = buzzerPin;
    doc["channel"]     = apChannel;
    doc["staSSID"]     = staSSID;
    doc["staPassword"] = staPassword;
    doc["ntpServer"]   = ntpServer;
    doc["ntpOffset"]   = ntpOffset;
    serializeJson(doc, out);
    out.close();
  }
}

// --- JSON helpers ---
void sendJSON(AsyncWebSocketClient* c, const DynamicJsonDocument& doc) {
  if (!c) return;
  String out;
  serializeJson(doc, out);
  c->text(out);
}

void broadcastJSON(const DynamicJsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  for (uint32_t id : authenticated) {
    AsyncWebSocketClient* c = ws.client(id);
    if (c) c->text(out);
  }
}

// --- File listing ---
void listFiles(AsyncWebSocketClient* c, const String& path) {
  if (!sdReady) {
    DynamicJsonDocument doc(256);
    doc["type"] = "error";
    doc["data"] = "SD not ready";
    sendJSON(c, doc);
    return;
  }
  String p = path;
  if (p.length() == 0) p = "/";
  File root = SD.open(p);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    DynamicJsonDocument doc(256);
    doc["type"] = "error";
    doc["data"] = "Cannot open " + p;
    sendJSON(c, doc);
    return;
  }

  DynamicJsonDocument doc(4096);
  doc["type"] = "files";
  doc["path"] = p;
  JsonArray arr = doc.createNestedArray("data");

  File file = root.openNextFile();
  int count = 0;
  while (file && count < 100) {
    JsonObject o = arr.createNestedObject();
    o["name"] = sanitize(file.name());
    o["size"] = (uint32_t)file.size();
    o["dir"]  = file.isDirectory();
    file = root.openNextFile();
    count++;
  }
  root.close();
  sendJSON(c, doc);
}

// --- Wi-Fi scan ---
void startWiFiScan() {
  if (scanPending) return;
  scanPending = true;
  // Need STA interface for scanning, but keep AP alive for client
  if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
  WiFi.scanNetworks(true);   // async
}

void startWardrive() {
  if (wardriveMode) return;
  wardriveMode = true;
  if (sdReady) {
    wardriveFile = SD.open("/wardrive.csv", FILE_APPEND);
    if (wardriveFile && wardriveFile.size() == 0) {
      wardriveFile.println("sec,ssid,rssi,channel,auth");
      wardriveFile.flush();
    }
  }
}

void stopWardrive() {
  if (wardriveFile) { wardriveFile.close(); }
  wardriveMode = false;
}

void sendClients(AsyncWebSocketClient* c) {
  DynamicJsonDocument doc(1024);
  doc["type"] = "clients";
  JsonArray arr = doc.createNestedArray("data");
  wifi_sta_list_t staList;
  if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK) {
    for (int i = 0; i < staList.num; i++) {
      JsonObject o = arr.createNestedObject();
      char mac[18];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
               staList.sta[i].mac[0], staList.sta[i].mac[1], staList.sta[i].mac[2],
               staList.sta[i].mac[3], staList.sta[i].mac[4], staList.sta[i].mac[5]);
      o["mac"] = mac;
      o["rssi"] = staList.sta[i].rssi;
    }
  }
  sendJSON(c, doc);
}

void finishWiFiScan() {
  int n = WiFi.scanComplete();
  if (n < 0) return;         // still running

  DynamicJsonDocument doc(4096);
  doc["type"] = "networks";
  JsonArray arr = doc.createNestedArray("data");
  JsonArray sel = doc.createNestedArray("selected");
  for (int idx : selectedAPs) sel.add(idx);

  apCache.clear();
  lastBestRssi = -100;
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.createNestedObject();
    String ssid = WiFi.SSID(i);
    int rssi    = WiFi.RSSI(i);
    if (rssi > lastBestRssi) lastBestRssi = rssi;
    uint8_t channel = WiFi.channel(i);

    wifi_auth_mode_t auth = (wifi_auth_mode_t)WiFi.encryptionType(i);
    String as;
    switch (auth) {
      case WIFI_AUTH_OPEN:         as = "OPEN"; break;
      case WIFI_AUTH_WEP:          as = "WEP"; break;
      case WIFI_AUTH_WPA_PSK:      as = "WPA_PSK"; break;
      case WIFI_AUTH_WPA2_PSK:     as = "WPA2_PSK"; break;
      case WIFI_AUTH_WPA_WPA2_PSK: as = "WPA_WPA2_PSK"; break;
      case WIFI_AUTH_WPA2_ENTERPRISE: as = "WPA2_ENTERPRISE"; break;
      default:                     as = "UNKNOWN"; break;
    }

    o["ssid"] = sanitize(ssid);
    o["bssid"] = WiFi.BSSIDstr(i);
    o["rssi"] = rssi;
    o["channel"] = channel;
    o["auth"] = as;

    apCache.push_back({ sanitize(ssid), WiFi.BSSIDstr(i), rssi, channel, as });

    if (wardriveMode && wardriveFile) {
      wardriveFile.println(String((millis() - bootMs) / 1000) + "," + sanitize(ssid) + "," + rssi + "," + channel + "," + as);
      wardriveFile.flush();
    }
  }

  WiFi.scanDelete();
  restoreAP();           // keep AP up and on the right channel
  scanPending = false;
  yield();
  broadcastJSON(doc);

  // Also echo to terminal
  DynamicJsonDocument term(256);
  term["type"] = "terminal";
  term["data"] = "Scan complete: " + String(n) + " networks";
  broadcastJSON(term);
}

// --- System status broadcast ---
void sendSysInfo(AsyncWebSocketClient* c = nullptr) {
  DynamicJsonDocument doc(768);
  doc["type"] = "sysinfo";
  JsonObject d = doc.createNestedObject("data");
  d["heap_free"]   = ESP.getFreeHeap();
  d["heap_total"]  = initialHeap;
  d["uptime"]      = (unsigned long)((millis() - bootMs) / 1000);
  d["clients"]     = ws.count();
  d["stations"]    = WiFi.softAPgetStationNum();
  d["sd"]          = sdReady;
  d["rssi"]        = lastBestRssi;
  d["ap_ssid"]     = apSSID;
  d["ap_ip"]       = WiFi.softAPIP().toString();
  d["ap_mac"]      = WiFi.macAddress();
  d["ap_channel"]  = WiFi.channel();
  d["attack"]      = wifiAttack.isRunning() ? wifiAttack.getModeName() : "idle";
  d["attack_pkts"] = wifiAttack.getPacketCount();
  d["attack_ch"]   = wifiAttack.getChannel();
  d["portal"]      = portalRunning;
  d["sd_total"]    = (unsigned long)(sdTotalBytes() / 1024);
  d["sd_used"]     = (unsigned long)(sdUsedBytes() / 1024);
  d["sd_free"]     = (unsigned long)((sdTotalBytes() - sdUsedBytes()) / 1024);
  d["sta_ip"]      = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  d["sta_status"]  = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
  d["time"]        = (unsigned long)time(nullptr);

  if (c) sendJSON(c, doc);
  else   broadcastJSON(doc);
}

void sendNetworkList(AsyncWebSocketClient* c) {
  DynamicJsonDocument doc(4096);
  doc["type"] = "networks";
  JsonArray arr = doc.createNestedArray("data");
  JsonArray sel = doc.createNestedArray("selected");
  for (int idx : selectedAPs) sel.add(idx);
  for (const auto& n : apCache) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = n.ssid;
    o["rssi"] = n.rssi;
    o["channel"] = n.channel;
    o["auth"] = n.auth;
  }
  sendJSON(c, doc);
}

void updateAttackTargets() {
  std::vector<AttackTarget> t;
  if (selectedAPs.empty()) {
    for (const auto& n : apCache) t.push_back({n.ssid, n.bssid, n.rssi, n.channel});
  } else {
    for (int idx : selectedAPs) {
      if (idx >= 0 && idx < (int)apCache.size()) {
        const auto& n = apCache[idx];
        t.push_back({n.ssid, n.bssid, n.rssi, n.channel});
      }
    }
  }
  wifiAttack.setTargets(t);
}

// --- Command parser ---
void processCmd(AsyncWebSocketClient* c, const String& raw) {
  String orig = raw;
  orig.trim();
  String cmd = orig;
  cmd.toLowerCase();

  DynamicJsonDocument term(512);
  term["type"] = "terminal";

  if (cmd == "help" || cmd == "?") {
    term["data"] =
      "Available commands:\n"
      "  help          show this message\n"
      "  sysinfo       one-shot system info\n"
      "  ls [path]     list SD files\n"
      "  mkdir <path>  create a folder\n"
      "  rm <path>     delete a file or empty folder\n"
      "  mv <src> <dst> move a file\n"
      "  touch <path>  create an empty file\n"
      "  cat <path>    show first 400 bytes of a file\n"
      "  readfile <p>  get a download URL for a file\n"
      "  scanall       full Wi-Fi scan\n"
      "  list -a       list cached APs\n"
      "  select -a <i> select AP by index\n"
      "  clearlist -a  clear selected APs\n"
      "  clients       stations on the ESP AP\n"
      "  wardrive      toggle wardrive CSV logging\n"
      "  arpscan       ping sweep AP subnet\n"
      "  pingscan      ping sweep AP subnet\n"
      "  portscan <ip> [s] [e] TCP port scan\n"
      "  stopscan      cancel scan/attack/wardrive\n"
      "  attack -t <type> [opts]  start a Wi-Fi attack\n"
      "  sniff -c <ch> -t <s>      capture 802.11 frames to SD\n"
      "  setsta <ssid> <pass>  save and connect to router\n"
      "  wifistatus    show AP/STA status\n"
      "  reconnect     retry STA connection\n"
      "  wifi scan     scan nearby networks\n"
      "  beep [ms]     buzzer beep (default 200)\n"
      "  ota           show OTA upload URL\n"
      "  portal -c <start|stop|status>  captive portal\n"
      "  resetui       delete old UI files; open /setup to reupload\n"
      "  reboot        restart the device\n"
      "  settings get  show current settings\n"
      "  curl <url>      fetch URL (requires STA internet)\n"
      "  nslookup <host> resolve a hostname\n"
      "  ping <host>     resolve + TCP/80 probe\n"
      "  md5sum <file>   compute MD5 hash\n"
      "  hexdump <file>  hex dump first 512 bytes\n"
      "  ps              FreeRTOS tasks and heap";
    sendJSON(c, term);
  }
  else if (cmd == "sysinfo") {
    sendSysInfo(c);
    term["data"] = "System info sent to System Status window";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("ls")) {
    String path = currentDir;
    int sp = cmd.indexOf(' ');
    if (sp > 0) {
      path = cmd.substring(sp + 1);
      path.trim();
    }
    path = resolvePath(path);
    if (path.length() == 0) path = "/";
    listFiles(c, path);
  }
  else if (cmd == "scanall") {
    apCache.clear();
    selectedAPs.clear();
    startWiFiScan();
    term["data"] = "Full Wi-Fi scan started...";
    sendJSON(c, term);
  }
  else if (cmd == "list -a") {
    sendNetworkList(c);
  }
  else if (cmd.startsWith("select -a ")) {
    int idx = cmd.substring(10).toInt();
    if (idx >= 0 && idx < (int)apCache.size()) {
      selectedAPs.insert(idx);
      term["data"] = "Selected " + String(idx);
    } else {
      term["data"] = "Invalid index";
    }
    sendJSON(c, term);
  }
  else if (cmd == "clearlist -a") {
    selectedAPs.clear();
    term["data"] = "AP selection cleared";
    sendJSON(c, term);
  }
  else if (cmd == "wifi scan") {
    startWiFiScan();
    term["data"] = "Wi-Fi scan started...";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("beep")) {
    int ms = 200;
    int sp = cmd.indexOf(' ');
    if (sp > 0) ms = cmd.substring(sp + 1).toInt();
    if (ms <= 0) ms = 200;
    beep(ms);
    term["data"] = "Beep " + String(ms) + "ms";
    sendJSON(c, term);
  }
  else if (cmd == "ota") {
    term["data"] = "OTA endpoint: http://192.168.4.1/update?token=<your-admin-password>\n"
                   "POST the compiled .bin firmware file with the token query parameter.";
    sendJSON(c, term);
  }
  else if (cmd == "resetui") {
    if (!sdReady) {
      term["data"] = "SD not ready";
    } else {
      bool a = SD.remove("/index.html");
      bool b = SD.remove("/service-worker.js");
      restoreAP();
      term["data"] = "Old UI removed. Open http://192.168.4.1/setup to reupload.";
    }
    sendJSON(c, term);
  }
  else if (cmd == "reboot") {
    term["data"] = "Rebooting now...";
    sendJSON(c, term);
    delay(500);
    ESP.restart();
  }
  else if (cmd == "wardrive") {
    if (wardriveMode) {
      stopWardrive();
      term["data"] = "Wardrive stopped";
    } else {
      startWardrive();
      term["data"] = "Wardrive started; CSV: /wardrive.csv";
    }
    sendJSON(c, term);
  }
  else if (cmd == "clients") {
    sendClients(c);
    term["data"] = "Client list sent";
    sendJSON(c, term);
  }
  else if (cmd == "arpscan" || cmd == "pingscan") {
    DynamicJsonDocument doc(4096);
    doc["type"] = "scan_result";
    doc["method"] = cmd;
    JsonArray arr = doc.createNestedArray("open");
    int found = 0;
    WiFiClient probe;
    for (int h = 2; h <= 254 && found < 50; h++) {
      IPAddress ip(192, 168, 4, h);
      if (probe.connect(ip, 80, 300)) {
        arr.add("192.168.4." + String(h));
        found++;
        probe.stop();
      }
      yield();
    }
    sendJSON(c, doc);
    term["data"] = String(cmd) + " complete: " + String(found) + " hosts up";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("portscan ")) {
    String rest = cmd.substring(9);
    rest.trim();
    int sp1 = rest.indexOf(' ');
    String ipStr = (sp1 > 0) ? rest.substring(0, sp1) : rest;
    int startP = 1, endP = 100;
    if (sp1 > 0) {
      String p2 = rest.substring(sp1 + 1);
      p2.trim();
      int sp2 = p2.indexOf(' ');
      startP = p2.substring(0, sp2 > 0 ? sp2 : p2.length()).toInt();
      if (sp2 > 0) endP = p2.substring(sp2 + 1).toInt();
    }
    if (startP <= 0) startP = 1;
    if (endP <= startP) endP = startP + 99;
    if (endP - startP > 500) endP = startP + 500;
    IPAddress ip;
    DynamicJsonDocument doc(1024);
    doc["type"] = "scan_result";
    doc["method"] = "tcp";
    doc["ip"] = ipStr;
    JsonArray arr = doc.createNestedArray("open");
    if (ip.fromString(ipStr)) {
      WiFiClient client;
      JsonObject banners = doc.createNestedObject("banners");
      for (int p = startP; p <= endP && arr.size() < 50; p++) {
        if (client.connect(ip, p, 200)) {
          arr.add(p);
          client.setTimeout(200);
          uint8_t b[64];
          int n = client.read(b, sizeof(b));
          if (n > 0) banners[String(p)] = String((char*)b, n);
          client.stop();
        }
        yield();
      }
    }
    sendJSON(c, doc);
    term["data"] = "Port scan complete";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("attack ")) {
    updateAttackTargets();
    String attackCmd = orig;
    // Allow -f <file> on SD to load a list of SSIDs for beacon/probe/ssidspam
    int fIdx = attackCmd.indexOf(" -f ");
    if (fIdx >= 0) {
      int fStart = fIdx + 4;
      int fEnd = attackCmd.indexOf(' ', fStart);
      if (fEnd < 0) fEnd = attackCmd.length();
      String file = attackCmd.substring(fStart, fEnd);
      file = resolvePath(file);
      if (sdReady && file.length() > 0 && SD.exists(file)) {
        File f = SD.open(file, FILE_READ);
        if (f) {
          String ssidList = "";
          while (f.available() && ssidList.length() < 900) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length()) {
              if (ssidList.length()) ssidList += ",";
              ssidList += line;
            }
          }
          f.close();
          attackCmd = attackCmd.substring(0, fIdx) + " -s " + ssidList + attackCmd.substring(fEnd);
        }
      }
    }
    term["data"] = wifiAttack.runCommand(attackCmd);
    sendJSON(c, term);
  }
  else if (cmd == "stopscan") {
    scanPending = false;
    WiFi.scanDelete();
    stopWardrive();
    wifiAttack.stop();
    stopSniff();
    restoreAP();
    term["data"] = "Scan/attack/wardrive stopped";
    sendJSON(c, term);
  }
  else if (cmd == "sniff stop") {
    stopSniff();
    term["data"] = "Sniffer stopped";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("sniff ")) {
    String rest = cmd.substring(6);
    rest.trim();
    int ch = 1;
    unsigned int sec = 5;
    int cIdx = rest.indexOf(" -c ");
    if (cIdx >= 0) {
      int sEnd = rest.indexOf(' ', cIdx + 4);
      if (sEnd < 0) sEnd = rest.length();
      ch = rest.substring(cIdx + 4, sEnd).toInt();
      if (ch < 1 || ch > 13) ch = 1;
    }
    int tIdx = rest.indexOf(" -t ");
    if (tIdx >= 0) {
      int sEnd = rest.indexOf(' ', tIdx + 4);
      if (sEnd < 0) sEnd = rest.length();
      sec = rest.substring(tIdx + 4, sEnd).toInt();
      if (sec < 1) sec = 1;
      if (sec > 30) sec = 30;
    }
    startSniff((uint8_t)ch, (uint32_t)sec);
    term["data"] = "Sniffer started on channel " + String(ch) + " for " + String(sec) + " s";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("portal ")) {
    String rest = cmd.substring(7);
    rest.trim();
    if (rest == "start" || rest == "-c start") {
      portalRunning = true;
      term["data"] = "Captive portal started. http://192.168.4.1/ now serves the portal.";
    } else if (rest == "stop" || rest == "-c stop") {
      portalRunning = false;
      term["data"] = "Captive portal stopped. http://192.168.4.1/ serves the OS again.";
    } else if (rest == "status" || rest == "-c status") {
      term["data"] = "Portal: " + String(portalRunning ? "running" : "stopped");
    } else {
      term["data"] = "Usage: portal start | stop | status";
    }
    sendJSON(c, term);
  }
  else if (cmd == "settings get") {
    DynamicJsonDocument s(1024);
    s["type"] = "settings";
    JsonObject d = s.createNestedObject("data");
    d["ssid"]        = apSSID;
    d["password"]    = apPassword;          // visible to admin
    d["adminPass"]   = adminPass;
    d["buzzerGPIO"]  = buzzerPin;
    d["channel"]     = apChannel;
    d["staSSID"]     = staSSID;
    d["staPassword"] = staPassword;
    d["ntpServer"]   = ntpServer;
    d["ntpOffset"]   = ntpOffset;
    d["staIP"]       = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
    d["staStatus"]   = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
    sendJSON(c, s);
  }
  else if (cmd.startsWith("curl ")) {
    if (WiFi.status() != WL_CONNECTED) {
      term["data"] = "No internet. Connect to a router in Settings first.";
    } else {
      String url = orig.substring(5);
      url.trim();
      HTTPClient http;
      http.begin(url);
      http.setTimeout(10000);
      int code = http.GET();
      String payload = (code == 200) ? http.getString().substring(0, 1200) : "HTTP " + String(code);
      http.end();
      term["data"] = payload;
    }
    sendJSON(c, term);
  }
  else if (cmd.startsWith("nslookup ")) {
    String host = orig.substring(9);
    host.trim();
    IPAddress ip;
    if (WiFi.hostByName(host.c_str(), ip)) term["data"] = host + " = " + ip.toString();
    else term["data"] = "Could not resolve " + host;
    sendJSON(c, term);
  }
  else if (cmd.startsWith("ping ")) {
    String host = orig.substring(5);
    host.trim();
    if (host.length() == 0) { term["data"] = "Usage: ping <host>"; sendJSON(c, term); }
    else if (WiFi.status() != WL_CONNECTED) { term["data"] = "No internet"; sendJSON(c, term); }
    else {
      WiFiClient cl;
      IPAddress ip;
      unsigned long t0 = millis();
      bool resolved = WiFi.hostByName(host.c_str(), ip);
      unsigned long t1 = millis();
      if (!resolved) { term["data"] = host + " could not resolve"; sendJSON(c, term); }
      else {
        unsigned long t2 = millis();
        bool open = cl.connect(ip, 80);
        unsigned long t3 = millis();
        cl.stop();
        term["data"] = host + " resolved to " + ip.toString() + "\nDNS: " + String(t1 - t0) + " ms\nTCP/80: " + (open ? "open" : "closed/no reply") + " (" + String(t3 - t2) + " ms)";
        sendJSON(c, term);
      }
    }
  }
  else if (cmd.startsWith("md5sum ")) {
    String path = resolvePath(cmd.substring(7));
    if (path.length() == 0 || !sdReady || !SD.exists(path)) {
      term["data"] = "File not found";
    } else {
      File f = SD.open(path);
      mbedtls_md5_context ctx;
      mbedtls_md5_init(&ctx);
      mbedtls_md5_starts_ret(&ctx);
      uint8_t buf[512];
      size_t n;
      while ((n = f.read(buf, sizeof(buf))) > 0) mbedtls_md5_update_ret(&ctx, buf, n);
      f.close();
      uint8_t digest[16];
      mbedtls_md5_finish_ret(&ctx, digest);
      mbedtls_md5_free(&ctx);
      String out = "";
      for (int i = 0; i < 16; i++) {
        if (digest[i] < 16) out += "0";
        out += String(digest[i], HEX);
      }
      term["data"] = out + "  " + path;
    }
    sendJSON(c, term);
  }
  else if (cmd.startsWith("hexdump ")) {
    String path = resolvePath(cmd.substring(8));
    if (path.length() == 0 || !sdReady || !SD.exists(path)) {
      term["data"] = "File not found";
    } else {
      File f = SD.open(path);
      String out = "";
      int lines = 0;
      while (f.available() && lines < 32) {
        uint8_t b[16];
        int n = f.read(b, 16);
        String hex = "", asc = "";
        for (int i = 0; i < n; i++) {
          if (b[i] < 16) hex += "0";
          hex += String(b[i], HEX) + " ";
          if (b[i] >= 32 && b[i] < 127) asc += (char)b[i];
          else asc += ".";
        }
        out += "000000" + String(lines * 16, HEX) + "  " + hex + " |" + asc + "|\n";
        lines++;
      }
      f.close();
      term["data"] = out;
    }
    sendJSON(c, term);
  }
  else if (cmd == "ps") {
    term["data"] = "FreeRTOS tasks: " + String(uxTaskGetNumberOfTasks()) + "\nHeap free: " + String(esp_get_free_heap_size()) + " bytes";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("mkdir ")) {
    String path = resolvePath(cmd.substring(6));
    if (path.length() == 0) { term["data"] = "Invalid path"; sendJSON(c, term); }
    bool ok = sdReady && SD.mkdir(path);
    term["data"] = ok ? "Created " + path : "Failed";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("rm ")) {
    String path = resolvePath(cmd.substring(3));
    if (path.length() == 0) { term["data"] = "Invalid path"; sendJSON(c, term); }
    bool ok = sdReady && SD.remove(path);
    term["data"] = ok ? "Removed " + path : "Failed";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("mv ")) {
    String rest = cmd.substring(3);
    int sp = rest.indexOf(' ');
    if (sp > 0) {
      String src = resolvePath(rest.substring(0, sp));
      String dst = resolvePath(rest.substring(sp + 1));
      bool ok = false;
      if (sdReady) {
        File s = SD.open(src, FILE_READ);
        if (s) {
          File d = SD.open(dst, FILE_WRITE);
          if (d) {
            uint8_t buf[512];
            while (s.available()) {
              size_t r = s.read(buf, sizeof(buf));
              d.write(buf, r);
            }
            d.close();
            ok = true;
          }
          s.close();
          SD.remove(src);
        }
      }
      term["data"] = ok ? "Moved " + src + " to " + dst : "Failed";
    } else {
      term["data"] = "Usage: mv <src> <dst>";
    }
    sendJSON(c, term);
  }
  else if (cmd.startsWith("touch ")) {
    String path = resolvePath(cmd.substring(6));
    if (path.length() == 0) { term["data"] = "Invalid path"; sendJSON(c, term); }
    bool ok = false;
    if (sdReady) {
      File f = SD.open(path, FILE_WRITE);
      if (f) { f.close(); ok = true; }
    }
    term["data"] = ok ? "Touched " + path : "Failed";
    sendJSON(c, term);
  }
  else if (cmd.startsWith("cat ")) {
    String path = resolvePath(cmd.substring(4));
    if (path.length() == 0) { term["data"] = "Invalid path"; sendJSON(c, term); }
    String out = "";
    if (sdReady) {
      File f = SD.open(path, FILE_READ);
      if (f) {
        while (f.available() && out.length() < 300) {
          out += (char)f.read();
        }
        f.close();
      } else {
        out = "Cannot open " + path;
      }
    } else {
      out = "SD not ready";
    }
    term["data"] = sanitize(out);
    sendJSON(c, term);
  }
  else if (cmd.startsWith("readfile ")) {
    String path = resolvePath(cmd.substring(9));
    if (path.length() == 0) { term["data"] = "Invalid path"; sendJSON(c, term); }
    term["data"] = "Download: http://192.168.4.1/files?token=" + adminPass + "&path=" + urlEncode(path);
    sendJSON(c, term);
  }
  else if (cmd.startsWith("setwall ")) {
    String src = resolvePath(cmd.substring(8));
    if (src.length() == 0) { term["data"] = "Invalid path"; sendJSON(c, term); }
    if (!sdReady) { term["data"] = "SD not ready"; sendJSON(c, term); }
    File in = SD.open(src, FILE_READ);
    if (!in) { term["data"] = "Cannot open " + src; sendJSON(c, term); }
    File out = SD.open("/wallpaper.jpg", FILE_WRITE);
    if (!out) { in.close(); term["data"] = "Cannot write /wallpaper.jpg"; sendJSON(c, term); }
    uint8_t buf[256];
    while (in.available()) {
      size_t r = in.read(buf, sizeof(buf));
      out.write(buf, r);
      yield();
    }
    in.close(); out.close();
    term["data"] = "Wallpaper set to " + src + ". Refresh the page to see it.";
    sendJSON(c, term);
  }
  else {
    String shell = shellCommand(cmd);
    if (shell.length() > 0) {
      term["data"] = shell;
      sendJSON(c, term);
    } else {
      term["data"] = "Unknown command: " + raw + "\nType 'help' for available commands.";
      sendJSON(c, term);
      beep(100);
    }
  }
}

// --- URL / path utilities ---

String urlDecode(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '+' && i < in.length() - 2 && in[i + 1] == ' ')
      out += ' ';
    else if (c == '%' && i + 2 < in.length()) {
      char h1 = in[i + 1], h2 = in[i + 2];
      int v1 = (h1 >= '0' && h1 <= '9') ? h1 - '0' : (h1 >= 'A' && h1 <= 'F') ? h1 - 'A' + 10 : (h1 >= 'a' && h1 <= 'f') ? h1 - 'a' + 10 : -1;
      int v2 = (h2 >= '0' && h2 <= '9') ? h2 - '0' : (h2 >= 'A' && h2 <= 'F') ? h2 - 'A' + 10 : (h2 >= 'a' && h2 <= 'f') ? h2 - 'a' + 10 : -1;
      if (v1 >= 0 && v2 >= 0) { out += (char)(v1 * 16 + v2); i += 2; }
      else out += c;
    } else
      out += c;
  }
  return out;
}

String resolvePath(const String& in) {
  String p = in;
  p.trim();
  if (p.length() == 0) return currentDir;
  if (p == "/") return "/";
  if (p.indexOf("..") >= 0) return ""; // reject traversal
  if (p.indexOf('\0') >= 0) return "";
  if (p[0] != '/') {
    if (currentDir == "/") p = "/" + p;
    else p = currentDir + "/" + p;
  }
  while (p.indexOf("//") >= 0) p.replace("//", "/");
  if (p.length() > 1 && p.endsWith("/")) p = p.substring(0, p.length() - 1);
  return p;
}

// --- File copy helper ---
bool copyFile(const String& src, const String& dst) {
  if (!sdReady) return false;
  File in = SD.open(src, FILE_READ);
  if (!in) return false;
  File out = SD.open(dst, FILE_WRITE);
  if (!out) { in.close(); return false; }
  uint8_t buf[256];
  while (in.available()) {
    size_t r = in.read(buf, sizeof(buf));
    out.write(buf, r);
    yield();
  }
  in.close(); out.close();
  return true;
}

// --- Shell-like command processor ---

String shellCommand(const String& raw) {
  String s = raw;
  s.trim();
  String orig = s;
  s.toLowerCase();

  // cd
  if (s.startsWith("cd ")) {
    String p = resolvePath(s.substring(3));
    if (p.length() == 0) return "Invalid path";
    if (!sdReady) return "SD not ready";
    File d = SD.open(p);
    if (!d || !d.isDirectory()) return "Not a directory: " + p;
    currentDir = p;
    return "Changed to " + p;
  }
  if (s == "cd") {
    currentDir = "/";
    return "Changed to /";
  }
  if (s == "pwd") return currentDir;

  // df
  if (s == "df") {
    uint64_t total = sdTotalBytes();
    uint64_t used  = sdUsedBytes();
    uint64_t free  = total - used;
    if (total == 0) return "SD not ready";
    String out = "Filesystem     1K-blocks      Used  Available Use%\n";
    out += "SD              " + String((unsigned long)(total / 1024)) + "  " + String((unsigned long)(used / 1024)) + "  " + String((unsigned long)(free / 1024)) + "  " + String((int)(used * 100 / total)) + "%";
    return out;
  }

  // du
  if (s.startsWith("du ")) {
    String p = resolvePath(s.substring(3));
    if (p.length() == 0) return "Invalid path";
    if (!sdReady) return "SD not ready";
    uint64_t sum = 0;
    int count = 0;
    File root = SD.open(p);
    if (!root) return "Cannot open " + p;
    File f = root.openNextFile();
    while (f && count < 100) {
      sum += f.size();
      f = root.openNextFile();
      count++;
      yield();
    }
    root.close();
    return "Total: " + String((unsigned long)(sum / 1024)) + " KB in " + String(count) + " files";
  }

  // free
  if (s == "free") {
    return "total: " + String(initialHeap / 1024) + " KB\nfree:  " + String(ESP.getFreeHeap() / 1024) + " KB\nused:  " + String((initialHeap - ESP.getFreeHeap()) / 1024) + " KB";
  }

  // backup
  if (s == "backup") {
    if (!sdReady) return "SD not ready";
    if (!SD.exists("/backup")) SD.mkdir("/backup");
    String ts = String(millis());
    bool ok1 = copyFile("/settings.json", "/backup/" + ts + "_settings.json");
    bool ok2 = copyFile("/wallpaper.jpg", "/backup/" + ts + "_wallpaper.jpg");
    return (ok1 ? "settings" : "no settings") + String(" and ") + (ok2 ? "wallpaper" : "no wallpaper") + " backed up";
  }

  // restore
  if (s == "restore") {
    if (!sdReady) return "SD not ready";
    File root = SD.open("/backup");
    if (!root || !root.isDirectory()) { if (root) root.close(); return "No /backup folder"; }
    String latestSettings, latestWallpaper;
    File f = root.openNextFile();
    while (f) {
      String n = f.name();
      if (n.endsWith("_settings.json")) latestSettings = "/backup/" + n;
      if (n.endsWith("_wallpaper.jpg")) latestWallpaper = "/backup/" + n;
      f = root.openNextFile();
    }
    root.close();
    String out;
    if (latestSettings.length() && copyFile(latestSettings, "/settings.json")) out += "settings restored ";
    if (latestWallpaper.length() && copyFile(latestWallpaper, "/wallpaper.jpg")) out += "wallpaper restored ";
    if (out.length() == 0) return "No backups found";
    loadSettings();
    restoreAP();
    return out + "- reboot to apply AP settings";
  }

  // uptime
  if (s == "uptime") {
    unsigned long up = (millis() - bootMs) / 1000;
    unsigned long d = up / 86400, h = (up % 86400) / 3600, m = (up % 3600) / 60, sec = up % 60;
    String out = "up ";
    if (d) out += String(d) + " days, ";
    if (h) out += String(h) + " hours, ";
    out += String(m) + " min " + String(sec) + " sec";
    return out;
  }

  // whoami / id
  if (s == "whoami" || s == "id") return "root";

  // hostname
  if (s == "hostname") return "esp32-os";

  // ifconfig / iwconfig
  if (s == "ifconfig" || s == "iwconfig") {
    String out = "esp0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>\n";
    out += "        inet " + WiFi.softAPIP().toString() + " netmask 255.255.255.0\n";
    out += "        ether " + String(WiFi.macAddress()) + "\n";
    return out;
  }

  // iw / list nearby APs from cache
  if (s == "iw" || s == "iwlist" || s == "iwlist scan") {
    String out = "Nearby networks:\n";
    for (size_t i = 0; i < apCache.size(); i++) {
      out += String(i) + "  " + apCache[i].ssid + "  " + String(apCache[i].rssi) + " dBm  ch" + String(apCache[i].channel) + "  " + apCache[i].auth + "\n";
    }
    return out;
  }

  // neofetch
  if (s == "neofetch") {
    String out = "   ___  ____  __  __  ___  _   _ \n";
    out += "  | __||__ / |  /  || __|| | | |\n";
    out += "  | _|  |_ \\ | |/| || _| | |_| |\n";
    out += "  |___||___/ |_|  |_||___| \\___/ \n";
    out += "OS:        Icy OS Web OS\n";
    out += "Host:      ESP32-D0WD-V3\n";
    out += "Uptime:    " + shellCommand("uptime") + "\n";
    out += "AP IP:     " + WiFi.softAPIP().toString() + "\n";
    out += "Stations:  " + String(WiFi.softAPgetStationNum()) + "\n";
    out += "Free heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
    if (sdReady) {
      out += "SD:        " + String((unsigned long)((sdTotalBytes() - sdUsedBytes()) / 1024)) + " KB free";
    }
    return out;
  }

  // setsta <ssid> <pass>  — connect to a router for internet
  if (s.startsWith("setsta ")) {
    String rest = orig.substring(7);
    int p = 0;
    while (p < rest.length() && rest[p] == ' ') p++;
    if (p >= rest.length()) return "Usage: setsta <ssid> <password>";
    char q = 0;
    if (rest[p] == '"' || rest[p] == '\'') { q = rest[p]; p++; }
    int e = p;
    while (e < rest.length() && (q ? rest[e] != q : rest[e] != ' ')) e++;
    if (e == p) return "Usage: setsta <ssid> <password>";
    staSSID = rest.substring(p, e);
    if (q && e < rest.length()) e++;
    while (e < rest.length() && rest[e] == ' ') e++;
    staPassword = rest.substring(e);
    saveSettings();
    connectSTA();
    return "Saved STA " + staSSID + ", connecting...";
  }

  // wifistatus
  if (s == "wifistatus") {
    String out = "AP: " + apSSID + "  " + WiFi.softAPIP().toString() + "  ch" + String(apChannel) + "\n";
    out += "AP stations: " + String(WiFi.softAPgetStationNum()) + "\n";
    if (WiFi.status() == WL_CONNECTED) {
      out += "STA: connected to " + WiFi.SSID() + "  IP " + WiFi.localIP().toString() + "\n";
    } else {
      out += "STA: " + String(WiFi.status() == WL_IDLE_STATUS ? "idle" : WiFi.status() == WL_CONNECT_FAILED ? "failed" : "connecting") + "\n";
    }
    return out;
  }

  // reconnect
  if (s == "reconnect") {
    connectSTA();
    return "Reconnecting STA...";
  }

  // forgetsta  — clear saved STA credentials
  if (s == "forgetsta") {
    staSSID = "";
    staPassword = "";
    WiFi.disconnect(true);
    saveSettings();
    restoreAP();
    return "STA forgotten; using AP only";
  }

  // ps
  if (s == "ps") {
    return "  PID TTY          TIME CMD\n";
  }

  // cat
  if (s.startsWith("cat ")) {
    String p = resolvePath(s.substring(4));
    if (p.length() == 0) return "Invalid path";
    if (!sdReady) return "SD not ready";
    File f = SD.open(p);
    if (!f || f.isDirectory()) return "Cannot open " + p;
    String out;
    out.reserve(1024);
    while (f.available() && out.length() < 1000) out += (char)f.read();
    f.close();
    if (out.length() == 0) out = "(empty file)";
    return out;
  }

  // head
  if (s.startsWith("head ")) {
    int n = 10;
    String rest = s.substring(5);
    rest.trim();
    int sp = rest.indexOf(' ');
    String p = rest;
    if (sp > 0 && rest.substring(0, sp) == "-n") {
      n = rest.substring(sp + 1).toInt();
      int sp2 = rest.indexOf(' ', sp + 1);
      if (sp2 <= 0) return "Usage: head -n <num> <path>";
      p = rest.substring(sp2 + 1);
    }
    p = resolvePath(p);
    if (p.length() == 0) return "Invalid path";
    if (!sdReady) return "SD not ready";
    File f = SD.open(p);
    if (!f || f.isDirectory()) return "Cannot open " + p;
    String out;
    out.reserve(1024);
    int lines = 0;
    while (f.available() && lines < n && out.length() < 1000) {
      char c = f.read();
      out += c;
      if (c == '\n') lines++;
    }
    f.close();
    return out;
  }

  // tail
  if (s.startsWith("tail ")) {
    int n = 10;
    String rest = s.substring(5);
    rest.trim();
    int sp = rest.indexOf(' ');
    String p = rest;
    if (sp > 0 && rest.substring(0, sp) == "-n") {
      n = rest.substring(sp + 1).toInt();
      int sp2 = rest.indexOf(' ', sp + 1);
      if (sp2 <= 0) return "Usage: tail -n <num> <path>";
      p = rest.substring(sp2 + 1);
    }
    p = resolvePath(p);
    if (p.length() == 0) return "Invalid path";
    if (!sdReady) return "SD not ready";
    File f = SD.open(p);
    if (!f || f.isDirectory()) return "Cannot open " + p;
    // Read all, keep last N lines
    String all;
    all.reserve(2048);
    while (f.available() && all.length() < 2000) all += (char)f.read();
    f.close();
    int lines = 0;
    for (int i = all.length() - 1; i >= 0; i--) if (all[i] == '\n') { lines++; if (lines == n) { all = all.substring(i + 1); break; } }
    if (all.length() == 0) return "(empty file)";
    return all;
  }

  // wc
  if (s.startsWith("wc ")) {
    String p = resolvePath(s.substring(3));
    if (p.length() == 0) return "Invalid path";
    if (!sdReady) return "SD not ready";
    File f = SD.open(p);
    if (!f || f.isDirectory()) return "Cannot open " + p;
    int lines = 0, words = 0, bytes = 0, inWord = 0;
    while (f.available()) {
      char c = f.read();
      bytes++;
      if (c == '\n') lines++;
      if (isspace((unsigned char)c)) inWord = 0;
      else if (!inWord) { inWord = 1; words++; }
    }
    f.close();
    return String(lines) + " " + String(words) + " " + String(bytes) + " " + p;
  }

  // find
  if (s.startsWith("find ")) {
    String p = resolvePath(s.substring(5));
    if (p.length() == 0) return "Invalid path";
    if (!sdReady) return "SD not ready";
    File root = SD.open(p);
    if (!root) return "Cannot open " + p;
    String out;
    File f = root.openNextFile();
    int count = 0;
    while (f && count < 100) {
      out += String(f.name()) + "\n";
      f = root.openNextFile();
      count++;
      yield();
    }
    root.close();
    if (out.length() == 0) out = "(empty directory)";
    return out;
  }

  // grep
  if (s.startsWith("grep ")) {
    String rest = s.substring(5);
    int sp = rest.lastIndexOf(' ');
    if (sp <= 0) return "Usage: grep <pattern> <path>";
    String pat = rest.substring(0, sp);
    String p = resolvePath(rest.substring(sp + 1));
    if (p.length() == 0) return "Invalid path";
    if (!sdReady) return "SD not ready";
    File f = SD.open(p);
    if (!f || f.isDirectory()) return "Cannot open " + p;
    String out;
    String line;
    int ln = 1;
    while (f.available() && out.length() < 1000) {
      char c = f.read();
      if (c == '\n') {
        if (line.indexOf(pat) >= 0) out += String(ln) + ":" + line + "\n";
        line = "";
        ln++;
      } else line += c;
    }
    f.close();
    if (out.length() == 0) out = "No matches";
    return out;
  }

  // file
  if (s.startsWith("file ")) {
    String p = resolvePath(s.substring(5));
    if (p.length() == 0) return "Invalid path";
    if (!sdReady) return "SD not ready";
    File f = SD.open(p);
    if (!f) return "Cannot open " + p;
    String out = p + ": " + (f.isDirectory() ? "directory" : ("file, " + String(f.size()) + " bytes"));
    f.close();
    return out;
  }

  // cp
  if (s.startsWith("cp ")) {
    String rest = s.substring(3);
    int sp = rest.indexOf(' ');
    if (sp <= 0) return "Usage: cp <src> <dst>";
    String src = resolvePath(rest.substring(0, sp));
    String dst = resolvePath(rest.substring(sp + 1));
    if (!sdReady) return "SD not ready";
    File in = SD.open(src, FILE_READ);
    if (!in) return "Cannot open " + src;
    File out = SD.open(dst, FILE_WRITE);
    if (!out) { in.close(); return "Cannot write " + dst; }
    while (in.available()) {
      uint8_t buf[128];
      size_t r = in.read(buf, sizeof(buf));
      out.write(buf, r);
      yield();
    }
    in.close();
    out.close();
    return "Copied " + src + " -> " + dst;
  }

  // rmdir
  if (s.startsWith("rmdir ")) {
    String p = resolvePath(s.substring(6));
    if (!sdReady) return "SD not ready";
    File d = SD.open(p);
    if (!d || !d.isDirectory()) return "Not a directory";
    d.close();
    if (!SD.rmdir(p)) return "Failed to remove " + p;
    return "Removed " + p;
  }

  // env
  if (s == "env") {
    return "HOME=/\nUSER=root\nHOSTNAME=esp32-os\nPWD=" + currentDir + "\nSHELL=/bin/esh";
  }

  // echo
  if (s.startsWith("echo ")) return s.substring(5);
  if (s == "echo") return "";

  // poweroff
  if (s == "poweroff" || s == "halt" || s == "shutdown") {
    return "Poweroff not available. Use 'reboot' to restart.";
  }

  // man / help
  if (s.startsWith("man ") || s.startsWith("help ")) {
    String topic = s.startsWith("man ") ? s.substring(4) : s.substring(5);
    topic.trim();
    if (topic == "ls")      return "ls [path]  List directory contents.";
    if (topic == "cat")     return "cat <file>  Show file contents.";
    if (topic == "cd")      return "cd <dir>  Change current directory.";
    if (topic == "pwd")     return "pwd  Print working directory.";
    if (topic == "df")      return "df  Show SD card disk usage.";
    if (topic == "free")    return "free  Show free RAM.";
    if (topic == "uptime")  return "uptime  Show system uptime.";
    if (topic == "neofetch")return "neofetch  Show system info with ASCII logo.";
    if (topic == "attack")  return "attack -t <type> [opts]  Start a Wi-Fi attack.";
    if (topic == "portal")  return "portal start | stop | status  Control captive portal.";
    return "No manual entry for " + topic;
  }

  if (s == "help") {
    return "Shell commands:\n"
           "  cd, pwd, ls, cat, head, tail, wc, find, grep, file\n"
           "  cp, mv, rm, mkdir, rmdir, touch\n"
           "  df, du, free, uptime, whoami, hostname, ifconfig, iw\n"
           "  backup, restore   backup/restore settings and wallpaper\n"
           "  neofetch, ps, env, echo, clear, history\n"
           "  help [cmd], man [cmd]\n"
           "Use 'help <command>' for details.";
  }

  return "";
}

// --- AP restore helper ---
void restoreAP() {
  if (apSSID.length() == 0) apSSID = "Icy-OS";
  if (apPassword.length() < 8) apPassword = "Password123";
  if (apChannel < 1 || apChannel > 13) apChannel = 1;
  wifi_mode_t target = (staSSID.length() > 0 && WiFi.status() == WL_CONNECTED) ? WIFI_AP_STA : WIFI_AP;
  if (WiFi.getMode() != target) WiFi.mode(target);
  if (WiFi.softAPIP() != localIP) WiFi.softAPConfig(localIP, gateway, subnet);
  // Only restart softAP if config changed to avoid disconnecting existing clients
  static String lastSsid, lastPass;
  if (lastSsid != apSSID || lastPass != apPassword) {
    WiFi.softAP(apSSID.c_str(), apPassword.c_str(), apChannel, 0, 4);
    lastSsid = apSSID;
    lastPass = apPassword;
  }
}

// --- STA / internet connection ---
void connectSTA() {
  if (staSSID.length() == 0) { restoreAP(); staConnectStart = 0; return; }
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == staSSID) { restoreAP(); staConnectStart = 0; return; }
  if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(true);
  delay(100);
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  Serial.println("[ICY] STA connecting to " + staSSID);
  WiFi.begin(staSSID.c_str(), staPassword.c_str());
  staConnectStart = millis();
}

// --- Packet sniffer / pcap ---
void IRAM_ATTR pcapPacketHandler(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!sniffing || !pcapFile) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  uint16_t len = pkt->rx_ctrl.sig_len;
  if (!len) return;
  uint32_t now = micros();
  uint32_t ts_sec = now / 1000000UL;
  uint32_t ts_usec = now % 1000000UL;
  uint8_t hdr[16];
  memcpy(hdr, &ts_sec, 4);
  memcpy(hdr + 4, &ts_usec, 4);
  memcpy(hdr + 8, &len, 2);
  memcpy(hdr + 10, &len, 2);
  memset(hdr + 12, 0, 4);
  pcapFile.write(hdr, 16);
  pcapFile.write(pkt->payload, len);
}

void startSniff(uint8_t ch, uint32_t seconds) {
  if (sniffing) stopSniff();
  if (!sdReady) return;
  if (!SD.exists("/captures")) SD.mkdir("/captures");
  String path = "/captures/capture_" + String(millis()) + ".pcap";
  pcapFile = SD.open(path, FILE_WRITE);
  if (!pcapFile) return;
  uint8_t ghdr[24] = {
    0xd4, 0xc3, 0xb2, 0xa1, 0x02, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00
  };
  pcapFile.write(ghdr, 24);
  sniffDuration = seconds * 1000;
  sniffStart = millis();
  sniffing = true;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&pcapPacketHandler);
}

void stopSniff() {
  if (!sniffing) return;
  sniffing = false;
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  if (pcapFile) { pcapFile.flush(); pcapFile.close(); }
}

// --- WebSocket event handler ---
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    DynamicJsonDocument doc(256);
    doc["type"] = "auth";
    doc["data"] = "required";
    sendJSON(client, doc);
    return;
  }

  if (type == WS_EVT_DISCONNECT) {
    authenticated.erase(client->id());
    scannerSubs.erase(client->id());
    return;
  }

  if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String msg;
      msg.reserve(len);
      for (size_t i = 0; i < len; i++) msg += (char)data[i];

      DynamicJsonDocument doc(1024);
      DeserializationError e = deserializeJson(doc, msg);
      if (e) {
        DynamicJsonDocument err(256);
        err["type"] = "error";
        err["data"] = "Invalid JSON";
        sendJSON(client, err);
        beep(100);
        return;
      }

      const char* t = doc["type"];
      if (!t) {
        DynamicJsonDocument err(256);
        err["type"] = "error";
        err["data"] = "Missing type";
        sendJSON(client, err);
        beep(100);
        return;
      }

      // Auth
      if (strcmp(t, "auth") == 0) {
        String tok = doc["token"];
        if (tok == adminPass) {
          authenticated.insert(client->id());
          DynamicJsonDocument ok(256);
          ok["type"] = "auth";
          ok["data"] = true;
          sendJSON(client, ok);
          sendSysInfo(client);
          return;
        } else {
          DynamicJsonDocument no(256);
          no["type"] = "auth";
          no["data"] = false;
          sendJSON(client, no);
          beep(150);
          return;
        }
      }

      // Require auth for everything else
      if (!authenticated.count(client->id())) {
        DynamicJsonDocument err(256);
        err["type"] = "error";
        err["data"] = "Not authenticated";
        sendJSON(client, err);
        beep(100);
        return;
      }

      if (strcmp(t, "cmd") == 0) {
        String cmd = doc["cmd"];
        processCmd(client, cmd);
      }
      else if (strcmp(t, "settings") == 0) {
        const char* action = doc["action"];
        if (action && strcmp(action, "get") == 0) {
          DynamicJsonDocument s(1024);
          s["type"] = "settings";
          JsonObject d = s.createNestedObject("data");
          d["ssid"]        = apSSID;
          d["password"]    = apPassword;
          d["adminPass"]   = adminPass;
          d["buzzerGPIO"]  = buzzerPin;
          d["channel"]     = apChannel;
          d["staSSID"]     = staSSID;
          d["staPassword"] = staPassword;
          d["ntpServer"]   = ntpServer;
          d["ntpOffset"]   = ntpOffset;
          d["staIP"]       = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
          d["staStatus"]   = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
          sendJSON(client, s);
        } else if (action && strcmp(action, "set") == 0) {
          JsonObject val = doc["value"].as<JsonObject>();
          if (val) {
            String newSSID      = val["ssid"] ? sanitize(val["ssid"].as<String>()) : apSSID;
            String newPassword  = val["password"] ? sanitize(val["password"].as<String>()) : apPassword;
            String newAdmin     = val["adminPass"] ? sanitize(val["adminPass"].as<String>()) : adminPass;
            int newBuzzer       = val["buzzerGPIO"] ? val["buzzerGPIO"].as<int>() : buzzerPin;
            int newChannel      = val["channel"] ? val["channel"].as<int>() : apChannel;
            String newStaSSID   = val["staSSID"] ? sanitize(val["staSSID"].as<String>()) : staSSID;
            String newStaPass   = val["staPassword"] ? sanitize(val["staPassword"].as<String>()) : staPassword;
            String newNtpServer = val["ntpServer"] ? sanitize(val["ntpServer"].as<String>()) : ntpServer;
            int newNtpOffset    = val["ntpOffset"] ? val["ntpOffset"].as<int>() : ntpOffset;

            if (newSSID.length() == 0) {
              DynamicJsonDocument e(256);
              e["type"] = "error";
              e["data"] = "AP SSID cannot be empty";
              sendJSON(client, e);
              return;
            }
            if (newPassword.length() < 8 || newPassword.length() > 63) {
              DynamicJsonDocument e(256);
              e["type"] = "error";
              e["data"] = "AP password must be 8-63 chars";
              sendJSON(client, e);
              return;
            }
            if (newAdmin.length() == 0) {
              DynamicJsonDocument e(256);
              e["type"] = "error";
              e["data"] = "Admin password cannot be empty";
              sendJSON(client, e);
              return;
            }
            if (newBuzzer < -1 || newBuzzer > 39) {
              DynamicJsonDocument e(256);
              e["type"] = "error";
              e["data"] = "Buzzer GPIO must be -1 to 39";
              sendJSON(client, e);
              return;
            }
            if (newChannel < 1 || newChannel > 13) {
              DynamicJsonDocument e(256);
              e["type"] = "error";
              e["data"] = "AP channel must be 1-13";
              sendJSON(client, e);
              return;
            }
            if (newNtpOffset < -12 || newNtpOffset > 14) {
              DynamicJsonDocument e(256);
              e["type"] = "error";
              e["data"] = "NTP offset must be between -12 and +14 hours";
              sendJSON(client, e);
              return;
            }

            bool apChanged = (newSSID != apSSID || newPassword != apPassword || newChannel != apChannel);
            apSSID      = newSSID;
            apPassword  = newPassword;
            adminPass   = "admin";          // keep the web auth token predictable and stable
            buzzerPin   = newBuzzer;
            apChannel   = newChannel;
            staSSID     = newStaSSID;
            staPassword = newStaPass;
            ntpServer   = newNtpServer;
            ntpOffset   = newNtpOffset;
            saveSettings();

            if (staSSID.length() > 0) connectSTA();
            else WiFi.disconnect();
            configTime(ntpOffset * 3600L, 0, ntpServer.c_str());

            DynamicJsonDocument s(1024);
            s["type"] = "settings";
            JsonObject d = s.createNestedObject("data");
            d["ssid"]        = apSSID;
            d["password"]    = apPassword;
            d["adminPass"]   = adminPass;
            d["buzzerGPIO"]  = buzzerPin;
            d["channel"]     = apChannel;
            d["staSSID"]     = staSSID;
            d["staPassword"] = staPassword;
            d["ntpServer"]   = ntpServer;
            d["ntpOffset"]   = ntpOffset;
            d["staIP"]       = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
            d["staStatus"]   = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
            d["rebootRequired"] = apChanged;
            sendJSON(client, s);

            DynamicJsonDocument term(256);
            term["type"] = "terminal";
            term["data"] = apChanged ? "Settings saved. Reboot to apply AP/SSID/channel changes." : "Settings saved. STA/NTP changes are active.";
            sendJSON(client, term);
          }
        }
      }
      else if (strcmp(t, "scanner") == 0) {
        const char* a = doc["action"];
        if (a && strcmp(a, "subscribe") == 0) scannerSubs.insert(client->id());
        else if (a && strcmp(a, "unsubscribe") == 0) scannerSubs.erase(client->id());
        // kick a scan immediately
        startWiFiScan();
      }
      else if (strcmp(t, "portal") == 0) {
        const char* a = doc["action"];
        if (a && strcmp(a, "save") == 0) {
          String html = doc["html"].as<String>();
          if (sdReady) {
            File f = SD.open("/portal.html", FILE_WRITE);
            if (f) {
              f.print(html);
              f.close();
              DynamicJsonDocument ok(256);
              ok["type"] = "terminal";
              ok["data"] = "Saved /portal.html";
              sendJSON(client, ok);
            } else {
              DynamicJsonDocument e(256);
              e["type"] = "error";
              e["data"] = "Cannot write /portal.html";
              sendJSON(client, e);
            }
          } else {
            DynamicJsonDocument e(256);
            e["type"] = "error";
            e["data"] = "SD not ready";
            sendJSON(client, e);
          }
        }
      }
    }
  }
}

// --- SD card init / re-init ---
static bool spiStarted = false;
void initSD() {
  if (sdReady) return;
  disableLoopWDT();
  if (!spiStarted) {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    spiStarted = true;
    delay(100);
  }
  for (int attempt = 0; attempt < 8; attempt++) {
    SD.end();
    delay(20);
    sdReady = SD.begin(SD_CS, SPI, 1000000);
    if (sdReady) break;
    Serial.println("SD retry " + String(attempt + 1));
    delay(500);
    yield();
  }
  enableLoopWDT();
  if (sdReady) {
    Serial.println("SD card ready");
    loadSettings();
    saveSettings();
    restoreAP();
    connectSTA();
    configTime(ntpOffset * 3600L, 0, ntpServer.c_str());
  }
}

// --- Serial file upload from the same USB cable ---
void handleSerialUpload() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      line.trim();
      if (line.startsWith("__ICY_UPLOAD__ ")) {
        // Format: __ICY_UPLOAD__ <size> <path>
        int s1 = line.indexOf(' ');
        int s2 = line.indexOf(' ', s1 + 1);
        if (s1 > 0 && s2 > s1) {
          String sizeStr = line.substring(s1 + 1, s2);
          String path = line.substring(s2 + 1);
          long fsize = sizeStr.toInt();
          if (path.startsWith("/")) path = path.substring(1);
          if (fsize > 0 && fsize < 4194304L && path.indexOf("..") < 0) {
            if (!sdReady) {
              Serial.println("FAIL " + path + " SD not ready");
            } else {
              String target = "/" + path;
              int slash = target.lastIndexOf('/');
              if (slash > 0) SD.mkdir(target.substring(0, slash));
              File f = SD.open(target, FILE_WRITE);
              if (f) {
                Serial.println("READY " + path);
                Serial.setTimeout(30000);
                uint8_t buf[512];
                long remaining = fsize;
                bool ok = true;
                disableLoopWDT();
                while (remaining > 0) {
                  size_t toRead = (remaining > (long)sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
                  size_t got = Serial.readBytes(buf, toRead);
                  if (got == 0) { ok = false; break; }
                  if (f.write(buf, got) != got) { ok = false; break; }
                  remaining -= got;
                  yield();
                }
                enableLoopWDT();
                f.close();
                if (ok) Serial.println("OK " + path);
                else     Serial.println("FAIL " + path + " write/timeout");
              } else {
                Serial.println("FAIL " + path + " cannot open");
              }
            }
          } else {
            Serial.println("FAIL bad request");
          }
        } else {
          Serial.println("FAIL bad request");
        }
      } else if (line.startsWith("__ICY_LISTSD__")) {
        Serial.println(sdReady ? "SD ready" : "SD not ready");
      } else if (line.startsWith("__ICY_INITSD__")) {
        initSD();
      } else if (line.length() > 0) {
        // ignore unknown serial lines (could be noise)
      }
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[ICY] Booting...");

  // Buzzer PWM
  Serial.println("[ICY] Buzzer init");
  ledcSetup(0, 2000, 8);
  ledcAttachPin(buzzerPin, 0);
  ledcWriteTone(0, 0);

  // Bring up Wi-Fi AP first so the SSID is visible even if SD init hangs
  Serial.println("[ICY] WiFi init");
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  restoreAP();
  delay(100);

  Serial.print("[ICY] AP IP: ");
  Serial.println(WiFi.softAPIP());

  // SD card on VSPI
  initSD();
  if (!sdReady) Serial.println("SD card init failed");

  // Root handler: serve portal (if active), index.html, or the setup page
  const char* setupHtml = R"ICYUPLOAD(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Icy OS - Setup</title>
  <style>
    *{box-sizing:border-box}
    body{margin:0;background:#050505;color:#e0e6ed;font-family:'Segoe UI',Roboto,sans-serif;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px}
    .box{width:100%;max-width:520px;background:#171a21;border:1px solid #2b3039;border-radius:16px;padding:28px;box-shadow:0 12px 40px rgba(0,0,0,0.6)}
    h1{margin:0 0 6px;color:#36d13c;font-size:26px} .sub{color:#9aa3ad;font-size:13px;margin-bottom:18px}
    .field{display:flex;align-items:center;gap:10px;margin:12px 0}
    label{min-width:90px;font-size:13px;color:#9aa3ad}
    input[type=password], input[type=file]{flex:1;background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:8px 10px;border-radius:6px}
    #drop{background:#0f1115;border:2px dashed #2b3039;border-radius:10px;padding:24px;text-align:center;margin:14px 0;transition:.2s}
    #drop.drag{border-color:#36d13c;background:#101910}
    #list{max-height:200px;overflow:auto;margin:14px 0;border:1px solid #2b3039;border-radius:8px;background:#0f1115}
    .row{display:flex;align-items:center;justify-content:space-between;padding:8px 12px;border-bottom:1px solid #1b1f25;font-size:13px}
    .row:last-child{border-bottom:none}
    .name{flex:1;word-break:break-all}
    .ok{color:#36d13c} .err{color:#ff6b6b} .wait{color:#9aa3ad}
    button{background:#36d13c;color:#050505;border:none;padding:10px 18px;border-radius:8px;cursor:pointer;font-weight:600;transition:.15s}
    button:hover{background:#4eea5a}
    button:disabled{background:#2b3039;color:#9aa3ad;cursor:not-allowed}
    .actions{display:flex;gap:10px;margin-top:14px}
    .note{font-size:12px;color:#9aa3ad;margin-top:12px;line-height:1.5}
    #open{display:none;background:#2b3039;color:#e0e6ed}
  </style>
</head>
<body>
  <div class="box">
    <h1>Icy OS Setup</h1>
    <p class="sub">Upload the OS files to the SD card to start the Web OS.</p>

    <div class="field"><label>Admin password</label><input type="password" id="tok" value="admin"></div>

    <div id="drop">
      <div>Drag & drop files here or click to select</div>
      <input type="file" id="file" multiple style="display:none">
    </div>

    <div id="list"></div>

    <div class="actions">
      <button id="up" onclick="uploadAll()" disabled>Upload all files</button>
      <button id="open" onclick="location.href='/'">Open Icy OS</button>
      <button id="wipe" onclick="wipeCache()" style="background:#ff6b6b">Wipe browser cache</button>
    </div>

    <p class="note"><b>Still seeing the old UI / games?</b> Click <b>Wipe browser cache</b>, then upload the 7 files.<br>Required: index.html, style.css, ui.js, service-worker.js, manifest.json, favicon.svg, wallpaper.jpg.<br>You can select all 7 at once. If a file fails, the token may be wrong or the SD card is not writable.</p>
  </div>

  <script>
    const files = [];
    const drop = document.getElementById('drop');
    const input = document.getElementById('file');
    const list = document.getElementById('list');
    const upBtn = document.getElementById('up');

    async function wipeCache() {
      if ('caches' in window) {
        const keys = await caches.keys();
        await Promise.all(keys.map(k => caches.delete(k)));
      }
      if ('serviceWorker' in navigator) {
        const regs = await navigator.serviceWorker.getRegistrations();
        await Promise.all(regs.map(r => r.unregister()));
      }
      alert('Cache wiped. Reload this page and upload the new files.');
      location.reload();
    }

    drop.addEventListener('click', () => input.click());
    input.addEventListener('change', () => addFiles(input.files));
    drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('drag'); });
    drop.addEventListener('dragleave', () => drop.classList.remove('drag'));
    drop.addEventListener('drop', e => { e.preventDefault(); drop.classList.remove('drag'); addFiles(e.dataTransfer.files); });

    function addFiles(fileList) {
      for (const f of fileList) {
        if (!files.some(x => x.name === f.name)) {
          files.push(f);
          const d = document.createElement('div'); d.className = 'row'; d.id = 'row-' + f.name;
          d.innerHTML = '<span class="name">' + f.name + '</span><span class="wait" id="st-' + f.name + '">ready</span>';
          list.appendChild(d);
        }
      }
      upBtn.disabled = files.length === 0;
    }

    async function uploadAll() {
      const tok = document.getElementById('tok').value;
      upBtn.disabled = true;
      for (const f of files) {
        const st = document.getElementById('st-' + f.name);
        if (!st) continue;
        st.textContent = 'uploading...'; st.className = 'wait';
        const form = new FormData();
        form.append('file', f, f.name);
        try {
          const r = await fetch('/fs/upload?token=' + encodeURIComponent(tok), { method: 'POST', body: form });
          const t = await r.text();
          st.textContent = (r.ok ? 'OK: ' : 'ERR: ') + t;
          st.className = r.ok ? 'ok' : 'err';
        } catch (e) {
          st.textContent = 'ERR: ' + e;
          st.className = 'err';
        }
      }
      upBtn.disabled = false;
      document.getElementById('open').style.display = 'inline-block';
    }
  </script>
</body>
</html>
)ICYUPLOAD";

  server.on("/setup", HTTP_GET, [setupHtml](AsyncWebServerRequest* request) {
    request->send(200, "text/html", setupHtml);
  });

  // Hard reset of the web UI files (token required to prevent abuse)
  server.on("/resetui", HTTP_GET, [setupHtml](AsyncWebServerRequest* request) {
    if (!request->hasParam("token") || request->getParam("token")->value() != adminPass) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    if (sdReady) {
      SD.remove("/index.html");
      SD.remove("/service-worker.js");
    }
    String msg = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Icy OS - UI Reset</title>"
                 "<style>body{font-family:sans-serif;background:#050505;color:#e0e6ed;padding:20px;text-align:center}"
                 "a{color:#36d13c;font-size:18px}</style></head><body>"
                 "<h1>UI reset done</h1>"
                 "<p>Old index.html and service-worker.js removed from SD.</p>"
                 "<p>Wait a moment, then <a href='/setup'>click here to upload the new files</a>.</p>"
                 "<script>"
                 "navigator.serviceWorker.getRegistrations().then(regs => regs.forEach(r => r.unregister()));"
                 "setTimeout(()=>{ window.location.href='/setup'; }, 2000);"
                 "</script></body></html>";
    request->send(200, "text/html", msg);
  });

  server.on("/", HTTP_GET, [setupHtml](AsyncWebServerRequest* request) {
    if (request->hasParam("setup")) {
      request->send(200, "text/html", setupHtml);
      return;
    }
    if (portalRunning) {
      if (sdReady && SD.exists("/portal.html")) {
        request->send(SD, "/portal.html", "text/html");
      } else {
        request->send(200, "text/html", DEFAULT_PORTAL);
      }
      return;
    }
    if (sdReady && SD.exists("/index.html")) {
      request->send(SD, "/index.html", "text/html");
    } else if (!sdReady) {
      request->send(503, "text/html", "<h1>SD not ready</h1><p>Mount or replace the SD card and reset.</p>");
    } else {
      request->send(200, "text/html", setupHtml);
    }
  });

  server.on("/upload", HTTP_GET, [setupHtml](AsyncWebServerRequest* request) {
    request->send(200, "text/html", setupHtml);
  });

  // Raw file download / view endpoint
  server.on("/files", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("token") || request->getParam("token")->value() != adminPass) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    if (!sdReady) {
      request->send(503, "text/plain", "SD not ready");
      return;
    }
    String path = "/";
    if (request->hasParam("path")) path = urlDecode(request->getParam("path")->value());
    if (path.length() == 0) path = "/";
    if (!path.startsWith("/")) path = "/" + path;
    path = resolvePath(path);
    if (path.length() == 0) {
      request->send(403, "text/plain", "Forbidden");
      return;
    }
    File f = SD.open(path);
    bool ok = f && !f.isDirectory();
    f.close();
    if (!ok) {
      request->send(404, "text/plain", "Not found");
      return;
    }
    String filename = path.substring(path.lastIndexOf('/') + 1);
    String ct = "application/octet-stream";
    if (filename.endsWith(".html")) ct = "text/html";
    else if (filename.endsWith(".css")) ct = "text/css";
    else if (filename.endsWith(".js")) ct = "application/javascript";
    else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) ct = "image/jpeg";
    else if (filename.endsWith(".png")) ct = "image/png";
    else if (filename.endsWith(".txt") || filename.endsWith(".csv") || filename.endsWith(".json")) ct = "text/plain";
    request->send(SD, path, ct.c_str());
  });

  // Static web assets from SD (only if card is mounted)
  if (sdReady) {
    server.serveStatic("/", SD, "/").setDefaultFile("index.html");
  }

  // HTTP auth for token retrieval
  server.on("/auth", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("username", true) && request->hasParam("password", true)) {
      String u = request->getParam("username", true)->value();
      String p = request->getParam("password", true)->value();
      if (u == "admin" && p == adminPass) {
        DynamicJsonDocument doc(256);
        doc["ok"] = true;
        doc["token"] = adminPass;   // the token is the current admin password
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
      } else {
        request->send(401, "application/json", "{\"ok\":false}");
      }
    } else {
      request->send(400, "application/json", "{\"ok\":false}");
    }
  });

  // OTA upload endpoint (protected by token query param)
  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest* request){
      // Response is prepared here; actual work done in upload handler
    },
    [](AsyncWebServerRequest* request, const String& filename, size_t index,
       uint8_t* data, size_t len, bool final) {
      // Token check on first chunk
      if (index == 0) {
        if (!request->hasParam("token") || request->getParam("token")->value() != adminPass) {
          request->send(401, "text/plain", "Unauthorized");
          return;
        }
        size_t contentLen = request->contentLength();
        if (contentLen == 0 || contentLen > 0x150000) {
          request->send(400, "text/plain", "Invalid OTA size");
          return;
        }
        if (!Update.begin(contentLen)) {
          request->send(500, "text/plain", Update.errorString());
          return;
        }
      }
      if (Update.getError() == UPDATE_ERROR_OK && len) {
        if (Update.write(data, len) != len) {
          // keep going, error reported at end
        }
      }
      if (final) {
        if (Update.end(true)) {
          request->send(200, "text/plain", "Update OK. Rebooting...");
          delay(500);
          ESP.restart();
        } else {
          request->send(500, "text/plain", Update.errorString());
        }
      }
    }
  );

  // SD file upload endpoint (protected by token)
  server.on("/fs/upload", HTTP_POST,
    [](AsyncWebServerRequest* request){
      if (fsUploadSuccess) request->send(fsUploadCode, "text/plain", fsUploadResponse);
      else if (fsUploadCode == 401) request->send(401, "text/plain", fsUploadResponse);
      else request->send(fsUploadCode, "text/plain", fsUploadResponse);
    },
    [](AsyncWebServerRequest* request, const String& filename, size_t index,
       uint8_t* data, size_t len, bool final) {
      if (index == 0) {
        fsUploadSuccess = false;
        fsUploadResponse = "";
        fsUploadCode = 500;
        if (!request->hasParam("token") || request->getParam("token")->value() != adminPass) {
          fsUploadCode = 401;
          fsUploadResponse = "Unauthorized";
        } else if (!sdReady) {
          fsUploadCode = 503;
          fsUploadResponse = "SD not ready";
        } else {
          String target = filename;
          target.replace('\\', '/');
          int slash = target.lastIndexOf('/');
          if (slash >= 0) target = target.substring(slash + 1);
          if (target.length() == 0 || target == "") {
            fsUploadCode = 400;
            fsUploadResponse = "Bad filename";
          } else {
            if (!target.startsWith("/")) target = "/" + target;
            // Keep only safe chars
            String safe = "";
            for (size_t i = 0; i < target.length(); i++) {
              char c = target[i];
              if (c == '/' || isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') safe += c;
              else safe += '_';
            }
            target = safe;
            fsUploadFile = SD.open(target, FILE_WRITE);
            if (!fsUploadFile) {
              fsUploadCode = 500;
              fsUploadResponse = "Cannot open file for writing";
            }
          }
        }
      }
      if (fsUploadFile && len) {
        if (fsUploadFile.write(data, len) != len) {
          // write failed part way
        }
      }
      if (final) {
        if (fsUploadFile) {
          fsUploadFile.close();
          fsUploadSuccess = true;
          fsUploadCode = 200;
          fsUploadResponse = "Saved " + filename;
        } else if (fsUploadCode == 200) {
          fsUploadCode = 500;
          fsUploadResponse = "Save failed";
        }
      }
    }
  );

  // Captive portal POST logger
  server.on("/post", HTTP_POST, [](AsyncWebServerRequest* request) {
    uint8_t mac[6];
    String macStr = "00:00:00:00:00:00";
    if (sdReady && request->hasHeader("User-Agent")) {
      // user agent could be logged if desired
    }
    if (WiFi.softAPgetStationNum() > 0) {
      // Best effort: get MAC of the requestor not available in AsyncWebServer; use client IP
    }
    IPAddress ip = request->client()->remoteIP();
    if (sdReady) {
      File log = SD.open("/portal_log.csv", FILE_APPEND);
      if (log) {
        if (log.size() == 0) log.println("ts,ip,params");
        String line = String((millis() - bootMs) / 1000) + "," + ip.toString() + ",";
        int n = request->params();
        for (int i = 0; i < n; i++) {
          AsyncWebParameter* pp = request->getParam(i);
          if (pp) {
            if (i > 0) line += ";";
            line += sanitize(pp->name()) + "=" + sanitize(pp->value());
          }
        }
        log.println(line);
        log.close();
      }
    }
    request->send(200, "text/html", PORTAL_OK);
  });

  // Favicon fallback (browsers always ask for favicon.ico)
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (sdReady && SD.exists("/favicon.svg")) {
      request->send(SD, "/favicon.svg", "image/svg+xml");
    } else {
      request->send(204);
    }
  });

  // Wallpaper endpoint: serve if present, otherwise 204 to avoid 404 noise
  server.on("/wallpaper.jpg", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (sdReady && SD.exists("/wallpaper.jpg")) {
      request->send(SD, "/wallpaper.jpg", "image/jpeg");
    } else {
      request->send(204);
    }
  });

  // Public static UI assets (SD root); no token needed
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (sdReady && SD.exists("/style.css")) {
      request->send(SD, "/style.css", "text/css");
    } else {
      request->send(404, "text/plain", "style.css missing");
    }
  });

  server.on("/ui.js", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (sdReady && SD.exists("/ui.js")) {
      request->send(SD, "/ui.js", "application/javascript");
    } else {
      request->send(404, "text/plain", "ui.js missing");
    }
  });

  server.on("/service-worker.js", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (sdReady && SD.exists("/service-worker.js")) {
      request->send(SD, "/service-worker.js", "application/javascript");
    } else {
      request->send(404, "text/plain", "service-worker.js missing");
    }
  });

  server.on("/manifest.json", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (sdReady && SD.exists("/manifest.json")) {
      request->send(SD, "/manifest.json", "application/json");
    } else {
      request->send(404, "text/plain", "manifest.json missing");
    }
  });

  server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (sdReady && SD.exists("/favicon.svg")) {
      request->send(SD, "/favicon.svg", "image/svg+xml");
    } else {
      request->send(204);
    }
  });

  // No-cache for all dynamic assets so the OS UI updates after SD refresh
  DefaultHeaders::Instance().addHeader("Cache-Control", "no-cache, no-store, must-revalidate");

  // WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.onNotFound([](AsyncWebServerRequest* request) {
    if (portalRunning) {
      request->redirect("/");
    } else {
      request->send(404, "text/plain", "Not found");
    }
  });

  server.begin();

  Serial.println("__ICY_OS_READY__");

  initialHeap = ESP.getFreeHeap();
  bootMs = millis();

  // Boot beep
  beep(100);
}

// --- Main loop ---
void loop() {
  // Serial cable file upload (one command does firmware + SD)
  handleSerialUpload();

  // Heartbeat marker so one-command installers can see the device is alive
  static unsigned long lastReadyMarker = 0;
  if (millis() - lastReadyMarker > 5000) {
    Serial.println("__ICY_OS_READY__");
    lastReadyMarker = millis();
  }

  // Clean up closed sockets
  static unsigned long lastClean = 0;
  if (millis() - lastClean > 5000) {
    ws.cleanupClients();
    lastClean = millis();
  }

  // Sysinfo broadcast every 1s
  if (millis() - lastSysInfo > 1000) {
    sendSysInfo();
    lastSysInfo = millis();
  }

  // Wi-Fi async scan completion
  if (scanPending) {
    finishWiFiScan();
  }

  // STA connect watchdog: give up after 30s so AP stays stable
  if (staConnectStart > 0) {
    if (WiFi.status() == WL_CONNECTED) {
      staConnectStart = 0;
      restoreAP();
    } else if (millis() - staConnectStart > 30000) {
      WiFi.disconnect();
      restoreAP();
      staConnectStart = 0;
      Serial.println("STA connect timeout, keeping AP only");
    }
  }

  // Attack engine tick
  wifiAttack.update(millis());

  // Sniffer auto-stop
  if (sniffing && millis() - sniffStart > sniffDuration) {
    stopSniff();
  }

  // Try to re-init the SD card every 10 s if it failed at boot
  static unsigned long lastSDTry = 0;
  if (!sdReady && millis() - lastSDTry > 10000) {
    lastSDTry = millis();
    initSD();
  }

  delay(10);
}
