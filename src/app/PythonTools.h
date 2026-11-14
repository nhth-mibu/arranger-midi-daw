#pragma once

#include <juce_core/juce_core.h>

#include <functional>

/**
    The app's own Python environment, for helpers that run AI models on this
    computer (audio analysis, and later others). One environment for all:
    Python 3.10 (Basic Pitch needs it), made with uv in the app's data folder.
    A helper is a Python script that prints "PROGRESS <0..1> <text>" lines
    and ends with "RESULT <text>" or "ERROR <text>".
*/
namespace arranger::python_tools
{

/** The environment's folder: <app data>/Arranger MIDI DAW/Python. */
juce::File folder();
/** Its python program (it may not be there yet). */
juce::File program();
bool isInstalled();

/** Writes a helper script from the app's resources into the folder (when it changed) and gives its file. */
juce::File script (const juce::String& fileName, const void* data, std::size_t size);

/**
    A shell script that makes the environment (getting uv first when it is
    missing) and installs `packages` (pip names, each quoted as needed, e.g.
    "demucs 'basic-pitch[onnx]'"). Run it with run (shell (...)).
*/
juce::String installScript (const juce::String& packages);

/** A command for the user's login shell (macOS, Linux) or PowerShell (Windows). */
juce::StringArray shell (const juce::String& script);

/** A file path quoted for shell(). */
juce::String quoted (const juce::File&);

/**
    Runs a program and gives each line it writes (stdout and stderr). Stops
    it when `thread` should exit. Returns the exit code, or -1 when it could
    not start or was stopped.
*/
int run (const juce::StringArray& args, juce::Thread& thread, const std::function<void (const juce::String&)>& onLine);

} // namespace arranger::python_tools
