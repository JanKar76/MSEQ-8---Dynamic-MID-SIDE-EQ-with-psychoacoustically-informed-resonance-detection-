# MSEQ 8 — Pre-release testing guide

Run through this checklist after every major change. These steps need a built
binary on your own machine, so this is the part of the job that stays with
you.

## 1. pluginval (automated validation)

Download [pluginval](https://github.com/Tracktion/pluginval/releases) and run:

```powershell
pluginval --strictness-level 5 --validate "C:\Program Files\Common Files\VST3\MSEQ 8.vst3"
```

Raise it to `--strictness-level 10` once level 5 passes cleanly. Level 5+
covers opening/closing the editor mid-playback, automation of every
parameter, state recall, different buffer sizes and sample rates, and
thread safety.

Also worth running Steinberg's own VST3 validator (bundled with the VST3
SDK) if you have it built.

## 2. DAW matrix

Test in at least FL Studio + Reaper (free trial, the strictest of the common
hosts).

| Test | What you're verifying |
|---|---|
| Automating band freq/gain/Q | Smooth, click-free (parameter smoothing), the curve follows |
| Automating dyn threshold/range | The ghost ring reacts correctly |
| A/B/C/D + save/reload project | Exact recall of every parameter + window size |
| User preset: save, restart DAW, load preset | Preset persists and loads correctly |
| Resize mid-playback | No audio glitches, rendering pauses/resumes cleanly |
| Delta + MONITOR M/S combined | Hearing delta of the correct channel |
| Find Resonances mid-playback, APPLY, UNDO | Proposals are reasonable, UNDO restores exactly |
| Bypass toggling mid-playback | Click-free enough, badge shows |
| 44.1 / 48 / 96 kHz | Curves and spectrum line up, no aliasing in the UI |
| Buffer size 64 / 512 / 2048 | No difference in behaviour |
| Mono track (if the DAW allows it) | Plugin fails gracefully (stereo required) — no crashes |
| Render/export with delta OFF | The export sounds like playback |
| Ctrl+hover audition on a band | Isolates that band's frequency range, releases cleanly |
| Independent Mid/Side HP/LP | Two nodes appear, each with its own frequency/slope |
| Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y | Undo/redo up to 5 steps, across parameters, Find Resonances, and presets |
| Multiple instances (3+), unfocused windows | Host stays responsive; no audible glitches from the render throttle |

## 3. CPU check

- Load 8 instances in one project, all with RES on and 2-3 dynamic bands
  each.
- Compare the DAW's CPU meter with editor windows closed vs. open
  (spectrum/detection only run while a window is open).
- Rendering already throttles automatically for unfocused plugin windows
  (skipping most ticks unless Find Resonances is actively listening), so
  CPU load with several instances open but only one focused should stay
  low. If it doesn't, that throttle is the first place to check.

## 4. Listening tests

- A zeroed EQ should be bit-transparent aside from floating-point noise:
  render in/out and null them against each other (phase-inverted) — the
  residual should sit below roughly -120 dBFS.
- Static +18 dB boost, Q 10, sweep the freq knob fast: no zipper clicks.
- Dynamic band with extreme range (-18): the pumping should be smooth, no
  clicks.
- HP 48 dB/oct at 30 Hz on bass-heavy material: no instability/DC drift.

## Known remaining simplifications

- HP/LP frequency sweeps aren't smoothed (the bands are) — fast automation
  of hp_freq/lp_freq can zipper slightly.
- The spectrum/detection view shows the delta signal while DELTA is active
  (intentional: show what you hear). Don't run Find Resonances with delta
  on.
