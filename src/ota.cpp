#include "ota.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>

#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// ESP-IDF's bundle of root certificates, built into the SDK. Declared here
// because the Arduino core ships a header of the same name that hides it.
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

// Tells the Arduino core not to accept a new firmware the moment it starts;
// confirm_running_firmware() does that once a fish has been drawn.
extern "C" bool verifyRollbackLater() { return true; }

namespace ota {

static const char* const PREFS = "wifi";

bool wifi_configured() {
  Preferences p;
  if (!p.begin(PREFS, true)) return false;
  bool ok = p.getString("ssid", "").length() > 0;
  p.end();
  return ok;
}

void save_wifi(const char* ssid, const char* password) {
  Preferences p;
  if (!p.begin(PREFS, false)) return;
  if (p.getString("ssid", "") != ssid) p.putString("ssid", ssid);
  if (p.getString("pass", "") != password) p.putString("pass", password);
  p.end();
}

void seed_wifi_from_build() {
  if (WIFI_SSID[0]) save_wifi(WIFI_SSID, WIFI_PASSWORD);
}

void confirm_running_firmware() { esp_ota_mark_app_valid_cancel_rollback(); }

const char* describe(Result r) {
  switch (r) {
    case NOT_CONFIGURED: return "no Wi-Fi settings saved, updates are off";
    case NO_WIFI: return "could not join Wi-Fi";
    case CHECK_FAILED: return "could not look up the latest release";
    case UP_TO_DATE: return "firmware is up to date";
    case UPDATE_FAILED: return "a newer release exists but installing it failed";
    case UPDATED: return "new firmware installed";
  }
  return "?";
}

// Reads "v1.2.3" (anything after the third number is ignored).
static bool parse_version(const char* s, int out[3]) {
  if (*s == 'v' || *s == 'V') s++;
  return sscanf(s, "%d.%d.%d", &out[0], &out[1], &out[2]) == 3;
}

static bool is_newer(const char* candidate, const char* current) {
  int a[3], b[3];
  if (!parse_version(candidate, a) || !parse_version(current, b)) return false;
  for (int i = 0; i < 3; i++) {
    if (a[i] != b[i]) return a[i] > b[i];
  }
  return false;
}

// github.com/<repo>/releases/latest answers with a redirect to
// .../releases/tag/<tag>. That one header is all we need, so the request is a
// HEAD and the redirect is not followed.
static String g_location;

static esp_err_t on_http_event(esp_http_client_event_t* evt) {
  if (evt->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(evt->header_key, "location") == 0) {
    g_location = evt->header_value;
  }
  return ESP_OK;
}

static bool latest_release_tag(String& tag) {
  g_location = "";
  esp_http_client_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.url = "https://github.com/" OTA_REPO "/releases/latest";
  cfg.method = HTTP_METHOD_HEAD;
  cfg.disable_auto_redirect = true;
  cfg.event_handler = on_http_event;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;  // verify the server against the built-in root certificates
  cfg.timeout_ms = 15000;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 2048;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return false;
  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  // With redirects not followed, this SDK reports the redirect itself as
  // ESP_ERR_HTTP_MAX_REDIRECT. That is the answer we came for.
  if (err != ESP_OK && err != ESP_ERR_HTTP_MAX_REDIRECT) {
    Serial.printf("release lookup failed: %s\n", esp_err_to_name(err));
    return false;
  }
  int at = g_location.indexOf("/releases/tag/");
  if (status / 100 != 3 || at < 0) {
    Serial.printf("release lookup: unexpected answer (HTTP %d)\n", status);
    return false;
  }
  tag = g_location.substring(at + 14);
  tag.trim();
  // A tag becomes part of a URL below; accept only what a version tag contains.
  if (!tag.length() || tag.length() > 32) return false;
  for (size_t i = 0; i < tag.length(); i++) {
    char c = tag[i];
    if (!(isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_')) return false;
  }
  return true;
}

static bool install(const String& tag) {
  String url = String("https://github.com/" OTA_REPO "/releases/download/") + tag + "/daily-fish-" + tag + ".bin";
  Serial.printf("downloading %s\n", url.c_str());
  esp_http_client_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.url = url.c_str();
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 20000;
  // GitHub redirects to a signed download address well over a thousand
  // characters long; both buffers must hold it.
  cfg.buffer_size = 8192;
  cfg.buffer_size_tx = 4096;
  cfg.keep_alive_enable = true;
  esp_err_t err = esp_https_ota(&cfg);
  if (err != ESP_OK) {
    Serial.printf("install failed: %s\n", esp_err_to_name(err));
    return false;
  }
  return true;
}

Result check_and_update(const char* current_version) {
  Preferences p;
  String ssid, pass;
  if (p.begin(PREFS, true)) {
    ssid = p.getString("ssid", "");
    pass = p.getString("pass", "");
    p.end();
  }
  if (!ssid.length()) return NOT_CONFIGURED;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) delay(100);

  Result res;
  if (WiFi.status() != WL_CONNECTED) {
    res = NO_WIFI;
  } else {
    String tag;
    if (!latest_release_tag(tag)) {
      res = CHECK_FAILED;
    } else {
      Serial.printf("running %s, latest release %s\n", current_version, tag.c_str());
      if (!is_newer(tag.c_str(), current_version)) {
        res = UP_TO_DATE;
      } else {
        res = install(tag) ? UPDATED : UPDATE_FAILED;
      }
    }
  }
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  return res;
}

}  // namespace ota
