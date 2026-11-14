#pragma once

#include "arranger/core/Song.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
    AI music tools: humanize, drumify, continue a part, a melody over the
    chords, and "write a part under this". The models run in helper programs
    outside the app (Magenta.js in Node, the Anticipatory Music Transformer in
    Python). This module only makes the request for a helper from the song and
    puts the helper's answer back into the song, so it has no model code and
    is easy to test. The simple humanize runs here, without a helper.

    The request and the answer are JSON. Times are in quarter notes from the
    start of the range (negative = before it). A note is
    {"start", "length", "pitch", "velocity", "program" (0-127), "drum" (bool)}.
*/
namespace arranger::ai
{

/** The helper programs. */
inline constexpr const char* kMagenta = "magenta";
inline constexpr const char* kAnticipation = "anticipation";

/** A piece of AI work from a tool call. */
struct Job
{
    std::string tool;    ///< the tool name, e.g. "ai_humanize"
    std::string title;   ///< the undo step, e.g. "AI: Humanize"
    std::string helper;  ///< kMagenta, kAnticipation, or empty when nothing has to run
    std::string request; ///< JSON for the helper (an object with "op")
    /** Puts the helper's answer (JSON, empty when there is no helper) into the song and returns the tool's result (JSON). Throws on a bad answer. */
    std::function<std::string (Song&, const std::string& answer)> apply;
};

/** True for the names of the AI tools. */
bool isTool (const std::string& name);

/** The AI tool definitions for the MCP server (a JSON array). */
std::string toolDefinitions();

/** Makes a job from a tool call. `arguments` is a JSON object. Throws std::runtime_error with a plain message on bad arguments. */
Job prepare (const Song&, const std::string& tool, const std::string& arguments);

// ---- the simple humanize ----------------------------------------------------------------

struct HumanizeOptions
{
    double amount = 0.5;       ///< 0 = no change, 1 = strong
    bool drums = false;        ///< drum notes: snare a bit late, hi-hats with more spread
    std::uint32_t seed = 1;    ///< the same seed gives the same result
};

/** Gives notes (song positions) small changes in time and loudness, with louder beats and softer off-beats, so they sound played. */
std::vector<Note> humanize (std::vector<Note> notes, const TimeSignature&, const HumanizeOptions&);

/** Moves note starts to the nearest step (ticks); a note is at least one step long. step 0: no change. */
std::vector<Note> snap (std::vector<Note> notes, Tick step);

} // namespace arranger::ai
