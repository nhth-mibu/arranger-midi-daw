#pragma once

#include "arranger/core/Theory.h"

#include <string>
#include <string_view>
#include <vector>

/**
    Common chord progressions for the AI and for the chord tools: pop, rock,
    jazz and the twelve-bar blues. The progressions are written as roman
    numerals, so they work in every key. The examples follow the common
    progressions listed by Musicca's chord player (musicca.com/chord-player).
*/
namespace arranger::progressions
{

struct Entry
{
    std::string id;    ///< stable name, such as "pop-1"
    std::string name;  ///< "Pop: I–V–vi–IV"
    std::string genre; ///< "Pop", "Rock", "Jazz" or "Blues"
    std::string roman; ///< the chords as roman numerals, separated by spaces
    std::string key;   ///< the key of the example, such as "C major"
};

/** The built-in list, in a fixed order. */
const std::vector<Entry>& all();

/**
    Finds entries by words in the name, genre, roman numerals or key, and by
    genre. An empty query and genre return all. Matching ignores case.
*/
std::vector<Entry> find (std::string_view query, std::string_view genre = {});

/** The chords of an entry in a key. */
std::vector<Chord> chords (const Entry&, const theory::Key&);

} // namespace arranger::progressions
