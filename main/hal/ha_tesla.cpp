#include "ha_tesla.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <esp_timer.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ha_tesla {
namespace {
portMUX_TYPE model_lock = portMUX_INITIALIZER_UNLOCKED;
Snapshot current;
Action queued_action = Action::None;
uint32_t command_at = 0;
uint32_t command_entity_revision = 0;
uint32_t command_location_revision = 0;
uint32_t next_id = 10;
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
Entity action_entity(Action a)
{
    return a == Action::OpenTrunk || a == Action::CloseTrunk ? Entity::Trunk : Entity::Climate;
}
void apply(Entity entity, const char* state)
{
    const char* s = state ? state : "unknown";
    if (entity == Entity::Soc) {
        current.available = parse_soc(s, current.soc);
    } else if (entity == Entity::Trunk) {
        current.trunk = strcmp(s, "closed") == 0 ? Trunk::Closed :
                        strcmp(s, "open") == 0 ? Trunk::Open :
                        strcmp(s, "opening") == 0 ? Trunk::Opening :
                        strcmp(s, "closing") == 0 ? Trunk::Closing :
                        strcmp(s, "unavailable") == 0 ? Trunk::Unavailable : Trunk::Unknown;
    } else if (entity == Entity::Odometer) {
        char* end = nullptr;
        const double km = strtod(s, &end);
        while (end && (*end == ' ' || *end == '\t')) ++end;
        current.odometer_available = end != s && end && *end == '\0' &&
            std::isfinite(km) && km >= 0 && km <= UINT32_MAX;
        if (current.odometer_available) current.odometer_km = km;
    } else if (entity == Entity::Location) {
        current.at_home = strcmp(s, "home") == 0;
    } else {
        // Verified on this entity: hvac_modes = [heat_cool, off].
        current.climate = strcmp(s, "off") == 0 ? Climate::Off :
                          strcmp(s, "heat_cool") == 0 ? Climate::On :
                          strcmp(s, "unavailable") == 0 ? Climate::Unavailable : Climate::Unknown;
    }
    ++current.entity_revision[static_cast<unsigned>(entity)];
    ++current.revision;
}
}
const char* entity_id(Entity entity)
{
    switch (entity) {
        case Entity::Soc: return "sensor.teslunia_poziom_naladowania_baterii";
        case Entity::Trunk: return "cover.teslunia_trunk";
        case Entity::Climate: return "climate.teslunia_klimat";
        case Entity::Location: return "device_tracker.teslunia_location";
        case Entity::Odometer: return "sensor.teslunia_odometer";
    }
    return "";
}
Snapshot snapshot()
{
    portENTER_CRITICAL(&model_lock);
    const Snapshot result = current;
    portEXIT_CRITICAL(&model_lock);
    return result;
}
bool parse_soc(const char* text, float& soc)
{
    if (!text || !*text) return false;
    char* end = nullptr;
    const float parsed = strtof(text, &end);
    while (end && (*end == ' ' || *end == '\t')) ++end;
    if (end == text || !end || *end != '\0' || !std::isfinite(parsed) || parsed < 0 || parsed > 100) return false;
    soc = parsed;
    return true;
}
void set_online(bool online)
{
    portENTER_CRITICAL(&model_lock);
    if (current.online != online) {
        current.online = online;
        ++current.generation;
        current.available = false;
        current.odometer_available = false;
        current.at_home = false;
        current.trunk = Trunk::Unknown;
        current.climate = Climate::Unknown;
        for (auto& r : current.entity_revision) ++r;
        if (busy(current)) current.command = Command::Offline;
        queued_action = Action::None;
        ++current.revision;
    }
    portEXIT_CRITICAL(&model_lock);
}
void update(Entity entity, const char* state)
{
    portENTER_CRITICAL(&model_lock);
    if (current.online) apply(entity, state);
    portEXIT_CRITICAL(&model_lock);
}
bool update_initial(Entity entity, const char* state, const Snapshot& before)
{
    const unsigned i = static_cast<unsigned>(entity);
    portENTER_CRITICAL(&model_lock);
    const bool valid = current.online && current.generation == before.generation &&
                       current.entity_revision[i] == before.entity_revision[i];
    if (valid) apply(entity, state);
    portEXIT_CRITICAL(&model_lock);
    return valid;
}
bool requires_home(Action a)
{
    return a == Action::OpenTrunk || a == Action::ClimateOn;
}
bool allowed(const Snapshot& s, Action a)
{
    if (!s.online || (requires_home(a) && !s.at_home)) return false;
    switch (a) {
        case Action::OpenTrunk: return s.trunk == Trunk::Closed;
        case Action::CloseTrunk: return s.trunk == Trunk::Open;
        case Action::ClimateOn: return s.climate == Climate::Off;
        case Action::ClimateOff: return s.climate == Climate::On;
        default: return false;
    }
}
bool busy(const Snapshot& s) { return s.command == Command::Queued || s.command == Command::Sending; }
bool request(Action a, const Snapshot& confirmed)
{
    const unsigned i = static_cast<unsigned>(action_entity(a));
    portENTER_CRITICAL(&model_lock);
    const bool ok = allowed(current, a) && !busy(current) &&
                    current.generation == confirmed.generation &&
                    current.entity_revision[i] == confirmed.entity_revision[i] &&
                    (!requires_home(a) || current.entity_revision[3] == confirmed.entity_revision[3]) && next_id < 0x7fffffff;
    if (ok) {
        current.command = Command::Queued;
        current.command_id = next_id++;
        command_entity_revision = current.entity_revision[i];
        command_location_revision = current.entity_revision[3];
        queued_action = a;
        command_at = now_ms();
        ++current.revision;
    }
    portEXIT_CRITICAL(&model_lock);
    return ok;
}
bool take_command(Action& action, uint32_t& id)
{
    portENTER_CRITICAL(&model_lock);
    bool ok = current.command == Command::Queued;
    if (ok && (!allowed(current, queued_action) ||
               current.entity_revision[static_cast<unsigned>(action_entity(queued_action))] != command_entity_revision ||
               (requires_home(queued_action) && current.entity_revision[3] != command_location_revision))) {
        current.command = Command::Stale;
        ++current.revision;
        ok = false;
    }
    if (ok) {
        action = queued_action;
        id = current.command_id;
        current.command = Command::Sending;
        ++current.revision;
    }
    portEXIT_CRITICAL(&model_lock);
    return ok;
}
void command_result(uint32_t id, Command result)
{
    portENTER_CRITICAL(&model_lock);
    if (id == current.command_id && busy(current)) {
        current.command = result;
        ++current.revision;
    }
    portEXIT_CRITICAL(&model_lock);
}
void poll_timeout()
{
    portENTER_CRITICAL(&model_lock);
    if (busy(current) && now_ms() - command_at >= 15000) {
        current.command = Command::Timeout;
        ++current.revision;
    }
    portEXIT_CRITICAL(&model_lock);
}
}  // namespace ha_tesla
