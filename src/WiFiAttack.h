#ifndef WIFI_ATTACK_H
#define WIFI_ATTACK_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <vector>

struct AttackTarget {
  String ssid;
  String bssid;
  int rssi;
  uint8_t channel;
};

class WiFiAttack {
public:
  WiFiAttack();

  void stop();
  bool isRunning() const { return running; }
  uint8_t getMode() const { return attackMode; }
  const char* getModeName() const;
  uint32_t getPacketCount() const { return packetCount; }
  uint8_t getChannel() const { return channel; }
  uint32_t getTxInterval() const { return txInterval; }

  // Parse a full "attack -t <type> ..." command and start the matching attack.
  // Returns a status message for the terminal.
  String runCommand(const String& raw);

  void setChannel(uint8_t c) { channel = c; }
  void setSsidList(const std::vector<String>& list) { ssidList = list; }
  void setTargets(const std::vector<AttackTarget>& t) { targets = t; }
  void setPassword(const String& p) { password = p; }
  void setHidden(bool h) { hidden = h; }
  void setBssid(const String& b) { customBssid = b; }
  void setHopping(bool h) { hopping = h; }
  void setTxInterval(uint32_t ms) { txInterval = ms; }
  void setHopInterval(uint32_t ms) { hopInterval = ms; }
  void setSsidCycleInterval(uint32_t ms) { ssidCycleInterval = ms; }

  void update(uint32_t now);

  // Individual attack starters
  void startDeauth(uint8_t targetIdx = 0xFF, const String& bssid = "");
  void startDeauthAll();
  void startBeacon(const std::vector<String>& ssids, bool cycle = false);
  void startProbe(const std::vector<String>& ssids, bool flood = false);
  void startSsidSpam(const std::vector<String>& ssids);
  void startFakeAp(const std::vector<String>& ssids, const String& pass = "", bool hid = false);
  void startKarma();
  void startRandomSsid(int count = 8);
  void startAuthFlood(const String& bssid);
  void startAssocFlood(const String& bssid);
  void startPmkid(const String& bssid);
  void startSae(const String& bssid);
  void startCsa(const String& bssid, uint8_t newChannel);
  void startQuiet(const String& bssid, uint8_t duration = 30, uint8_t period = 0);
  void startBadMsg(const String& bssid);
  void startSleep(const String& bssid);

  static const uint8_t MODE_NONE = 0;
  static const uint8_t MODE_DEAUTH = 1;
  static const uint8_t MODE_DEAUTH_ALL = 2;
  static const uint8_t MODE_BEACON = 3;
  static const uint8_t MODE_PROBE = 4;
  static const uint8_t MODE_SSID_SPAM = 5;
  static const uint8_t MODE_FAKE_AP = 6;
  static const uint8_t MODE_KARMA = 7;
  static const uint8_t MODE_RANDOM_SSID = 8;
  static const uint8_t MODE_AUTH_FLOOD = 9;
  static const uint8_t MODE_ASSOC_FLOOD = 10;
  static const uint8_t MODE_PMKID = 11;
  static const uint8_t MODE_SAE = 12;
  static const uint8_t MODE_CSA = 13;
  static const uint8_t MODE_QUIET = 14;
  static const uint8_t MODE_BADMSG = 15;
  static const uint8_t MODE_SLEEP = 16;

private:
  bool running;
  uint8_t attackMode;
  uint8_t channel;
  uint8_t ssidIndex;
  bool hopping;
  bool cycling;
  int randomSsidCount;

  uint32_t lastTx;
  uint32_t lastHop;
  uint32_t lastSsidCycle;
  uint32_t lastStatus;
  uint32_t txInterval;
  uint32_t hopInterval;
  uint32_t ssidCycleInterval;
  uint32_t packetCount;
  uint16_t seqNum;

  String password;
  bool hidden;
  String customBssid;
  std::vector<String> ssidList;
  std::vector<AttackTarget> targets;

  void baseSetup(uint8_t mode);
  void broadcastStatus(uint32_t now);
  void doTx(uint32_t now);
  void channelHop(uint32_t now);
  void cycleSsid(uint32_t now);

  // Utilities
  void macFromString(const String& s, uint8_t* out) const;
  void randomMac(uint8_t* out) const;
  void bssidMac(uint8_t* out) const;

  // Frame transmitters
  void txDeauth(const uint8_t* bssid, const uint8_t* client, uint8_t reason) const;
  void txBeacon(const String& ssid, const uint8_t* bssid, uint8_t ch) const;
  void txProbeReq(const String& ssid, const uint8_t* sa) const;
  void txProbeResp(const String& ssid, const uint8_t* bssid, const uint8_t* client, uint8_t ch) const;
  void txAuth(const uint8_t* bssid, const uint8_t* client) const;
  void txAssocReq(const uint8_t* bssid, const uint8_t* client) const;
  void txActionCsa(const uint8_t* bssid, uint8_t newChannel) const;
  void txActionQuiet(const uint8_t* bssid, uint8_t duration, uint8_t period) const;
};

#endif
