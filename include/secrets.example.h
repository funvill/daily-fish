// Optional. Copy this file to include/secrets.h and fill in your Wi-Fi
// network; secrets.h is ignored by git.
//
// The panel uses Wi-Fi only to look for new firmware (see "Updates" in the
// README). A firmware built with this file saves the settings to the panel's
// flash the first time it runs, where later firmware, including releases
// downloaded over the air, finds them.
//
// The other way to give a panel its Wi-Fi settings is over USB serial; see
// the README.
#pragma once

#define WIFI_SSID "your-network-name"
#define WIFI_PASSWORD "your-network-password"
