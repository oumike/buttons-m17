#pragma once
// Wi-Fi, following camillia-mt: an open SoftAP "buttons-m17" at
// http://192.168.4.1/ on first boot (no credentials) or when the saved network
// cannot be joined; station mode with mDNS "m17.local" otherwise. While in
// the fallback AP the saved network keeps being retried (AP+STA).

void wifiBegin();
void wifiLoop();          // call often from the network task
bool wifiPortalActive();  // SoftAP is up
const char *wifiHostname();
