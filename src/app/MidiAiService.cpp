#include "MidiAiService.h"

#include "McpService.h"
#include "PythonTools.h"
#include "MessageThreadTask.h"

#include "BinaryData.h"


#include <stdexcept>

namespace arranger
{

std::function<void (const juce::String&)> MidiAiService::onActivity;

namespace
{
    constexpr const char* kNodeVersion = "v22.11.0";

    juce::File magentaFolder() { return MidiAiService::folder().getChildFile ("magenta"); }
    juce::File modelsFolder() { return MidiAiService::folder().getChildFile ("models"); }

    /** Writes a file from the app's resources when it changed. */
    juce::File writeResource (const juce::File& f, const char* data, int size)
    {
        const juce::String text (juce::CharPointer_UTF8 (data), static_cast<size_t> (size));
        if (! f.existsAsFile() || f.loadFileAsString() != text)
        {
            f.getParentDirectory().createDirectory();
            f.replaceWithText (text);
        }
        return f;
    }

    juce::File magentaScript() { return writeResource (magentaFolder().getChildFile ("arranger_magenta.js"), BinaryData::arranger_magenta_js, BinaryData::arranger_magenta_jsSize); }
    juce::File magentaPackage() { return writeResource (magentaFolder().getChildFile ("package.json"), BinaryData::magenta_package_json, BinaryData::magenta_package_jsonSize); }
    juce::File generateScript() { return writeResource (MidiAiService::folder().getChildFile ("arranger_generate.py"), BinaryData::arranger_generate_py, BinaryData::arranger_generate_pySize); }

    /** The Node.js program the install found or downloaded (kept in node-path.txt). */
    juce::File nodeProgram()
    {
        const auto saved = MidiAiService::folder().getChildFile ("node-path.txt").loadFileAsString().trim();
        return saved.isNotEmpty() ? juce::File (saved) : juce::File();
    }

    juce::String installMagentaScript()
    {
        const auto ai = MidiAiService::folder();
        const auto magenta = magentaFolder();
#if JUCE_WINDOWS
        return "$ErrorActionPreference = 'Stop'; "
               "$ai = " + python_tools::quoted (ai) + "; New-Item -ItemType Directory -Force -Path $ai | Out-Null; "
               "$node = $null; $c = Get-Command node -ErrorAction SilentlyContinue; "
               "if ($c) { $v = [int]((& $c.Source --version).TrimStart('v').Split('.')[0]); if ($v -ge 18) { $node = $c.Source } }; "
               "if (-not $node) { Write-Output 'Getting Node.js'; $zip = Join-Path $ai 'node.zip'; "
               "Invoke-WebRequest -Uri 'https://nodejs.org/dist/" + juce::String (kNodeVersion) + "/node-" + kNodeVersion + "-win-x64.zip' -OutFile $zip; "
               "if (Test-Path (Join-Path $ai 'node')) { Remove-Item -Recurse -Force (Join-Path $ai 'node') }; "
               "Expand-Archive -Path $zip -DestinationPath $ai; Remove-Item $zip; "
               "Rename-Item (Join-Path $ai 'node-" + kNodeVersion + "-win-x64') 'node'; $node = Join-Path $ai 'node\\node.exe' }; "
               "Set-Content -Path (Join-Path $ai 'node-path.txt') -Value $node; "
               "$env:Path = (Split-Path $node) + ';' + $env:Path; "
               "Write-Output 'Installing Magenta.js'; Set-Location " + python_tools::quoted (magenta) + "; "
               "npm install --no-audit --no-fund --loglevel=error; "
               "Write-Output 'Downloading the Magenta models'; & $node arranger_magenta.js prefetch";
#else
        return "set -e; export PATH=\"/opt/homebrew/bin:/usr/local/bin:$PATH\"; "
               "AI=" + python_tools::quoted (ai) + "; mkdir -p \"$AI\"; NODE=''; "
               "if command -v node >/dev/null 2>&1 && node -e 'process.exit(parseInt(process.versions.node) >= 18 ? 0 : 1)'; then NODE=\"$(command -v node)\"; fi; "
               "if [ -z \"$NODE\" ]; then echo 'Getting Node.js'; "
               "case \"$(uname -s)\" in Darwin) OS=darwin;; *) OS=linux;; esac; "
               "case \"$(uname -m)\" in arm64|aarch64) ARCH=arm64;; *) ARCH=x64;; esac; "
               "NAME=node-" + juce::String (kNodeVersion) + "-$OS-$ARCH; "
               "curl -fsSL \"https://nodejs.org/dist/" + kNodeVersion + "/$NAME.tar.gz\" | tar xz -C \"$AI\"; "
               "rm -rf \"$AI/node\"; mv \"$AI/$NAME\" \"$AI/node\"; NODE=\"$AI/node/bin/node\"; fi; "
               "echo \"$NODE\" > \"$AI/node-path.txt\"; export PATH=\"$(dirname \"$NODE\"):$PATH\"; "
               "echo 'Installing Magenta.js'; cd " + python_tools::quoted (magenta) + "; "
               "npm install --no-audit --no-fund --loglevel=error; "
               "echo 'Downloading the Magenta models'; \"$NODE\" arranger_magenta.js prefetch";
#endif
    }

    /** Runs a helper command and reads its PROGRESS/RESULT/ERROR lines. Returns the RESULT text. */
    std::string runLines (const juce::StringArray& args, juce::Thread& thread, const std::function<void (double, const juce::String&)>& progress)
    {
        juce::String result, error, last;
        const int code = python_tools::run (args, thread, [&] (const juce::String& line) {
            if (line.startsWith ("PROGRESS "))
            {
                const auto rest = line.substring (9);
                if (progress)
                    progress (rest.upToFirstOccurrenceOf (" ", false, false).getDoubleValue(), rest.fromFirstOccurrenceOf (" ", false, false));
            }
            else if (line.startsWith ("RESULT "))
                result = line.substring (7).trim();
            else if (line.startsWith ("ERROR "))
                error = line.substring (6).trim();
            else if (line.trim().isNotEmpty())
                last = line.trim();
        });
        if (thread.threadShouldExit())
            throw std::runtime_error ("Stopped.");
        if (error.isNotEmpty())
            throw std::runtime_error (error.toStdString());
        if (code != 0 || result.isEmpty())
            throw std::runtime_error (code == -1 ? "the AI helper could not start" : ("the AI helper stopped: " + last).toStdString());
        return result.toStdString();
    }

    void showActivity (const juce::String& text)
    {
        juce::MessageManager::callAsync ([text] {
            if (MidiAiService::onActivity)
                MidiAiService::onActivity (text);
        });
    }
} // namespace

juce::File MidiAiService::folder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Arranger MIDI DAW").getChildFile ("AI");
}

bool MidiAiService::magentaInstalled()
{
    return nodeProgram().existsAsFile()
        && magentaFolder().getChildFile ("node_modules").getChildFile ("@magenta").isDirectory()
        && magentaFolder().getChildFile ("magenta-models").getChildFile ("music_rnn").isDirectory();
}

bool MidiAiService::anticipationInstalled()
{
    return python_tools::isInstalled() && modelsFolder().getChildFile ("music-small-800k").getChildFile ("config.json").existsAsFile();
}

juce::String MidiAiService::statusText()
{
    return juce::String ("Magenta: ") + (magentaInstalled() ? "ready" : "not installed") + ". Anticipatory Music Transformer: "
         + (anticipationInstalled() ? "ready" : "not installed") + ".";
}

// ---- install ---------------------------------------------------------------------------------
struct MidiAiService::Installer final : juce::Thread
{
    Installer (MidiAiService& o, bool a, std::function<void (const juce::String&)> out, std::function<void (const juce::String&)> d)
        : juce::Thread ("AI music tools install"), owner (o), anticipation (a), output (std::move (out)), done (std::move (d))
    {
    }
    ~Installer() override { stopThread (10000); }

    void say (const juce::String& line)
    {
        juce::MessageManager::callAsync ([o = output, line] {
            if (o)
                o (line);
        });
    }

    juce::String step (const juce::String& script)
    {
        juce::String last;
        const int code = python_tools::run (python_tools::shell (script), *this, [&] (const juce::String& line) {
            const auto shown = line.startsWith ("PROGRESS ") ? line.substring (9).fromFirstOccurrenceOf (" ", false, false) : line;
            if (shown.trim().isNotEmpty())
            {
                last = shown.trim();
                say (last);
            }
        });
        if (threadShouldExit())
            return "Stopped.";
        if (code != 0)
            return code == -1 ? juce::String ("The install could not start.") : "The install stopped: " + last;
        return {};
    }

    void run() override
    {
        magentaScript();
        magentaPackage();
        auto error = step (installMagentaScript());
        if (error.isEmpty() && anticipation)
        {
            const auto script = generateScript();
            say ("Installing PyTorch and the Anticipatory Music Transformer (about 2 GB)");
            error = step (python_tools::installScript ("torch transformers huggingface_hub safetensors 'numpy<2'"));
            if (error.isEmpty())
            {
                say ("Downloading the Anticipatory Music Transformer model");
#if JUCE_WINDOWS
                error = step ("& " + python_tools::quoted (python_tools::program()) + " " + python_tools::quoted (script) + " prefetch small");
#else
                error = step (python_tools::quoted (python_tools::program()) + " " + python_tools::quoted (script) + " prefetch small");
#endif
            }
        }
        juce::MessageManager::callAsync ([this, weak = std::weak_ptr<int> (lifetime), error] {
            if (weak.expired())
                return;
            auto d = done;
            owner.installer.reset(); // deletes this thread object after run() returned
            if (d)
                d (error);
        });
    }

    MidiAiService& owner;
    const std::shared_ptr<int> lifetime = std::make_shared<int> (0);
    bool anticipation;
    std::function<void (const juce::String&)> output;
    std::function<void (const juce::String&)> done;
};

MidiAiService::MidiAiService() = default;

MidiAiService::~MidiAiService()
{
    runner.reset();
    installer.reset();
}

juce::String MidiAiService::install (bool anticipation, std::function<void (const juce::String&)> output, std::function<void (const juce::String&)> done)
{
    if (installer != nullptr)
        return "The install is running.";
    installer = std::make_unique<Installer> (*this, anticipation, std::move (output), std::move (done));
    installer->startThread();
    return {};
}

// ---- running a helper ---------------------------------------------------------------------
std::string MidiAiService::runHelper (const std::string& helper, const std::string& request, juce::Thread& thread,
                                      const std::function<void (double, const juce::String&)>& progress)
{
    juce::StringArray args;
    if (helper == ai::kMagenta)
    {
        if (! magentaInstalled())
            throw std::runtime_error ("Magenta is not installed. Ask the user to choose AI > Install AI Music Tools.");
        args = { nodeProgram().getFullPathName(), magentaScript().getFullPathName(), "run" };
    }
    else if (helper == ai::kAnticipation)
    {
        if (! anticipationInstalled())
            throw std::runtime_error ("the Anticipatory Music Transformer is not installed. Ask the user to choose AI > Install AI Music Tools and tick it.");
        args = { python_tools::program().getFullPathName(), generateScript().getFullPathName(), "run" };
    }
    else
    {
        throw std::runtime_error ("no AI helper " + helper);
    }
    const auto work = folder().getChildFile ("jobs");
    work.createDirectory();
    const auto requestFile = work.getNonexistentChildFile ("request", ".json");
    const auto answerFile = requestFile.withFileExtension (".answer.json");
    requestFile.replaceWithText (juce::String::fromUTF8 (request.c_str()));
    args.add (requestFile.getFullPathName());
    args.add (answerFile.getFullPathName());

    std::string answer;
    try
    {
        runLines (args, thread, progress);
        answer = answerFile.loadFileAsString().toStdString();
    }
    catch (...)
    {
        requestFile.deleteFile();
        answerFile.deleteFile();
        throw;
    }
    requestFile.deleteFile();
    answerFile.deleteFile();
    return answer;
}

struct MidiAiService::Runner final : juce::Thread
{
    Runner (MidiAiService& o, ai::Job j, std::function<void (double, const juce::String&)> p,
            std::function<void (const ai::Job&, const std::string&, const juce::String&)> d)
        : juce::Thread ("AI music tool"), owner (o), job (std::move (j)), progress (std::move (p)), done (std::move (d))
    {
    }
    ~Runner() override { stopThread (5000); }

    void run() override
    {
        std::string answer;
        juce::String error;
        try
        {
            answer = MidiAiService::runHelper (job.helper, job.request, *this, [this] (double v, const juce::String& text) {
                juce::MessageManager::callAsync ([p = progress, v, text] {
                    if (p)
                        p (v, text);
                });
            });
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        if (threadShouldExit())
            return;
        juce::MessageManager::callAsync ([this, weak = std::weak_ptr<int> (lifetime), answer, error] {
            if (weak.expired())
                return;
            auto d = done;
            auto j = job;
            owner.runner.reset();
            if (d)
                d (j, answer, error);
        });
    }

    MidiAiService& owner;
    const std::shared_ptr<int> lifetime = std::make_shared<int> (0);
    ai::Job job;
    std::function<void (double, const juce::String&)> progress;
    std::function<void (const ai::Job&, const std::string&, const juce::String&)> done;
};

juce::String MidiAiService::start (ai::Job job, std::function<void (double, const juce::String&)> progress,
                                   std::function<void (const ai::Job&, const std::string&, const juce::String&)> done)
{
    if (runner != nullptr)
        return "An AI tool is running.";
    runner = std::make_unique<Runner> (*this, std::move (job), std::move (progress), std::move (done));
    runner->startThread();
    return {};
}

void MidiAiService::cancel()
{
    runner.reset();
}

// ---- tool calls from a worker thread ----------------------------------------------------------
std::string MidiAiService::runToolCall (const std::shared_ptr<SessionTools>& tools, const std::string& message, juce::Thread* thread, const std::function<bool()>& cancelled)
{
    // Cheap check first: most messages are no AI tool calls.
    if (message.find ("\"ai_") == std::string::npos)
        return {};
    struct Preparation
    {
        std::optional<mcp::Server::AiCall> call;
        std::string reply;
    };
    auto prepared = std::make_shared<Preparation>();
    if (! onMessageThread ([tools, message, prepared] { prepared->call = tools->server().startAiCall (message, prepared->reply); }, cancelled))
        return {};
    if (! prepared->call)
        return prepared->reply;
    const auto& call = *prepared->call;

    struct Stopper final : juce::Thread
    {
        Stopper() : juce::Thread ("AI stop flag") {}
        void run() override {}
    };
    std::string answer, error;
    showActivity ("AI: " + juce::String (call.job.title.substr (4)) + "...");
    try
    {
        // runHelper stops when the thread should exit; a connection thread does that when the app quits.
        if (thread != nullptr)
        {
            answer = runHelper (call.job.helper, call.job.request, *thread, [] (double v, const juce::String& text) {
                showActivity ("AI: " + text + " " + juce::String (juce::roundToInt (v * 100.0)) + "%");
            });
        }
        else
        {
            Stopper stopper;
            answer = runHelper (call.job.helper, call.job.request, stopper, [] (double v, const juce::String& text) {
                showActivity ("AI: " + text + " " + juce::String (juce::roundToInt (v * 100.0)) + "%");
            });
        }
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }
    showActivity ({});
    if (cancelled && cancelled())
        return {};
    if (! onMessageThread ([tools, prepared, answer, error] { prepared->reply = tools->server().finishAiCall (*prepared->call, answer, error); }, cancelled))
        return {};
    return prepared->reply;
}

} // namespace arranger
