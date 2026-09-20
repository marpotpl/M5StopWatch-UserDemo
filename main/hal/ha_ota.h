#pragma once

#include <cstdint>

namespace ha_ota {

enum class Phase : uint8_t {
    NotInitialized,
    SelfTest,
    Ready,
    Downloading,
    Switching,
    RollingBack,
    Failed,
};

enum class Error : uint8_t {
    None,
    InvalidUrl,
    NonLocalUrl,
    Busy,
    WifiUnavailable,
    NoUpdatePartition,
    OutOfMemory,
    TaskStartFailed,
    ImageTooLarge,
    DownloadFailed,
    IncompleteImage,
    ValidationFailed,
    RollbackFailed,
};

struct Status {
    Phase phase;
    Error error;
    uint8_t progress_percent;
};

// Logs the running and next OTA partitions. Starts a self-test only when the
// running image is PENDING_VERIFY. Call before HAL initialization.
bool init();

// Call after HAL initialization and app installation, then once per completed
// iteration of the main UI loop. These calls never block.
void report_boot_ready();
void heartbeat();

// Starts a background HTTPS download to the inactive slot. The default uses
// the ESP-IDF certificate bundle; ca_cert_pem can supply a private LAN CA.
// URL must point to a private IPv4 address on the trusted local network.
// The caller must also protect the trigger from unauthorized use.
bool start_update(const char* url, const char* ca_cert_pem = nullptr);

// Only meaningful for a running image in PENDING_VERIFY state.
void request_rollback();

Status status();

}  // namespace ha_ota
