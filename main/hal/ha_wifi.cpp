#include "ha_wifi.h"

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ha_secrets.h"
#include "wifi_manager.h"
#include "ssid_manager.h"
#include "ha_time.h"

namespace ha_wifi {

static const char* TAG = "HA_WIFI";

static constexpr const char* WIFI_SSID = HA_WIFI_SSID;
static constexpr const char* WIFI_PASSWORD = HA_WIFI_PASSWORD;

void init()
{
    ESP_LOGI(TAG, "Starting Wi-Fi");

    auto& ssids = SsidManager::GetInstance();
    ssids.AddSsid(WIFI_SSID, WIFI_PASSWORD);

    WifiManagerConfig config;
    config.ssid_prefix = "M5StopWatch";
    config.language = "en-US";
    config.station_scan_min_interval_seconds = 5;
    config.station_scan_max_interval_seconds = 60;

    auto& wifi = WifiManager::GetInstance();

    wifi.SetEventCallback([](WifiEvent event, const std::string& data) {
        switch (event) {

            case WifiEvent::Scanning:
                ESP_LOGI(TAG, "Scanning...");
                break;

            case WifiEvent::Connecting:
                ESP_LOGI(
                    TAG,
                    "Connecting to: %s",
                    data.c_str()
                );
                break;

case WifiEvent::Connected:
    ESP_LOGI(TAG, "Wi-Fi connected");

    xTaskCreate(
        [](void*) {
            ESP_LOGI(TAG, "Starting NTP from separate task");

            ha_time::init();

            ESP_LOGI(TAG, "NTP task finished");

            vTaskDelete(nullptr);
        },
        "ha_time_init",
        8192,
        nullptr,
        5,
        nullptr
    );

    break;		

            case WifiEvent::Disconnected:
                ESP_LOGW(TAG, "Wi-Fi disconnected");
                break;

            case WifiEvent::ConfigModeEnter:
                ESP_LOGI(TAG, "Config AP started");
                break;

            case WifiEvent::ConfigModeExit:
                ESP_LOGI(TAG, "Config AP stopped");
                break;
        }
    });

    if (!wifi.Initialize(config)) {
        ESP_LOGE(
            TAG,
            "WifiManager initialization failed"
        );
        return;
    }

    ESP_LOGI(
        TAG,
        "WifiManager initialized"
    );

    wifi.StartStation();

    ESP_LOGI(
        TAG,
        "Station started"
    );
}

bool is_connected()
{
    return WifiManager::GetInstance().IsConnected();
}

}  // namespace ha_wifi
