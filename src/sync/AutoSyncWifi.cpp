#include "AutoSyncWifi.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "WifiCredentialStore.h"

namespace autosync {
namespace {

constexpr uint32_t POLL_INTERVAL_MS = 100;
constexpr uint32_t PER_NETWORK_TIMEOUT_MS = 8000;
constexpr uint32_t FAST_JOIN_TIMEOUT_MS = 4000;
constexpr uint32_t MIN_JOIN_BUDGET_MS = 200;
constexpr uint32_t SCAN_TIMEOUT_MS = 6000;
constexpr uint32_t LAST_JOIN_MAGIC = 0x4A4F494E;

// Where the access point was found last time. A known channel and BSSID let WiFi.begin skip its own
// all-channel scan, the slowest part of a join.
struct JoinHint {
  bool valid = false;
  int32_t channel = 0;
  uint8_t bssid[6] = {};
};

// Survives deep sleep and software resets, not power loss; the magic tells a kept value from noise.
struct LastJoin {
  uint32_t magic;
  char ssid[33];
  int32_t channel;
  uint8_t bssid[6];
};
RTC_NOINIT_ATTR LastJoin lastJoin;

struct VisibleNetwork {
  std::string ssid;
  JoinHint hint;
};

struct ScanResult {
  bool completed = false;
  bool sawHiddenNetwork = false;
  std::vector<VisibleNetwork> networks;
};

struct Candidate {
  WifiCredential cred;
  JoinHint hint;
};

JoinHint lastJoinHintFor(const std::string& ssid) {
  JoinHint hint;
  if (lastJoin.magic != LAST_JOIN_MAGIC) return hint;
  if (strncmp(lastJoin.ssid, ssid.c_str(), sizeof(lastJoin.ssid)) != 0) return hint;
  if (lastJoin.channel <= 0) return hint;

  hint.valid = true;
  hint.channel = lastJoin.channel;
  memcpy(hint.bssid, lastJoin.bssid, sizeof(hint.bssid));
  return hint;
}

void rememberJoin(const std::string& ssid) {
  const int32_t channel = WiFi.channel();
  if (channel <= 0 || ssid.size() >= sizeof(lastJoin.ssid)) {
    lastJoin.magic = 0;
    return;
  }
  memset(&lastJoin, 0, sizeof(lastJoin));
  memcpy(lastJoin.ssid, ssid.c_str(), ssid.size());
  lastJoin.channel = channel;
  WiFi.BSSID(lastJoin.bssid);
  lastJoin.magic = LAST_JOIN_MAGIC;
}

ScanResult scanVisibleNetworks(const Deadline& deadline) {
  ScanResult scan;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(POLL_INTERVAL_MS);

  const uint32_t scanStart = millis();
  WiFi.scanNetworks(true);
  int16_t found = WiFi.scanComplete();
  while (found == WIFI_SCAN_RUNNING) {
    if (deadline.expired(millis()) || millis() - scanStart >= SCAN_TIMEOUT_MS) {
      break;
    }
    delay(POLL_INTERVAL_MS);
    found = WiFi.scanComplete();
  }

  scan.completed = found >= 0;
  for (int i = 0; i < found; i++) {
    VisibleNetwork network;
    network.ssid = WiFi.SSID(i).c_str();
    if (network.ssid.empty()) {
      scan.sawHiddenNetwork = true;
      continue;
    }
#ifndef SIMULATOR
    network.hint.valid = true;
    network.hint.channel = WiFi.channel(i);
    memcpy(network.hint.bssid, WiFi.BSSID(i), sizeof(network.hint.bssid));
#endif
    // Results come strongest first; keep that access point for an SSID seen more than once.
    const bool seen = std::any_of(scan.networks.begin(), scan.networks.end(), [&](const VisibleNetwork& n) {
      return n.ssid == network.ssid;
    });
    if (!seen) {
      scan.networks.push_back(std::move(network));
    }
  }
  WiFi.scanDelete();
  LOG_INF("AUTOSYNC", "Wi-Fi scan: %d networks in %u ms", static_cast<int>(found),
          static_cast<unsigned>(millis() - scanStart));
  return scan;
}

const VisibleNetwork* findVisible(const ScanResult& scan, const std::string& ssid) {
  const auto it = std::find_if(scan.networks.begin(), scan.networks.end(), [&](const VisibleNetwork& n) {
    return n.ssid == ssid;
  });
  return it == scan.networks.end() ? nullptr : &*it;
}

// Saved networks the scan saw, last-connected first. When none is visible, the last-connected one is still
// tried directly if it may be hidden: the scan failed, or it reported a network without a name.
std::vector<Candidate> joinCandidates(const ScanResult& scan) {
  std::vector<Candidate> candidates;
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const auto last = WIFI_STORE.findCredential(lastSsid);
  if (last) {
    if (const auto* visible = findVisible(scan, last->ssid)) {
      candidates.push_back({*last, visible->hint});
    }
  }
  const size_t count = WIFI_STORE.getCredentialCount();
  for (size_t i = 0; i < count; i++) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred || cred->ssid == lastSsid) {
      continue;
    }
    if (const auto* visible = findVisible(scan, cred->ssid)) {
      candidates.push_back({*cred, visible->hint});
    }
  }
  if (!candidates.empty() || !last) return candidates;

  if (!scan.completed || scan.sawHiddenNetwork) {
    candidates.push_back({*last, JoinHint{}});
  }
  return candidates;
}

void startJoin(const WifiCredential& cred, const JoinHint& hint) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(POLL_INTERVAL_MS);

  WiFi.setScanMethod(hint.valid ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    char hostname[sizeof("CrossPoint-Reader-") + 12];
    snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    WiFi.setHostname(hostname);
  }

  const char* password = cred.password.empty() ? nullptr : cred.password.c_str();
#ifdef SIMULATOR
  WiFi.begin(cred.ssid.c_str(), password);
#else
  if (hint.valid) {
    WiFi.begin(cred.ssid.c_str(), password, hint.channel, hint.bssid);
  } else {
    WiFi.begin(cred.ssid.c_str(), password);
  }
#endif
}

bool tryJoin(const Candidate& candidate, const Deadline& deadline, const uint32_t timeoutMs, const char* how) {
  if (deadline.remaining(millis()) < MIN_JOIN_BUDGET_MS) return false;

  startJoin(candidate.cred, candidate.hint);
  const uint32_t attemptStart = millis();
  while (WiFi.status() != WL_CONNECTED && !deadline.expired(millis()) && millis() - attemptStart < timeoutMs) {
    delay(POLL_INTERVAL_MS);
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOG_DBG("AUTOSYNC", "Wi-Fi join (%s) failed: %s", how, candidate.cred.ssid.c_str());
    return false;
  }
  LOG_INF("AUTOSYNC", "Wi-Fi joined (%s) in %u ms", how, static_cast<unsigned>(millis() - attemptStart));
  WIFI_STORE.setLastConnectedSsid(candidate.cred.ssid);
  rememberJoin(candidate.cred.ssid);
  return true;
}

}  // namespace

bool connectSavedWifi(const Deadline& deadline) {
  if (WiFi.status() == WL_CONNECTED) return true;

  // The store is otherwise loaded only when WifiSelectionActivity opens.
  WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCredentialCount() == 0) {
    LOG_DBG("AUTOSYNC", "No saved Wi-Fi networks");
    return false;
  }

  // At home the access point is usually where it was: join it directly and only scan when that fails.
  if (const auto last = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid())) {
    const JoinHint hint = lastJoinHintFor(last->ssid);
    if (hint.valid && tryJoin({*last, hint}, deadline, FAST_JOIN_TIMEOUT_MS, "direct")) {
      return true;
    }
  }

  const auto candidates = joinCandidates(scanVisibleNetworks(deadline));
  for (const auto& candidate : candidates) {
    if (tryJoin(candidate, deadline, PER_NETWORK_TIMEOUT_MS, "after scan")) {
      return true;
    }
  }

  if (candidates.empty()) {
    LOG_INF("AUTOSYNC", "No saved network in range");
  }
  shutdownWifi();
  return false;
}

void shutdownWifi() {
  if (WiFi.getMode() == WIFI_MODE_NULL) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace autosync
