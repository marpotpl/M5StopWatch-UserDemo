#include <apps/app_tesla/tesla_controls.h>
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>
using namespace ha_tesla;
using namespace tesla_ui;
int64_t test_time_us = 0;
constexpr Buttons released{false,false,false,false,false,false};
constexpr Buttons clickA{false,false,false,false,true,false};
constexpr Buttons clickB{false,false,false,false,false,true};
constexpr Buttons chord{true,true,false,false,false,false};
constexpr Buttons held{true,true,true,true,false,false};
void ready_model()
{
    set_online(false);
    set_online(true);
    update(Entity::Location,"home");
    update(Entity::Soc,"99");
    update(Entity::Trunk,"closed");
    update(Entity::Climate,"off");
}
int main()
{
    ready_model();
    float f = 0;
    assert(parse_soc("25",f) && f == 25);
    for (const char* s : {"nan","inf","101","-1","","unknown","23garbage"}) assert(!parse_soc(s,f));
    const auto initial = snapshot();
    update(Entity::Trunk,"open");
    assert(!update_initial(Entity::Trunk,"closed",initial));
    assert(update_initial(Entity::Climate,"off",initial)); // Independent entity revisions.
    set_online(false); set_online(true);
    assert(!update_initial(Entity::Soc,"99",initial)); // Old session REST cannot revive data.
    for (const char* s : {"unknown","unavailable","unexpected","opening","closing"}) {
        update(Entity::Trunk,s);
        assert(!allowed(snapshot(),Action::OpenTrunk) && !allowed(snapshot(),Action::CloseTrunk));
    }
    for (const char* s : {"unknown","unavailable","unexpected","cool"}) {
        update(Entity::Climate,s);
        assert(!allowed(snapshot(),Action::ClimateOn) && !allowed(snapshot(),Action::ClimateOff));
    }
    update(Entity::Climate,"heat_cool"); assert(allowed(snapshot(),Action::ClimateOff));
    ready_model();
    auto s = snapshot();
    Controls c;
    c.update(0,released,s);
    assert(c.update(10,clickA,s).confirmed == Action::None);
    assert(c.pending == Action::OpenTrunk);
    c.update(20,released,s);
    assert(c.update(30,chord,s).confirmed == Action::OpenTrunk);
    assert(!c.update(600,held,s).home); // Confirmation must not leak into GoHome.
    assert(c.update(650,held,s).confirmed == Action::None);
    c.update(700,{false,true,false,true,true,false},s);
    c.update(800,clickB,s); // Trailing release must not arm another action.
    assert(c.pending == Action::None);
    assert(c.update(900,held,s).home); // Fresh chord restores factory behavior.

    c = Controls{}; c.update(0,released,s);
    c.update(10,clickA,s); c.update(20,released,s);
    c.update(30,clickB,s); assert(c.pending == Action::ClimateOn);
    c.update(40,released,s);
    assert(c.update(50,chord,s).confirmed == Action::ClimateOn);
    c = Controls{}; c.update(0,released,s); c.update(10,clickA,s);
    assert(c.update(10010,chord,s).confirmed == Action::None);
    assert(c.notice == Notice::Cancelled && c.pending == Action::None);
    assert(!c.update(11000,held,s).home);
    c = Controls{}; c.update(0,released,s); c.update(10,clickA,s); c.update(20,released,s);
    update(Entity::Trunk,"open");
    assert(c.update(30,chord,snapshot()).confirmed == Action::None);
    c = Controls{}; c.update(0,released,s); c.update(10,clickA,s); c.update(20,released,s);
    set_online(false);
    assert(c.update(30,chord,snapshot()).confirmed == Action::None);
    c = Controls{}; c.update(0,released,s); c.update(10,chord,s);
    c.update(20,clickA,s); assert(c.pending == Action::None); // Unprepared short chord.
    c = Controls{}; c.update(0,released,s);
    c.update(UINT32_MAX-500,clickA,s); c.update(UINT32_MAX-400,released,s);
    assert(c.update(9500,chord,s).confirmed == Action::None); // Timer wrap.

    ready_model(); s = snapshot();
    assert(request(Action::OpenTrunk,s));
    assert(!request(Action::OpenTrunk,s)); // Queue reservation is atomic.
    Action a; uint32_t id;
    assert(take_command(a,id) && a == Action::OpenTrunk);
    assert(!take_command(a,id));
    assert(!request(Action::ClimateOn,s)); // One request at a time, even across domains.
    command_result(id+1,Command::Accepted); assert(busy(snapshot()));
    command_result(id,Command::Accepted); assert(!busy(snapshot()));
    assert(snapshot().trunk == Trunk::Closed); // HA success is NOT a physical state update.
    assert(request(Action::OpenTrunk,snapshot()));
    test_time_us = 15000000; poll_timeout(); assert(snapshot().command == Command::Timeout);
    assert(!take_command(a,id)); // Expired queued action must not execute later.
    assert(request(Action::OpenTrunk,snapshot()));
    assert(take_command(a,id));
    set_online(false); set_online(true); update(Entity::Trunk,"closed");
    command_result(id,Command::Accepted); assert(snapshot().command == Command::Offline);
    assert(!request(Action::OpenTrunk,s)); // Old UI confirmation after reconnect rejected.
    update(Entity::Location,"home");
    assert(request(Action::OpenTrunk,snapshot()));
    update(Entity::Trunk,"open");
    assert(!take_command(a,id) && snapshot().command == Command::Stale);
    // Location gate: only exact home permits opening or turning climate on.
    for (const char* location : {"not_home", "work", "unknown", "unavailable", "", "Home"}) {
        ready_model(); update(Entity::Location,location);
        s = snapshot();
        assert(!allowed(s,Action::OpenTrunk) && !allowed(s,Action::ClimateOn));
        assert(!request(Action::OpenTrunk,s) && !request(Action::ClimateOn,s));
        for (const auto click : {clickA,clickB}) {
            Controls gate; gate.update(0,released,s); gate.update(10,click,s);
            assert(gate.pending == Action::None && gate.notice == Notice::HomeRequired);
            assert(gate.update(20,chord,s).confirmed == Action::None);
        }
        update(Entity::Trunk,"open"); update(Entity::Climate,"heat_cool");
        assert(allowed(snapshot(),Action::CloseTrunk) && allowed(snapshot(),Action::ClimateOff));
    }
    for (const auto action : {Action::OpenTrunk,Action::ClimateOn}) {
        ready_model(); s = snapshot();
        Controls gate; gate.update(0,released,s);
        gate.update(10,action == Action::OpenTrunk ? clickA : clickB,s);
        gate.update(20,released,s);
        update(Entity::Location,"not_home");
        assert(gate.update(30,chord,snapshot()).confirmed == Action::None);
        assert(gate.notice == Notice::HomeRequired);
        assert(!request(action,s));
        update(Entity::Location,"home");
        assert(!request(action,s)); // Even away -> home invalidates the old confirmation.
        assert(request(action,snapshot()));
        update(Entity::Location,"not_home"); update(Entity::Location,"home");
        assert(!take_command(a,id)); // Also invalidate a command already queued for transport.
        assert(snapshot().command == Command::Stale);
    }
    ready_model(); s = snapshot(); update(Entity::Location,"not_home");
    assert(!update_initial(Entity::Location,"home",s));
    set_online(false); set_online(true);
    assert(!snapshot().at_home && !update_initial(Entity::Location,"home",s));
    update(Entity::Trunk,"closed"); update(Entity::Climate,"off");
    assert(!allowed(snapshot(),Action::OpenTrunk) && !allowed(snapshot(),Action::ClimateOn));
    ready_model();
    update(Entity::Odometer,"123456.7");
    assert(snapshot().odometer_available && snapshot().odometer_km == 123456.7);
    s = snapshot(); update(Entity::Odometer,"123457");
    assert(!update_initial(Entity::Odometer,"123450",s));
    for (const char* invalid : {"unknown","unavailable","nan","inf","-1","", "123 km"}) {
        update(Entity::Odometer,invalid); assert(!snapshot().odometer_available);
    }
    update(Entity::Odometer,"0"); assert(snapshot().odometer_available);
    s = snapshot(); set_online(false); set_online(true);
    assert(!snapshot().odometer_available && !update_initial(Entity::Odometer,"100",s));
    puts("Tesla input/model safety tests passed (no network or vehicle operations).");
}
