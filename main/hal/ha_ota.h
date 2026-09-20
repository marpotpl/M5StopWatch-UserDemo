#pragma once

#include <cstdint>
#include <cstddef>

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
    InvalidImageMetadata,
    NonLocalUrl,
    NoTrustedCa,
    Busy,
    WifiUnavailable,
    NoUpdatePartition,
    OutOfMemory,
    TaskStartFailed,
    ImageTooLarge,
    SizeMismatch,
    HashMismatch,
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

// The trusted CA is compiled from the Git-ignored ha_ota_ca.h. Event data
// cannot choose or replace it. URL must point to a private IPv4 address.
bool start_update(const char* url, const char* expected_sha256, size_t expected_size);

// Read-only checks for a caller validating a request before starting OTA.
bool is_valid_url(const char* url);
size_t next_partition_size();

// Only meaningful for a running image in PENDING_VERIFY state.
void request_rollback();

Status status();

}  // namespace ha_ota
