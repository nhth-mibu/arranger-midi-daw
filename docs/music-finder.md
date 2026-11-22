# Music Finder

The desktop Music Finder stores named songs with their style, tempo, voices and notes in a JSON file.

```json
{
  "format": "arranger-music-finder",
  "entries": [
    { "name": "Song A", "style": "SlowBossa.sty", "tempo": 92.0, "voices": ["Piano", "Bass"], "notes": "" }
  ]
}
```

## AI tools

- `add_music_finder_entry` adds a song to the file, creating it when needed.
- `list_music_finder` lists its songs.
- `export_music_finder_csv` writes the CSV format for a spreadsheet.
- `import_music_finder_csv` reads that CSV back into the JSON book.
- `import_music_finder_mfd` imports a Yamaha `.mfd` file from the Music Finder folder.

CSV columns are `name,style,tempo,voices,notes`; several voices are separated by `|`.

## MFD layout

The record layout follows the Yamaha `MDB-100-100-3000` Music Finder format: 36-byte header with a big-endian record count at bytes 34-35, then 92-byte records. A record stores the internal style number at 4-5, meter at 6-7, tempo at 8-9, title at 11, genre at 43 and keywords at 59. The style-path tail after the counted records is ignored. This was validated against the supplied `Nhat.mfd` (1,653 records).
