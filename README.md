# buttons-m17

An internet-only [M17](https://m17project.org/) voice client for the ESP32-S3. It is a standalone handheld with a mic, speaker and PTT button, and it links to an M17 reflector (mrefd / URF) over Wi-Fi. No radio is needed. Voice uses the open **Codec2 3200** codec in software, so no vocoder chip is required.

> **Licensed operators only.** The firmware will not transmit until a callsign has been configured, and it never transmits in listen-only mode. No callsign is hard-coded anywhere.

License: **GPL-3.0-or-later** (see `LICENSE`).

## Status

| # | Milestone | State |
|---|-----------|-------|
| 1 | Skeleton: tasks, NVS config, Wi-Fi + AP setup, web status page, settings backup, built-in reflector list | **done** (host-tested; on Wio: AP setup, Wi-Fi join, m17.local, web login verified) |
| 2 | M17 core library (base-40 callsigns, CRC-16 0x5935, control and stream packet build/parse) + native tests | todo |
| 3 | Audio: ES8311 / ES7243E init, mic → speaker loopback | todo |
| 4 | Codec2 3200 encode/decode loopback (mic → Codec2 → speaker) | todo |
| 5 | Reflector client: CONN/LSTN, ACKN/NACK, PING/PONG keepalive, receive and play a live stream. Includes a Python fake-reflector harness | todo |
| 6 | Transmit: PTT → stream → reflector, confirmed on the reflector dashboard | todo |
| 7 | Polish: display, last heard, OTA, reconnect hardening | todo |

## Hardware: Seeed Wio Tracker L2 (first target)

ESP32-S3 with 16 MB flash and 8 MB octal PSRAM. The pin map comes from camillia-mt's `hw_wio_tracker_l2.h`, and every pin lives in `src/board_config.h`.

| Function | On the Wio Tracker L2 |
|----------|-----------------------|
| Microphone | ES7243E mic ADC on the shared I2S bus (M3) |
| Speaker | ES8311 codec + PA (I2C 0x18). PA power on expander bit 12 |
| PTT | GPIO0 (the only direct button) |
| Status LED | user LED on the PCA9555 expander (bit 10) |
| Display | NV3031B QSPI TFT (M7) |
| Storage | microSD (SD_MMC, 1-bit) for settings backups |
| GPS | L76K (UART1). Not used yet; held in reset and unpowered |

Holding the **Wake** button (expander bit 0) for 5 s during boot does a factory reset.

## First boot and configuration

The setup flow follows camillia-mt: an open access point with no captive-portal DNS.

1. Power on. With no Wi-Fi configured, the device opens the open AP **`buttons-m17`**.
2. Join the AP and browse to **http://192.168.4.1/setup**. Enter your Wi-Fi network and set the web admin password (minimum 8 characters). The device then reboots.
3. On your network, browse to **http://m17.local/**. Log in as user `admin` with the password you set.
4. Under **Settings**, enter:
   - your callsign: up to 9 characters from the M17 alphabet `A-Z 0-9 - / .`
   - the reflector (`M17-XXX`, with autocomplete) and its module (A–Z)
   - any extra reflectors
   - auto-link and listen-only
   - PTT mode, volume and mic gain

If the saved network can't be reached within 20 s, the AP comes back up (AP+STA) so you can fix the settings. Station mode keeps retrying every 30 s.

### Reflectors

The firmware ships with a built-in list of about 130 M17 reflectors (`src/config/reflectors_builtin.cpp`). The list is generated from the Pi-Star `M17_Hosts.txt` snapshot in `data/`. **Extra reflectors** in Settings (`M17-XXX host [port]` per line, default port 17000) add new names or override built-in ones. To refresh the built-in list:

```sh
curl -fsSL -o data/M17_Hosts.txt https://www.pistar.uk/downloads/M17_Hosts.txt
python3 tools/gen_reflectors.py
```

Reflector data is provided by [DVRef](https://dvref.com/) under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), via [Pi-Star](https://www.pistar.uk/). Change made: IPv6-only entries are dropped.

### Backup and restore

The **Backup** page offers:
- **Download** the settings as `buttons-m17-<CALLSIGN>.yaml`, or **upload** a saved file to restore it.
- **Save to SD** at `/buttons-m17/settings.yaml` (the previous file is kept as `settings.prev.yaml`), or **restore from SD**.

Backups never contain the web admin password, and a restore keeps the current one. Uploads are validated in full before anything is applied.

## Build and test

```sh
scripts/test.sh                              # host unit tests (pio test -e native)
scripts/build-upload-monitor.sh --just-build # compile every device env, with a summary
scripts/package.sh                           # dist/: factory image, OTA image and ELF
scripts/build-upload-monitor.sh -W           # build, upload and monitor a Wio Tracker L2
scripts/flash.sh wio-tracker-l2 /dev/cu.usbmodem*  # flash the factory image from dist/
```

To keep PlatformIO's core in a local directory, add a gitignored `platformio_local.ini` containing `[platformio]` and `core_dir = ...`. This works the same way as in camillia-mt.

## Project layout

```
src/
  main.cpp            task startup only
  board_config.h      pins (one place)
  board/              PCA9555 expander, power rails, safe boot state
  config/             settings model + validation, NVS store, YAML backup, reflector list
  storage/            SD card mount + backup files
  net/                Wi-Fi manager (later: M17 reflector client)
  app/                shared status (later: PTT state machine, stream router)
  ui/                 web server, HTTP guard, LEDs (later: display)
  m17/ audio/ codec/  (milestones 2–4)
data/                 M17_Hosts.txt snapshot (input to tools/gen_reflectors.py)
tools/                build-time helpers
test/native/          host unit tests
scripts/              build, package, flash, test
```

Protocol and config code makes no hardware calls, so it builds and tests on the host.

## Open questions (TODO)

- [ ] Which reflector and module to use by default. `M17-M17 A` is a placeholder.
- [ ] Is any M17 reflector cross-linked (URF + transcoder) to the D-Star "89C" module used by K8UNS? If so, this device could talk to that repeater's users.
- [ ] Which Codec2 build to use on the ESP32-S3 (LGPL-2.1, GPL-compatible). It must be verified before M4.
- [ ] ES7243E I2C address on the Wio.
- [ ] SD-detect polarity (assumed active-low).

## Reference sources

These are read for byte layouts. No GPL code is pasted in without attribution, and this project is GPL-3.0-or-later.

- the M17 specification (spec.m17project.org)
- n7tae/mrefd `Packet-Description.md` (the reflector UDP protocol)
- libm17
- Codec2
