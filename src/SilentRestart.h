#pragma once

#include <cstdint>

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();                                                // home screen
void silentRestartToReader(bool suppressChineseFontPrompt = false);  // currently-open EPUB (APP_STATE.openEpubPath)
void silentRestartToReaderAndPreloadChineseFont(uint8_t pointSize);

// Reboots immediately after an activity releases exclusive raw storage. The
// RTC target ensures setup() lands on Home instead of resuming a reader.
void restartToHomeAfterStorageHandoff();
