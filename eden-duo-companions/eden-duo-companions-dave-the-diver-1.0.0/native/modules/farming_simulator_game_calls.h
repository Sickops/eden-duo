// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Instant game calls (Garage Enter / Reset, Map Visit / Tag place) through the game's own Lua.
//
// The guest load plan (tools/farming-simulator/gen_load_plan.py) hooks the main loop's
// `bl beginAsyncGC` (main+0x8440): at that point the main thread owns the idle Lua state (no Lua
// frame active, the Giants async-GC thread parked). Once per frame the stub bumps a heartbeat and,
// for a fresh request, loads the Lua chunk this module wrote into the mailbox with the game's own
// luaL_loadbuffer and runs it with lua_pcall (two results: an integer code and a text), restores
// the Lua stack and publishes the outcome. A Lua error can never escape: luaL_loadbuffer and
// lua_pcall are both protected.
//
// The chunks repeat exactly what the native InGameMenu handlers do (decompiled from the game's
// dataS/scripts/gui/InGameMenu.lua bytecode, see reviews/20261017-game-calls-11/GAME-CALLS.md):
//   Enter  onClickEnterVehicle   g_currentMission:requestToEnterVehicle(vehicle)
//   Reset  onYesNoReset(true)    leave the vehicle if driving it, refuse a root with aiIsStarted,
//                                then ResetVehicleEvent's own server-side run (reload from its XML
//                                at the shop, delete the old one) -- captured locally instead of
//                                round-tripping the client event, so no "vehicle reset" dialog
//   Visit  onClickVisitPlace     onLeaveVehicle(x, terrain + 1, z) when driving, else
//                                player:moveTo(x, 1, z)
//   Tag    onClickTagPlace       setMapTargetHotspot(hotspot), or nil when it is already the target
// Every chunk first re-validates the target by identity (vehicle root node + network id, hotspot
// object id or exact position) and the play state (mission loaded and running, not paused or
// frozen, no GUI or dialog open). Pure and unit-tested.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace farming_simulator::game_calls {

/// Mailbox layout shared with the stub (gen_load_plan.py).
namespace mb {
inline constexpr std::uint32_t Seq = 0x00,  ///< module: bumped after the request is written
    Taken = 0x04,                           ///< stub: seq it has started (never runs twice)
    Heartbeat = 0x08,                       ///< stub: +1 every frame
    Done = 0x0C,                            ///< stub: seq whose outcome is stored
    Length = 0x10,                          ///< module: chunk bytes (1..ChunkMax; 0 = cancelled)
    Status = 0x14,                          ///< stub: Status below
    Result = 0x18,                          ///< stub: i64 first chunk result (lua_tointeger)
    Text = 0x20, TextMax = 0xE0,            ///< stub: second result / error text, NUL-terminated
    Chunk = 0x100, ChunkMax = 0xF00,        ///< module: Lua source
    Size = 0x1000;
} // namespace mb

enum class Status : std::uint32_t {
    Ran = 0,          ///< the chunk ran; Result/Text are its two results
    Unavailable = 1,  ///< no script system, or the async GC thread owns the state this frame
    LoadError = 2,    ///< luaL_loadbuffer refused the chunk (Text = message)
    RuntimeError = 3, ///< lua_pcall caught an error (Text = message)
    BadLength = 4,    ///< length 0 / too long: a cancelled request
};

/// Codes the chunks return (Result when Status::Ran).
enum class Code : std::int64_t {
    Done = 1,        ///< the game's call was made
    NotInPlay = 2,   ///< no mission, still loading, paused or player frozen
    MenuOpen = 3,    ///< a game menu or dialog is open
    Gone = 4,        ///< the vehicle / place no longer exists (or is another object now)
    NotAllowed = 5,  ///< the native rule forbids it (no seat, not saved, no permission)
    InUse = 6,       ///< controlled / broken / an AI worker drives its combination
    Failed = 7,      ///< the game's own call reported failure
};

enum class Kind { Enter, Reset, Visit, Tag };
inline const char* KindName(Kind k) {
    switch (k) {
    case Kind::Enter: return "enter";
    case Kind::Reset: return "reset";
    case Kind::Visit: return "visit";
    case Kind::Tag: return "tag";
    }
    return "";
}

/// A vehicle as the reader saw it: root node id (the key of g_currentMission.nodeToVehicle and the
/// vehicle hotspot's objectId) and its network object id, both rechecked by the chunk.
struct VehicleRef {
    std::uint64_t root_node{};
    std::uint64_t id{};
};

/// A map place: the hotspot's objectId when it has one, otherwise its exact map position.
struct PlaceRef {
    std::uint32_t object_id{};
    double x{}, z{};
};

namespace detail {
inline std::string Num(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}
inline std::string U64(std::uint64_t v) {
    return std::to_string(v);
}
// Common prelude: the play-state gate (InGameMenu's own conditions plus "no GUI open").
inline constexpr std::string_view Prelude =
    "local m=g_currentMission\n"
    "if m==nil or not m.isLoaded or not m.isRunning then return 2,'not in play' end\n"
    "if m.paused or m.isPlayerFrozen then return 2,'paused' end\n"
    "if g_gui~=nil and g_gui:getIsGuiVisible() then return 3,'menu open' end\n";
inline std::string VehicleLookup(const VehicleRef& v) {
    return "local v=m.nodeToVehicle and m.nodeToVehicle[" + U64(v.root_node) +
           "]\n"
           "if v==nil or v.isDeleted or v.id~=" +
           U64(v.id) + " then return 4,'vehicle gone' end\n";
}
inline std::string PlaceLookup(const PlaceRef& p) {
    // hotspots_sorted is keyed by booleans (each an array of hotspots); an older layout is one
    // array. Match by objectId when the reader saw one, else by exact map position.
    std::string match = p.object_id ? "h.objectId==" + U64(p.object_id)
                                    : "h.xMapPos~=nil and h.zMapPos~=nil and math.abs(h.xMapPos-(" +
                                          Num(p.x) + "))<0.001 and math.abs(h.zMapPos-(" +
                                          Num(p.z) + "))<0.001";
    return "local map=m.ingameMap\n"
           "if map==nil or map.hotspots_sorted==nil then return 4,'no map' end\n"
           "local target\n"
           "local function scan(list) for _,h in ipairs(list) do if type(h)=='table' and (" +
           match +
           ") then target=h return end end end\n"
           "for _,g in pairs(map.hotspots_sorted) do if target==nil and type(g)=='table' then "
           "if g.xMapPos~=nil then scan({g}) else scan(g) end end end\n"
           "if target==nil then return 4,'place gone' end\n";
}
} // namespace detail

/// InGameMenu:onClickEnterVehicle for one vehicle.
inline std::string EnterChunk(const VehicleRef& v) {
    return std::string{detail::Prelude} + detail::VehicleLookup(v) +
           "if v.enterReferenceNode==nil or v.exitPoint==nil then return 5,'no seat' end\n"
           "if v.isBroken or v.isControlled then return 6,'in use' end\n"
           "m:requestToEnterVehicle(v)\n"
           "return 1,'enter requested'\n";
}

/// InGameMenu:onYesNoReset(true) for one vehicle, with ResetVehicleEvent.run's server branch
/// called directly (a local connection object captures the state the server would send back).
inline std::string ResetChunk(const VehicleRef& v) {
    return std::string{detail::Prelude} + detail::VehicleLookup(v) +
           "local root=v:getRootAttacherVehicle()\n"
           "if root~=nil and root.aiIsStarted then return 6,'worker in use' end\n"
           "if not m.controlPlayer and m.controlledVehicle==v then m:onLeaveVehicle() end\n"
           "local E=ResetVehicleEvent\n"
           "local state\n"
           "local c={getIsServer=function() return false end,getIsLocal=function() return true end,"
           "sendEvent=function(_,e) state=e.state end}\n"
           "E.run(E:new(v),c)\n"
           "if state==E.STATE_SUCCESS then return 1,'reset' end\n"
           "if state==E.STATE_IN_USE then return 6,'in use' end\n"
           "if state==E.STATE_NO_PERMISSION then return 5,'no permission' end\n"
           "return 7,'reset failed'\n";
}

/// InGameMenu:onClickVisitPlace for one place.
inline std::string VisitChunk(const PlaceRef& p) {
    return std::string{detail::Prelude} + detail::PlaceLookup(p) +
           "local x,z=target.xMapPos,target.zMapPos\n"
           "if x==nil or z==nil then return 5,'no position' end\n"
           "if m.controlledVehicle~=nil then\n"
           " local y=getTerrainHeightAtWorldPos(m.terrainRootNode,x,300,z)+1\n"
           " m:onLeaveVehicle(x,y,z)\n"
           "else m.player:moveTo(x,1,z) end\n"
           "return 1,'visited'\n";
}

/// InGameMenu:onClickTagPlace for one place (a toggle; one target at a time).
inline std::string TagChunk(const PlaceRef& p) {
    return std::string{detail::Prelude} + detail::PlaceLookup(p) +
           "if target.xMapPos==nil or target.zMapPos==nil then return 5,'no position' end\n"
           "if m.currentMapTargetHotspot~=target then m:setMapTargetHotspot(target) "
           "return 1,'tagged' end\n"
           "m:setMapTargetHotspot(nil)\n"
           "return 1,'untagged'\n";
}

/// Aligned mailbox accessors (the host's base extensions: loads acquire, stores release).
struct MailboxIo {
    std::function<bool(std::uint32_t, std::uint32_t*)> load32;
    std::function<bool(std::uint32_t, std::uint32_t)> store32;
    std::function<bool(std::uint32_t, std::uint64_t*)> load64;
    std::function<bool(std::uint32_t, std::uint64_t)> store64;
};

/// One request at a time. The stub serves a request when seq != taken, so the chunk and its length
/// are written before seq; a new request waits until done == seq. A request the stub has not taken
/// within ServeFor is cancelled (length 0: the stub then answers BadLength without running it).
/// A request the stub HAS taken (taken == pending_seq) but never finishes (the game died or froze
/// mid-stub, between loading taken and storing done) is recovered too -- see Poll()'s own comment
/// -- so Busy() cannot stay true forever either way.
class CallMailbox {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto AliveFor = std::chrono::seconds(1);  ///< heartbeat freshness
    static constexpr auto ServeFor = std::chrono::seconds(3);  ///< untaken requests are cancelled
    /// Hard backstop for a TAKEN request that never finishes, independent of the heartbeat check
    /// in Poll() (belt-and-suspenders: covers a heartbeat that keeps ticking from something other
    /// than the hook that is actually stuck, which Alive() alone could not see).
    static constexpr auto TakenTimeout = std::chrono::seconds(5);

    struct Finished {
        Kind kind{};
        bool timed_out{};   ///< never served (no heartbeat / cancelled)
        Status status{};
        std::int64_t code{};
        std::string text;
    };

    void Configure(MailboxIo io_) {
        io = std::move(io_);
        waiting = false;
        beat_seen = false;
    }
    bool Present() const {
        return io.load32 && io.store32 && io.load64 && io.store64;
    }
    /// Once per sample: notes a heartbeat change.
    void Beat(Clock::time_point now) {
        std::uint32_t beat = 0;
        if (Present() && io.load32(mb::Heartbeat, &beat) && (beat != last_beat || !beat_seen)) {
            if (beat_seen)
                last_beat_change = now;
            beat_seen = true;
            last_beat = beat;
        }
    }
    bool Alive(Clock::time_point now) const {
        return beat_seen && last_beat_change != Clock::time_point{} &&
               now - last_beat_change < AliveFor;
    }
    bool Busy() const {
        return waiting;
    }
    /// Writes the chunk and bumps seq. False when absent, busy, dead or the chunk is too long.
    ///
    /// Rejects at >= ChunkMax, not > ChunkMax: the guest stub's own length gate
    /// (gen_load_plan.py, "sub w9,w8,#1; cmp w9,#0xeff; b.hs finish") accepts lengths 1..0xEFF
    /// (ChunkMax-1), i.e. it already answers BadLength for a chunk of exactly ChunkMax bytes
    /// without ever running it. A chunk of exactly ChunkMax would therefore pass this host-side
    /// check only to fail silently on the guest side every time -- reject it here instead, one
    /// byte earlier than the hard mailbox limit, so Submit()'s own success/failure matches what
    /// the stub will actually do with it.
    bool Submit(Kind kind, std::string_view chunk, Clock::time_point now) {
        if (!Present() || waiting || !Alive(now) || chunk.empty() || chunk.size() >= mb::ChunkMax)
            return false;
        std::uint32_t seq = 0, done = 0;
        if (!io.load32(mb::Seq, &seq) || !io.load32(mb::Done, &done) || seq != done)
            return false;
        for (std::size_t at = 0; at < chunk.size(); at += 8) {
            std::uint64_t word = 0;
            for (std::size_t i = 0; i < 8 && at + i < chunk.size(); ++i)
                word |= std::uint64_t(static_cast<unsigned char>(chunk[at + i])) << (8 * i);
            if (!io.store64(mb::Chunk + std::uint32_t(at), word))
                return false;
        }
        if (!io.store32(mb::Length, std::uint32_t(chunk.size())) || !io.store32(mb::Seq, seq + 1))
            return false;
        waiting = true;
        pending_seq = seq + 1;
        pending_kind = kind;
        submitted = now;
        return true;
    }
    /// Once per sample after Beat(): the outcome of the pending request, once.
    std::optional<Finished> Poll(Clock::time_point now) {
        if (!waiting)
            return std::nullopt;
        std::uint32_t done = 0, taken = 0;
        if (io.load32(mb::Done, &done) && done == pending_seq) {
            waiting = false;
            Finished f{pending_kind};
            std::uint32_t status = 0;
            std::uint64_t result = 0;
            io.load32(mb::Status, &status);
            io.load64(mb::Result, &result);
            f.status = Status(status);
            f.code = std::int64_t(result);
            f.text = ReadText();
            return f;
        }
        const bool have_taken = io.load32(mb::Taken, &taken);
        if (have_taken && taken == pending_seq) {
            // The stub took the request (taken advanced to pending_seq) but `done` never arrived:
            // the game died or froze between loading taken and storing done, so the untaken-only
            // ServeFor cancel below (which requires taken != pending_seq) can never fire for this
            // request -- without this, Busy() would stay true forever. Recover once the heartbeat
            // itself goes stale (the same signal Alive() already uses everywhere else) or, as a
            // backstop, once a longer hard deadline passes since Submit() regardless of the
            // heartbeat.
            if (!Alive(now) || now - submitted > TakenTimeout) {
                waiting = false;
                return Finished{pending_kind, true, Status::BadLength, 0, {}};
            }
            return std::nullopt;
        }
        if (now - submitted > ServeFor && have_taken && taken != pending_seq && !cancelled) {
            io.store32(mb::Length, 0); // the stub answers BadLength if it ever takes it
            cancelled = true;
            cancelled_at = now;
        }
        if (cancelled && now - cancelled_at > ServeFor) {
            // No stub at all: give the slot back so a later heartbeat can serve new requests.
            io.store32(mb::Done, pending_seq);
            waiting = cancelled = false;
            return Finished{pending_kind, true, Status::BadLength, 0, {}};
        }
        return std::nullopt;
    }

private:
    std::string ReadText() {
        std::string out;
        for (std::uint32_t at = 0; at < mb::TextMax; at += 8) {
            std::uint64_t word = 0;
            if (!io.load64(mb::Text + at, &word))
                break;
            for (int i = 0; i < 8; ++i) {
                const char c = char((word >> (8 * i)) & 0xFF);
                if (!c)
                    return out;
                out += c;
            }
        }
        return out;
    }

    MailboxIo io;
    bool waiting{}, cancelled{}, beat_seen{};
    std::uint32_t pending_seq{}, last_beat{};
    Kind pending_kind{};
    Clock::time_point submitted{}, cancelled_at{}, last_beat_change{};
};

/// Short user-facing line for an outcome (the companion's result bind).
inline std::string OutcomeText(const CallMailbox::Finished& f) {
    if (f.timed_out)
        return "Game did not respond";
    if (f.status == Status::Unavailable)
        return "Game busy, try again";
    if (f.status != Status::Ran)
        return "Game call failed";
    switch (Code(f.code)) {
    case Code::Done:
        switch (f.kind) {
        case Kind::Enter: return "Entering vehicle";
        case Kind::Reset: return "Vehicle reset";
        case Kind::Visit: return "Travelled";
        case Kind::Tag: return f.text == "untagged" ? "Tag removed" : "Place tagged";
        }
        break;
    case Code::NotInPlay: return "Not available right now";
    case Code::MenuOpen: return "Close the game menu first";
    case Code::Gone: return "No longer available";
    case Code::NotAllowed: return "Not possible here";
    case Code::InUse: return "Vehicle in use";
    case Code::Failed: return "The game could not do that";
    }
    return "Game call failed";
}

} // namespace farming_simulator::game_calls
