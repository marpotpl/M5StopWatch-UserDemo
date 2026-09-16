#include "ha_time.h"

#include <atomic>
#include <ctime>
#include <sys/time.h>

#include <esp_log.h>
#include <esp_sntp.h>

#include "hal.h"

namespace ha_time {

static const char* TAG = "HA_TIME";

static std::atomic<bool> synced{false};
static std::atomic<bool> started{false};


// Wywoływane automatycznie po udanej synchronizacji NTP.
static void time_sync_notification(struct timeval* tv)
{
    synced = true;

    time_t now = tv->tv_sec;

    // ---------------------------------------------------------
    // Czas UTC
    // ---------------------------------------------------------

    struct tm utc_time;
    gmtime_r(&now, &utc_time);

    ESP_LOGI(
        TAG,
        "NTP synchronized: %04d-%02d-%02d %02d:%02d:%02d UTC",
        utc_time.tm_year + 1900,
        utc_time.tm_mon + 1,
        utc_time.tm_mday,
        utc_time.tm_hour,
        utc_time.tm_min,
        utc_time.tm_sec
    );

    // ---------------------------------------------------------
    // Zapis prawidłowego czasu UTC do sprzętowego RTC RX8130.
    //
    // RTC przechowuje UTC.
    // Strefa czasowa jest nakładana dopiero przez localtime().
    // ---------------------------------------------------------

    GetHAL().syncSystemTimeToRtc();

    // ---------------------------------------------------------
    // Czas lokalny
    // ---------------------------------------------------------

    struct tm local_time;
    localtime_r(&now, &local_time);

    ESP_LOGI(
        TAG,
        "Local time: %04d-%02d-%02d %02d:%02d:%02d",
        local_time.tm_year + 1900,
        local_time.tm_mon + 1,
        local_time.tm_mday,
        local_time.tm_hour,
        local_time.tm_min,
        local_time.tm_sec
    );
}


void init()
{
    // ---------------------------------------------------------
    // Polska strefa czasowa:
    //
    // CET  = UTC+1
    // CEST = UTC+2
    //
    // ostatnia niedziela marca    -> czas letni
    // ostatnia niedziela października -> czas zimowy
    //
    // Zapisujemy ustawienie również do NVS,
    // dzięki czemu pozostaje po restarcie.
    // ---------------------------------------------------------

    GetHAL().setTimezone(
        "CET-1CEST,M3.5.0/2,M10.5.0/3"
    );

    ESP_LOGI(
        TAG,
        "Timezone set to CET/CEST"
    );

    // SNTP uruchamiamy tylko raz.
    if (started.exchange(true)) {
        ESP_LOGI(
            TAG,
            "NTP already started"
        );
        return;
    }

    ESP_LOGI(
        TAG,
        "Starting NTP"
    );

    // ---------------------------------------------------------
    // Konfiguracja SNTP
    // ---------------------------------------------------------

    esp_sntp_setoperatingmode(
        SNTP_OPMODE_POLL
    );

    // Główny serwer NTP
    esp_sntp_setservername(
        0,
        const_cast<char*>(
            "pool.ntp.org"
        )
    );

    // Zapasowy serwer NTP
    esp_sntp_setservername(
        1,
        const_cast<char*>(
            "time.google.com"
        )
    );

    // Callback wywoływany po ustawieniu czasu systemowego.
    esp_sntp_set_time_sync_notification_cb(
        time_sync_notification
    );

    // Start klienta SNTP.
    esp_sntp_init();

    ESP_LOGI(
        TAG,
        "NTP started"
    );
}


bool is_synced()
{
    return synced.load();
}

}  // namespace ha_time
