# MSEQ 8 v1.0.0 — Initial Public Release

MSEQ 8 is an open-source 8-band parametric equalizer with full Mid/Side
processing, dynamic (level-dependent) EQ on every band, and a
psychoacoustically-informed resonance detector that can propose corrective
EQ moves on its own. VST3 on Windows/macOS/Linux, AU on macOS. Built with
JUCE 8, licensed AGPL-3.0.

This is the plugin's first tagged release on GitHub, after 18 rounds of
iteration during development — this note summarizes the full feature set as
it stands today, not just the latest changes (see
[`CHANGELOG.md`](CHANGELOG.md) / [`CHANGELOG.sv.md`](CHANGELOG.sv.md) for
the complete version-by-version history).

## Highlights

- **8 bands, 4 filter types each** — Bell, Low Shelf, High Shelf, Notch —
  each independently routable to Mid, Side, or both.
- **Dynamic EQ on every band**: Threshold/Range/Attack/Release turn any band
  into a frequency-selective compressor or expander, with an optional
  external sidechain input per band.
- **Find Resonances assistant**: listens for 8 seconds and proposes up to 6
  tailored dynamic bands for the worst resonances, ranked the way the ear
  actually perceives them (ERB-scaled neighbourhoods, masking gate,
  equal-loudness weighting) — reviewed in the graph, one click to apply or
  undo.
- **39 factory presets across 14 genres** (Pop/Vocal, EDM/Electronic,
  Rock/Band, Hip-Hop/Trap, Acoustic/Singer-Songwriter, Podcast/Voice,
  Mastering/Bus, R&B/Soul, Jazz/Acoustic Ensemble, Classical/Orchestral,
  Reggae/Dub, Funk/Disco, Latin/Afrobeats, Cinematic/Film), browsed through
  a two-level genre menu, plus your own saved user presets.
- **Match Gain & correlation meter**: one-click output-gain matching for
  fair A/B comparisons, and a live -1…+1 phase correlation strip for
  checking mono compatibility.
- **5-step undo/redo**, **A/B/C/D snapshots**, **Ctrl+hover band audition**,
  **Delta** listening, resizable window, full DAW automation.

## Full feature list

**EQ engine**
- 8 bands, Freq 20 Hz–20 kHz, Gain ±18 dB, Q 0.1–40 (effective ceiling ramps
  from 10 below 150 Hz to 40 above 500 Hz), switchable between
  Bell / Low Shelf / High Shelf / Notch
- Per-band Mid / Mid+Side / Side routing
- High-pass and low-pass filters, Butterworth 12/24/48 dB/oct, with
  independent Mid/Side mode (different frequency and slope per channel)
- Dynamic EQ (Threshold, Range, Attack, Release) on every band, with a
  band-pass detector and a Q-scaled soft knee
- External sidechain input with a per-band SC toggle, falling back to the
  band's own signal when nothing is connected
- Ctrl+hover band audition, mono-compatibility warning
- Output gain ±12 dB, Delta listening, global bypass, Match Gain
- All sound-shaping parameters automatable, full state recall

**Analysis & UI**
- Real-time post-EQ spectrum (Mid/Side/Stereo), 4096-point FFT, adaptive
  rendering rate, FAST/MED/SLOW response
- Live resonance detection (RES) with psychoacoustic weighting and
  hysteresis-stabilized markers
- Find Resonances assistant with tailored Q/depth/attack/release per
  proposal
- 5-step undo/redo, A/B/C/D snapshots
- Draggable graph nodes, ghost rings for active dynamics, crosshair with
  note/cents readout, spectrum freeze, zoomable dB axis with adaptive
  gridlines
- 39 factory presets in a genre-grouped menu + user presets (XML), large
  IN/OUT meters, phase correlation strip, resizable window

## Documentation

- User guide: [`MSEQ8-Guide-EN.pdf`](MSEQ8-Guide-EN.pdf) /
  [`MSEQ8-Guide.pdf`](MSEQ8-Guide.pdf) (Swedish), now illustrated with
  interface screenshots.
- Full development history: [`CHANGELOG.md`](CHANGELOG.md) /
  [`CHANGELOG.sv.md`](CHANGELOG.sv.md).
- Manual test checklist: [`TESTING.md`](TESTING.md).
- Planned direction: [`ROADMAP.md`](ROADMAP.md) — linear-phase mode,
  searchable preset browser, CLAP support, and expanded snapshots are next.

## Installing / building

Prebuilt binaries: see the release assets on this page (if attached), or
build from source — CMake ≥ 3.22 and a C++17 compiler, JUCE 8 is fetched
automatically:

```sh
cmake -B build
cmake --build build --config Release
```

VST3 ships on all platforms, AU additionally on macOS. macOS builds are not
yet code-signed/notarized by CI, so a locally built copy needs Gatekeeper
bypassed manually for now. Full instructions in [`README.md`](README.md).

CI builds and runs `pluginval` at strictness level 5 on every push; the
plugin has also been manually verified at level 10.

## Known limitations

- Stereo input/output only (Mid/Side processing requires it).
- macOS builds are unsigned until notarization is set up in CI.

## License

AGPL-3.0. JUCE is used under its AGPL-3.0 option, so distributed binaries
must remain open source under AGPL-compatible terms. See
[Licensing in the README](README.md#licensing) for how this applies to VST3
and AU.

## Thanks

Thanks to everyone who tests, files issues, or opens PRs — see
[`ROADMAP.md`](ROADMAP.md) for what's planned next, and the
[issue tracker](https://github.com/JanKar76/MSEQ-8---Dynamic-MID-SIDE-EQ-with-psychoacoustically-informed-resonance-detection-/issues)
for open items.
