#include "AutoSyncWifi.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "WifiCredentialStore.h"

namespace autosync {
namespace {

constexpr uint32_t POLL_INTERVAL_MS = 100;
constexpr uint32_t PER_NETWORK_TIMEOUT_MS = 8000;
constexpr uint32_t MIN_JOIN_BUDGET_MS = 200;
constexpr uint32_t SCAN_TIMEOUT_MS = 6000;

struct ScanResult {
  bool completed = false;
  bool sawHiddenNetwork = false;
  std::vector<std::string> ssids;
};

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
    std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) {
      scan.sawHiddenNetwork = true;
      continue;
    }
    scan.ssids.push_back(std::move(ssid));
  }
  WiFi.scanDelete();
  LOG_INF("AUTOSYNC", "Wi-Fi scan: %d networks in %u ms", static_cast<int>(found),
          static_cast<unsigned>(millis() - scanStart));
  return scan;
}

bool isVisible(const ScanResult& scan, const std::string& ssid) {
  return std::find(scan.ssids.begin(), scan.ssids.end(), ssid) != scan.ssids.end();
}

// Saved networks the scan saw, last-connected first. When none is visible, the last-connected one is still
// tried directly if it may be hidden: the scan failed, or it reported a network without a name.
std::vector<WifiCredential> joinCandidates(const ScanResult& scan) {
  std::vector<WifiCredential> candidates;
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const auto last = WIFI_STORE.findCredential(lastSsid);
  if (last && isVisible(scan, last->ssid)) {
    candidates.push_back(*last);
  }
  const size_t count = WIFI_STORE.getCredentialCount();
  for (size_t i = 0; i < count; i++) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred || cred->ssid == lastSsid || !isVisible(scan, cred->ssid)) {
      continue;
    }
    candidates.push_back(*cred);
  }
  if (!candidates.empty() || !last) return candidates;

  if (!scan.completed || scan.sawHiddenNetwork) {
    candidates.push_back(*last);
  }
  return candidates;
}

void startJoin(const WifiCredential& cred) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(POLL_INTERVAL_MS);

  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    char hostname[sizeof("CrossPoint-Reader-") + 12];
    snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    WiFi.setHostname(hostname);
  }

  if (cred.password.empty()) {
    WiFi.begin(cred.ssid.c_str());
  } else {
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
  }
}

}  // namespace

bool connectSavedWifi(const Deadline& deadline) {
  if (WiFi.status() == WL_CONNECTED) return true;

  // The store is otherwise loaded only when WifiSelectionActivity opens.
  WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCredentialCount() == 0) return false;

  const auto candidates = joinCandidates(scanVisibleNetworks(deadline));
  for (const auto& cred : candidates) {
    if (deadline.remaining(millis()) < MIN_JOIN_BUDGET_MS) {
      break;
    }

    startJoin(cred);
    const uint32_t attemptStart = millis();
    while (WiFi.status() != WL_CONNECTED && !deadline.expired(millis()) &&
           millis() - attemptStart < PER_NETWORK_TIMEOUT_MS) {
      delay(POLL_INTERVAL_MS);
    }

    if (WiFi.status() == WL_CONNECTED) {
      WIFI_STORE.setLastConnectedSsid(cred.ssid);
      return true;
    }
    LOG_DBG("AUTOSYNC", "Saved network join failed: %s", cred.ssid.c_str());
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
