#pragma once

#include <mooncake.h>
#include <lvgl.h>
#include <hal/ha_client.h>

class AppBrama : public mooncake::AppAbility {
public:
    AppBrama();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void refresh();
    lv_obj_t* _panel = nullptr;
    lv_obj_t* _overlay = nullptr;
    lv_obj_t* _message = nullptr;
    lv_obj_t* _question = nullptr;
    bool _pending = false;
    bool _ready = false;
    bool _input_armed = false;
    bool _home_latched = false;
    uint32_t _pending_at = 0;
    uint32_t _message_until = 0;
    ha_client::ButtonPressStatus _last_button_status = ha_client::ButtonPressStatus::Idle;
};
