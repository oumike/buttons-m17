#pragma once
// Board-level power and I/O. On the Wio Tracker L2 this is the PCA9555
// expander, which gates almost every rail; on a board without one these
// become plain GPIO writes. Call boardIoBegin() before any other peripheral.

#include <cstdint>

bool boardIoBegin();
bool boardIoReady();

bool boardSetStatusLed(bool on);       // the single user LED
bool boardSetAudioPaPower(bool on);
bool boardSetUsbOtgPower(bool on);
bool boardSetSdPower(bool on);
bool boardReadSdDetect(bool &present);
bool boardReadWakeButton(bool &pressed);
