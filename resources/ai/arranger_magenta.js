// Arranger MIDI DAW: Magenta helper (Magenta.js in Node, Apache 2.0).
//
//   node arranger_magenta.js run <request.json> <answer.json>
//   node arranger_magenta.js prefetch
//
// The app writes a request and reads the answer (see src/core/include/arranger/core/MidiAi.h).
// It prints "PROGRESS <0..1> <text>" lines, then "RESULT <answer.json>" or "ERROR <text>".
// The model checkpoints are kept in ARRANGER_MAGENTA_MODELS (prefetch downloads them),
// so the tools also work with no internet.

'use strict';

const fs = require('fs');
const path = require('path');

const BASE = 'https://storage.googleapis.com/magentadata/js/checkpoints/';
const MODELS = {
  groove: 'music_vae/groovae_2bar_humanize',
  drumify: 'music_vae/groovae_tap2drum_2bar',
  melody: 'music_rnn/melody_rnn',
  drums: 'music_rnn/drum_kit_rnn',
  improv: 'music_rnn/chord_pitches_improv',
};
const modelsDir = process.env.ARRANGER_MAGENTA_MODELS || path.join(__dirname, 'magenta-models');

function say(line) { process.stdout.write(line + '\n'); }
function progress(p, text) { say(`PROGRESS ${p.toFixed(2)} ${text}`); }

// Magenta.js loads its checkpoints with fetch(). Serve the downloaded files from disk.
const realFetch = globalThis.fetch;
globalThis.fetch = async (url, options) => {
  const u = String(url);
  if (u.startsWith(BASE)) {
    const local = path.join(modelsDir, u.slice(BASE.length));
    if (fs.existsSync(local)) {
      const data = fs.readFileSync(local);
      const type = local.endsWith('.json') ? 'application/json' : 'application/octet-stream';
      return new Response(data, { status: 200, headers: { 'Content-Type': type } });
    }
  }
  return realFetch(url, options);
};
globalThis.performance = globalThis.performance || require('perf_hooks').performance;

// Magenta.js prints a lot to the console: keep stdout for the app's lines.
const quiet = () => {};
console.log = quiet;
console.info = quiet;
console.warn = quiet;

async function download(url, file) {
  const r = await realFetch(url);
  if (!r.ok) throw new Error(`download failed (${r.status}): ${url}`);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file + '.part', Buffer.from(await r.arrayBuffer()));
  fs.renameSync(file + '.part', file);
}

async function prefetch() {
  const names = Object.keys(MODELS);
  for (let i = 0; i < names.length; ++i) {
    const dir = MODELS[names[i]];
    progress(i / names.length, `Downloading the Magenta ${names[i]} model`);
    for (const f of ['config.json', 'weights_manifest.json']) {
      const file = path.join(modelsDir, dir, f);
      if (!fs.existsSync(file)) await download(BASE + dir + '/' + f, file);
    }
    const manifest = JSON.parse(fs.readFileSync(path.join(modelsDir, dir, 'weights_manifest.json'), 'utf8'));
    for (const group of manifest)
      for (const p of group.paths) {
        const file = path.join(modelsDir, dir, p);
        if (!fs.existsSync(file)) await download(BASE + dir + '/' + p, file);
      }
  }
  progress(1, 'Magenta models ready');
}

// ---- notes ----------------------------------------------------------------------------------
const STEPS = 4; // steps per quarter note

function quantised(notes, offset, totalQuarters, drum) {
  const seq = { notes: [], totalQuantizedSteps: Math.max(1, Math.round(totalQuarters * STEPS)), quantizationInfo: { stepsPerQuarter: STEPS } };
  for (const n of notes) {
    const s = Math.round((n.start + offset) * STEPS);
    if (s < 0 || s >= seq.totalQuantizedSteps) continue;
    const e = Math.min(seq.totalQuantizedSteps, Math.max(s + 1, Math.round((n.start + n.length + offset) * STEPS)));
    seq.notes.push({ pitch: n.pitch, quantizedStartStep: s, quantizedEndStep: e, velocity: n.velocity || 90, isDrum: drum });
  }
  return seq;
}

/** One note at a time: at each step the highest note that starts there; notes end where the next one starts. */
function monophonic(seq) {
  const byStep = new Map();
  for (const n of seq.notes)
    if (!byStep.has(n.quantizedStartStep) || byStep.get(n.quantizedStartStep).pitch < n.pitch) byStep.set(n.quantizedStartStep, n);
  const notes = [...byStep.values()].sort((a, b) => a.quantizedStartStep - b.quantizedStartStep);
  for (let i = 0; i + 1 < notes.length; ++i)
    notes[i].quantizedEndStep = Math.min(notes[i].quantizedEndStep, notes[i + 1].quantizedStartStep);
  return { ...seq, notes };
}

function fromSteps(seq, offsetQuarters, drum) {
  return seq.notes.map(n => ({
    start: n.quantizedStartStep / STEPS + offsetQuarters,
    length: Math.max(1, n.quantizedEndStep - n.quantizedStartStep) / STEPS,
    pitch: n.pitch,
    velocity: n.velocity || (drum ? 100 : 90),
    drum,
  }));
}

/** Notes with startTime/endTime in seconds at 60 BPM, so seconds are quarter notes. */
function fromSeconds(seq, offsetQuarters, drum) {
  return seq.notes.map(n => ({
    start: Math.max(0, n.startTime + offsetQuarters),
    length: Math.max(0.05, n.endTime - n.startTime),
    pitch: n.pitch,
    velocity: n.velocity || 100,
    drum,
  }));
}

// ---- the models -------------------------------------------------------------------------------
let mm = null;
function magenta() {
  if (!mm) {
    mm = {
      vae: require('@magenta/music/node/music_vae'),
      rnn: require('@magenta/music/node/music_rnn'),
      core: require('@magenta/music/node/core'),
    };
  }
  return mm;
}

async function vae(name) {
  const m = new (magenta().vae.MusicVAE)(BASE + MODELS[name]);
  await m.initialize();
  return m;
}

async function rnn(name) {
  const m = new (magenta().rnn.MusicRNN)(BASE + MODELS[name]);
  await m.initialize();
  return m;
}

/** GrooVAE works on 2 bars of 4/4 at a time. */
async function grooveWindows(model, request, drumInput, label) {
  const window = 8; // quarters
  const out = [];
  const count = Math.max(1, Math.ceil(request.quarters / window));
  for (let w = 0; w < count; ++w) {
    progress(0.2 + 0.8 * w / count, `${label} bars ${w * 2 + 1}-${w * 2 + 2}`);
    const from = w * window;
    const notes = request.notes.filter(n => n.start >= from && n.start < from + window);
    if (notes.length === 0) continue;
    const seq = quantised(notes, -from, window, drumInput);
    const z = await model.encode([seq]);
    const decoded = await model.decode(z, request.temperature || 0.5, undefined, STEPS, 60);
    z.dispose();
    for (const n of fromSeconds(decoded[0], from, true))
      if (n.start < request.quarters) out.push(n);
  }
  return out;
}

async function groove(request) {
  progress(0.1, 'Loading Magenta Groove');
  return grooveWindows(await vae('groove'), request, true, 'Groove');
}

async function drumify(request) {
  progress(0.1, 'Loading Magenta Drumify');
  return grooveWindows(await vae('drumify'), request, false, 'Drumify');
}

/** Moves pitches by octaves into a model's range; returns the shift. */
function octaveShift(notes, low, high) {
  if (notes.length === 0) return 0;
  const mid = (Math.min(...notes.map(n => n.pitch)) + Math.max(...notes.map(n => n.pitch))) / 2;
  let shift = 0;
  while (mid + shift < low + 6) shift += 12;
  while (mid + shift > high - 6) shift -= 12;
  return shift;
}

async function continuation(request) {
  const drum = request.kind === 'drums';
  progress(0.1, drum ? 'Loading Magenta DrumsRNN' : 'Loading Magenta MelodyRNN');
  const model = await rnn(drum ? 'drums' : 'melody');
  const primerQuarters = Math.max(1, Math.ceil(-Math.min(...request.primer.map(n => n.start))));
  let seq = quantised(request.primer, primerQuarters, primerQuarters, drum);
  let shift = 0;
  if (!drum) {
    seq = monophonic(seq);
    shift = octaveShift(seq.notes, 48, 83);
    for (const n of seq.notes) n.pitch = Math.min(83, Math.max(48, n.pitch + shift));
  }
  progress(0.4, 'Writing');
  const result = await model.continueSequence(seq, Math.round(request.quarters * STEPS), request.temperature || 1.0);
  const notes = fromSteps(result, 0, drum);
  for (const n of notes) n.pitch -= shift;
  return notes;
}

/** A chord symbol Magenta can read: no bass note; else just the root (and minor). */
function readableChord(text) {
  const { chords } = magenta().core;
  const tries = [text, text.split('/')[0], (text.match(/^[A-G][#b]?m?(?!aj)/) || ['N.C.'])[0]];
  for (const t of tries) {
    if (t === 'N.C.') return t;
    try { chords.ChordSymbols.pitches(t); return t; } catch (e) { /* next */ }
  }
  return 'N.C.';
}

async function improvise(request) {
  progress(0.1, 'Loading Magenta ImprovRNN');
  const model = await rnn('improv');
  const steps = Math.round(request.quarters * STEPS);
  // One chord per step (Magenta spreads the list evenly over the primer and the new steps).
  const primerSteps = 1;
  const list = [];
  const chords = request.chords.map(c => ({ at: Math.round(c.start * STEPS), chord: readableChord(c.chord) }));
  for (let s = -primerSteps; s < steps; ++s) {
    let chord = chords[0].chord;
    for (const c of chords) if (c.at <= Math.max(0, s)) chord = c.chord;
    list.push(chord);
  }
  const primer = { notes: [], totalQuantizedSteps: primerSteps, quantizationInfo: { stepsPerQuarter: STEPS } };
  progress(0.4, 'Writing a melody');
  const result = await model.continueSequence(primer, steps, request.temperature || 1.0, list);
  return fromSteps(result, 0, false);
}

async function run(requestFile, answerFile) {
  const request = JSON.parse(fs.readFileSync(requestFile, 'utf8'));
  const ops = { groove, drumify, continue: continuation, improvise };
  if (!ops[request.op]) throw new Error(`the Magenta helper can't do "${request.op}"`);
  const notes = await ops[request.op](request);
  fs.writeFileSync(answerFile, JSON.stringify({ notes }));
  progress(1, 'Done');
  say('RESULT ' + answerFile);
}

(async () => {
  try {
    const [command, a, b] = process.argv.slice(2);
    if (command === 'prefetch') await prefetch();
    else if (command === 'run') await run(a, b);
    else throw new Error('use: arranger_magenta.js run <request.json> <answer.json> | prefetch');
    process.exit(0);
  } catch (e) {
    say('ERROR ' + String(e && e.message ? e.message : e).replace(/\s+/g, ' '));
    process.exit(1);
  }
})();
