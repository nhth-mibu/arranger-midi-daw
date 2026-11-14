#pragma once

#include "McpService.h"
#include "Session.h"

#include "arranger/core/Assistant.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace arranger
{

/**
    The chat box's conversation with an AI assistant: a program that uses a
    subscription (Claude Code, Codex, Gemini CLI, or one in assistants.json),
    or a service with an API key. Each message runs on a background thread;
    the events come back on the message thread.
*/
class AssistantChat final
{
public:
    /** `startService` turns on Options > Allow AI Assistants (programs need it); false when it can't. */
    AssistantChat (Session&, juce::PropertiesFile* settings, std::function<bool()> startService);
    ~AssistantChat();

    struct Choice
    {
        juce::String id, name;
        bool usesApiKey = false;
    };
    /** The programs, then the API services. */
    juce::Array<Choice> choices() const;
    juce::String chosen() const { return choiceId; }
    void choose (const juce::String& id);
    bool chosenUsesApiKey() const;

    /** The program's command (a name on the PATH or a full path). */
    juce::String programCommand (const juce::String& id) const;
    void setProgramCommand (const juce::String& id, const juce::String& command);
    /** How to install and sign in. */
    juce::String programHelp (const juce::String& id) const;

    struct ApiValues
    {
        juce::String url, model, key;
    };
    ApiValues apiValues (const juce::String& id) const;
    void setApiValues (const juce::String& id, const ApiValues&);

    /** Sends a message. `onEvent` gets the assistant's events on the message thread, ending with done or error. */
    void send (const juce::String& message, std::function<void (const assistant::Event&)> onEvent);
    void stop();
    bool isBusy() const;
    /** Forgets the conversation. */
    void newChat();

    const std::vector<assistant::Message>& messages() const noexcept { return history; }

    /** Called on the message thread when a message's work has ended (after its last event). */
    std::function<void()> onIdle;

    /** Where the programs run and their files go. */
    static juce::File workFolder();
    /** More programs, written by the user (a JSON list). */
    static juce::File programsFile();

private:
    class Job;
    std::vector<assistant::Program> programs() const;
    const assistant::Program* findProgram (const juce::String& id) const;
    assistant::ApiSettings apiSettings() const;
    void finished (Job*);

    Session& session;
    juce::PropertiesFile* settings;
    std::function<bool()> startService;
    std::shared_ptr<SessionTools> tools;
    juce::String choiceId;
    juce::String sessionId; ///< the program's own conversation id
    juce::String reply;     ///< the answer so far
    std::vector<assistant::Message> history;
    std::unique_ptr<assistant::ApiChat> api;
    std::unique_ptr<Job> job;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace arranger
