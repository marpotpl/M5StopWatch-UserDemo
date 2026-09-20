#pragma once

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

}  // namespace ha_client
