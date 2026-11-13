#pragma once

#include "arranger/core/Notation.h"
#include "arranger/core/Song.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

/**
    MusicXML: the exchange format of notation programs (Sibelius, Finale,
    Dorico, MuseScore). Writing sends the score that the Score view shows.
    Reading takes the notes, the key, the meter, the tempo and the sounds of
    an uncompressed file (.musicxml or .xml). A compressed .mxl file is a ZIP
    archive; the app unpacks it and passes the text here.
*/
namespace arranger::musicxml
{

struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** The song as a partwise MusicXML 4.0 document: one part per track that has notes on the Timeline. */
std::string write (const Song&, const notation::Options& = {});
void save (const Song&, const std::filesystem::path&, const notation::Options& = {});

/** Reads a partwise document into a new song: one track with one clip per part. Throws Error. */
Song read (std::string_view xml);
Song load (const std::filesystem::path&);

} // namespace arranger::musicxml
