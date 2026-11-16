#include "arranger/core/Progressions.h"

#include <algorithm>
#include <cctype>

namespace arranger::progressions
{

namespace
{
    const std::vector<Entry> kEntries = {
        // Pop.
        { "pop-1", "Pop: I–V–vi–IV", "Pop", "I V vi IV", "C major" },
        { "pop-2", "Pop: I–IV–V–I", "Pop", "I IV V I", "C major" },
        { "pop-3", "Pop: I–IV–Vsus4–V", "Pop", "I IV Vsus4 V", "C major" },
        { "pop-4", "Pop: I–vi–ii–V", "Pop", "I vi ii V", "C major" },
        { "pop-5", "Pop: I–ii–IV–V", "Pop", "I ii IV V", "C major" },
        { "pop-6", "Pop: I–V6–vi–V", "Pop", "I V6 vi V", "C major" },
        { "pop-7", "Pop: i–iv–V–i", "Pop", "i iv V i", "A minor" },
        { "pop-8", "Pop: i–III–iv–v", "Pop", "i III iv v", "A minor" },

        // Rock.
        { "rock-1", "Rock: I–vi–IV–V", "Rock", "I vi IV V", "C major" },
        { "rock-2", "Rock: I–IV–I–V", "Rock", "I IV I V", "C major" },
        { "rock-3", "Rock: I–I–bIII–IV", "Rock", "I I bIII IV", "C major" },
        { "rock-4", "Rock: I–III–IV–iv", "Rock", "I III IV iv", "C major" },
        { "rock-5", "Rock: ii–I–V–bVII", "Rock", "ii I V bVII", "C major" },
        { "rock-6", "Rock: V–V–IV–I", "Rock", "V V IV I", "C major" },
        { "rock-7", "Rock: vi–IV–I–V", "Rock", "vi IV I V", "C major" },
        { "rock-8", "Rock: vi–V7/vi–II–V7", "Rock", "vi V7/vi II V7", "C major" },

        // Jazz.
        { "jazz-1", "Jazz: ii7–V7–Imaj7–Imaj7", "Jazz", "ii7 V7 Imaj7 Imaj7", "C major" },
        { "jazz-2", "Jazz: ii7–V7–Imaj7–VI7", "Jazz", "ii7 V7 Imaj7 VI7", "C major" },
        { "jazz-3", "Jazz: Imaj7–vi7–ii7–V7", "Jazz", "Imaj7 vi7 ii7 V7", "C major" },
        { "jazz-4", "Jazz: Imaj7–#Idim7–ii7–V7", "Jazz", "Imaj7 #Io7 ii7 V7", "C major" },
        { "jazz-5", "Jazz: Imaj7–I7–IVmaj7–iv7", "Jazz", "Imaj7 I7 IVmaj7 iv7", "C major" },
        { "jazz-6", "Jazz: viø7–ii7–V7–Imaj7", "Jazz", "viø7 ii7 V7 Imaj7", "C major" },
        { "jazz-7", "Jazz: iiø7–V7–i7–i7", "Jazz", "iiø7 V7 i7 i7", "C minor" },
        { "jazz-8", "Jazz: i7–V7–i7–V7", "Jazz", "i7 V7 i7 V7", "C minor" },

        // Twelve-bar blues: one chord for each bar.
        { "blues-1", "Blues: 12-bar I7–IV7–V7", "Blues", "I7 I7 I7 I7 IV7 IV7 I7 I7 V7 IV7 I7 I7", "C major" },
    };

    std::string lower (std::string_view text)
    {
        std::string out (text);
        std::transform (out.begin(), out.end(), out.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return out;
    }

    std::vector<std::string> words (std::string_view text)
    {
        std::vector<std::string> out;
        std::string word;
        for (const char c : text)
        {
            if (std::isspace (static_cast<unsigned char> (c)))
            {
                if (! word.empty())
                    out.push_back (word);
                word.clear();
            }
            else
            {
                word += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
            }
        }
        if (! word.empty())
            out.push_back (word);
        return out;
    }
} // namespace

const std::vector<Entry>& all()
{
    return kEntries;
}

std::vector<Entry> find (std::string_view query, std::string_view genre)
{
    const auto wantedGenre = lower (genre);
    const auto wanted = words (query);
    std::vector<Entry> out;
    for (const auto& entry : kEntries)
    {
        if (! wantedGenre.empty() && lower (entry.genre) != wantedGenre)
            continue;
        const auto haystack = lower (entry.id + " " + entry.name + " " + entry.genre + " " + entry.roman + " " + entry.key);
        bool matches = true;
        for (const auto& word : wanted)
            if (haystack.find (word) == std::string::npos)
            {
                matches = false;
                break;
            }
        if (matches)
            out.push_back (entry);
    }
    return out;
}

std::vector<Chord> chords (const Entry& entry, const theory::Key& key)
{
    return theory::progression (entry.roman, key);
}

} // namespace arranger::progressions
