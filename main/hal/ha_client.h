#pragma once
#include <cstdint>

namespace ha_client {

enum class State {
    Stopped,
    WaitingForWifi,
    Connecting,
    WaitingForAuth,
    Authenticated,
    AuthFailed,
    RetryWait,
};

// Starts the client task. Safe to call once after ha_wifi::init().
bool init();

State state();
bool is_connected();
bool is_authenticated();

enum class ButtonPressStatus : uint8_t { Idle, Queued, Sending, Accepted, Failed, Offline };
bool request_button_press(const char* entity_id);
ButtonPressStatus button_press_status();

}  // namespace ha_client
