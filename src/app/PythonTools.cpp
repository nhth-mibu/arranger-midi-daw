#include "PythonTools.h"

#if JUCE_MAC || JUCE_LINUX
 #include <cerrno>
 #include <csignal>
 #include <fcntl.h>
 #include <poll.h>
 #include <sys/wait.h>
 #include <unistd.h>
#endif

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

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
    if (args.isEmpty() || args[0].isEmpty() || thread.threadShouldExit())
        return -1;

#if JUCE_MAC || JUCE_LINUX
    std::vector<std::string> owned;
    std::vector<char*> argv;
    owned.reserve (static_cast<std::size_t> (args.size()));
    argv.reserve (static_cast<std::size_t> (args.size()) + 1);
    for (const auto& a : args)
    {
        owned.push_back (a.toStdString());
        argv.push_back (owned.back().data());
    }
    argv.push_back (nullptr);

    int outPipe[2] { -1, -1 };
    int errPipe[2] { -1, -1 };
    if (::pipe (outPipe) != 0)
        return -1;
    if (::pipe (errPipe) != 0)
    {
        ::close (outPipe[0]);
        ::close (outPipe[1]);
        return -1;
    }
    ::fcntl (errPipe[0], F_SETFD, FD_CLOEXEC);
    ::fcntl (errPipe[1], F_SETFD, FD_CLOEXEC);

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close (outPipe[0]);
        ::close (outPipe[1]);
        ::close (errPipe[0]);
        ::close (errPipe[1]);
        return -1;
    }
    if (pid == 0)
    {
        ::setpgid (0, 0);
        ::close (outPipe[0]);
        ::close (errPipe[0]);
        ::dup2 (outPipe[1], STDOUT_FILENO);
        ::dup2 (outPipe[1], STDERR_FILENO);
        if (outPipe[1] != STDOUT_FILENO && outPipe[1] != STDERR_FILENO)
            ::close (outPipe[1]);
        const int nullFd = ::open ("/dev/null", O_RDONLY);
        if (nullFd >= 0)
        {
            ::dup2 (nullFd, STDIN_FILENO);
            if (nullFd != STDIN_FILENO)
                ::close (nullFd);
        }
        ::execvp (argv[0], argv.data());
        const int err = errno;
        (void) ::write (errPipe[1], &err, sizeof (err));
        ::_exit (127);
    }

    ::setpgid (pid, pid);
    ::close (outPipe[1]);
    ::close (errPipe[1]);
    const int pipeFlags = ::fcntl (outPipe[0], F_GETFL, 0);
    ::fcntl (outPipe[0], F_SETFL, (pipeFlags >= 0 ? pipeFlags : 0) | O_NONBLOCK);

    int execErr = 0;
    if (::read (errPipe[0], &execErr, sizeof (execErr)) > 0)
    {
        ::close (errPipe[0]);
        ::close (outPipe[0]);
        int st = 0;
        ::waitpid (pid, &st, 0);
        return -1;
    }
    ::close (errPipe[0]);

    std::string pending;
    char buffer[512];
    for (;;)
    {
        if (thread.threadShouldExit())
        {
            ::kill (-pid, SIGKILL);
            ::kill (pid, SIGKILL);
            ::close (outPipe[0]);
            int st = 0;
            ::waitpid (pid, &st, 0);
            return -1;
        }
        pollfd pfd { outPipe[0], POLLIN | POLLERR | POLLHUP, 0 };
        const int ready = ::poll (&pfd, 1, 20);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        if (ready == 0)
        {
            int st = 0;
            const auto w = ::waitpid (pid, &st, WNOHANG);
            if (w == pid)
            {
                // Drain any remaining buffered bytes after child exit (non-blocking).
                for (;;)
                {
                    const auto n = ::read (outPipe[0], buffer, sizeof (buffer));
                    if (n > 0)
                    {
                        pending.append (buffer, static_cast<std::size_t> (n));
                        continue;
                    }
                    if (n < 0 && errno == EINTR)
                        continue;
                    break;
                }
                for (auto nl = pending.find_first_of ("\r\n"); nl != std::string::npos; nl = pending.find_first_of ("\r\n"))
                {
                    if (nl > 0)
                        onLine (juce::String::fromUTF8 (pending.substr (0, nl).c_str()));
                    pending.erase (0, nl + 1);
                }
                if (! pending.empty())
                    onLine (juce::String::fromUTF8 (pending.c_str()));
                ::close (outPipe[0]);
                return WIFEXITED (st) ? WEXITSTATUS (st) : -1;
            }
            continue;
        }
        const auto got = ::read (outPipe[0], buffer, sizeof (buffer));
        if (got <= 0)
        {
            if (got < 0 && errno == EINTR)
                continue;
            if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            break;
        }
        pending.append (buffer, static_cast<std::size_t> (got));
        for (auto nl = pending.find_first_of ("\r\n"); nl != std::string::npos; nl = pending.find_first_of ("\r\n"))
        {
            if (nl > 0)
                onLine (juce::String::fromUTF8 (pending.substr (0, nl).c_str()));
            pending.erase (0, nl + 1);
        }
    }
    ::close (outPipe[0]);
    if (! pending.empty())
        onLine (juce::String::fromUTF8 (pending.c_str()));

    int status = 0;
    for (int i = 0; i < 250; ++i)
    {
        if (thread.threadShouldExit())
        {
            ::kill (-pid, SIGKILL);
            ::kill (pid, SIGKILL);
            ::waitpid (pid, &status, 0);
            return -1;
        }
        if (::waitpid (pid, &status, WNOHANG) == pid)
            return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
        juce::Thread::sleep (20);
    }
    ::kill (-pid, SIGKILL);
    ::kill (pid, SIGKILL);
    ::waitpid (pid, &status, 0);
    return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
#else
    juce::ChildProcess p;
    if (! p.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return -1;

    std::mutex mutex;
    std::string sharedBuffer;
    std::atomic<bool> readerDone { false };
    juce::WaitableEvent wake;

    std::thread reader ([&] {
        char ch = 0;
        while (p.readProcessOutput (&ch, 1) > 0)
        {
            {
                const std::lock_guard<std::mutex> lock (mutex);
                sharedBuffer.push_back (ch);
            }
            if (ch == '\n' || ch == '\r')
                wake.signal();
        }
        readerDone.store (true);
        wake.signal();
    });

    std::string pending;
    const auto drainLines = [&] {
        {
            const std::lock_guard<std::mutex> lock (mutex);
            if (! sharedBuffer.empty())
            {
                pending.append (sharedBuffer);
                sharedBuffer.clear();
            }
        }
        for (auto nl = pending.find_first_of ("\r\n"); nl != std::string::npos; nl = pending.find_first_of ("\r\n"))
        {
            if (nl > 0)
                onLine (juce::String::fromUTF8 (pending.substr (0, nl).c_str()));
            pending.erase (0, nl + 1);
        }
    };

    for (;;)
    {
        drainLines();
        if (thread.threadShouldExit())
        {
            p.kill();
            p.waitForProcessToFinish (1000);
            if (reader.joinable())
                reader.join();
            return -1;
        }
        if (readerDone.load() && ! p.isRunning())
            break;
        wake.wait (20);
    }
    if (reader.joinable())
        reader.join();
    drainLines();
    if (! pending.empty())
        onLine (juce::String::fromUTF8 (pending.c_str()));
    p.waitForProcessToFinish (5000);
    return static_cast<int> (p.getExitCode());
#endif
}

} // namespace arranger::python_tools
