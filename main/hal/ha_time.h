#pragma once

namespace ha_time {

// Uruchamia synchronizację czasu przez NTP.
// Wywoływać po uzyskaniu połączenia Wi-Fi i adresu IP.
void init();

// Informuje, czy przynajmniej jedna synchronizacja NTP się udała.
bool is_synced();

}  // namespace ha_time
