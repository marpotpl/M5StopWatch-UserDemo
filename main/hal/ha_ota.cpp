#include "ha_ota.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>

#include "ha_wifi.h"

namespace ha_ota {
namespace {

constexpr char kTag[] = "HA_OTA";
constexpr uint32_t kSelfTestTimeoutMs = 90000;
constexpr uint32_t kLoopStableMs = 10000;
constexpr uint32_t kWifiStableMs = 5000;
constexpr uint32_t kHeartbeatMaxAgeMs = 2000;
constexpr size_t kMaxUrlLength = 512;
constexpr size_t kMaxCertificateLength = 8192;

struct UpdateRequest {
    char* url;
    char* ca_cert_pem;
};

std::atomic<Phase> current_phase{Phase::NotInitialized};
std::atomic<Error> current_error{Error::None};
std::atomic<uint8_t> current_progress{0};
std::atomic<bool> boot_ready{false};
std::atomic<uint32_t> last_heartbeat_ms{0};

const char* state_name(esp_ota_img_states_t state)
{
    switch (state) {
        case ESP_OTA_IMG_NEW: return "NEW";
        case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
        case ESP_OTA_IMG_VALID: return "VALID";
        case ESP_OTA_IMG_INVALID: return "INVALID";
        case ESP_OTA_IMG_ABORTED: return "ABORTED";
        case ESP_OTA_IMG_UNDEFINED: return "UNDEFINED";
        default: return "UNKNOWN";
    }
}

bool is_pending_verify()
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    return running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

void fail(Error error, const char* message, esp_err_t detail = ESP_OK)
{
    current_error.store(error);
    current_phase.store(Phase::Failed);
    if (detail == ESP_OK) {
        ESP_LOGE(kTag, "%s", message);
    } else {
        ESP_LOGE(kTag, "%s: %s", message, esp_err_to_name(detail));
    }
}

void self_test_task(void*)
{
    const uint32_t started_ms = esp_log_timestamp();
    uint32_t loop_since_ms = 0;
    uint32_t wifi_since_ms = 0;

    while (esp_log_timestamp() - started_ms < kSelfTestTimeoutMs) {
        const uint32_t now = esp_log_timestamp();
        const uint32_t heartbeat_ms = last_heartbeat_ms.load();
        const bool loop_alive = boot_ready.load() && heartbeat_ms != 0 &&
                                now - heartbeat_ms <= kHeartbeatMaxAgeMs;
        if (loop_alive) {
            if (loop_since_ms == 0) loop_since_ms = now;
        } else {
            loop_since_ms = 0;
        }

        // Wi-Fi is checked only after HAL and app startup completed.
        if (boot_ready.load() && ha_wifi::is_connected()) {
            if (wifi_since_ms == 0) wifi_since_ms = now;
        } else {
            wifi_since_ms = 0;
        }

        if (loop_since_ms != 0 && wifi_since_ms != 0 &&
            now - loop_since_ms >= kLoopStableMs &&
            now - wifi_since_ms >= kWifiStableMs) {
            ESP_LOGI(kTag, "OTA self-test passed");
            const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
            if (result == ESP_OK) {
                ESP_LOGI(kTag, "Firmware marked VALID");
                current_phase.store(Phase::Ready);
                vTaskDelete(nullptr);
                return;
            }
            fail(Error::ValidationFailed, "Could not mark firmware VALID", result);
            request_rollback();
            vTaskDelete(nullptr);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    ESP_LOGE(kTag, "OTA self-test timed out (HAL/UI loop or Wi-Fi not ready)");
    request_rollback();
    vTaskDelete(nullptr);
}

void free_request(UpdateRequest* request)
{
    if (!request) return;
    free(request->url);
    free(request->ca_cert_pem);
    free(request);
}

void update_task(void* parameter)
{
    auto* request = static_cast<UpdateRequest*>(parameter);
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target || target == esp_ota_get_running_partition()) {
        fail(Error::NoUpdatePartition, "No inactive OTA partition");
        free_request(request);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(kTag, "Updating inactive partition: %s", target->label);

    esp_http_client_config_t http_config{};
    http_config.url = request->url;
    http_config.timeout_ms = 15000;
    http_config.buffer_size = 4096;
    http_config.disable_auto_redirect = true;
    if (request->ca_cert_pem) {
        http_config.cert_pem = request->ca_cert_pem;
    } else {
        http_config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_https_ota_config_t ota_config{};
    ota_config.http_config = &http_config;

    esp_https_ota_handle_t handle = nullptr;
    esp_err_t result = esp_https_ota_begin(&ota_config, &handle);
    if (result != ESP_OK) {
        fail(Error::DownloadFailed, "HTTPS OTA connection failed", result);
        free_request(request);
        vTaskDelete(nullptr);
        return;
    }

    const int image_size = esp_https_ota_get_image_size(handle);
    if (image_size > 0 && static_cast<size_t>(image_size) > target->size) {
        esp_https_ota_abort(handle);
        fail(Error::ImageTooLarge, "OTA image exceeds inactive partition");
        free_request(request);
        vTaskDelete(nullptr);
        return;
    }

    do {
        result = esp_https_ota_perform(handle);
        const int bytes_read = esp_https_ota_get_image_len_read(handle);
        if (image_size > 0 && bytes_read >= 0) {
            const int percent = bytes_read >= image_size ? 100 :
                                static_cast<int>((static_cast<int64_t>(bytes_read) * 100) / image_size);
            current_progress.store(static_cast<uint8_t>(percent));
        }
    } while (result == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (result != ESP_OK) {
        esp_https_ota_abort(handle);
        fail(Error::DownloadFailed, "HTTPS OTA transfer failed", result);
        free_request(request);
        vTaskDelete(nullptr);
        return;
    }
    if (!esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        fail(Error::IncompleteImage, "Incomplete OTA image");
        free_request(request);
        vTaskDelete(nullptr);
        return;
    }

    // finish validates the image and changes otadata only after success.
    result = esp_https_ota_finish(handle);
    if (result != ESP_OK) {
        fail(Error::ValidationFailed, "OTA image validation failed", result);
        free_request(request);
        vTaskDelete(nullptr);
        return;
    }

    current_progress.store(100);
    current_phase.store(Phase::Switching);
    ESP_LOGI(kTag, "OTA image verified; restarting into %s", target->label);
    free_request(request);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

}  // namespace

bool init()
{
    if (current_phase.load() != Phase::NotInitialized) return true;

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    if (!running || !next || running == next) {
        fail(Error::NoUpdatePartition, "OTA partition layout unavailable");
        return false;
    }
    ESP_LOGI(kTag, "Running partition: %s", running->label);
    ESP_LOGI(kTag, "Next OTA partition: %s", next->label);

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        state = ESP_OTA_IMG_UNDEFINED;  // First USB boot with empty otadata.
    }
    ESP_LOGI(kTag, "OTA state: %s", state_name(state));
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        current_phase.store(Phase::Ready);
        return true;
    }

    current_phase.store(Phase::SelfTest);
    if (xTaskCreate(self_test_task, "ha_ota_selftest", 4096, nullptr, 5, nullptr) != pdPASS) {
        fail(Error::TaskStartFailed, "OTA self-test task could not start");
        request_rollback();
        return false;
    }
    return true;
}

void report_boot_ready() { boot_ready.store(true); }

void heartbeat()
{
    if (current_phase.load() == Phase::SelfTest) {
        last_heartbeat_ms.store(esp_log_timestamp());
    }
}

bool start_update(const char* url, const char* ca_cert_pem)
{
    if (!url || strncmp(url, "https://", 8) != 0 || strlen(url) <= 8 ||
        strlen(url) > kMaxUrlLength || strchr(url, '#') || strchr(url, '?')) {
        current_error.store(Error::InvalidUrl);
        return false;
    }
    const char* authority_end = strchr(url + 8, '/');
    if (!authority_end) authority_end = url + strlen(url);
    if (memchr(url + 8, '@', authority_end - (url + 8)) != nullptr) {
        current_error.store(Error::InvalidUrl);  // No credentials in URLs.
        return false;
    }
    // An IP literal cannot resolve to a public address or be DNS-rebound.
    const char* port = static_cast<const char*>(memchr(url + 8, ':', authority_end - (url + 8)));
    const size_t host_length = (port ? port : authority_end) - (url + 8);
    if (host_length == 0 || host_length > 15 || (port && port + 1 == authority_end)) {
        current_error.store(Error::InvalidUrl);
        return false;
    }
    char host[16]{};
    memcpy(host, url + 8, host_length);
    in_addr address{};
    if (inet_pton(AF_INET, host, &address) != 1) {
        current_error.store(Error::NonLocalUrl);
        return false;
    }
    const uint32_t ip = ntohl(address.s_addr);
    const bool private_ip = (ip & 0xff000000U) == 0x0a000000U ||
                            (ip & 0xfff00000U) == 0xac100000U ||
                            (ip & 0xffff0000U) == 0xc0a80000U;
    if (!private_ip) {
        current_error.store(Error::NonLocalUrl);
        return false;
    }
    if (port) {
        unsigned int port_number = 0;
        for (const char* digit = port + 1; digit < authority_end; ++digit) {
            if (*digit < '0' || *digit > '9') {
                current_error.store(Error::InvalidUrl);
                return false;
            }
            port_number = port_number * 10 + static_cast<unsigned int>(*digit - '0');
            if (port_number > 65535) {
                current_error.store(Error::InvalidUrl);
                return false;
            }
        }
        if (port_number == 0) {
            current_error.store(Error::InvalidUrl);
            return false;
        }
    }
    if (ca_cert_pem && strlen(ca_cert_pem) > kMaxCertificateLength) {
        current_error.store(Error::InvalidUrl);
        return false;
    }
    if (!ha_wifi::is_connected()) {
        current_error.store(Error::WifiUnavailable);
        return false;
    }
    Phase expected = Phase::Ready;
    if (!current_phase.compare_exchange_strong(expected, Phase::Downloading) &&
        (expected != Phase::Failed ||
         !current_phase.compare_exchange_strong(expected, Phase::Downloading))) {
        current_error.store(Error::Busy);
        return false;
    }
    if (is_pending_verify()) {
        current_error.store(Error::Busy);
        current_phase.store(Phase::Failed);
        return false;
    }

    auto* request = static_cast<UpdateRequest*>(calloc(1, sizeof(UpdateRequest)));
    if (request) {
        request->url = strdup(url);
        if (ca_cert_pem) request->ca_cert_pem = strdup(ca_cert_pem);
    }
    if (!request || !request->url || (ca_cert_pem && !request->ca_cert_pem)) {
        free_request(request);
        fail(Error::OutOfMemory, "OTA request allocation failed");
        return false;
    }

    current_error.store(Error::None);
    current_progress.store(0);
    if (xTaskCreate(update_task, "ha_ota_update", 8192, request, 5, nullptr) != pdPASS) {
        free_request(request);
        fail(Error::TaskStartFailed, "OTA update task could not start");
        return false;
    }
    return true;
}

void request_rollback()
{
    if (!is_pending_verify()) {
        ESP_LOGW(kTag, "Rollback ignored: firmware is not PENDING_VERIFY");
        return;
    }
    current_phase.store(Phase::RollingBack);
    ESP_LOGW(kTag, "OTA rollback requested");
    const esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (result != ESP_OK) {
        fail(Error::RollbackFailed, "OTA rollback failed", result);
    }
}

Status status()
{
    return {current_phase.load(), current_error.load(), current_progress.load()};
}

}  // namespace ha_ota
