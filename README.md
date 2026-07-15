# MSEQ 8

**An open-source 8-band parametric Mid/Side equalizer with dynamic EQ — VST3 / AU, built with JUCE.**

MSEQ 8 lets you EQ the *centre* (mid) and the *width* (side) of a stereo signal
independently. Clean up muddy bass in the stereo width without touching the kick,
add air to the sides without sharpening the lead vocal, or make any band dynamic
so it only works when it is needed.


<img width="1000 " height="6000" alt="MSEQ8_plugin" src="https://github.com/user-attachments/assets/eed72d4d-1058-41d4-b824-007e27557979" />


> Licensed under **AGPL-3.0** — free to use, study, modify and redistribute.
> See [Licensing](#licensing) for the details that apply to JUCE and VST3.

Repository: <https://github.com/JanKar76/MSEQ-8---Dynamic-MID-SIDE-EQ-with-psychoacoustically-informed-resonance-detection->

---

## Features

**EQ engine**
- 8 bands: Freq 20 Hz–20 kHz, Gain ±18 dB, Q 0.1–10, each independently switchable
  between **Bell, Low Shelf, High Shelf and Notch** from the right-click panel
  (Notch is gain-less — a pure surgical cut, its node locks to 0 dB)
- Per-band Mid / Mid+Side / Side routing (M/S encode–decode inside the plugin)
- High-pass and low-pass filters: Butterworth 12/24/48 dB per octave, each with
  its own Stereo / Mid / Side routing
- **Independent Mid/Side high-pass & low-pass**: switch either filter into
  independent mode to run completely different frequencies (and slopes) on
  Mid and Side — e.g. high-pass the sides harder than the centre to keep the
  low end mono without narrowing the stereo image
- **Dynamic EQ on every band**: per-band Threshold, Range, Attack and Release
  (all four adjustable from the right-click dynamics panel), band-pass
  detector with soft 12 dB knee, allocation-free coefficient updates in
  sub-blocks
- **External sidechain input**: an optional stereo sidechain bus plus a
  per-band `SC` toggle in the dynamics panel — lets any band's gain
  reduction be triggered by an external signal (e.g. duck a band whenever a
  kick drum on another track hits) instead of its own audio. Falls back
  silently to the band's own signal if the host hasn't connected the
  sidechain bus
- **Ctrl+hover band audition**: hold Ctrl and hover a band to solo just that
  frequency range (bandpass isolation with crossfade), so you can hear what a
  band is sitting on before you touch it
- **Mono-compatibility warning**: flags excessive low-frequency Side energy
  relative to Mid — a common cause of phase cancellation on mono playback
- Output gain ±12 dB (ramped), global bypass, click-free Mid/Side solo monitoring
- **DELTA**: listen to only what the EQ is removing/adding — computed in the
  M/S domain before the monitor stage, so it combines with MONITOR M/S to
  isolate the change in just the centre or just the width
- **MATCH button**: auto-sets output gain from a ~2 s running average of
  input vs. post-EQ loudness, so a boost or cut doesn't skew an A/B
  comparison with perceived loudness
- All sound-shaping parameters exposed for DAW automation, full state recall

**Analysis & UI**
- Real-time post-EQ spectrum for mid, side and stereo (√(mid²+side²)):
  4096-point FFT with 75 % overlap, +3 dB/oct display tilt, adaptive
  rendering rate (throttled when the window is unfocused, e.g. with several
  instances open at once), frame rate-independent smoothing with
  FAST/MED/SLOW response
- **RES — live resonance detection**: flags the strongest persistent narrow
  peaks per channel using a dedicated RMS detector spectrum, ERB-scaled
  critical-band neighbourhoods, frequency-stability gating, a masking gate
  and harmonic discrimination — perceptually weighted (inverted
  equal-loudness ≈ 80 phon) so markers point at what actually sounds harsh.
  Runs independently of the RES visual toggle, so it always has signal to
  work with. Labels stack into rows when resonances are close in frequency
  so none get hidden
- **Find Resonances assistant** (formerly "ANALYZE"): listens for 8 seconds,
  measures the bandwidth (Q), severity (cut depth) and level (threshold) of
  the most problematic resonances, and proposes up to 6 dynamic bands —
  review in the graph, then APPLY (with one-click UNDO, auto-dismissed on
  the next click elsewhere) or DISCARD
- **5-step undo/redo** (Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y): covers every
  parameter change, Find Resonances proposals, and preset switches — built
  on debounced whole-state snapshots, so nothing is ever a risk to try
- Draggable nodes (drag = freq/gain, scroll = Q, right-click = dynamics panel
  sized and positioned to stay clear of the curve you're editing),
  ghost rings showing dynamic bands' actual gain in real time
- Crosshair with frequency + musical note + cents, hover readouts,
  spectrum freeze for before/after comparison, zoomable dB axis
- A/B/C/D snapshots, 39 factory presets across 14 genres (browsed via a
  genre → preset menu) + user presets (XML), large IN/OUT meters with dB
  scale, a broadband phase correlation strip (-1..+1), resizable window

Full manual: [`MSEQ8-Guide-EN.pdf`](MSEQ8-Guide-EN.pdf) (English) /
[`MSEQ8-Guide.pdf`](MSEQ8-Guide.pdf) (Swedish).
Development changelog: [`CHANGELOG.md`](CHANGELOG.md) (English) /
[`CHANGELOG.sv.md`](CHANGELOG.sv.md) (Swedish original, updated first).

## Building

Requirements: CMake ≥ 3.22 and a C++17 compiler
(MSVC 2022 / Xcode / GCC or Clang). JUCE 8 is fetched automatically via
CMake FetchContent on the first configure.

```sh
cmake -B build
cmake --build build --config Release
```

- **Formats:** VST3 on all platforms, AU additionally on macOS. macOS builds
  are not yet code-signed/notarized by CI — a local unsigned build will need
  Gatekeeper bypassed manually until that's set up (see [`ROADMAP.md`](ROADMAP.md)).
- `COPY_PLUGIN_AFTER_BUILD` is enabled, so the plugin is installed to the
  system plugin folder after building (on Windows this may require running
  the build from an elevated prompt the first time).
- If you already have JUCE locally, replace the `FetchContent` block in
  `CMakeLists.txt` with `add_subdirectory(path/to/JUCE)`.

### Project layout

```
CMakeLists.txt              CMake + JUCE setup (VST3/AU targets)
Source/
  PluginProcessor.h/.cpp    DSP: M/S engine, bands, HP/LP, dynamics, spectrum taps
  PluginEditor.h/.cpp       Editor: header, band columns, meters, presets, A/B/C/D
  EQGraphComponent.h/.cpp   Interactive graph: curves, spectra, nodes, resonances
  LookAndFeel.h             Theme, knob look-and-feel, level meter
```

## Quick start

1. Load MSEQ 8 on a stereo track or bus.
2. Drag a node to EQ. Set the band's **M/MS/S** button to choose whether it
   affects the centre, the width, or both.
3. Right-click a node to make the band **dynamic** (Threshold, Range, Attack,
   Release; negative range ducks the band only when that frequency region
   gets loud).
4. Enable **RES** in the graph legend to have the detector point out the
   strongest resonances per channel — cut, and the next one appears. Click
   **FIND RESONANCES** to let the assistant listen for 8 seconds and propose
   dynamic bands for you automatically.
5. Hold **Ctrl and hover** a band to audition just that frequency range
   before committing to a change.
6. Use **MONITOR M/S** to hear exactly the channel you are shaping, and
   compensate level with the **GAIN** knob before A/B-ing with **BYPASS**.
7. Made a mistake? **Ctrl+Z** / **Ctrl+Shift+Z** (or **Ctrl+Y**) undo and
   redo up to 5 steps of anything you've changed.

## Licensing

This project's own source code is released under the
**GNU Affero General Public License v3.0 (AGPL-3.0)**.

- **JUCE** is dual-licensed; this project uses it under the AGPL-3.0 option,
  which requires derived distributed works (including binaries) to remain
  open source under AGPL-compatible terms. If you want to ship a closed-source
  fork you must obtain a [commercial JUCE license](https://juce.com/get-juce/)
  instead.
- **VST3**: the VST 3 SDK interfaces used via JUCE are available under GPLv3
  (compatible with this project's AGPL-3.0) or Steinberg's proprietary license.
  *VST is a registered trademark of Steinberg Media Technologies GmbH.*
- **AU**: the Audio Unit format is part of Apple's SDKs.

The [`LICENSE`](LICENSE) file contains the canonical AGPL-3.0 text.

## Contributing

[Issues](https://github.com/JanKar76/MSEQ-8---Dynamic-MID-SIDE-EQ-with-psychoacoustically-informed-resonance-detection-/issues)
and pull requests are welcome. Please keep PRs focused, describe the
audible/visible effect of DSP or UI changes, and verify the plugin still
passes `pluginval` at strictness level 5 or higher before submitting. See
[`ROADMAP.md`](ROADMAP.md) for planned direction.

## Known limitations

- Stereo input/output only (Mid/Side requires it).
