#pragma once
// Status indication. The Wio Tracker L2 has a single user LED (on the
// expander), so it carries the most important state as a pattern:
//   fast blink  setup AP up, no network
//   slow blink  joining Wi-Fi
//   solid       connected
// Linked / TX / RX move to the TFT in milestone 7.

void ledsTask(void *);
