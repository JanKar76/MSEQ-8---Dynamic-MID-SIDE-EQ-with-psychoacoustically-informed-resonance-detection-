# MSEQ 8 — 8-band Mid/Side parametric EQ (JUCE, VST3/AU)

## v19 — Frequency-dependent Q ceiling

- **New: effective Q ceiling now scales with frequency instead of being a
  flat 10.** Bandwidth ≈ freq/Q, so a Q of 10 already carves a vanishingly
  narrow notch on sub-bass while barely denting a resonance in the mid/high
  range — raising the cap flatly to 40 would have blown past the dynamics
  engine's attack/release/knee formulas (which share the same Q) at low
  frequencies without buying anything musically useful down there. Instead
  `MSEQ8AudioProcessor::maxQForFreq()` ramps the effective ceiling from 10
  below 150 Hz to 40 above 500 Hz (linear ramp in between), applied
  silently everywhere Q is actually used for processing: filter
  coefficients, the dynamics detector/knee, Ctrl+hover audition, and Find
  Resonances' own proposed Q. The raw `Q` parameter's range is now 0.1–40
  (knob skew unchanged) so the extra headroom is reachable by typing an
  exact value or via presets/automation; the graph's scroll-wheel Q step
  respects the same frequency-dependent ceiling per node.
- Listening-tested: less musical material gets filtered away as collateral
  when using higher Q for surgical removal in the mid/high range.
- **Fix: the graph's Bell/Notch curves stopped getting visually narrower
  above Q=10 (Notch: Q=25), even though the actual filter kept narrowing
  correctly.** `EQGraphComponent`'s `bandMagnitudeDb()` approximates band
  shape with `x = octaveOffset / max(floor, bandwidth * 0.5)`; the floor
  (0.05 for Bell, 0.02 for Notch) was harmless while Q was hard-capped at a
  flat 10 everywhere, but once the new frequency-dependent ceiling allowed
  Q up to 40, it started getting hit for any Q above 10/25 and silently
  froze the drawn curve's width at that point on — the on-screen shape
  stopped tracking the knob while the audio kept changing underneath it.
  Lowered both floors (0.005 / 0.002) to sit below the narrowest bandwidth
  the ceiling can now produce (0.0125 at Q=40), so the curve tracks Q all
  the way to the cap again.

## v18 — Genre-grouped factory presets, header/grid polish

- **New: 38 new factory presets across 14 genres**, on top of the original 3 (Vocal
  Clarity, Wide Master, Low-End Focus - kept unchanged). Genres: Pop/Vocal,
  EDM/Electronic, Rock/Band, Hip-Hop/Trap, Acoustic/Singer-Songwriter, Podcast/Voice,
  Mastering/Bus, R&B/Soul, Jazz/Acoustic Ensemble, Classical/Orchestral, Reggae/Dub,
  Funk/Disco, Latin/Afrobeats, and Cinematic/Film. Each preset is a curated set of
  band gain/Q/mode/type and dynamics-engine deltas from the neutral default (only the
  original three factory frequencies/slots are ever touched, never `freqID`). See
  `MSEQ8AudioProcessor::getPresetList()` / `applyPreset()`.
- **New: two-level preset browser.** The flat preset `ComboBox` is replaced by a
  `TextButton` that opens a `PopupMenu` with one submenu per genre (built from
  `getPresetList()`'s order), "Default" at the top level, then "User Presets" and
  "Save preset..." below - scales cleanly now that the preset count has grown from 4
  to 39 (+ user presets).
- **New: "Dialogue-Safe Mix" (Cinematic/Film) enables sidechain by default** on the
  band4 dynamics detector (`SC` toggle), intended to be paired with a dialogue stem
  routed into the plugin's sidechain input for ducking music under dialogue; falls
  back to self-detection if nothing is connected, per the existing sidechain fallback.
- **Fix: `CorrelationMeter` didn't compile with `setTooltip()`.** It's a bare
  `juce::Component` and needs `juce::SettableTooltipClient` mixed in, same as
  `Button`/`TextButton` already get for free.
- **New: adaptive dB grid step lines.** The EQ graph's horizontal gridlines now use
  2 dB spacing when zoomed in tight (`displayMaxDb <= 9`), 3 dB at medium zoom
  (`<= 15`), and 6 dB at the original wide zoom - replacing a fixed 6 dB spacing that
  was too coarse once zoomed in, and that silently stopped drawing lines past ±12 dB
  even when zoomed out further than that.
- **Change: header control cluster (presets/A-B-C-D/monitor/delta/gain/match/bypass)
  anchored immediately after the "8-BAND MID/SIDE EQUALIZER" subtitle text**, measured
  via `GlyphArrangement::getStringWidthInt` rather than a guessed fixed offset, for
  tighter use of small-screen width.

## v17 — Match Gain, correlation meter, sidechain input for dynamic bands

- **New: Match Gain button (MATCH, in the header next to the GAIN knob).**
  Keeps a ~2 second running average of input power (`avgInPower`, measured
  before any processing) and of output power right before the output-gain
  stage (`avgOutPower`, measured directly on the decoded L/R pair). One
  click sets `output_gain` to `jlimit(-12, 12, inDb - outDb)` so A/B
  comparisons aren't skewed by a boost or cut simply sounding louder/
  quieter on its own. Does nothing if there hasn't been enough signal yet
  (near-silence on either side).
- **New: broadband phase correlation meter (CORR, under the IN/OUT
  meters).** Computes a classic correlation coefficient
  (`sum(L*R) / sqrt(sum(L^2)*sum(R^2))`) directly on the decoded L/R pair
  every block, EMA-smoothed over ~5 blocks for a stable readout. -1 = fully
  out of phase (mono-incompatible), 0 = wide/uncorrelated, +1 =
  mono-compatible. A cheap extension of the same signal data the
  mono-compatibility warning already looks at, exactly as proposed in the
  roadmap.
- **New: external sidechain input for the dynamic bands.** An optional
  stereo sidechain bus (`.withInput("Sidechain", ..., false)`, disabled by
  default until the host connects it) plus a per-band `SC` toggle in the
  right-click dynamics panel. When enabled and the host is actually
  delivering sidechain audio, the band's detector reads the external L/R
  pair (encoded to mid/side the same way as the main input) instead of the
  band's own processed signal - the gain reduction itself still always
  applies to the band's own audio. Falls back silently to the band's own
  signal if the host hasn't connected the sidechain bus.
  `isBusesLayoutSupported()` requires the sidechain bus to be either
  disabled or stereo.
- **Fix: `output_gain` ramp explicitly limited to channels 0/1.** With the
  sidechain bus enabled, the shared `buffer` passed to `processBlock()` can
  now carry more than 2 channels; `applyGainRamp` without a channel
  argument affects ALL channels in the buffer, which would have needlessly
  touched the sidechain channels too. Switched to two explicit per-channel
  calls (channel 0 and 1).
- **Minimum window width raised 1020 -> 1140.** The MATCH button widened
  the header's right-hand cluster by ~54px; because that cluster is
  anchored to the window's right edge while the preset/A-B-C-D block scales
  from the centre, a flat +54 to the floor left zero clearance between them
  at the minimum size. 1140 restores the original ~24px margin.

## v16 — GitHub cleanup: CI, English source comments, documentation fix

- **New: GitHub Actions CI (`.github/workflows/build.yml`).** Builds VST3 on
  Windows and VST3+AU on macOS on every push/PR, caches JUCE's
  `FetchContent` download, runs `pluginval --strictness-level 5` on the
  built plugin, and (best-effort, `continue-on-error`) Apple's `auval` for
  the AU format. Provides regression protection and signals to potential
  contributors that the project is actively maintained.
- **Fix: README's "Known limitations" section was stale.** It claimed
  static bands lacked parameter smoothing and allocated on the audio thread
  during coefficient updates - both already fixed since v12 (see the
  `SmoothedValue`-based smoothing and the `ArrayCoefficients` update further
  down this log). A pure documentation fix, no code change required.
- **All comments in `Source/*.cpp` and `Source/*.h` translated to
  English.** The source's identifiers (variable/function/class names) and
  all UI strings were already English; only the inline comments written
  during development were in Swedish. No logic or code changed - a pure
  comment translation, done to make the codebase approachable for
  international contributors now that the repo is public on GitHub.

## v15 (cont'd) — Dynamics panel knob size & placement

- **Fix: the BandDynPanel's (right-click a node) Threshold/Dyn Range/Attack/
  Release knobs were visibly smaller than the FREQ/GAIN/Q knobs at the
  bottom of the main UI.** Cause: they used `TextBoxBelow`, which makes
  JUCE's built-in slider layout steal height from the rotary control before
  it's drawn — whereas the band columns' knobs use `NoTextBox` (the value
  is drawn by hand in `paint()`) and therefore get their whole allocated
  area. Switched BandDynPanel to the exact same pattern: `NoTextBox` +
  fixed 42px knob height (identical to BandColumn) + values drawn manually
  below the labels. The panel is now tighter (280×152 instead of
  280×170).
- **Fix: the panel covered the bell/shelf curve for the node being
  edited.** `CallOutBox` never draws itself on top of its `targetArea`,
  only outside it (left/right/top/bottom — whichever side has the most
  room). Previously `targetArea` was just an 8×8 point at the click
  location, so the panel still ended up right next to/over the curve
  shape. `targetArea` now spans the full plot height and ±140px around the
  node's x-position, which forces `CallOutBox` out to the left or right of
  that entire region — the curve is now visible live while dragging
  dynamics parameters.

## v15 (cont'd) — Undo/redo (5 steps) + Applied message dismisses on click

- **New feature: Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z for redo) undo/redo up to 5
  steps.** Built on whole APVTS state snapshots (the same pattern the
  A/B/C/D slots and Find Resonances' Apply/Undo already use), not
  fine-grained per-parameter tracking. An `AudioProcessorValueTreeState::
  Listener` is registered for ALL parameters (including any added later —
  no hand-picked list); `parameterChanged()` only does a lock-free atomic
  increment of a counter — thread-safe regardless of whether the call
  comes from the audio thread (host automation) or the message thread
  (GUI). All the heavy work (ValueTree copying, `replaceState`) instead
  happens in the editor's `timerCallback()` (guaranteed to be the message
  thread), which debounces for ~400 ms of no further changes before
  committing a snapshot — so one continuous knob/node drag becomes ONE
  undo step, not one per millimetre of mouse movement. Preset switches,
  A/B/C/D changes, and Find Resonances Apply/Undo are automatically
  covered too, for free, since they already go through the same
  parameter-change mechanism.
- **The "Applied — click UNDO" affordance now dismisses immediately on
  any click** elsewhere in the plugin, not just after the 10s timeout. The
  click then continues to do its normal thing (node, legend, etc.) exactly
  as before.

## v15 (cont'd) — Independent HP/LP filters for mid and side

- **New feature: HP and LP can now run at DIFFERENT frequencies (and
  slopes) for mid and side at the same time**, instead of only a single
  shared frequency routed to Stereo/Mid/Side. Enabled via "Independent
  Mid/Side" in the HP/LP right-click menu. Off (default): identical
  behaviour to before this feature existed — a single shared
  hp_freq/hp_slope, routed via hp_mode. On: hp_freq/hp_slope control MID,
  new hp_side_freq/hp_side_slope control SIDE, both always active
  simultaneously (the routing menu is hidden in this case — it's
  meaningless once both channels run independently anyway). Same pattern
  for LP (lp_independent/lp_side_freq/lp_side_slope).
- **New, additive APVTS parameters** (hp_independent, hp_side_freq,
  hp_side_slope, lp_independent, lp_side_freq, lp_side_slope) — backward
  compatible with old projects/presets/automation lanes, which only know
  about the existing hp_freq/hp_slope/hp_mode parameters and therefore
  fall back to identical behaviour to before.
- **DSP:** hpMid/hpSide (and lpMid/lpSide) were already separate filters
  per channel internally, just configured with the same values — so
  independent mode required no new filter infrastructure, just letting
  the two channels take different coefficients (and therefore potentially
  a different number of cascaded sections at different slopes, hence the
  separate hpSections/hpSideSections).
- **UI:** two new, conditional graph nodes (hpSideNode/lpSideNode) that
  only exist/are clickable/draggable when the respective independent mode
  is on — coloured green (mid) and orange (side) to match the convention
  used throughout the rest of the graph, offset one row below the mid
  triangle so they stay distinguishable even when the frequencies happen
  to coincide.

## v15 (cont'd) — Three new features + RES label/text fixes

- **Ctrl+hover band audition.** Hold Ctrl and hover a band node → that
  band's approximate frequency range (a bandpass around its freq/Q) is
  isolated in the final L/R signal so you can hear just that band, plus
  two thin vertical lines in the graph at the band's approximate -3dB
  edges. Affects monitoring only, not the actual EQ curve. No APVTS
  parameter (shouldn't be automated/saved) — driven by an atomic
  `auditionBand` that the UI sets on every timer tick, with a ~15 ms
  crossfade in the DSP for click-free on/off and a safety reset in the
  editor's destructor if the window closes mid-audition.
- **Mono-compatibility warning.** If the side signal is on average
  clearly stronger than mid below 500 Hz for a while, the text "SIDE
  SIGNAL HIGH IN LOW FREQUENCIES" appears in the graph's top-left corner
  (below any crosshair readout, so they never collide). Reuses
  detDbMid/Side (the same data RES uses), with hysteresis (3 dB on / 1 dB
  off) and ~1.5s persistence so it doesn't flicker on short transients.
- **RES labels now stack** instead of overwriting each other when several
  resonances sit close in frequency: sorted by x-position, and a label
  that would collide with the previous one on the same row is moved to a
  new row below (the marker/line stays at the correct frequency, only the
  text moves).
- **The legend text "ANALYZE"** renamed to **"FIND RESONANCES"** (all
  caps, matching the MID/SIDE/RES/ST style).
- **Mojibake fix:** "Applied — click UNDO" had a broken UTF-8 byte escape
  for the em dash that produced garbled characters in the UI. Switched to
  a plain hyphen.

## v15 (cont'd) — Focus-based throttling (3+ instances made FL Studio sluggish)

- **With 3+ MSEQ 8 windows open at once, FL Studio became sluggish/
  unresponsive, even though 2 windows worked fine after earlier fixes.**
  The 60→30 fps reduction alone wasn't enough. Every open window runs its
  FFT/smoothing/resonance chain at full rate ALL THE TIME, regardless of
  whether the window is actually visible/active at that moment — with
  several open simultaneously, often on the same UI thread in the host,
  the cost adds up linearly until the thread can't keep up.
  **Fix:** a plugin window that lacks OS focus (`peer->isFocused()`) now
  runs FFT/smoothing/resonance detection/repaint at a reduced rate (every
  4th tick, ~7.5 Hz instead of 30 Hz) — enough that the graph doesn't
  look frozen, but far cheaper in the background. EXCEPTION: an ongoing
  Find Resonances listening pass always runs at full rate regardless of
  focus, so its measurement data is never incomplete just because the
  user clicked over to another window.
- **Follow-up bug: background windows got choppy/stepping spectrum
  rendering.** `dt` (time since the last update, used by all exponential
  smoothing/detection) was set from `lastFrameMs` BEFORE the throttle
  skip, so it was always ~33 ms — even though in background mode (every
  4th tick) roughly 133 ms had actually passed since the last heavy
  update. The curve therefore only managed to move a quarter of the way
  to its target each time it did update. **Fix:** `dt`/`lastFrameMs` are
  now set after the throttle skip, so it always reflects the time since
  the last ACTUAL update.

## v15 (cont'd) — Find Resonances gave false "NO SIGNAL" without RES enabled + CPU reduction

- **Find Resonances (then still named ANALYZE) could report "NO SIGNAL"
  despite real audio playing (DELTA off, the IN meter moving, RES finding
  resonances separately).** Root cause in `updateResonances()`: the
  entire detector chain (`updateDetector()` + `detectChannelResonances()`)
  had an early return if the RES visualisation toggle (`resEnabled`) was
  off — and that toggle resets to off on every new editor instance
  (not saved in the preset). Without RES enabled, `detLevMid/Side` (Find
  Resonances' signal check) and `flagTimeMid/Side` (the basis for
  `buildProposals()`) were never populated, so Find Resonances only
  worked by chance if the user happened to have RES on.
  **Fix:** the detector chain now always runs, regardless of the RES
  toggle. The drawing itself (`drawResonances`) has its own separate
  `resEnabled` check and is unaffected — RES visualisation is still
  on/off as before.
- **Reduced update rate 60 → 30 fps** in the graph component's timer.
  Everything is already dt-based (exponential smoothing), so the result
  is identical at a lower rate — only the redraw/FFT polling becomes
  sparser. This also lowers the actual FFT rate (from the production
  rate's ~43/s down to the polling rate's 30/s), reducing CPU load per
  open window, especially noticeable with several instances open at
  once.
- **Renamed the legend text "ANALYZE" to "Find Resonances"** in the
  graph's legend, to more clearly signal what the feature actually does.

## v15 (cont'd) — Extreme CPU load with multiple instances open simultaneously

- **Critical bug: two open MSEQ 8 instances in the same DAW (reported in
  FL Studio) caused near 100% CPU and a completely unresponsive editor,
  even though a single instance alone sat at ~3%.** Root cause:
  `juce::LookAndFeel::setDefaultLookAndFeel(&knobLnf)` in the
  `PluginEditor` constructor sets a **process-global** pointer, shared by
  ALL plugin instances in the same host process (not a per-instance
  setting). Every editor overwrote the same global pointer to point at
  its *own* `knobLnf` instance on construction, and unconditionally
  nulled it on destruction — so as soon as instance A closed (or was even
  just created after B), instance B's still-open window could be left
  with no valid LookAndFeel at all, which in turn caused heavy extra
  load/repeated rebuilding on every paint call for any
  PopupMenu/ComboBox/AlertWindow/CallOutBox relying on it.
  **Fix:** switched to instance-bound `setLookAndFeel()` on the editor
  itself (correctly inherited by child components, no global state left).
  The standalone top-level windows that don't inherit through the
  component tree (the PopupMenu in the HP/LP menu, the CallOutBox and its
  BandDynPanel content, the AlertWindow in "Save preset...") each now get
  their own explicit `setLookAndFeel()` binding instead of relying on a
  shared global. This was the only shared/global mutable state in the
  entire codebase — everything else (FFT, detection, DSP caches) was
  already correctly instance-bound per plugin instance.

## v15 (cont'd) — Find Resonances bands completely silent in the dynamics engine (root cause found)

- **Bands applied by Find Resonances (then ANALYZE) produced no audible
  result at all (confirmed with DELTA: nothing was being removed).**
  Manually created "gain 0, dynamics only" bands worked fine, which
  pointed away from the dynamics engine itself and specifically toward
  the apply path. Root cause found in `updateFilters()`'s type-change
  branch — `applyProposals()` always sets the band type to Bell at the
  same time as the frequency changes; if the band happened to have a
  *different* type from earlier (e.g. Notch/Shelf tried earlier in the
  session), that triggered the discrete type-change branch, which wrote
  `lastFreq[i]` to the new target **without** ever calling
  `setTargetValue()` on the frequency smoother. Result: the smoother
  never ramped, `isSmoothing()` never became true, and the detector's
  bandpass filter (which was only synced when `dynamic && smoothing` were
  both true) froze permanently at the band's old frequency — the detector
  measured the wrong part of the spectrum and therefore never triggered.
  **Fix (two parts, should work regardless of what the user did with the
  band beforehand):**
  1. The type-change branch now snaps the smoother directly
     (`setCurrentAndTargetValue`) and updates *both* filter and detector
     coefficients immediately, instead of only writing cache variables
     and leaving the smoother/detector untouched.
  2. The detector update in `processBandChannel()` no longer requires a
     freq/Q ramp to happen to be in progress at the same time the band is
     dynamic (`dynamic && smoothing` → `dynamic`) — and a new
     `lastRange[]` cache in `updateFilters()` forces an immediate
     coefficient refresh (filter + detector) as soon as a band switches
     from static to dynamic or back, so the first block after an
     apply/activation always syncs the detector correctly (otherwise the
     first block could be missing both an in-progress ramp and an
     already-changed effective gain, and the update condition never
     triggered).
- **The Find Resonances (then ANALYZE) chips' status message ("N
  suggestions ready" etc.) visually collided with the APPLY/DISCARD/UNDO
  buttons.** The message was drawn right-aligned all the way to the edge
  regardless of which buttons were visible. Now the right edge is
  computed from `analyzeState` (stops at APPLY's left edge in proposal
  mode, UNDO's left edge in applied mode) so they never overlap.

## v15 (cont'd) — Dynamics panel labels

- **The THRESHOLD/DYN RANGE/ATTACK/RELEASE text ended up behind the
  rotary controls' arc, unreadable.** `paint()` drew the labels at a
  fixed y-position (76px) that had never been updated when the panel grew
  from 2 to 4 knobs — `resized()` didn't reserve any dedicated space for
  the row, so the labels ended up in the middle of the knobs' drawing area
  instead of above them. Fixed: `resized()` now reserves a dedicated 14px
  row for the labels before laying out the knobs, and `paint()` computes
  the same y-position (8 inset + 26 title + 24 type row + 6 gap = 64px)
  instead of a loose hardcoded number. Panel height raised 150 → 170px so
  the knobs fit comfortably below the new row.

## v15 (cont'd) — Build verification

- **pluginval `--strictness-level 10`: SUCCESS.** First full build
  verification of the entire v15 batch (FFT calibration, Find Resonances
  fixes, Q measurement/dedup, GUI cleanup). All tests green at
  44.1/48/96 kHz and buffer sizes 64-1024: audio processing, automation
  (incl. sub-block), state save/restore, editor automation, thread safety
  for parameters and the background thread, bus handling, and the
  fuzz-parameter test. Remaining per `TESTING.md`: the manual DAW matrix
  (FL Studio/Reaper) and the listening tests.

## v15 (cont'd) — GUI quality pass

- **`Colours::white` removed.** The node ring outline and the band number
  in `drawNodes()` used pure white (`Colours::white.withAlpha(...)`) —
  the only place in the entire UI that didn't go through `Theme::`.
  Switched to `Theme::text` (the same light greyish-beige as all other
  text); pure white/black is now avoided consistently throughout.
- **Embedded font (Montserrat + Bebas Neue) tried and reverted.** Added
  via `juce_add_binary_data`, but required manually downloading the font
  files and placing them in `Assets/Fonts/` before building — the sandbox
  couldn't fetch them on its own, and the extra build step (`juceaide`)
  crashed with an unreadable error (`MSB8066`/"Unhandled exception") the
  first time the files were missing. After weighing the tradeoff (extra
  build complexity and a manual download step against marginal branding
  gain), Jan chose to go back to JUCE's system sans (same as before this
  change) — simpler, no build dependencies, no licensing questions.
  `CMakeLists.txt`, `LookAndFeel.h` and the logo text in
  `PluginEditor.cpp` reverted; `Assets/Fonts/` removed.

## v15 — FFT calibration, Find Resonances fixes and UI cleanup

- **Find Resonances' (then ANALYZE) Q measurement is now frequency-
  smoothed before the -3dB bandwidth is measured** (a light
  neighbouring-point averaging, used only for the bandwidth walk — peak
  detection itself is untouched). Previously a single noisy point could
  make a genuinely moderately-wide resonance measure as falsely narrow,
  producing an artificially high Q that almost always clamped to the
  ceiling (10) regardless of the resonance's real character.
- **Dedup between proposed bands is now based on actual -3dB overlap
  instead of a fixed point distance.** Two narrow, distinct resonances
  close together in frequency can now become two separate narrow
  proposals instead of the weaker one being discarded — several narrow
  filters are often better than one wide reduction that also removes
  sound you want to keep between the peaks.

- **Fixed EMA(1s) removed — it was accidentally drowning out the SPEED
  control** (the SPEED response nearly disappeared entirely because the
  subsequent fixed 1000 ms smoothing was always the bottleneck). Replaced
  with fractional-octave-like frequency smoothing: a triangle-weighted
  window over neighbouring points along the frequency axis, the same
  established method REW/SMAART and others use. Fixes the raggedness on
  the correct axis without touching SPEED's time response.
- **`analyzePeakLevel` (controls the "NO SIGNAL" message) now reads
  `detLevMid/Side`** (max-based) instead of the new average-based
  display — the threshold (0.02) was calibrated against the old max
  levels and incorrectly produced "NO SIGNAL" despite a clear signal.
- **Attack, knee width, and threshold margin are now Q-dependent — three
  adjustments to match the ear's actual perception of narrow vs. wide
  resonances:**
  - **Attack** in Find Resonances' proposals now takes the larger of "two
    periods" (as before) and a Q/f term (τ ≈ Q/(π·f), the detector
    bandpass filter's own time constant) — the same physics release
    already used. Narrow/high-Q resonances get a longer attack so the
    envelope has time to become a meaningful measurement before the gain
    moves.
  - **The knee width** in the dynamics engine (was a fixed 12 dB,
    applies to *all* dynamic bands, not just Find Resonances) is now
    Q-dependent: ~7 dB (steep, decisive) at high Q, up to 18 dB (gentle)
    at low Q — narrow tones stand out perceptually even at a small excess
    (weaker masking within a critical band than broadband energy gets),
    broad resonances fare better when not clamped too hard.
  - **The threshold margin** in Find Resonances' proposals (was a fixed
    -2 dB) is now Q-dependent: tighter (~2 dB) at high Q for
    consistent taming of the tone, looser (~4-5 dB) at low Q to avoid
    choking broadband, musical material.

- **FFT magnitude calibrated to a real dBFS reference** (`processTap()`):
  it was previously completely unnormalised (Hann window + 4096-point
  transform with no compensation), which read all material tens of dB
  too hot. That produced two separate bugs with the same root cause: the
  spectrum display slammed into the ceiling at normal listening levels,
  and Find Resonances' Threshold proposals ended up on a scale the real
  dynamics engine's envelope detector never reaches — so Apply wrote the
  correct parameters but produced practically no audible cut. A single
  normalisation at the source (`kFftMagRef = fftSize / 4`) fixes both,
  no separate auto-gain solution needed.
- **Find Resonances' Q proposals clamped to 2-10** (was 2-12) to match
  the bands' actual Q limit — the chips and the applied values now agree.
- **RES marker lines now reach down to the 0 dB line** instead of a
  fixed 8px stub at the top of the graph, so they visually connect with
  the EQ curve's zero point. Each marker also now has its own frequency
  label directly to its right (green = Mid, orange = Side) instead of a
  combined "RES ..." line in the corner.
- **The crosshair readout (frequency/note/dB) is now fixed in the plot
  area's top-left corner** (inside the plot, not in the header rows). It
  used to sit behind the Find Resonances chips and disappear when a
  proposal was shown; an intermediate version that followed the mouse
  cursor in turn ended up covering the MID/SIDE/RES clickable area, so
  the fixed position in the graph is the final solution.
- **The UNDO button after Apply auto-hides after 10 seconds** (the
  applied parameters are unaffected, only the affordance disappears).
- **Minimum window width raised 900 → 1020 px**: below ~980 px the
  preset/A-B-C-D block collided with the monitor/delta/gain/bypass block.
  An old, narrower saved `uiWidth` is now clamped up on load instead of
  recreating the overlap.
- **Extra visual smoothing (EMA, tau=1s) of the spectrum curves** on top
  of the existing SPEED-driven attack/release smoothing, for a calmer
  curve. Fully separate from detection's own ~400 ms averaging — affects
  neither RES nor Find Resonances.
- **Find Resonances can now propose up to 6 bands** (was 4), still
  within the existing 8 — at least 2 bands are always left free for
  manual work.
- **Right-clicking a node now opens the Type & Dynamics panel directly**,
  without the previous single-line intermediate menu (a pure extra click
  with no function).
- **Attack and Release added to the panel** (was only Threshold + Dyn
  Range, even though the v13 changelog entry described four knobs — the
  parameters already existed and were already used by the DSP/Find
  Resonances, they'd just never been wired into this particular popup).
  The panel is now four knobs in a row and slightly wider.
- **The spectrum curve is now built from a power/RMS average across each
  point's bin range** instead of the max. The earlier raggedness wasn't
  actually caused by the bin width itself but by the max jumping between
  different individual FFT bins frame to frame — EMA(1s) therefore
  didn't help much (time smoothing doesn't fix a frequency-axis problem).
  The RES/Find Resonances detector continues to read the max (its own
  `*Det` arrays), so sensitivity to narrow resonance peaks is unchanged;
  only the drawn curve (the `*Disp` arrays) became smoother.
- **The HP/LP menu now opens at the triangle's actual position** instead
  of an undefined default location (it used to appear in the bottom-left
  corner, far from the control).
- **Global dark LookAndFeel for PopupMenu/ComboBox/AlertWindow/
  TextButton**: previously only the rotary knobs were styled, so
  right-click menus, the preset selector's list, and the "Save
  preset..." dialog fell back to JUCE's light default look with an
  oversized font — noticeably jarring in a dark mixing environment. Set
  as the application's default LookAndFeel in the editor's constructor,
  so everything inherits the same theme automatically.

## v14 — Per-band filter types + larger fonts

- Every band can now be Bell (default), Low Shelf, High Shelf, or Notch.
  The type is chosen from a button row at the top of the right-click
  panel; the parameter ("bandN_type") is automatable and included in
  presets/A-B-C-D. Type changes are discrete (no ramp).
- Shelf Q is clamped to 0.3-1.2 in coefficient generation so the
  transition is always civilised. Notch has no gain: the node is locked
  to the 0 dB line (dragging = frequency only), the gain knob is greyed
  out (value shown as —), and dynamics are disabled.
- Find Resonances (then ANALYZE) always sets Bell on proposed bands. The
  hover readout shows the type name.
- Readability: the smallest font sizes raised 1-2 pt throughout (axes,
  legend, resonance labels, readouts, column headers, footer text,
  panel).

## v13 — Per-band Attack/Release + tailored times in Find Resonances

- Two new parameters per band: Dyn Attack (0.1-100 ms, default 5) and Dyn
  Release (20-1000 ms, default 150), logarithmic and automatable. Envelope
  coefficients are cached per band and only recalculated on change. The
  dynamics panel (right-click a node) now has four knobs; the hover
  readout shows the times.
- Find Resonances (then ANALYZE) tailors the times per resonance: attack
  ≈ 2 periods of the resonance frequency (lets the transient through,
  catches the ringing), release ≈ the resonator's ring time t60 ≈
  2.2·Q/f from the measured Q and frequency (clamped 60-600 ms) — the
  attenuation releases in step with the ringing dying out, without
  flutter or pumping.

## v12 — Consolidation: real-time safety + parameter smoothing

- The audio thread is now completely allocation-free: static bands'
  coefficients, the detectors' bandpass, and the HP/LP cascades
  (Butterworth Q tables replace FilterDesign) are all written via
  ArrayCoefficients directly into existing coefficient objects.
- Parameter smoothing ~20 ms per band and channel (Freq/Q
  multiplicatively, Gain in dB): changes are ramped via the sub-block
  path — no zipper noise on fast automation. Preset/state loading and
  A/B/C/D jump directly with no ramp (intentional).
- Code review: the Find Resonances (then ANALYZE) chips moved below the
  badge row (collided with the legend at narrow window widths).
  TESTING.md added with a pluginval, DAW, CPU, and listening-test
  checklist ahead of release.

## v11 — Delta listening

- Δ button in the header (parameter "delta", automatable): plays what
  was REMOVED, i.e. dry − wet, with a ~10 ms crossfade. Computed in the
  M/S domain before the monitor decode, so MONITOR M/S solos the delta's
  mid or side portion respectively. Output gain is applied afterward (the
  listening level is controlled as usual). DELTA badge in the graph when
  active.
- Note: the spectrum and detection show what you hear — i.e. the delta
  signal when Δ is active. Don't run Find Resonances with delta on.

## v10 — Detection steps 1-5 + the Find Resonances assistant

The detection chain was upgraded in five steps:
1. A dedicated RMS detector spectrum (power average ~400 ms, independent
   of the SPEED setting and the display's max-binning) — detection sees
   energy over time, not individual peaks.
2. ERB-scaled neighbourhood windows (critical bands): wide in the bass,
   relatively narrower higher up — "stands out from its context" matches
   the ear's auditory filters.
3. Frequency stability: a parabolic peak-offset jitter gate per point
   rejects wandering content (formants, vibrato) — only stationary peaks
   qualify.
4. Masking gate: candidates below the neighbourhood energy's masking
   threshold are ignored (spread ~10 dB/ERB upward, ~25 dB/ERB downward).
5. Harmonic discrimination: peaks with a strong sub-harmonic (f/2, f/3,
   f/4) are penalised as probable musical overtones.

The Find Resonances assistant (then named ANALYZE, clickable in the
legend): listens for 8s while music plays, gathers time statistics on
flagged resonances, measures bandwidth (→ Q, 2-12), severity (→ cut
2-9 dB, ~60% of the excess), and level (→ threshold). Proposes up to 4
dynamic bands (gain 0, negative range — only attenuates while the
resonance is ringing) drawn as proposals in the graph. APPLY writes to
free bands (never touches bands you've set yourself) and saves undo
state; UNDO restores. Mid/side resonances are routed to M/S mode
respectively.

## Structure

```
CMakeLists.txt              CMake + JUCE (FetchContent), targets VST3 (+ AU on macOS)
Source/
  PluginProcessor.h/.cpp    DSP: M/S encode/decode, 8 peak bands (juce::dsp::IIR),
                            APVTS parameters, peak/RMS meters, A/B snapshots, presets
  PluginEditor.h/.cpp       Editor: header (preset, A/B, meters, bypass),
                            8 band columns (M/MS/S, Freq/Gain/Q knobs, bypass)
  EQGraphComponent.h/.cpp   Frequency curve: separate Mid (green) and Side (orange)
                            curves, draggable nodes (drag = freq/gain)
  LookAndFeel.h             Colour theme, KnobLookAndFeel (rotary), LevelMeter
```

## Building

Requires CMake ≥ 3.22 and a C++17 compiler (MSVC/Xcode/Clang).
JUCE 8.0.4 is fetched automatically via FetchContent on the first
configure.

```
cmake -B build
cmake --build build --config Release
```

`COPY_PLUGIN_AFTER_BUILD TRUE` installs the plugin to the system plugin
folder. The AU target only builds on macOS.

If you have JUCE locally: replace the FetchContent block with
`add_subdirectory(path/to/JUCE)`.

## DSP design

- Encode: `mid = (L+R)*0.5`, `side = (L-R)*0.5`; decode: `L = mid+side`,
  `R = mid-side`.
- Per band: one mono `juce::dsp::IIR::Filter<float>` (peak/bell) for mid
  and one for side. The band's M/S mode (Mid / Mid+Side / Side)
  determines which are processed.
- Coefficients are only recalculated when Freq/Gain/Q change (cached in
  `updateFilters()`).
- All parameters (`bandN_freq/gain/q/mode/bypass`, `global_bypass`) live
  in `AudioProcessorValueTreeState` → DAW automation and full recall via
  get/setStateInformation.

## HP/LP and spectrum (v2)

- HP/LP: Butterworth with selectable slope 12/24/48 dB/oct (cascaded
  biquad sections via `FilterDesign`), routing Stereo/Mid/Side per
  filter. Controlled via the graph nodes: drag = frequency, right-click
  = menu (enable/slope/routing), double-click = on/off.
- Two spectrum taps (mid/side, pre-EQ), FFT 4096 + Hann, drawn as filled
  areas in mid/side colours with a +3 dB/oct display tilt (music looks
  flat). Interpolation between bins in the bass, max-binning in the
  treble, temporal smoothing.
- The y-axis zooms with the mouse wheel over the left edge (±6…±30 dB,
  visual only).
- The window is resizable (900×550-1800×1100); the size is saved in the
  plugin state.

## Post-EQ spectrum etc. (v4)

- The spectrum taps now sit AFTER the bands + HP/LP (before decode), so
  the background reacts immediately when the EQ is adjusted. Output gain
  isn't included in the analysis view (intentional).
- The STEREO spectrum (the power sum √(mid²+side²), purple-grey) is
  toggled in the legend, off by default.
- FREEZE in the legend freezes visible spectra as reference outlines.
- The frequency/note text and resonance labels sit on fixed rows near
  the top of the graph.
- FFT/spectrum/text rendering pauses during window resize (resumes 200
  ms after the last change); audio processing is never affected by
  resizing.
- Double-clicking any knob (Freq/Gain/Q/Output) opens text entry; "1.5k"
  = 1500 Hz.

## v9 — Hearing-weighted resonance detection

- Detection now uses a perceptual weighting curve (a rough inverted
  equal-loudness curve, ~80 phon): the presence region 2-5 kHz (+6 dB
  around 3.5 kHz) is prioritised up, the bass (-12 dB at 20 Hz) and the
  extreme treble (-6 dB at 20 kHz) down. The weighting affects both the
  qualification floor and the top-3 ranking, so markers land closer to
  what actually sounds bothersome.
- The neighbourhood level is now computed with a median instead of a
  mean — more robust when several resonances sit close together.

## v8 — Smoother spectrum

- Overlapping FFT: a ring buffer publishes a block every (fftSize/4)
  samples (75% overlap) → ~43 updates/s instead of ~11, with no loss of
  bass resolution. Published via a seqlock so the UI always reads the
  latest complete block (no dropped blocks).
- Frame-rate-independent smoothing: targets (from the FFT) are separated
  from displayed levels; upward attack interpolation and downward
  exponential decay are scaled by the measured frame time. The SPEED
  modes are now real time constants (release ~0.12/0.3/0.8 s).
- The graph renders at 60 fps; the resonance score is also dt-based so
  RES behaves the same regardless of frame rate.

## v7 — Dynamic EQ

- All 8 bands can be made dynamic: right-click a band node for Threshold
  (-60…0 dB) and Range (±18 dB, 0 = static band). Negative range lowers
  the band when the level in its frequency range exceeds the threshold
  (frequency-selective compression); positive range raises it.
- Detection per channel: a bandpass detector (same freq/Q as the band) +
  envelope follower (~5 ms attack, ~150 ms release), soft 12 dB knee.
- Coefficients update every 32-sample sub-block via `ArrayCoefficients`
  (allocation-free on the audio thread, only updates on a change > 0.05
  dB).
- Ghost nodes in the graph show the band's effective gain in real time
  (green = mid, orange = side) with a line to the static node — you see
  the compression working.
- The hover readout shows the DYN settings; the parameters are
  automatable and included in presets/A-B-C-D.

## v6

- Monitor selector in the header (MONITOR: ST / M / S): listen to the
  full stereo signal, mid only, or side only (mono in both ears).
  Click-free switching via a ~10 ms crossfade of the decode coefficients.
  Automatable parameter ("monitor"); an active solo is shown with a
  badge in the graph. The spectrum and resonance detection always show
  both channels.

## v5

- Resonance detection now shows the 3 strongest resonances per channel:
  mid (green, top label row) and side (orange, bottom row), with
  hysteresis so the list is stable. Runs on the post-EQ spectrum: tame a
  resonance and the next one takes its place.
- The IN/OUT meters are large vertical bars with a dB scale (0…-60) to
  the right of the graph.
- Global bypass has a clear on/off colour on the button + a "BYPASSED"
  badge in the graph.
- The A/B comparison expanded to four snapshots: A/B/C/D.
- User presets: "Save preset..." in the preset menu saves the current
  settings as XML in the user's appdata folder (MSEQ8/Presets); saved
  presets appear in the menu.

## Interaction & analysis (v3)

- Legend top-left: MID/SIDE toggles, RES (resonance detection), and
  SPEED FAST/MED/SLOW (the spectrum's attack/decay); RES and SPEED are
  saved in the plugin state.
- Resonance detection: points that persistently sit ~7 dB above their
  spectral neighbourhood are marked with a vertical line + frequency/
  note label.
- A crosshair with frequency + note (A4 = 440 Hz) + cents follows the
  mouse.
- Hovering a node: a readout with all of the band's values; the matching
  band column is highlighted.
- The mouse wheel over a band node adjusts Q (up = narrower).
- Output gain ±12 dB in the header (parameter `output_gain`, ramped,
  automatable).

## UI

- The graph shows the summed dB response per channel; nodes are coloured
  by M/S mode (green = Mid, purple-grey = Mid+Side, orange = Side).
  Dragging a node changes freq (log-x) and gain (y) with correct
  begin/endChangeGesture calls for host automation.
- A/B: two parameter snapshots (ValueTree copies) in the processor;
  switching saves the active slot and loads the other.
- Presets: hardcoded in `applyPreset()` — easy to switch to XML files
  later.

## Known simplifications (deliberate, for clarity)

- `makePeakFilter` in `updateFilters()` allocates; for production
  real-time safety, switch to `ArrayCoefficients` (JUCE ≥ 7.0.3) or
  compute coefficients into a lock-free buffer.
- Coefficient changes are unsmoothed — fast automation can cause small
  clicks; add a `SmoothedValue` per parameter if needed.
- The meters are peak with simple release smoothing; RMS is also
  exposed by the processor.
- The code hasn't been compiled here — expect possible minor
  adjustments depending on the exact JUCE version (written against JUCE
  8.x, the `FontOptions` API).
