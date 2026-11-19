# Intelligent feature ideas for Lazy Arranger MIDI DAW

Research date: 17 November 2026. These are product proposals. Proposals 1 (Arrangement Doctor), 2 (Fit to My Synths), 4 (Check the Transcription) and 10 (Song Forms) have a first version; see the user manual, "Arranging help".

The best direction is to build intelligence around relationships between parts, whole-song decisions, and the user's MIDI hardware. The app already provides AI conversation, melody continuation, part generation, humanization, audio analysis, chord recognition, voice-leading checks, chord-following styles, and clip scenes. The proposals below extend that foundation.

## Scope and evidence limits

The first five proposals are candidate product gaps. The research did not find a documented, complete built-in equivalent in the surveyed mainstream DAWs. This does not prove that no equivalent exists. Plug-ins, scripts, specialist sequencers, and academic systems already implement parts of these workflows. Novelty applies to each specified end-to-end interaction, not to its component algorithms. This is a product opportunity assessment, not a patent search.

The second five proposals have documented precedents. Product facts have citations; suggested behavior, value, priorities, and effort are our judgments. Effort is relative to this codebase and is not a delivery estimate.

See [market evidence](intelligent-features-market-research.md) for the wider comparison.

## Five candidate gaps

### 1. Arrangement doctor with controlled listening comparisons

The user sets a goal, such as making a chorus more distinct while preserving the melody. The app finds possible causes, proposes small edits, and renders comparable A/B examples. For example, it could reduce the verse accompaniment or move a competing keyboard phrase into a gap in the vocal. Each proposal explains the observations behind it and lists the notes, tracks, and bars affected. The user accepts individual changes. Preference learning uses explicit choices.

The opportunity is the evidence-to-edit-to-audition loop. Generic production advice and project-editing chat already exist: [Apple's intelligent assistant](https://www.apple.com/newsroom/2026/10/apple-creator-studio-adds-powerful-new-ai-features-and-even-more-creative-tools/) and [Splice Rheo](https://support.splice.com/en/articles/17907403-how-do-i-use-rheo-in-create) demonstrate this overlap. Do not present an aesthetic judgment as an objective error. Start with MIDI density, register overlap, repetition, and section contrast; audio masking needs additional analysis.

Fit: high. Effort: large. First version: three diagnostics, one user-selected goal, and two previews per proposal.

### 2. Hardware-aware orchestration

Arrange a song for the user's actual connected synthesizers. Combine sound choice and track roles with device-wide polyphony, timbral parts, channels, note ranges, and supported controllers. Explain conflicts and propose musical compromises, such as removing a doubled chord tone or moving a pad to the internal synth while protecting bass and melody.

Instrument ranges and allocation already exist in specialist composition software; [Synfire's instrument setup](https://docs.cognitone.com/synfire/G3/EN/concepts/InstrumentsGlobal.html) is a relevant precedent. The proposed gap is whole-song allocation and repair against the constraints of the user's hardware rig. Voice consumption can depend on patches, layering, sustain, and release tails, so profiles need verified limits and conservative estimates. The current device profiles describe sounds and controls; they do not provide the required complete capability model.

Fit: very high. Effort: large. First version: user-entered capabilities for two devices, peak-polyphony warnings, and previewed reductions.

### 3. Motif relationship map

Find related musical ideas across independently recorded or edited clips. Let the user confirm that a chorus hook, an inverted bridge phrase, and a bass rhythm are related. Store the relationship by rhythm, contour, interval pattern, and musical role. An edit can then propagate only the selected property to selected relatives while preserving their harmony, transformations, and local overrides. Show a preview and a note-level difference before applying it.

Simple linked phrases are established: [Synfire 3 parameter aliases](https://users.cognitone.com/news/synfire-30) update related material while allowing independent interpretation. The proposed gap is discovering relationships among independent performances and offering selective, semantic propagation. Pattern matching alone must not establish a link without user confirmation.

Fit: high. Effort: large to very large. First version: detect transposed and rhythmically similar MIDI phrases, confirm relationships, and propagate rhythm edits only.

### 4. Audio transcription that asks useful questions

After importing audio or a video's audio, identify ambiguities that affect the arrangement. Instead of asking the user to fix every note, offer a short original excerpt and two synthesized alternatives: for example, whether the bass is E or A. Lock the confirmed answer and recompute only the dependent harmony, notes, and generated accompaniment. Preserve unrelated manual corrections and show which results changed.

[Melodyne already exposes potential notes and detection corrections](https://helpcenter.celemony.com/M5/doc/melodyneStudio5/en/M5tour_NA_Mode_Tools?env=standAlone). The proposed gap is choosing a high-value clarification and propagating the answer across analysis and arrangement. Model scores are not automatically calibrated probabilities. The current chord result stores one choice; this feature needs alternatives and dependency tracking.

Fit: very high, especially with video-audio import. Effort: large. First version: bass/root ambiguities, original-versus-MIDI audition, and chord-only recomputation.

### 5. Rehearsal partner that learns where accompaniment should help

Track a player's position in a known song, handle repeats, and remember difficult passages across rehearsals. With the user's agreement, simplify accompaniment around those passages, preserve clear entry cues, and restore the full arrangement after consistent performances. When position is uncertain, use predictable behavior and an explicit recovery cue. Offer separate practice and performance policies.

Tempo following is established in [Ableton Live](https://help.ableton.com/hc/en-us/articles/360019100900-Tempo-Following-in-Live-11-FAQ). Position following and synchronous accompaniment are established in [IRCAM Antescofo](https://forum.ircam.fr/projects/detail/antescofo/), and [score-following research](https://arxiv.org/abs/1512.07748) already handles errors, repeats, and skips. The proposed gap is persistent rehearsal learning connected to musically selective accompaniment adaptation inside the arranger. Do not claim automatic detection of a player's intent is solved.

Fit: high for arranger-keyboard users. Effort: very large. First version: MIDI input, a known score, explicit repeat cues, and an opt-in simpler backing variation for selected bars.

## Five established features worth adapting

### 6. Global chord track with role-aware MIDI reharmonization

Provide a visible harmonic timeline. Bind selected bass, chord, and melodic parts to it, with controls for passing notes, fixed melody notes, inversions, and note ranges. Audition a chord substitution across bound parts while protecting unbound material.

Precedents include [Logic Session Players following the Chord track](https://support.apple.com/en-by/guide/logicpro/lgcpbf624405/12.3/mac/15.6) and [RapidComposer's master-track harmony and phrase tools](https://www.musicdevelopments.com/). The app already recognizes chords and follows chords in styles; the addition is persistent harmonic control over ordinary timeline MIDI parts.

Fit: very high. Effort: medium to large. Start with bass and chord parts, then add controlled melody adaptation.

### 7. Interactive bass, drum, and keyboard players

Expose controls for activity, complexity, register, fills, and playing behind or ahead of the beat. Let a part follow another track's rhythm. Generate several editable MIDI alternatives for the selected section, with musical roles coordinated across tracks.

[Logic Session Players](https://support.apple.com/en-by/guide/logicpro/lgcpbf624405/12.3/mac/15.6) and [rhythm following](https://support.apple.com/en-is/guide/logicpro/lgcpca8dca3d/12.3/mac/15.6) establish the workflow. The app already generates parts and plays styles; the new value is direct controls, coordinated roles, and fast in-context audition.

Fit: very high. Effort: large. Start with deterministic bass/drum patterns and use existing models for optional variations.

### 8. Groove extraction and transfer

Extract timing and velocity patterns from MIDI or audio. Apply timing and accents at separate strengths to selected parts. Support different treatment of kick, snare, and hi-hat. Keep the operation reversible and allow a deliberate user performance to define the song's feel.

[Ableton's Groove Pool](https://www.ableton.com/en/live-manual/12/using-grooves/) documents extraction from audio and MIDI, timing/velocity controls, and nondestructive application. This adds reference-based feel beyond the app's current humanization tools.

Fit: very high. Effort: medium for MIDI, larger for audio. Start with MIDI-to-MIDI transfer and a reusable groove library.

### 9. Selective regeneration with locks and saved alternatives

Select the weak part of a phrase and generate alternatives while preserving accepted notes, rhythm, harmony, or instrumentation. Store candidates in a small audition tray so the user can compare them in the full arrangement and accept one as an undoable edit.

[FL Studio's Chord Progression Tool](https://www.image-line.com/fl-studio-learning-content/fl-studio-online-manual/html/pianoroll_chordprogression.htm) documents locks, neighboring context, and alternatives. [Ableton MIDI Tools](https://www.ableton.com/en/live-manual/12/midi-tools/) document selected-note and selected-time transformations. For this app, begin with the existing melody and continuation tools. Different models may require candidate rejection or explicit constraints; the interface must not promise locks the backend cannot enforce.

Fit: very high. Effort: medium to large. First version: preserve notes outside a selection and compare three candidates.

### 10. Alternative song forms with intelligent transition assistance

Keep named arrangements that reference the same source sections: for example, a short version, an extended introduction, and a bridge variation. Compare them without duplicating the full project. When a new section boundary is created, offer a fill, pickup, held note, or rest that fits both sides. Commit the chosen form only after audition.

[Cubase 16 Arranger chains](https://www.steinberg.help/r/cubase-pro/16.0/en/cubase_nuendo/topics/arranger_track/arranger_track_arranger_editor_controls_r.html) provide alternative chains and flattening. [Splice Rheo](https://support.splice.com/en/articles/17907403-how-do-i-use-rheo-in-create) documents arrangement construction from a loop. The app already moves sections and launches scenes; the addition is persistent alternative song forms with assistance at changed boundaries.

Fit: very high. Effort: medium for arrangement chains, larger for contextual transitions.

## Recommended sequence

1. Groove extraction and transfer (8): start with MIDI and a clear audible result.
2. Global chord track (6): establish shared harmonic intent for later tools.
3. Selective regeneration and comparison (9): improve control over the existing AI tools.
4. Arrangement doctor (1): use the shared harmony, comparison, and preview infrastructure.
5. Hardware-aware orchestration (2): develop the feature most closely aligned with the app's device-profile and arranger-keyboard strengths.

Keep generation off the real-time audio thread. Use one shared proposal-and-preview mechanism for generated changes. Validate notes, timing, ranges, and user locks before a proposal can be accepted. Prefer deterministic theory and arrangement rules where they provide a useful first version; add larger models when they produce a measured musical improvement.
