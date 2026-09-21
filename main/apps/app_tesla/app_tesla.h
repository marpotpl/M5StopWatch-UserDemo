#pragma once
#include "tesla_controls.h"
#include <mooncake.h>
#include <lvgl.h>

class AppTesla : public mooncake::AppAbility {
public:
    AppTesla();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    void refresh(const ha_tesla::Snapshot& state, uint32_t now);
    tesla_ui::Controls _controls;
    lv_obj_t* _panel = nullptr;
    lv_obj_t* _value = nullptr;
    lv_obj_t* _odometer = nullptr;
    lv_obj_t* _trunk = nullptr;
    lv_obj_t* _climate = nullptr;
    lv_obj_t* _message = nullptr;
    lv_obj_t* _overlay = nullptr;
    lv_obj_t* _question = nullptr;
    uint32_t _last_revision = UINT32_MAX;
    ha_tesla::Action _last_pending = ha_tesla::Action::None;
    tesla_ui::Notice _last_notice = tesla_ui::Notice::None;
    ha_tesla::Command _last_command = ha_tesla::Command::Idle;
    uint32_t _command_at = 0;
    bool _command_visible = false;
};
