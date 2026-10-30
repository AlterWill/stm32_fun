#pragma once

// Copy this file to include/secrets.h and fill in your local values.
// secrets.h is gitignored; never commit real credentials or keys.

#define WIFI_SSID "YOUR_WIFI"
#define WIFI_PASSWORD "YOUR_PASSWORD"

// Plain HTTP origin only, no path:
//   http://192.168.1.50:3000
#define SERVER_URL "http://192.168.1.50:3000"

// Must match DEVICE_API_KEY in server/.env.
#define API_KEY "replace-with-DEVICE_API_KEY"
