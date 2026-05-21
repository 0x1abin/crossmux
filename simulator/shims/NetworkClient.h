#pragma once
// Modern Arduino-ESP32 exposes the plain (non-TLS) socket as NetworkClient.
// HTTPClient.h already defines `using NetworkClient = WiFiClient;` (libcurl drives
// the transport), so this shim just makes `#include <NetworkClient.h>` resolve.
#include <HTTPClient.h>
