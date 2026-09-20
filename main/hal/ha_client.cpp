#include "ha_client.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "ha_secrets.h"
#include "ha_wifi.h"

#ifndef HA_ACCESS_TOKEN
#error "Define HA_ACCESS_TOKEN in the local, Git-ignored main/hal/ha_secrets.h"
#endif

namespace ha_client {
namespace {

constexpr char kUrl[] = "ws://192.168.0.73:8123/api/websocket";
constexpr char kTag[] = "HA_CLIENT";
constexpr size_t kChunkSize = 512;
constexpr size_t kMaxMessageSize = 4096;
constexpr uint32_t kAuthTimeoutMs = 15000;
constexpr uint32_t kMaxRetryMs = 30000;

enum class EventType : uint8_t { Begin, BeforeConnect, Connected, Disconnected, Error, Closed, Finish, Data };

struct Event {
    EventType type;
    int data_len;
    int payload_len;
    int payload_offset;
    uint8_t opcode;
    bool fin;
    esp_websocket_error_codes_t error;
    int close_status_code;
    char data[kChunkSize];
};

std::atomic<State> current_state{State::Stopped};
std::atomic<bool> queue_overflow{false};
QueueHandle_t event_queue = nullptr;

void websocket_event(void*, esp_event_base_t, int32_t event_id, void* event_data)
{
    Event event{};
    switch (event_id) {
        case WEBSOCKET_EVENT_BEGIN: event.type = EventType::Begin; break;
        case WEBSOCKET_EVENT_BEFORE_CONNECT: event.type = EventType::BeforeConnect; break;
        case WEBSOCKET_EVENT_CONNECTED: event.type = EventType::Connected; break;
        case WEBSOCKET_EVENT_DISCONNECTED: event.type = EventType::Disconnected; break;
        case WEBSOCKET_EVENT_ERROR: event.type = EventType::Error; break;
        case WEBSOCKET_EVENT_CLOSED: event.type = EventType::Closed; break;
        case WEBSOCKET_EVENT_FINISH: event.type = EventType::Finish; break;
        case WEBSOCKET_EVENT_DATA: {
            const auto* data = static_cast<const esp_websocket_event_data_t*>(event_data);
            if (!data || data->data_len < 0 || data->data_len > static_cast<int>(kChunkSize) ||
                (data->data_len > 0 && !data->data_ptr)) {
                queue_overflow.store(true);
                return;
            }
            event.type = EventType::Data;
            event.data_len = data->data_len;
            event.payload_len = data->payload_len;
            event.payload_offset = data->payload_offset;
            event.opcode = data->op_code;
            event.fin = data->fin;
            if (data->data_len > 0) {
                memcpy(event.data, data->data_ptr, data->data_len);
            }
            break;
        }
        default: return;
    }
    if (event_data && event.type == EventType::Error) {
        const auto* data = static_cast<const esp_websocket_event_data_t*>(event_data);
        event.error.error_type = data->error_handle.error_type;
        event.error.esp_ws_handshake_status_code = data->error_handle.esp_ws_handshake_status_code;
        if (event.error.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
            event.error.esp_tls_last_esp_err = data->error_handle.esp_tls_last_esp_err;
            event.error.esp_tls_stack_err = data->error_handle.esp_tls_stack_err;
            event.error.esp_tls_cert_verify_flags = data->error_handle.esp_tls_cert_verify_flags;
            event.error.esp_transport_sock_errno = data->error_handle.esp_transport_sock_errno;
        }
    }
    if (event_data && event.type == EventType::Closed) {
        event.close_status_code = static_cast<const esp_websocket_event_data_t*>(event_data)->close_status_code;
    }

    if (xQueueSend(event_queue, &event, 0) != pdTRUE) {
        queue_overflow.store(true);
    }
}

void close_client(esp_websocket_client_handle_t& client)
{
    if (client) {
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
        client = nullptr;
    }
    xQueueReset(event_queue);
    queue_overflow.store(false);
}

bool open_client(esp_websocket_client_handle_t& client)
{
    esp_websocket_client_config_t config{};
    config.uri = kUrl;
    config.disable_auto_reconnect = true;
    config.task_stack = 8192;
    config.buffer_size = kChunkSize;
    config.network_timeout_ms = 10000;

    client = esp_websocket_client_init(&config);
    if (!client) {
        return false;
    }
    if (esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event, nullptr) != ESP_OK ||
        esp_websocket_client_start(client) != ESP_OK) {
        close_client(client);
        return false;
    }
    return true;
}

bool send_auth(esp_websocket_client_handle_t client, uint32_t connected_at_ms)
{
    ESP_LOGI(kTag, "Sending HA authentication (%lu ms after transport connected)",
             static_cast<unsigned long>(esp_log_timestamp() - connected_at_ms));
    cJSON* message = cJSON_CreateObject();
    if (!message) {
        return false;
    }
    bool ok = cJSON_AddStringToObject(message, "type", "auth") &&
              cJSON_AddStringToObject(message, "access_token", HA_ACCESS_TOKEN);
    char* json = ok ? cJSON_PrintUnformatted(message) : nullptr;
    cJSON_Delete(message);
    if (!json) {
        return false;
    }
    const int length = static_cast<int>(strlen(json));
    ok = esp_websocket_client_send_text(client, json, length, pdMS_TO_TICKS(5000)) == length;
    cJSON_free(json);
    return ok;
}

enum class AuthResult { Continue, Authenticated, Invalid, Retry };

AuthResult handle_message(const std::string& message, esp_websocket_client_handle_t client,
                          bool& auth_sent, uint32_t connected_at_ms)
{
    if (current_state.load() == State::Authenticated) {
        return AuthResult::Continue;  // No entity or service messages in this stage.
    }
    cJSON* json = cJSON_ParseWithLength(message.data(), message.size());
    if (!json) {
        return AuthResult::Retry;
    }
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    AuthResult result = AuthResult::Retry;
    if (cJSON_IsString(type) && type->valuestring) {
        // Only log known protocol types; never print untrusted message contents.
        if (strcmp(type->valuestring, "auth_required") == 0 ||
            strcmp(type->valuestring, "auth_ok") == 0 ||
            strcmp(type->valuestring, "auth_invalid") == 0) {
            ESP_LOGI(kTag, "HA message type: %s (%lu ms after transport connected)",
                     type->valuestring,
                     static_cast<unsigned long>(esp_log_timestamp() - connected_at_ms));
        } else {
            ESP_LOGW(kTag, "HA message has unexpected type");
        }
        if (strcmp(type->valuestring, "auth_required") == 0 && !auth_sent &&
            current_state.load() == State::WaitingForAuth) {
            auth_sent = send_auth(client, connected_at_ms);
            result = auth_sent ? AuthResult::Continue : AuthResult::Retry;
        } else if (strcmp(type->valuestring, "auth_ok") == 0 && auth_sent) {
            result = AuthResult::Authenticated;
        } else if (strcmp(type->valuestring, "auth_invalid") == 0) {
            result = AuthResult::Invalid;
        }
    }
    cJSON_Delete(json);
    return result;
}

// Called only from the ha_client task. Accepts chunks and fragmented WebSocket frames.
bool append_frame(const Event& event, std::string& message, size_t& frame_base,
                  bool& in_text_message,
                  bool& message_complete)
{
    message_complete = false;
    if (event.opcode != 0x1 && event.opcode != 0x0) {
        return true;  // Ignore control and binary frames during authentication.
    }
    if (event.payload_offset == 0) {
        if (event.opcode == 0x1) {
            if (in_text_message) return false;
            message.clear();
            frame_base = 0;
            in_text_message = true;
        } else if (!in_text_message) {
            return false;
        } else {
            frame_base = message.size();
        }
    }
    if (!in_text_message || event.data_len < 0 || event.payload_len < 0 ||
        event.payload_offset < 0 ||
        frame_base + static_cast<size_t>(event.payload_offset) != message.size() ||
        message.size() + static_cast<size_t>(event.data_len) > kMaxMessageSize) {
        return false;
    }
    message.append(event.data, event.data_len);
    if (event.payload_offset + event.data_len == event.payload_len && event.fin) {
        in_text_message = false;
        message_complete = true;
    }
    return true;
}

void client_task(void*)
{
    esp_websocket_client_handle_t client = nullptr;
    std::string message;
    message.reserve(kMaxMessageSize);
    size_t frame_base = 0;
    bool in_text_message = false;
    bool auth_sent = false;
    bool auth_failed_latched = false;
    uint32_t retry_ms = 1000;
    TickType_t retry_at = 0;
    TickType_t auth_deadline = 0;
    uint32_t connected_at_ms = 0;
    current_state.store(State::WaitingForWifi);

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        if (auth_failed_latched) {
            current_state.store(State::AuthFailed);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;  // Bad token: do not retry until the device is restarted.
        }
        if (!ha_wifi::is_connected()) {
            if (client) close_client(client);
            current_state.store(State::WaitingForWifi);
            retry_ms = 1000;
            in_text_message = false;
            auth_sent = false;
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (current_state.load() == State::WaitingForWifi ||
            (current_state.load() == State::RetryWait &&
             static_cast<int32_t>(now - retry_at) >= 0)) {
            current_state.store(State::Connecting);
            ESP_LOGI(kTag, "Connecting to Home Assistant WebSocket");
            if (!open_client(client)) {
                ESP_LOGW(kTag, "WebSocket start failed");
                goto retry;
            }
            auth_deadline = now + pdMS_TO_TICKS(kAuthTimeoutMs);
        }

        {
            Event event{};
            if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(250)) == pdTRUE) {
                switch (event.type) {
                    case EventType::Begin:
                        ESP_LOGI(kTag, "WebSocket event: BEGIN");
                        break;
                    case EventType::BeforeConnect:
                        ESP_LOGI(kTag, "WebSocket event: BEFORE_CONNECT");
                        break;
                    case EventType::Connected:
                        connected_at_ms = esp_log_timestamp();
                        ESP_LOGI(kTag, "WebSocket transport connected");
                        current_state.store(State::WaitingForAuth);
                        auth_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(kAuthTimeoutMs);
                        break;
                    case EventType::Disconnected:
                        ESP_LOGW(kTag, "WebSocket event: DISCONNECTED");
                        goto retry;
                    case EventType::Error:
                        ESP_LOGE(kTag, "WebSocket event: ERROR type=%d tls_esp_err=0x%x tls_stack_err=%d cert_flags=0x%x http_status=%d sock_errno=%d",
                                 static_cast<int>(event.error.error_type),
                                 static_cast<unsigned>(event.error.esp_tls_last_esp_err),
                                 event.error.esp_tls_stack_err,
                                 static_cast<unsigned>(event.error.esp_tls_cert_verify_flags),
                                 event.error.esp_ws_handshake_status_code,
                                 event.error.esp_transport_sock_errno);
                        goto retry;
                    case EventType::Closed:
                        ESP_LOGW(kTag, "WebSocket event: CLOSED status=%d", event.close_status_code);
                        goto retry;
                    case EventType::Finish:
                        ESP_LOGI(kTag, "WebSocket event: FINISH");
                        break;
                    case EventType::Data: {
                        ESP_LOGI(kTag, "WebSocket event: DATA opcode=0x%02x data_len=%d payload_len=%d payload_offset=%d fin=%d",
                                 event.opcode, event.data_len, event.payload_len,
                                 event.payload_offset, event.fin);
                        bool complete = false;
                        if (!append_frame(event, message, frame_base, in_text_message, complete)) {
                            ESP_LOGW(kTag, "Invalid or oversized WebSocket message");
                            goto retry;
                        }
                        if (complete) {
                            switch (handle_message(message, client, auth_sent, connected_at_ms)) {
                                case AuthResult::Continue: break;
                                case AuthResult::Authenticated:
                                    current_state.store(State::Authenticated);
                                    retry_ms = 1000;
                                    ESP_LOGI(kTag, "Home Assistant authenticated");
                                    break;
                                case AuthResult::Invalid:
                                    ESP_LOGE(kTag, "Home Assistant authentication rejected");
                                    close_client(client);
                                    auth_failed_latched = true;
                                    current_state.store(State::AuthFailed);
                                    break;
                                case AuthResult::Retry:
                                    ESP_LOGW(kTag, "Unexpected Home Assistant auth message");
                                    goto retry;
                            }
                            message.clear();
                        }
                        break;
                    }
                }
            }
        }
        if (queue_overflow.load()) {
            ESP_LOGW(kTag, "WebSocket event queue overflow");
            goto retry;
        }
        if ((current_state.load() == State::Connecting ||
             current_state.load() == State::WaitingForAuth) &&
            static_cast<int32_t>(xTaskGetTickCount() - auth_deadline) >= 0) {
            ESP_LOGW(kTag, "Home Assistant authentication timeout");
            goto retry;
        }
        continue;

    retry:
        close_client(client);
        message.clear();
        frame_base = 0;
        in_text_message = false;
        auth_sent = false;
        current_state.store(State::RetryWait);
        retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(retry_ms);
        retry_ms = retry_ms < kMaxRetryMs / 2 ? retry_ms * 2 : kMaxRetryMs;
    }
}

}  // namespace

bool init()
{
    if (event_queue) return true;
    event_queue = xQueueCreate(8, sizeof(Event));
    if (!event_queue) return false;
    if (xTaskCreate(client_task, "ha_client", 8192, nullptr, 5, nullptr) != pdPASS) {
        vQueueDelete(event_queue);
        event_queue = nullptr;
        return false;
    }
    return true;
}

State state() { return current_state.load(); }

bool is_connected()
{
    const State s = state();
    return s == State::WaitingForAuth || s == State::Authenticated;
}

bool is_authenticated() { return state() == State::Authenticated; }

}  // namespace ha_client
