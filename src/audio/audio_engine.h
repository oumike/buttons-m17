#pragma once
// Audio engine: owns the I2S bus and both codecs, and runs the 20 ms audio
// loop on core 1.
//
// For now the loop serves two hardware tests:
//   * echo test: hold PTT to record the mic, release to hear it played back
//     (a loopback without the feedback howl of a live mic -> speaker path).
//     By default the recording goes through Codec2 3200 encode and decode;
//   * test tone: 1 kHz for two seconds, triggered from the web status page.
//     raw mode skips the codec for comparison;
// The network (milestones 5-6) plugs into the same loop.

bool audioBegin();          // I2S + codecs; reports failures in AppStatus.audio
void audioTask(void *);     // never returns; start pinned to core 1

void audioRequestTone();    // thread-safe
void audioSetEchoCodec2(bool on);   // thread-safe; default on
bool audioEchoCodec2();
