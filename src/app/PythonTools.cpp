#include "PythonTools.h"

namespace arranger::python_tools
{

juce::File folder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Arranger MIDI DAW")
        .getChildFile ("Python");
}

juce::File program()
{
#if JUCE_WINDOWS
    return folder().getChildFile ("Scripts").getChildFile ("python.exe");
#else
    return folder().getChildFile ("bin").getChildFile ("python");
#endif
}

bool isInstalled() { return program().existsAsFile(); }

juce::File script (const juce::String& fileName, const void* data, std::size_t size)
{
    const auto f = folder().getSiblingFile ("Helpers").getChildFile (fileName);
    const juce::String text (static_cast<const char*> (data), size);
    if (! f.existsAsFile() || f.loadFileAsString() != text)
    {
        f.getParentDirectory().createDirectory();
        f.replaceWithText (text);
    }
    return f;
}

juce::String quoted (const juce::File& f)
{
#if JUCE_WINDOWS
    return "'" + f.getFullPathName().replace ("'", "''") + "'";
#else
    return "'" + f.getFullPathName().replace ("'", "'\\''") + "'";
#endif
}

juce::String installScript (const juce::String& packages)
{
#if JUCE_WINDOWS
    return "$ErrorActionPreference = 'Stop'; "
           "$env:Path = \"$env:USERPROFILE\\.local\\bin;$env:Path\"; "
           "if (-not (Get-Command uv -ErrorAction SilentlyContinue)) { Write-Output 'Getting uv, a Python installer'; irm https://astral.sh/uv/install.ps1 | iex; $env:Path = \"$env:USERPROFILE\\.local\\bin;$env:Path\" }; "
           "Write-Output 'Making a Python 3.10 environment'; uv venv --allow-existing --python 3.10 " + quoted (folder()) + "; "
           "Write-Output 'Installing the packages (this can take some minutes)'; uv pip install --python " + quoted (program()) + " " + packages;
#else
    return "set -e; export PATH=\"$HOME/.local/bin:$HOME/.cargo/bin:/opt/homebrew/bin:/usr/local/bin:$PATH\"; "
           "if ! command -v uv >/dev/null 2>&1; then echo 'Getting uv, a Python installer'; curl -LsSf https://astral.sh/uv/install.sh | sh; export PATH=\"$HOME/.local/bin:$PATH\"; fi; "
           "echo 'Making a Python 3.10 environment'; uv venv --allow-existing --python 3.10 " + quoted (folder()) + "; "
           "echo 'Installing the packages (this can take some minutes)'; uv pip install --python " + quoted (program()) + " " + packages;
#endif
}

juce::StringArray shell (const juce::String& text)
{
#if JUCE_WINDOWS
    return { "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", text };
#else
    const juce::String sh = juce::File ("/bin/zsh").existsAsFile() ? "/bin/zsh" : (juce::File ("/bin/bash").existsAsFile() ? "/bin/bash" : "/bin/sh");
    return { sh, "-lc", text };
#endif
}

int run (const juce::StringArray& args, juce::Thread& thread, const std::function<void (const juce::String&)>& onLine)
{
    juce::ChildProcess p;
    if (! p.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return -1;
    std::string pending;
    char buffer[512];
    for (;;)
    {
        if (thread.threadShouldExit())
        {
            p.kill();
            return -1;
        }
        const int got = p.readProcessOutput (buffer, static_cast<int> (sizeof (buffer)));
        if (got <= 0)
        {
            if (! p.isRunning())
                break;
            juce::Thread::sleep (30);
            continue;
        }
        pending.append (buffer, static_cast<std::size_t> (got));
        for (auto nl = pending.find_first_of ("\r\n"); nl != std::string::npos; nl = pending.find_first_of ("\r\n"))
        {
            if (nl > 0)
                onLine (juce::String::fromUTF8 (pending.substr (0, nl).c_str()));
            pending.erase (0, nl + 1);
        }
    }
    if (! pending.empty())
        onLine (juce::String::fromUTF8 (pending.c_str()));
    p.waitForProcessToFinish (5000);
    return static_cast<int> (p.getExitCode());
}

} // namespace arranger::python_tools
