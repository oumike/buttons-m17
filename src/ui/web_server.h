#pragma once
// HTTP UI on port 80: status page, settings, Wi-Fi/admin setup, JSON status.
// Basic auth (user "admin") once an admin password exists; on first boot only
// the setup page is reachable, and it requires setting that password.

void webBegin();
void webLoop();
