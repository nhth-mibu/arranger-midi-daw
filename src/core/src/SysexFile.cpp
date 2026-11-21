#include "arranger/core/SysexFile.h"

#include <stdexcept>

namespace arranger::sysex_file
{

std::vector<MidiBytes> parse (const std::string& bytes)
{
    std::vector<MidiBytes> messages;
    std::size_t i = 0;
    while (i < bytes.size())
    {
        const auto b = static_cast<unsigned char> (bytes[i]);
        if (b == 0xf0)
        {
            MidiBytes message;
            message.push_back (b);
            ++i;
            while (i < bytes.size() && static_cast<unsigned char> (bytes[i]) != 0xf7)
            {
                if (static_cast<unsigned char> (bytes[i]) == 0xf0)
                    throw std::runtime_error ("a SysEx message starts before the previous one ends");
                message.push_back (static_cast<unsigned char> (bytes[i]));
                ++i;
            }
            if (i >= bytes.size())
                throw std::runtime_error ("the last SysEx message has no F7");
            message.push_back (0xf7);
            ++i;
            messages.push_back (std::move (message));
            continue;
        }
        if (b == 0xf7)
            throw std::runtime_error ("F7 without a matching F0");
        // Some editors write a text header or whitespace. Ignore it only
        // between messages; a byte inside a message is copied above.
        throw std::runtime_error ("byte " + std::to_string (i + 1) + " is outside a SysEx message");
    }
    if (messages.empty())
        throw std::runtime_error ("the file has no SysEx messages");
    return messages;
}

std::string encode (const std::vector<MidiBytes>& messages)
{
    std::string out;
    for (const auto& m : messages)
    {
        if (m.size() < 2 || m.front() != 0xf0 || m.back() != 0xf7)
            throw std::runtime_error ("a message is not F0 ... F7");
        for (std::size_t i = 1; i + 1 < m.size(); ++i)
            if (m[i] >= 0xf8)
                throw std::runtime_error ("a message has a status byte inside it");
        out.append (reinterpret_cast<const char*> (m.data()), m.size());
    }
    if (out.empty())
        throw std::runtime_error ("there are no messages to write");
    return out;
}

} // namespace arranger::sysex_file
