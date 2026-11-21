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

CSV columns are `name,style,tempo,voices,notes`; several voices are separated by `|`.
