#include "ha_tesla_rest.h"

#include <atomic>
#include <cstring>
#include <string>

#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ha_client.h"
#include "ha_secrets.h"
#include "ha_tesla.h"

#ifndef HA_ACCESS_TOKEN
#error "Define HA_ACCESS_TOKEN in the local, Git-ignored main/hal/ha_secrets.h"
#endif

namespace ha_tesla_rest {
namespace {

constexpr char kTag[] = "HA_TESLA_REST";
constexpr size_t kMaxResponseBytes = 8192;

TaskHandle_t rest_task_handle = nullptr;
std::atomic<uint32_t> request_sequence{0};

struct Response {
    std::string body;
    bool too_large = false;
};

esp_err_t on_http_event(esp_http_client_event_t* event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || !event->user_data) return ESP_OK;
    auto* response = static_cast<Response*>(event->user_data);
    if (event->data_len < 0 ||
        static_cast<size_t>(event->data_len) > kMaxResponseBytes - response->body.size()) {
        response->too_large = true;
        return ESP_FAIL;
    }
    if (event->data_len > 0 && event->data) {
        response->body.append(static_cast<const char*>(event->data), event->data_len);
    }
    return ESP_OK;
}

void fetch_initial(uint32_t sequence, ha_tesla::Entity entity)
{
    const auto before = ha_tesla::snapshot();
    if (!before.online || sequence != request_sequence.load()) return;
    const std::string url = std::string("http://192.168.0.73:8123/api/states/") + ha_tesla::entity_id(entity);
    ESP_LOGI(kTag, "Tesla initial REST request started");

    Response response;
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.buffer_size = 512;
    config.event_handler = on_http_event;
    config.user_data = &response;
    config.disable_auto_redirect = true;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(kTag, "Tesla initial REST request failed: client init");
        return;
    }
    std::string authorization = "Bearer ";
    authorization += HA_ACCESS_TOKEN;
    esp_http_client_set_header(client, "Authorization", authorization.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");

    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (sequence != request_sequence.load() || !ha_client::is_authenticated()) return;
    if (result != ESP_OK || response.too_large) {
        ESP_LOGW(kTag, "Tesla initial REST request failed");
        return;
    }
    ESP_LOGI(kTag, "Tesla initial REST HTTP status: %d", status);
    if (status != 200) return;

    cJSON* json = cJSON_ParseWithLength(response.body.data(), response.body.size());
    const cJSON* entity_id = cJSON_GetObjectItemCaseSensitive(json, "entity_id");
    const cJSON* state = cJSON_GetObjectItemCaseSensitive(json, "state");
    if (cJSON_IsString(entity_id) &&
        strcmp(entity_id->valuestring, ha_tesla::entity_id(entity)) == 0) {
        const char* value = cJSON_IsString(state) ? state->valuestring : nullptr;
        if (ha_tesla::update_initial(entity, value, before)) {
            ESP_LOGI(kTag, "Tesla initial REST %s applied", ha_tesla::entity_id(entity));
        } else {
            ESP_LOGI(kTag, "Tesla initial REST %s superseded by newer state", ha_tesla::entity_id(entity));
        }
    }
    cJSON_Delete(json);
}

void rest_task(void*)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const auto sequence = request_sequence.load();
        for (auto entity : {ha_tesla::Entity::Soc, ha_tesla::Entity::Trunk, ha_tesla::Entity::Climate, ha_tesla::Entity::Location, ha_tesla::Entity::Odometer}) {
            fetch_initial(sequence, entity);
        }
    }
}

}  // namespace

bool init()
{
    if (rest_task_handle) return true;
    return xTaskCreate(rest_task, "ha_tesla_rest", 8192, nullptr, 4,
                       &rest_task_handle) == pdPASS;
}

bool request_initial()
{
    if (!rest_task_handle) return false;
    request_sequence.fetch_add(1);
    xTaskNotifyGive(rest_task_handle);
    return true;
}

}  // namespace ha_tesla_rest
