#pragma once
#include <hal/ha_tesla.h>

// Pure input state machine; only AppTesla uses it. No transport or LVGL calls.
namespace tesla_ui {
struct Buttons { bool a, b, holding_a, holding_b, click_a, click_b; };
enum class Notice { None, Cancelled, Offline, NoData, Moving, Busy, HomeRequired };
struct Result { bool home = false; ha_tesla::Action confirmed = ha_tesla::Action::None; };
class Controls {
public:
    ha_tesla::Action pending = ha_tesla::Action::None;
    ha_tesla::Snapshot prepared;
    Notice notice = Notice::None;
    uint32_t notice_at = 0;

    Result update(uint32_t now, Buttons b, const ha_tesla::Snapshot& s)
    {
        using namespace ha_tesla;
        Result result;
        if (notice != Notice::None && now - notice_at >= 2000) notice = Notice::None;
        if (pending != Action::None &&
            (now - pending_at >= 10000 || !s.online || s.generation != prepared.generation ||
             !allowed(s, pending) || entity_changed(s))) {
            const bool home_blocked = requires_home(pending) && !s.at_home;
            pending = Action::None;
            show(!s.online ? Notice::Offline : home_blocked ? Notice::HomeRequired : Notice::Cancelled, now);
            suppress = true;
        }
        // Consume a chord through BOTH releases, including trailing click events.
        if (suppress) {
            if (!b.a && !b.b) suppress = false;
            return result;
        }
        if (pending != Action::None) {
            if (!b.a && !b.b) ready = true;
            if (b.a && b.b) {
                if (ready) {
                    result.confirmed = pending;
                    pending = Action::None;
                    notice = Notice::None;
                }
                suppress = true;
                return result;
            }
        } else if (b.a && b.b) {
            chord_seen = true;
            if (b.holding_a && b.holding_b) {
                result.home = true;
                suppress = true;
                chord_seen = false;
            }
            return result;
        }
        if (chord_seen) {
            if (!b.a && !b.b) chord_seen = false;
            return result;
        }
        if ((b.click_a || b.click_b) && !b.a && !b.b) {
            pending = Action::None; // A new choice cancels the old choice.
            notice = Notice::None;
            if (b.click_a && b.click_b) return result;
            if (!s.online) show(Notice::Offline, now);
            else if (busy(s)) show(Notice::Busy, now);
            else {
                Action action = Action::None;
                if (b.click_a) {
                    if (s.trunk == Trunk::Closed) action = Action::OpenTrunk;
                    else if (s.trunk == Trunk::Open) action = Action::CloseTrunk;
                    else if (s.trunk == Trunk::Opening || s.trunk == Trunk::Closing) show(Notice::Moving, now);
                } else {
                    if (s.climate == Climate::Off) action = Action::ClimateOn;
                    else if (s.climate == Climate::On) action = Action::ClimateOff;
                }
                if (requires_home(action) && !s.at_home) {
                    show(Notice::HomeRequired, now);
                } else if (action != Action::None) {
                    pending = action;
                    prepared = s;
                    pending_at = now;
                    ready = false; // Requires a separate released sample before confirmation.
                } else if (notice == Notice::None) show(Notice::NoData, now);
            }
        }
        return result;
    }
private:
    bool suppress = true; // Ignore buttons used to enter the app.
    bool chord_seen = false;
    bool ready = false;
    uint32_t pending_at = 0;
    bool entity_changed(const ha_tesla::Snapshot& s) const
    {
        const unsigned i = pending == ha_tesla::Action::OpenTrunk || pending == ha_tesla::Action::CloseTrunk ? 1 : 2;
        return s.entity_revision[i] != prepared.entity_revision[i] ||
               (ha_tesla::requires_home(pending) && s.entity_revision[3] != prepared.entity_revision[3]);
    }
    void show(Notice value, uint32_t now) { notice = value; notice_at = now; }
};
}  // namespace tesla_ui
