#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/SongEdit.h"
#include "arranger/engine/Handoff.h"
#include "arranger/engine/Engine.h"
#include "arranger/engine/Playback.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

using namespace arranger;

namespace
{
    std::vector<SequenceEvent> events (const Song& song)
    {
        return buildSnapshot (song).tracks.front().events;
    }

    std::vector<std::pair<Tick, int>> noteOns (const Song& song)
    {
        std::vector<std::pair<Tick, int>> result;
        for (const auto& e : events (song))
            if ((e.bytes[0] & 0xf0) == 0x90 && e.bytes[2] > 0)
                result.emplace_back (e.at, e.bytes[1]);
        return result;
    }
}

TEST_CASE ("A handoff keeps both objects alive through an audio transition", "[audit][handoff]")
{
    Handoff<int> handoff;
    auto first = std::make_shared<int> (1);
    std::weak_ptr<int> old = first;
    handoff.publish (std::move (first));
    auto* previous = handoff.acquire();
    handoff.finishTransition();
    handoff.publish (std::make_shared<int> (2));
    REQUIRE (*handoff.acquire() == 2);
    handoff.collectGarbage();
    REQUIRE_FALSE (old.expired());
    CHECK (*previous == 1);
    handoff.finishTransition();
    handoff.collectGarbage();
    CHECK (old.expired());
    handoff.release();
}

TEST_CASE ("The engine can restart after its previous snapshot was collected", "[audit][handoff]")
{
    Engine engine;
    engine.prepare (48000.0, 32);
    auto first = std::make_shared<const PlaybackSnapshot> (buildSnapshot (Song {}));
    std::weak_ptr<const PlaybackSnapshot> old = first;
    engine.setSnapshot (std::move (first));
    float left[32] {}, right[32] {};
    float* channels[] { left, right };
    AudioBlock audio { channels, 2, 32 };
    MidiEventBuffer in, out;
    engine.process (audio, in, out);
    engine.released();
    Song next;
    next.tempo = TempoMap (90.0);
    engine.loadSong (next);
    engine.collectGarbage();
    REQUIRE (old.expired());
    engine.prepare (48000.0, 32);
    engine.play();
    engine.process (audio, in, out);
    CHECK (engine.isPlaying());
    CHECK (engine.positionTicks() > 0.0);
}

TEST_CASE ("Joining clips preserves audible loop repetitions", "[audit][editing]")
{
    Song song;
    const auto track = edit::addTrack (song, "Keys").id;
    Clip a;
    a.length = 7680;
    a.loopLength = 3840;
    a.notes = { { 0, 480, 60, 100 } };
    a.controls = { { 960, ControlEvent::Kind::pitchBend, 0, 500 } };
    const auto left = edit::addClip (song, track, a);
    Clip b;
    b.start = 7680;
    b.length = 7680;
    b.loopLength = 3840;
    b.notes = { { 0, 480, 64, 100 } };
    const auto right = edit::addClip (song, track, b);
    const auto before = noteOns (song);
    edit::joinClips (song, { left, right });
    CHECK (noteOns (song) == before);
    REQUIRE (song.tracks.front().clips.size() == 1);
    CHECK (song.tracks.front().clips.front()->loopLength == 0);
    CHECK (song.tracks.front().clips.front()->controls.size() == 2);
}

TEST_CASE ("Splitting loops preserves controller timing", "[audit][editing]")
{
    const Tick cut = GENERATE (3840, 5000);
    Song song;
    const auto track = edit::addTrack (song, "Keys").id;
    Clip clip;
    clip.length = 11520;
    clip.loopLength = 3840;
    clip.notes = { { 0, 240, 60, 100 } };
    clip.controls = { { 960, ControlEvent::Kind::controller, 1, 100 },
                      { 1920, ControlEvent::Kind::pitchBend, 0, 500 } };
    const auto id = edit::addClip (song, track, clip);
    const auto controllers = [] (const Song& s) {
        std::vector<std::pair<Tick, std::array<std::uint8_t, 3>>> out;
        for (const auto& e : events (s))
            if ((e.bytes[0] & 0xf0) != 0x90 && (e.bytes[0] & 0xf0) != 0x80)
                out.emplace_back (e.at, e.bytes);
        return out;
    };
    const auto before = controllers (song);
    REQUIRE (edit::splitClip (song, id, cut) != 0);
    CHECK (controllers (song) == before);
}

TEST_CASE ("An asynchronous AI answer cannot edit a changed or replaced document", "[audit][ai]")
{
    struct Host final : mcp::Host
    {
        Document doc;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& title, const std::function<void (Song&)>& fn) override { doc.edit (title, fn); }
    } host;
    Song song;
    const auto track = edit::addTrack (song, "Drums").id;
    song.tracks.front().output.channel = 10;
    Clip clip;
    clip.length = 3840;
    clip.notes = { { 0, 480, 36, 100 } };
    edit::addClip (song, track, clip);
    host.doc.reset (song);
    mcp::Server server (host);
    std::string reply;
    const auto call = server.startAiCall (R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ai_humanize","arguments":{"track":1,"from_bar":1,"to_bar":1,"engine":"magenta"}}})", reply);
    REQUIRE (call.has_value());
    SECTION ("Replacement with identical IDs and content") { host.doc.reset (song); }
    SECTION ("An edit while the helper runs") { host.doc.edit ("Rename", [] (Song& s) { s.title = "Changed"; }); }
    const auto before = host.doc.song();
    const auto response = nlohmann::json::parse (server.finishAiCall (*call, R"({"notes":[{"start":0,"length":0.5,"pitch":38,"velocity":90}]})", ""));
    CHECK (response.at ("result").at ("isError").get<bool>());
    CHECK (host.doc.song() == before);
}
