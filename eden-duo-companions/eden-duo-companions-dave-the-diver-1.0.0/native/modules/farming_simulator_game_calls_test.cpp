// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// The game-call mailbox protocol against a simulated load-plan stub, and the Lua chunks' shape.
#include "farming_simulator_game_calls.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace farming_simulator::game_calls;
using Clock = CallMailbox::Clock;

int failures = 0;
void Require(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

/// The guest side of gen_load_plan.py's stub, byte for byte on the same layout.
struct FakeGuest {
    std::vector<std::uint8_t> box = std::vector<std::uint8_t>(mb::Size);
    std::string last_chunk;
    std::uint32_t U32(std::uint32_t at) const {
        std::uint32_t v;
        std::memcpy(&v, &box[at], 4);
        return v;
    }
    void Put32(std::uint32_t at, std::uint32_t v) {
        std::memcpy(&box[at], &v, 4);
    }
    MailboxIo Io() {
        return {[this](std::uint32_t o, std::uint32_t* v) { return o % 4 == 0 && o < mb::Size && (*v = U32(o), true); },
                [this](std::uint32_t o, std::uint32_t v) { return o % 4 == 0 && o < mb::Size && (Put32(o, v), true); },
                [this](std::uint32_t o, std::uint64_t* v) {
                    return o % 8 == 0 && o < mb::Size && (std::memcpy(v, &box[o], 8), true);
                },
                [this](std::uint32_t o, std::uint64_t v) {
                    return o % 8 == 0 && o < mb::Size && (std::memcpy(&box[o], &v, 8), true);
                }};
    }
    /// One frame of the stub; `run` stands in for luaL_loadbuffer + lua_pcall.
    void Frame(const std::function<std::pair<std::int64_t, std::string>(const std::string&)>& run,
               bool yielded = false) {
        Put32(mb::Heartbeat, U32(mb::Heartbeat) + 1);
        const auto seq = U32(mb::Seq);
        if (seq == U32(mb::Taken))
            return;
        Put32(mb::Taken, seq);
        std::uint32_t status = 1;
        std::int64_t result = 0;
        std::string text;
        if (!yielded) {
            const auto len = U32(mb::Length);
            status = 4;
            if (len >= 1 && len <= mb::ChunkMax) {
                last_chunk.assign(reinterpret_cast<const char*>(&box[mb::Chunk]), len);
                auto [code, t] = run(last_chunk);
                status = 0;
                result = code;
                text = t;
            }
        }
        std::memcpy(&box[mb::Result], &result, 8);
        std::memset(&box[mb::Text], 0, mb::TextMax);
        std::memcpy(&box[mb::Text], text.data(), std::min<std::size_t>(text.size(), mb::TextMax - 1));
        Put32(mb::Status, status);
        Put32(mb::Done, seq);
    }
};

void TestRoundTrip() {
    FakeGuest g;
    CallMailbox box;
    box.Configure(g.Io());
    auto t = Clock::time_point{} + std::chrono::seconds(100);
    const auto ok = [](const std::string&) { return std::pair<std::int64_t, std::string>{1, "enter requested"}; };
    Require(!box.Submit(Kind::Enter, "return 1", t), "no heartbeat yet: refused");
    box.Beat(t);
    g.Frame(ok);
    t += std::chrono::milliseconds(33);
    box.Beat(t);
    Require(box.Alive(t), "heartbeat seen moving: alive");
    const auto chunk = EnterChunk({4242, 7});
    Require(box.Submit(Kind::Enter, chunk, t), "submit");
    Require(box.Busy() && !box.Submit(Kind::Reset, "return 1", t), "one call at a time");
    Require(!box.Poll(t), "not done before the stub ran");
    g.Frame(ok);
    Require(g.last_chunk == chunk, "the stub sees the whole chunk, unpadded");
    auto f = box.Poll(t);
    Require(f && !f->timed_out && f->status == Status::Ran && f->code == 1 && f->text == "enter requested" &&
                f->kind == Kind::Enter,
            "outcome read back");
    Require(f && OutcomeText(*f) == "Entering vehicle", "outcome text");
    Require(!box.Busy() && !box.Poll(t), "outcome delivered once");
    g.Frame(ok);
    Require(box.Submit(Kind::Tag, TagChunk({0, 10.5, -20.25}), t), "a second call after the first");
    g.Frame([](const std::string&) { return std::pair<std::int64_t, std::string>{1, "untagged"}; });
    f = box.Poll(t);
    Require(f && OutcomeText(*f) == "Tag removed", "tag toggle text");
}

void TestYieldedAndErrors() {
    FakeGuest g;
    CallMailbox box;
    box.Configure(g.Io());
    auto t = Clock::time_point{} + std::chrono::seconds(5);
    box.Beat(t);
    g.Frame({});
    t += std::chrono::milliseconds(33);
    box.Beat(t);
    Require(box.Submit(Kind::Reset, ResetChunk({1, 2}), t), "submit reset");
    g.Frame({}, /*yielded=*/true);
    auto f = box.Poll(t);
    Require(f && f->status == Status::Unavailable && OutcomeText(*f) == "Game busy, try again",
            "yielded state: unavailable, nothing ran");
    Require(!box.Submit(Kind::Reset, std::string(mb::ChunkMax + 1, 'x'), t), "oversized chunk refused");
    // A chunk of exactly ChunkMax bytes is refused too (not just > ChunkMax): the guest stub's own
    // length gate (gen_load_plan.py) only accepts 1..ChunkMax-1, so a ChunkMax-byte chunk would
    // otherwise pass this check only to fail silently on the guest side every time.
    Require(!box.Submit(Kind::Reset, std::string(mb::ChunkMax, 'x'), t), "exactly-ChunkMax chunk refused");
    Require(box.Submit(Kind::Reset, std::string(mb::ChunkMax - 1, 'x'), t), "ChunkMax-1 chunk accepted");
}

void TestCancelWithoutStub() {
    FakeGuest g;
    CallMailbox box;
    box.Configure(g.Io());
    auto t = Clock::time_point{} + std::chrono::seconds(5);
    box.Beat(t);
    g.Frame({});
    t += std::chrono::milliseconds(33);
    box.Beat(t);
    Require(box.Submit(Kind::Visit, VisitChunk({99, 0, 0}), t), "submit visit");
    t += CallMailbox::ServeFor + std::chrono::milliseconds(1);
    Require(!box.Poll(t), "first timeout cancels (length 0)");
    Require(g.U32(mb::Length) == 0, "cancelled request has length 0");
    // The stub wakes late: it must not run the cancelled request.
    bool ran = false;
    g.Frame([&](const std::string&) { ran = true; return std::pair<std::int64_t, std::string>{1, ""}; });
    auto f = box.Poll(t);
    Require(!ran && f && f->status == Status::BadLength, "late stub answers BadLength without running");
    // No stub at all: the slot is handed back after a second ServeFor.
    FakeGuest dead;
    CallMailbox box2;
    box2.Configure(dead.Io());
    auto u = Clock::time_point{} + std::chrono::seconds(5);
    box2.Beat(u);
    dead.Frame({});
    u += std::chrono::milliseconds(33);
    box2.Beat(u);
    Require(box2.Submit(Kind::Enter, "return 1", u), "submit to a stub that then stops");
    u += CallMailbox::ServeFor + std::chrono::milliseconds(1);
    box2.Poll(u);
    u += CallMailbox::ServeFor + std::chrono::milliseconds(1);
    f = box2.Poll(u);
    Require(f && f->timed_out && !box2.Busy(), "dead stub: timed out, slot free");
    Require(!box2.Alive(u), "dead stub: not alive");
}

/// A taken-but-never-finished request (the game died or froze between the stub storing Taken and
/// storing Done) must not leave Busy() stuck forever -- see CallMailbox::Poll's own comment.
void TestTakenButNeverDone() {
    // Heartbeat goes stale after the take: recovered via !Alive().
    {
        FakeGuest g;
        CallMailbox box;
        box.Configure(g.Io());
        auto t = Clock::time_point{} + std::chrono::seconds(5);
        box.Beat(t);
        g.Frame({});
        t += std::chrono::milliseconds(33);
        box.Beat(t);
        const auto chunk = EnterChunk({4242, 7});
        Require(box.Submit(Kind::Enter, chunk, t), "submit enter");
        // The stub takes the request (advances Taken to the pending seq) but then the game
        // freezes -- no more heartbeat bumps, Done never stored.
        g.Put32(mb::Taken, g.U32(mb::Seq));
        Require(!box.Poll(t), "still within the heartbeat's own freshness window");
        Require(box.Busy(), "busy while the heartbeat is still fresh");
        t += CallMailbox::AliveFor + std::chrono::milliseconds(1);
        Require(!box.Alive(t), "heartbeat is now stale");
        auto f = box.Poll(t);
        Require(f && f->timed_out && f->status == Status::BadLength, "taken-but-stale recovers as timed out");
        Require(!box.Busy(), "busy clears once recovered");
        Require(!box.Poll(t), "outcome delivered once");
    }
    // Heartbeat keeps ticking (something else is still alive) but this particular request never
    // finishes: recovered via the hard TakenTimeout backstop instead.
    {
        FakeGuest g;
        CallMailbox box;
        box.Configure(g.Io());
        auto t = Clock::time_point{} + std::chrono::seconds(5);
        box.Beat(t);
        g.Frame({});
        t += std::chrono::milliseconds(33);
        box.Beat(t);
        Require(box.Submit(Kind::Reset, ResetChunk({1, 2}), t), "submit reset");
        const auto submitted = t;
        g.Put32(mb::Taken, g.U32(mb::Seq));
        // Keep the heartbeat moving well past AliveFor, but never store Done.
        while (t + std::chrono::milliseconds(33) < submitted + CallMailbox::TakenTimeout) {
            t += std::chrono::milliseconds(33);
            g.Put32(mb::Heartbeat, g.U32(mb::Heartbeat) + 1);
            box.Beat(t);
            Require(!box.Poll(t), "not recovered before the hard deadline");
        }
        Require(box.Alive(t), "heartbeat is still fresh at the hard deadline");
        t = submitted + CallMailbox::TakenTimeout + std::chrono::milliseconds(1);
        g.Put32(mb::Heartbeat, g.U32(mb::Heartbeat) + 1);
        box.Beat(t);
        auto f = box.Poll(t);
        Require(f && f->timed_out, "the hard TakenTimeout backstop recovers it anyway");
        Require(!box.Busy(), "busy clears once recovered");
    }
}

void TestChunks() {
    const auto enter = EnterChunk({4242, 7});
    Require(enter.find("m.nodeToVehicle[4242]") != std::string::npos && enter.find("v.id~=7") != std::string::npos,
            "enter re-checks root node and network id");
    Require(enter.find("m:requestToEnterVehicle(v)") != std::string::npos, "enter calls requestToEnterVehicle");
    Require(enter.find("g_gui:getIsGuiVisible()") != std::string::npos && enter.find("m.paused") != std::string::npos,
            "play-state gate");
    const auto reset = ResetChunk({4242, 7});
    Require(reset.find("E.run(E:new(v),c)") != std::string::npos && reset.find("aiIsStarted") != std::string::npos,
            "reset runs ResetVehicleEvent and refuses AI workers");
    const auto visit_obj = VisitChunk({1234, 1, 2});
    Require(visit_obj.find("h.objectId==1234") != std::string::npos, "place by object id");
    const auto visit_pos = VisitChunk({0, -312.5, 77.125});
    Require(visit_pos.find("-312.5") != std::string::npos && visit_pos.find("77.125") != std::string::npos,
            "place by exact position");
    for (const auto& c : {enter, reset, visit_obj, visit_pos, TagChunk({0, 1, 2})})
        Require(c.size() <= mb::ChunkMax, "every chunk fits the mailbox");
}
} // namespace

int main() {
    TestRoundTrip();
    TestYieldedAndErrors();
    TestCancelWithoutStub();
    TestTakenButNeverDone();
    TestChunks();
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "farming simulator game calls: ok\n";
    return 0;
}
