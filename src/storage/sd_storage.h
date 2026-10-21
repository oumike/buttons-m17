#pragma once
// microSD storage (SD_MMC, 1-bit) and the settings backup files on it.
//
// Card layout:
//   /buttons-m17/settings.yaml       latest backup
//   /buttons-m17/settings.prev.yaml  the one before (rotated on each save)

#include <string>

bool storageBegin();       // power + mount; idempotent, retried on each call
bool storageMounted();
void storageUnmount();
std::string storageDescribe();   // "SD 29.7 GB" / "no card"

// Each returns "" on success or an error message.
std::string storageSaveBackup(const std::string &yaml);
std::string storageLoadBackup(std::string &yaml);
bool storageBackupExists();

constexpr const char *BACKUP_DIR = "/buttons-m17";
constexpr const char *BACKUP_PATH = "/buttons-m17/settings.yaml";
constexpr const char *BACKUP_PREV_PATH = "/buttons-m17/settings.prev.yaml";
