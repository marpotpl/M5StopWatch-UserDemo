#include "app_tesla.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <esp_timer.h>
#include <cmath>
#include <cstdio>

namespace {
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
lv_obj_t* label(lv_obj_t* parent, int y, const lv_font_t* font, uint32_t color)
{
    auto* obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(obj, 410);
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, y);
    return obj;
}
void button_label(lv_obj_t* parent, const char* text, int x, int angle, uint32_t color)
{
    auto* obj = label(parent, -145, &tesla_text_26, color);
    lv_label_set_text(obj, text);
    lv_obj_set_width(obj, LV_SIZE_CONTENT);
    lv_obj_update_layout(obj);
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, 0);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, 0);
    // LVGL expresses rotation in tenths of a degree.
    lv_obj_set_style_transform_rotation(obj, angle, 0);
    lv_obj_align(obj, LV_ALIGN_CENTER, x, -145);
}
const char* trunk_text(ha_tesla::Trunk t)
{
    using ha_tesla::Trunk;
    switch (t) {
        case Trunk::Open: return "Bagażnik: OTWARTY";
        case Trunk::Closed: return "Bagażnik: zamknięty";
        case Trunk::Opening: return "Bagażnik: OTWIERANIE";
        case Trunk::Closing: return "Bagażnik: ZAMYKANIE";
        case Trunk::Unavailable: return "Bagażnik: niedostępny";
        default: return "Bagażnik: brak danych";
    }
}
const char* question(ha_tesla::Action a)
{
    using ha_tesla::Action;
    switch (a) {
        case Action::OpenTrunk: return "OTWORZYĆ\nBAGAŻNIK?";
        case Action::CloseTrunk: return "ZAMKNĄĆ\nBAGAŻNIK?";
        case Action::ClimateOn: return "WŁĄCZYĆ\nKLIMATYZACJĘ?";
        case Action::ClimateOff: return "WYŁĄCZYĆ\nKLIMATYZACJĘ?";
        default: return "";
    }
}
}
AppTesla::AppTesla()
{
    setAppInfo().name = "Tesla";
    setAppInfo().icon = (void*)&icon_tesla;
}
void AppTesla::onOpen()
{
    _controls = tesla_ui::Controls{};
    _last_revision = UINT32_MAX;
    _last_pending = ha_tesla::Action::None;
    _last_notice = tesla_ui::Notice::None;
    _last_command = ha_tesla::snapshot().command;
    _command_visible = ha_tesla::busy(ha_tesla::snapshot());
    _command_at = now_ms();
    LvglLockGuard lock;
    _panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_panel, 466, 466);
    lv_obj_center(_panel);
    lv_obj_set_style_radius(_panel, 0, 0);
    lv_obj_set_style_border_width(_panel, 0, 0);
    lv_obj_set_style_pad_all(_panel, 0, 0);
    lv_obj_set_style_bg_color(_panel, lv_color_hex(0), 0);
    lv_obj_remove_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);
    button_label(_panel, "Bagażnik", -135, -450, 0xFF981F);
    button_label(_panel, "Klima", 135, 450, 0x3399FF);
    auto* reading = lv_obj_create(_panel);
    lv_obj_set_size(reading, 380, 140);
    lv_obj_align(reading, LV_ALIGN_CENTER, 0, -43);
    lv_obj_set_style_bg_opa(reading, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(reading, 0, 0);
    lv_obj_set_style_pad_all(reading, 0, 0);
    lv_obj_remove_flag(reading, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(reading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reading, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _value = lv_label_create(reading);
    lv_obj_set_style_text_font(_value, &CommissionerMedium108, 0);
    lv_obj_set_style_text_color(_value, lv_color_hex(0xFFFFFF), 0);
    auto* percent = lv_label_create(reading);
    lv_label_set_text(percent, "%");
    lv_obj_set_style_text_font(percent, &lv_font_maple_mono_medium_48, 0);
    lv_obj_set_style_text_color(percent, lv_color_hex(0xFFFFFF), 0);
    auto* caption = label(_panel, 35, &tesla_text_26, 0xAEB8C3);
    lv_label_set_text(caption, "poziom akumulatora");
    _odometer = label(_panel, 73, &tesla_text_26, 0xAEB8C3);
    _trunk = label(_panel, 108, &tesla_text_26, 0xAEB8C3);
    _climate = label(_panel, 143, &tesla_text_26, 0xAEB8C3);
    _message = label(_panel, 180, &tesla_text_26, 0xFFFFFF);
    lv_obj_set_width(_message, 280);
    _overlay = lv_obj_create(_panel);
    lv_obj_set_size(_overlay, 466, 466);
    lv_obj_center(_overlay);
    lv_obj_set_style_bg_color(_overlay, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_overlay, 0, 0);
    lv_obj_set_style_pad_all(_overlay, 0, 0);
    lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);
    _question = label(_overlay, -38, &tesla_text_32, 0xFFFFFF);
    auto* hint = label(_overlay, 65, &tesla_text_26, 0xFFC15A);
    lv_label_set_text(hint, "A+B = POTWIERDŹ");
    auto* timeout = label(_overlay, 108, &tesla_text_26, 0xAEB8C3);
    lv_label_set_text(timeout, "Wygasa po 10 s");
    refresh(ha_tesla::snapshot(), now_ms());
}
void AppTesla::refresh(const ha_tesla::Snapshot& s, uint32_t now)
{
    using namespace ha_tesla;
    _last_revision = s.revision;
    _last_pending = _controls.pending;
    _last_notice = _controls.notice;
    char value[12];
    if (s.available) snprintf(value, sizeof(value), "%d", static_cast<int>(std::lround(s.soc)));
    else snprintf(value, sizeof(value), "--");
    lv_label_set_text(_value, value);
    char odometer[32];
    if (s.odometer_available) snprintf(odometer, sizeof(odometer), "Przebieg: %.0f km", s.odometer_km);
    else snprintf(odometer, sizeof(odometer), "Przebieg: -- km");
    lv_label_set_text(_odometer, odometer);
    lv_label_set_text(_trunk, trunk_text(s.trunk));
    lv_obj_set_style_text_color(_trunk, lv_color_hex(s.trunk == Trunk::Open || s.trunk == Trunk::Opening ? 0xFF4040 : 0xAEB8C3), 0);
    lv_label_set_text(_climate, s.climate == Climate::On ? "Klima: WŁ." :
        s.climate == Climate::Off ? "Klima: wył." :
        s.climate == Climate::Unavailable ? "Klima: niedostępna" : "Klima: brak danych");
    lv_obj_set_style_text_color(_climate, lv_color_hex(s.climate == Climate::On ? 0xFF4040 : 0xAEB8C3), 0);
    if (_controls.pending != Action::None) {
        lv_label_set_text(_question, question(_controls.pending));
        lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    const char* message = "";
    if (!s.online) message = "HA OFFLINE";
    else if (_controls.notice != tesla_ui::Notice::None) {
        switch (_controls.notice) {
            case tesla_ui::Notice::Cancelled: message = "ANULOWANO"; break;
            case tesla_ui::Notice::Offline: message = "HA OFFLINE"; break;
            case tesla_ui::Notice::Busy: message = "WYSYŁANIE..."; break;
            case tesla_ui::Notice::HomeRequired: message = "TYLKO W DOMU"; break;
            case tesla_ui::Notice::Moving: message = "BAGAŻNIK W RUCHU"; break;
            default: message = "BRAK DANYCH"; break;
        }
    } else if (busy(s)) message = "WYSYŁANIE...";
    else if (_command_visible && now - _command_at < 4000) {
        switch (s.command) {
            case Command::Accepted: message = "POLECENIE PRZYJĘTE"; break;
            case Command::Failed: message = "BŁĄD POLECENIA"; break;
            case Command::Timeout: message = "BRAK ODPOWIEDZI HA"; break;
            case Command::Offline: message = "HA OFFLINE"; break;
            case Command::Stale: message = "ANULOWANO"; break;
            default: break;
        }
    }
    lv_label_set_text(_message, message);
}
void AppTesla::onRunning()
{
    GetHAL().updateButtonStates();
    const uint32_t now = now_ms();
    auto state = ha_tesla::snapshot();
    auto& a = GetHAL().btnA;
    auto& b = GetHAL().btnB;
    const auto result = _controls.update(now,
        {a.isPressed(), b.isPressed(), a.isHolding(), b.isHolding(), a.wasClicked(), b.wasClicked()}, state);
    if (result.home) { close(); return; }
    if (result.confirmed != ha_tesla::Action::None) {
        if (!ha_tesla::request(result.confirmed, _controls.prepared)) {
            _controls.notice = state.online ? tesla_ui::Notice::Cancelled : tesla_ui::Notice::Offline;
            _controls.notice_at = now;
        }
        state = ha_tesla::snapshot();
    }
    if (state.command != _last_command) {
        _last_command = state.command;
        _command_at = now;
        _command_visible = true;
    }
    const bool expired = _command_visible && !ha_tesla::busy(state) && now - _command_at >= 4000;
    if (expired) _command_visible = false;
    if (state.revision != _last_revision || _last_pending != _controls.pending ||
        _last_notice != _controls.notice || expired) {
        LvglLockGuard lock;
        refresh(state, now);
    }
}
void AppTesla::onClose()
{
    _controls = tesla_ui::Controls{};
    LvglLockGuard lock;
    if (_panel) lv_obj_delete(_panel);
    _panel = _value = _odometer = _trunk = _climate = _message = _overlay = _question = nullptr;
}
