#pragma once
// Audio engine: owns the I2S bus and both codecs, and runs the 20 ms audio
// loop on core 1.
//
// For now (milestone 3) the loop serves two hardware tests:
//   * echo test: hold PTT to record the mic, release to hear it played back
//     (a loopback without the feedback howl of a live mic -> speaker path);
//   * test tone: 1 kHz for two seconds, triggered from the web status page.
// Codec2 (milestone 4) and the network (5-6) plug into the same loop.

bool audioBegin();          // I2S + codecs; reports failures in AppStatus.audio
void audioTask(void *);     // never returns; start pinned to core 1

void audioRequestTone();    // thread-safe
