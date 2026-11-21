# UI languages

The app has English, Danish, French and Vietnamese menu translations.

## Use another language

Select `Options > Language`, then select a language. The app asks for a restart;
the new language is used after the restart.

`System Default` follows the operating system. English is the fallback when the
system language is not one of the four supported languages.

## Translation files

The translations are in `resources/languages`:

- `da.txt` - Danish
- `fr.txt` - French
- `vi.txt` - Vietnamese

The files use the JUCE `LocalisedStrings` format. Each key is the English text
that the code passes to `TRANS`, for example:

```
"File" = "Filer"
```

A missing key falls back to the English text. The files are built into the app,
so a translation change needs a new build.

## Add text to the translation

Use `TRANS` for menu text and other user-visible strings:

```
info.setInfo (TRANS ("New"), "New empty project", TRANS ("File"), 0);
```

Add the same English key to all three language files. Do not translate internal
command IDs, file formats, product names or text that a machine reads.

Changing a key in the code breaks its translation until the language files use
the new key. Keep the English key stable when possible.

## Test a language

The `--ui-language` option applies a language to one run without changing the
saved setting. It is useful for screenshots:

```
"Arranger MIDI DAW" --ui-language da --shortcuts --screenshot da.png
```

The current scope is the menu bar and the menu command labels. Command
descriptions, panels, dialogs, tooltips, error messages and the manual are still
English.
