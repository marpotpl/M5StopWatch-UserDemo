#pragma once
#include <cstdint>

namespace ha_tesla {
enum class Entity : uint8_t { Soc, Trunk, Climate, Location, Odometer };
enum class Trunk : uint8_t { Unknown, Unavailable, Closed, Open, Opening, Closing };
enum class Climate : uint8_t { Unknown, Unavailable, Off, On };
enum class Action : uint8_t { None, OpenTrunk, CloseTrunk, ClimateOn, ClimateOff };
enum class Command : uint8_t { Idle, Queued, Sending, Accepted, Failed, Timeout, Offline, Stale };
struct Snapshot {
    bool available = false;
    float soc = 0;
    bool odometer_available = false;
    double odometer_km = 0;
    Trunk trunk = Trunk::Unknown;
    Climate climate = Climate::Unknown;
    bool at_home = false;
    bool online = false;
    uint32_t generation = 0;
    uint32_t entity_revision[5]{};
    uint32_t revision = 0;
    Command command = Command::Idle;
    uint32_t command_id = 0;
};
const char* entity_id(Entity entity);
Snapshot snapshot();
bool parse_soc(const char* text, float& soc);
// Every reconnect invalidates outstanding REST responses and queued commands.
void set_online(bool online);
void update(Entity entity, const char* state);
bool update_initial(Entity entity, const char* state, const Snapshot& before);
bool requires_home(Action action);
bool allowed(const Snapshot& state, Action action);
bool busy(const Snapshot& state);
// Only called after a fresh physical A+B confirmation in AppTesla.
bool request(Action action, const Snapshot& confirmed);
bool take_command(Action& action, uint32_t& id);
void command_result(uint32_t id, Command result);
void poll_timeout();
}  // namespace ha_tesla
