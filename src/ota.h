// Over-the-air updates from this project's GitHub releases, and the Wi-Fi
// settings they need.
#pragma once

#include <Arduino.h>

namespace ota {

// Wi-Fi settings live in flash, not in the firmware, so that a firmware
// downloaded from a release can still get online.
bool wifi_configured();
void save_wifi(const char* ssid, const char* password);

// If this firmware was built with include/secrets.h, copies those settings to
// flash. Does nothing otherwise.
void seed_wifi_from_build();

enum Result {
  NOT_CONFIGURED,  // no Wi-Fi settings saved
  NO_WIFI,         // could not join the network
  CHECK_FAILED,    // could not find out the latest release
  UP_TO_DATE,
  UPDATE_FAILED,   // a newer release exists but could not be installed
  UPDATED,         // installed; restart to run it
};

// Joins Wi-Fi, looks up the latest release, and installs it if it is newer
// than `current_version` ("v1.2.3"). Wi-Fi is off again on return.
Result check_and_update(const char* current_version);

const char* describe(Result r);

// Call once this firmware has proved it works. Until then, a firmware that
// arrived over the air is on trial: if it resets first, the bootloader goes
// back to the previous version.
void confirm_running_firmware();

}  // namespace ota
