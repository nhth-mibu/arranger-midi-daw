# Intelligent arranger features: market research

Research date: 17 November 2026. This note supports a feature brainstorm for Lazy Arranger MIDI DAW. It uses first-party product documentation and release announcements. Proposed features below are design inferences, not claims made by those sources.

## Main finding

The strongest differentiation is a reliable musical workflow with clear constraints, previews, and user control. Generic chat control, chord generation, humanization, audio transcription, and loop-to-song generation already have substantial competition. In particular, the November 2026 market includes Logic Pro's intelligent assistant and Splice Create's Rheo. Calling a conversational music assistant new would be inaccurate. [Apple announcement](https://www.apple.com/newsroom/2026/10/apple-creator-studio-adds-powerful-new-ai-features-and-even-more-creative-tools/), [Splice AI overview](https://support.splice.com/en/articles/18233829-how-does-create-use-ai).

The five proposed gaps mean **no equivalent complete workflow found in the official mainstream product material reviewed**. They do not mean nobody has ever built the components. Absence from documentation cannot prove absence from every product, plug-in, research prototype, or private release. The closest existing features are identified explicitly.

## Existing features worth extending into this arranger

### 1. Accompaniment that follows another part's rhythm

Logic Session Player regions can follow the rhythm of another selected track, as well as emphasize chord changes. A documented example has drums and bass follow a guitar track in a chorus. This is a stronger model for an arranger than generating each part independently. [Apple: follow rhythm](https://support.apple.com/en-is/guide/logicpro/lgcpca8dca3d/12.3/mac/15.6).

Suggested adaptation: let the user select a leader track and separately set how much bass, drums, and keyboard parts follow its accents. Preserve editable MIDI, with section-specific behavior and a preview. This is an extension of existing chord-following styles and MIDI generation, rather than a claim that those foundations are missing.

### 2. Chord substitution and modulation paths

Fender Studio Pro's Chord Assistant recommends chords from an existing progression. Scaler 3 offers chord suggestions, automatic voice leading, substitutions, and paths to a destination key, including modal interchange and mediant methods. [Fender: Chord Track](https://fenderstudiopromanual.fender.com/en/Content/Arranging_Topics/Chord%20Track.htm), [Scaler 3](https://scalermusic.com/products/scaler-3/).

Suggested adaptation: an auditionable reharmonization panel that preserves the melody and bass notes selected by the user. Offer alternatives such as subtle, jazzier, brighter, or a bridge to another key. Show the chord and voicing changes before updating dependent parts. Existing harmony checks and chord-following generation provide a useful foundation.

### 3. Extract and transfer groove

Ableton can extract timing and volume information from an audio or MIDI clip as a groove. That groove can modify other clips. This is distinct from randomizing timing and velocity. [Ableton: grooves and extraction](https://www.ableton.com/en/live-manual/12/using-grooves/).

Suggested adaptation: extract a reusable timing/accent template from imported audio, including a video's extracted audio. Apply separate timing and velocity amounts to selected MIDI parts. Keep downbeats or selected notes fixed and preview different strengths. A first version can use a short, clearly percussive reference rather than attempt every full mix.

### 4. Local variations with locks and alternatives

FL Studio's Chord Progression Tool already supports locking chords, regenerating a selected chord with neighboring context, showing alternatives, and adding a passing chord. Ableton's scale-aware MIDI Tools generate new material and transform existing notes; their controls support audition and application. Bitwig Operators provide chance, repeat behavior, occurrence conditions, and loop recurrence. [FL Studio tool](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_chordprogression.htm), [Ableton MIDI Tools](https://www.ableton.com/en/manual/midi-tools/), [Bitwig Operators](https://www.bitwig.com/userguide/latest/operators/).

Suggested adaptation: select two bars, protect a hook and cadence, then compare a few fills or alternate bass phrases in place. Retain the favorite variation and its generation settings. This should improve the control and comparison around current AI generation, rather than merely add another generator.

### 5. Arrangement alternatives and safe experimentation

Fender Studio Pro Scratch Pads are alternate timelines inside the same session, with independent loop ranges and shared tracks. They can hold whole sections and move material back to the main arrangement. Splice's own Rheo instructions explicitly include building an arrangement from a loop. [Fender: Scratch Pad](https://fenderstudiopromanual.fender.com/en/Content/Arranging_Topics/ScratchPad.htm), [Splice: using Rheo](https://support.splice.com/en/articles/17907403-how-do-i-use-rheo-in-create).

Suggested adaptation: generate and audition distinct section plans from existing scenes: compact song, longer build, instrumental break. Keep each as an alternative arrangement until the user adopts it. Existing scenes make this a natural extension. Basic section generation is an established capability, so the value comes from coherent transitions and easy comparison.

## Five differentiated workflows to investigate

### 6. Arrangement doctor with controlled listening comparisons

The user sets a goal, such as making the chorus feel larger. The arranger proposes small musical edits, renders matched A/B excerpts, and states what changed: remove a competing piano octave, delay a fill, or leave space before the chorus. A preference selection teaches the system which changes the user likes. A useful implementation would separate measured facts, such as note density, from musical judgments.

Closest overlap: Rheo already analyzes audio, controls effects, and responds to project requests. Therefore generic advice, chat editing, or automatic EQ is not the differentiator. The proposed gap is a repeatable experiment workflow: isolated cross-track edits, matched playback, explicit hypothesis, and saved listening preference. [Splice AI overview](https://support.splice.com/en/articles/18233829-how-does-create-use-ai).

### 7. Whole-song arranging for the user's actual hardware

The user chooses connected synths and priorities. The arranger maps parts to available sounds and solves shared polyphony, MIDI channel, range, articulation, and controller limits. It reports compromises and previews them: for example, omit a doubled fifth during a dense section to preserve the melody and bass on a limited-voice device.

Closest overlap: Scaler has Divisi and voice allocation across instruments. Cubase 15 Expression Maps include articulation control and per-articulation attack compensation. Dorico's proofreading already warns about instrument ranges and some physical limitations. The proposed gap is their integration into a whole-song resource allocation and repair process for a particular MIDI rig, rather than voice splitting or range checking alone. [Scaler 3](https://scalermusic.com/products/scaler-3/), [Steinberg: Expression Maps](https://www.youtube.com/watch?v=ak4CRBgFd8Y), [Dorico 6.2.10 version history](https://download.steinberg.net/automated_updates/sda_downloads/aa79cebc-bdea-4359-98f9-c27535dc1b16/Dorico_6.2.10_Version_History.pdf).

### 8. Recognize and maintain a motif's identity across a song

Recognize that an independently recorded bass rhythm, an inverted melody, and a slower bridge phrase derive from the same hook. Show the relationship. Let the user propagate only a chosen property, such as the first three intervals or the accent pattern, while preserving intentional differences and local exceptions. Audition the affected occurrences together before committing.

Closest overlap is substantial: RapidComposer supports motif development and nondestructive phrase variations. Synfire Figures preserve melodic movement under different harmonies, and Synfire 3 parameter aliases update multiple derived phrases with independent transformations. A simple motif library or linked clip would not be new. The differentiated part is recognizing previously independent transformed occurrences and maintaining semantic relationships with explicit change previews. [RapidComposer features](https://www.musicdevelopments.com/features.html), [Synfire Figure](https://docs.cognitone.com/synfire/G2/EN/concepts/Figure.html), [Synfire 3 announcement](https://users.cognitone.com/news/synfire-30).

### 9. Transcription that asks the most useful correction question

When audio analysis is ambiguous, choose a short listening question that resolves several downstream decisions: “Does this bass note sound like E or A?” Audition both candidates, accept “neither,” lock the answer, and recalculate only affected chords, note assignments, and generated accompaniment. Preserve the original audio and explain unresolved confidence.

Closest overlap: Melodyne already exposes potential notes and lets the user activate or deactivate alternatives in Note Assignment Mode. Manual correction and alternative pitches are therefore established. The proposed gap is selecting questions by their expected usefulness, retaining answers as constraints, and updating the dependent arrangement coherently. [Celemony: note assignment tools](https://helpcenter.celemony.com/M5/doc/melodyneStudio5/en/M5tour_NA_Mode_Tools?env=standAlone).

### 10. Rehearsal accompaniment that learns where to help

Follow the player's song position and repeated sections, then offer help where recent performances show difficulty. Simplify accompaniment while preserving entry cues, and gradually restore complexity. Let the musician mark an alternate chord as intentional instead of classifying it as a mistake. Capture useful changes as a rehearsal version of the song.

Closest overlap: Ableton already follows incoming audio tempo; Logic follows another track's rhythm. Score-following accompaniment predates modern AI: classic SmartMusic Follow Me followed the soloist and accepted performance markers. Research published in 2015 explicitly handled performance errors, tempo changes, and arbitrary repeats or skips. Thus tempo following, score-position recovery, or responsive accompaniment alone is not new. The proposed gap combines an explicit intentional-change decision, persistent learning of difficult passages, and accompaniment simplification inside an arranger. Intended reharmonization cannot reliably be inferred from a single wrong-sounding note; the first version should ask the user. [Ableton Tempo Following](https://help.ableton.com/hc/en-us/articles/360019100900-Tempo-Following-in-Live-11-FAQ), [Finale/SmartMusic performance markers](https://usermanuals.finalemusic.com/FinaleWin/Content/Finale/SmartMusic_Performance_Markers.htm), [Nakamura et al.: real-time score alignment](https://arxiv.org/abs/1512.07748).

## Scope and limitations

The review covered official material for Ableton Live, Logic Pro, Cubase, Fender Studio Pro, FL Studio, and Bitwig. It also checked Splice Create, Scaler, RapidComposer, Synfire, Melodyne, Band-in-a-Box, and historical SmartMusic to avoid mistaking specialist features for new inventions. Cubase 16 was released on 4 November 2026; its documented highlights include instrument stacks, MIDI editing improvements, stem separation, and tempo detection. [Cubase 16 release notes](https://www.steinberg.net/cubase/release-notes/16/).

Band-in-a-Box already generates multi-instrument arrangements from chords and a chosen style, and its Audio Chord Wizard imports chords from audio. Audio-to-arrangement as a broad idea therefore cannot be claimed as new either. [Band-in-a-Box 2026 manual](https://www.pgmusic.com/manuals/bbm2026full/chapter1.htm).

These findings support product discovery, not patent novelty or freedom-to-operate conclusions. Validate the five differentiated workflows with a small prototype and musician listening sessions before investing in large models. No software was implemented or published as part of this research.
