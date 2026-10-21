#include "storage/sd_storage.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

#include <mutex>

#include "board/board_io.h"
#include "board_config.h"
#include "config/backup.h"

namespace {
std::recursive_mutex s_mu;
bool s_mounted = false;
}  // namespace

bool storageBegin() {
    std::lock_guard<std::recursive_mutex> lk(s_mu);
    if (s_mounted) return true;
#if defined(HAS_SD_MMC) && HAS_SD_MMC
    if (!boardSetSdPower(true)) {
        Serial.println("[sd] power enable failed");
        return false;
    }
    delay(20);
    if (!SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_D0)) {
        Serial.println("[sd] pin setup failed");
        return false;
    }
    // 1-bit mode, no format-on-fail: never wipe a user's card.
    s_mounted = SD_MMC.begin("/sdcard", true, false);
    if (s_mounted && SD_MMC.cardType() == CARD_NONE) {
        SD_MMC.end();
        s_mounted = false;
    }
    if (s_mounted) {
        Serial.printf("[sd] mounted, %llu MB\n",
                      (unsigned long long)(SD_MMC.cardSize() / (1024ULL * 1024ULL)));
    } else {
        Serial.println("[sd] no card");
        boardSetSdPower(false);
    }
#endif
    return s_mounted;
}

bool storageMounted() { return s_mounted; }

void storageUnmount() {
    std::lock_guard<std::recursive_mutex> lk(s_mu);
    if (!s_mounted) return;
    SD_MMC.end();
    s_mounted = false;
    boardSetSdPower(false);
}

std::string storageDescribe() {
    std::lock_guard<std::recursive_mutex> lk(s_mu);
    if (!s_mounted) return "no card";
    char buf[32];
    snprintf(buf, sizeof buf, "SD %.1f GB",
             static_cast<double>(SD_MMC.cardSize()) / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

std::string storageSaveBackup(const std::string &yaml) {
    std::lock_guard<std::recursive_mutex> lk(s_mu);
    if (!storageBegin()) return "No SD card";
    fs::FS &fs = SD_MMC;
    if (!fs.exists(BACKUP_DIR) && !fs.mkdir(BACKUP_DIR)) return "Could not create folder on SD";

    // Write to a temp file first so a failed write never destroys the
    // previous good backup, then rotate.
    const char *tmp = "/buttons-m17/settings.tmp";
    {
        File f = fs.open(tmp, FILE_WRITE);
        if (!f) return "Could not open file on SD";
        const size_t n = f.write(reinterpret_cast<const uint8_t *>(yaml.data()), yaml.size());
        f.close();
        if (n != yaml.size()) {
            fs.remove(tmp);
            return "Short write to SD (card full?)";
        }
    }
    if (fs.exists(BACKUP_PATH)) {
        fs.remove(BACKUP_PREV_PATH);
        fs.rename(BACKUP_PATH, BACKUP_PREV_PATH);
    }
    if (!fs.rename(tmp, BACKUP_PATH)) return "Could not rename backup on SD";
    Serial.printf("[sd] backup saved to %s (%u bytes)\n", BACKUP_PATH, (unsigned)yaml.size());
    return "";
}

std::string storageLoadBackup(std::string &yaml) {
    std::lock_guard<std::recursive_mutex> lk(s_mu);
    if (!storageBegin()) return "No SD card";
    File f = SD_MMC.open(BACKUP_PATH, FILE_READ);
    if (!f) return std::string("No backup at ") + BACKUP_PATH;
    if (f.size() > cfg::BACKUP_MAX_BYTES) {
        f.close();
        return "Backup file on SD is too large";
    }
    yaml.clear();
    yaml.reserve(f.size());
    while (f.available()) yaml += static_cast<char>(f.read());
    f.close();
    return "";
}

bool storageBackupExists() {
    std::lock_guard<std::recursive_mutex> lk(s_mu);
    return s_mounted && SD_MMC.exists(BACKUP_PATH);
}
