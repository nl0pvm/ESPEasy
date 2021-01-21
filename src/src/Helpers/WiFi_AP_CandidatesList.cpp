#include "../Helpers/WiFi_AP_CandidatesList.h"

#include "../ESPEasyCore/ESPEasy_Log.h"
#include "../Globals/RTC.h"
#include "../Globals/SecuritySettings.h"

WiFi_AP_CandidatesList::WiFi_AP_CandidatesList() {
  known_it = known.begin();
  load_knownCredentials();
}

void WiFi_AP_CandidatesList::load_knownCredentials() {
  mustLoadCredentials = false;
  known.clear();
  candidates.clear();
  addFromRTC();

  {
    // Add the known SSIDs
    String ssid, key;
    byte   index = 1; // Index 0 is the "unset" value

    while (get_SSID_key(index, ssid, key)) {
      known.emplace_back(index, ssid, key);
      ++index;
    }
  }
  known_it = known.begin();
  purge_unusable();
}

void WiFi_AP_CandidatesList::process_WiFiscan(uint8_t scancount) {
  if (mustLoadCredentials) { load_knownCredentials(); }
  candidates.clear();
  addFromRTC();

  known_it = known.begin();

  // Now try to merge the known SSIDs, or add a new one if it is a hidden SSID
  for (uint8_t i = 0; i < scancount; ++i) {
    add(i);
  }
  purge_unusable();
  candidates.sort();


  for (auto it = candidates.begin(); it != candidates.end(); ++it) {
    String log = F("WIFI  : Scan result: ");
    log += it->toString();
    addLog(LOG_LEVEL_INFO, log);
  }
}

bool WiFi_AP_CandidatesList::getNext() {
  if (candidates.empty()) { return false; }

  if (mustLoadCredentials) { load_knownCredentials(); }

  bool mustPop = true;

  currentCandidate = candidates.front();

  if (currentCandidate.isHidden) {
    // Iterate over the known credentials to try them all
    // Hidden SSID stations do not broadcast their SSID, so we must fill it in ourselves.
    if (known_it != known.end()) {
      currentCandidate.ssid  = known_it->ssid;
      currentCandidate.key   = known_it->key;
      currentCandidate.index = known_it->index;
      ++known_it;
    }

    if (known_it != known.end()) {
      mustPop = false;
    }
  }


  if (currentCandidate.usable()) {
    // Store in RTC
    RTC.lastWiFiChannel = currentCandidate.channel;

    for (byte i = 0; i < 6; ++i) {
      RTC.lastBSSID[i] = currentCandidate.bssid[i];
    }
    RTC.lastWiFiSettingsIndex = currentCandidate.index;
  }


  if (mustPop) {
    known_it = known.begin();
    candidates.pop_front();
  }
  return true;
}

const WiFi_AP_Candidate& WiFi_AP_CandidatesList::getCurrent() const {
  return currentCandidate;
}

WiFi_AP_Candidate WiFi_AP_CandidatesList::getBestScanResult() const {
  for (auto it = candidates.begin(); it != candidates.end(); ++it) {
    if (it->rssi < -1) { return *it; }
  }
  return WiFi_AP_Candidate();
}

bool WiFi_AP_CandidatesList::hasKnownCredentials() {
  if (mustLoadCredentials) { load_knownCredentials(); }
  return !known.empty();
}

void WiFi_AP_CandidatesList::add(uint8_t networkItem) {
  WiFi_AP_Candidate tmp(networkItem);

  if (tmp.isHidden) {
    candidates.push_back(tmp);
    return;
  }

  if (tmp.ssid.length() == 0) { return; }

  for (auto it = known.begin(); it != known.end(); ++it) {
    if (it->ssid.equals(tmp.ssid)) {
      tmp.key   = it->key;
      tmp.index = it->index;

      if (tmp.usable()) {
        candidates.push_back(tmp);

        // Do not return as we may have several AP's with the same SSID and different passwords.
      }
    }
  }
}

void WiFi_AP_CandidatesList::addFromRTC() {
  if (RTC.lastWiFiSettingsIndex == 0) { return; }

  String ssid, key;

  if (!get_SSID_key(RTC.lastWiFiSettingsIndex, ssid, key)) {
    return;
  }

  WiFi_AP_Candidate tmp(RTC.lastWiFiSettingsIndex, ssid, key);

  tmp.setBSSID(RTC.lastBSSID);
  tmp.channel = RTC.lastWiFiChannel;
  tmp.rssi    = -1; // Set to best possible RSSI so it is tried first.

  if (tmp.usable() && tmp.allowQuickConnect()) {
    currentCandidate = tmp;
  }
}

void WiFi_AP_CandidatesList::purge_unusable() {
  for (auto it = known.begin(); it != known.end();) {
    if (it->usable()) {
      ++it;
    } else {
      it = known.erase(it);
    }
  }

  for (auto it = candidates.begin(); it != candidates.end();) {
    if (it->usable()) {
      ++it;
    } else {
      it = candidates.erase(it);
    }
  }
}

bool WiFi_AP_CandidatesList::get_SSID_key(byte index, String& ssid, String& key) const {
  switch (index) {
    case 1:
      ssid = SecuritySettings.WifiSSID;
      key  = SecuritySettings.WifiKey;
      return true;
    case 2:
      ssid = SecuritySettings.WifiSSID2;
      key  = SecuritySettings.WifiKey2;
      return true;
  }

  // TODO TD-er: Read other credentials from extra file.
  return false;
}
