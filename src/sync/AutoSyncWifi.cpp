#include "AutoSyncWifi.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <string>
#include <vector>

#include "WifiCredentialStore.h"

namespace autosync {
namespace {

constexpr uint32_t POLL_INTERVAL_MS = 100;
constexpr uint32_t PER_NETWORK_TIMEOUT_MS = 8000;
constexpr uint32_t MIN_JOIN_BUDGET_MS = 200;

std::vector<WifiCredential> orderedCredentials() {
  std::vector<WifiCredential> result;
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) {
    const auto last = WIFI_STORE.findCredential(lastSsid);
    if (last) {
      result.push_back(*last);
    }
  }
  const size_t count = WIFI_STORE.getCredentialCount();
  for (size_t i = 0; i < count; i++) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred || cred->ssid == lastSsid) {
      continue;
    }
    result.push_back(*cred);
  }
  return result;
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
  const auto credentials = orderedCredentials();
  if (credentials.empty()) return false;

  for (const auto& cred : credentials) {
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

  shutdownWifi();
  return false;
}

void shutdownWifi() {
  if (WiFi.getMode() == WIFI_MODE_NULL) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace autosync
