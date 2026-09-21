#include "app_brama.h"

#include <assets/assets.h>
#include <hal/ha_client.h>
#include <hal/hal.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace {
constexpr char kTag[] = "GATE";
constexpr char kEntity[] = "button.esp32s3_dom_sterownik_bramy_dom_brama_step_by_step";
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

lv_obj_t* label(lv_obj_t* parent, const char* text, int y, const lv_font_t* font, uint32_t color)
{
    auto* obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(obj, 430);
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, y);
    return obj;
}

void rotated_button_label(lv_obj_t* parent)
{
    auto* obj = label(parent, "otwórz bramę", 0, &tesla_text_26, 0xFF981F);
    lv_obj_set_width(obj, LV_SIZE_CONTENT);
    lv_obj_update_layout(obj);
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, 0);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, 0);
    lv_obj_set_style_transform_rotation(obj, -450, 0);
    lv_obj_align(obj, LV_ALIGN_CENTER, -145, -145);
}
}

AppBrama::AppBrama()
{
    setAppInfo().name = "Brama";
    setAppInfo().icon = (void*)&icon_brama;
}

void AppBrama::onOpen()
{
    ESP_LOGI(kTag, "app opened");
    _pending = false;
    _ready = false;
    _input_armed = false;
    _home_latched = false;
    _message_until = 0;
    _last_button_status = ha_client::button_press_status();
    LvglLockGuard lock;
    _panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_panel, 466, 466);
    lv_obj_center(_panel);
    lv_obj_set_style_radius(_panel, 0, 0);
    lv_obj_set_style_border_width(_panel, 0, 0);
    lv_obj_set_style_pad_all(_panel, 0, 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_panel, lv_color_hex(0), 0);
    lv_obj_remove_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);

    auto* image = lv_image_create(_panel);
    lv_image_set_src(image, &icon_brama);
    lv_obj_set_size(image, 466, 466);
    lv_obj_center(image);
    lv_obj_set_style_opa(image, LV_OPA_COVER, 0);

    auto* shade = lv_obj_create(_panel);
    lv_obj_set_size(shade, 466, 466);
    lv_obj_center(shade);
    lv_obj_set_style_bg_color(shade, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(shade, LV_OPA_30, 0);
    lv_obj_set_style_border_width(shade, 0, 0);
    lv_obj_set_style_pad_all(shade, 0, 0);
    lv_obj_remove_flag(shade, LV_OBJ_FLAG_SCROLLABLE);

    label(_panel, "BRAMA", -35, &tesla_text_32, 0xFFFFFF);
    rotated_button_label(_panel);
    _message = label(_panel, "", 92, &tesla_text_26, 0xFFFFFF);

    _overlay = lv_obj_create(_panel);
    lv_obj_set_size(_overlay, 466, 466);
    lv_obj_center(_overlay);
    lv_obj_set_style_bg_color(_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(_overlay, 0, 0);
    lv_obj_set_style_pad_all(_overlay, 0, 0);
    lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);
    _question = label(_overlay, "OTWORZYĆ\nBRAMĘ?", -34, &tesla_text_32, 0xFFFFFF);
    auto* hint = label(_overlay, "A+B = POTWIERDŹ", 60, &tesla_text_26, 0xFFC15A);
    auto* timeout = label(_overlay, "Wygasa po 10 s", 102, &tesla_text_26, 0xAEB8C3);
    (void)hint;
    (void)timeout;
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

void AppBrama::refresh()
{
    if (!_panel) return;
    if (_pending) {
        lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_message, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(_message, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppBrama::onRunning()
{
    GetHAL().updateButtonStates();
    auto& a = GetHAL().btnA;
    auto& b = GetHAL().btnB;
    const uint32_t now = now_ms();
    if (!a.isPressed() && !b.isPressed()) _input_armed = true;
    if (!_input_armed) return;

    if (_pending) {
        if (!a.isPressed() && !b.isPressed()) _ready = true;
        if (a.isPressed() && b.isPressed() && _ready) {
            _pending = false;
            _ready = false;
            if (!ha_client::request_button_press(kEntity)) {
                lv_label_set_text(_message, ha_client::is_authenticated() ? "BŁĄD HA" : "BRAK HA");
                _message_until = now + 3000;
            } else {
                ESP_LOGI(kTag, "confirmation accepted");
                ESP_LOGI(kTag, "button.press queued");
                lv_label_set_text(_message, "POLECENIE WYSŁANE");
                _message_until = now + 4000;
            }
            refresh();
            return;
        }
        if (now - _pending_at >= 10000) {
            _pending = false;
            _ready = false;
            ESP_LOGI(kTag, "confirmation timeout");
            lv_label_set_text(_message, "ANULOWANO");
            _message_until = now + 2000;
            refresh();
        }
        return;
    }

    if (a.isHolding() && b.isHolding()) {
        if (!_home_latched) {
            _home_latched = true;
            close();
        }
        return;
    }
    if (!a.isPressed() && !b.isPressed()) _home_latched = false;
    if (a.wasClicked() && !b.wasClicked()) {
        _pending = true;
        _ready = false;
        _pending_at = now;
        ESP_LOGI(kTag, "open requested, awaiting confirmation");
        refresh();
        return;
    }
    if (_message_until != 0 && now >= _message_until) {
        _message_until = 0;
        lv_label_set_text(_message, "");
    }

    const auto status = ha_client::button_press_status();
    if (status != _last_button_status && status == ha_client::ButtonPressStatus::Accepted) {
        lv_label_set_text(_message, "HA: OK");
        _message_until = now + 3000;
    } else if (status != _last_button_status && status == ha_client::ButtonPressStatus::Failed) {
        lv_label_set_text(_message, "BŁĄD HA");
        _message_until = now + 3000;
    }
    _last_button_status = status;
}

void AppBrama::onClose()
{
    _pending = false;
    LvglLockGuard lock;
    if (_panel) lv_obj_delete(_panel);
    _panel = _overlay = _message = _question = nullptr;
}
