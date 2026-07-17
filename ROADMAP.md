# MSEQ 8 — Roadmap

This is a directional roadmap, not a release schedule — dates aren't promised, especially
further out. It reflects where the project is heading and why, so users and potential
contributors know what to expect. Feedback and PRs on any of this are welcome — see
[Contributing](README.md#contributing) in the README, or open an issue directly on
[GitHub](https://github.com/JanKar76/MSEQ-8---Dynamic-MID-SIDE-EQ-with-psychoacoustically-informed-resonance-detection-/issues).

## Now

Groundwork before more user-facing features land, so growth doesn't sit on top of known
rough edges.

- ~~Parameter smoothing for static bands.~~ **Already in place** (since v12): static
  Freq/Q/Gain changes ramp via `SmoothedValue` over ~20 ms on the sub-block path, same as
  dynamic bands. README's "Known limitations" section referenced a stale claim here and has
  been corrected.
- ~~Allocation-free coefficient updates for static bands.~~ **Already in place** (since
  v12): all coefficient writes — static bands, dynamic bands, HP/LP cascades — go through
  `ArrayCoefficients` directly into the existing coefficient objects, no audio-thread
  allocation.
- **CI on GitHub.** A GitHub Actions workflow that builds VST3 (and AU on a macOS runner)
  and runs `pluginval` on every push/PR — regression protection, and a signal to anyone
  considering a contribution that the project is maintained.
- **Source comments translated to English.** The C++ source (identifiers were always
  English) had Swedish-language comments throughout from development — translated so the
  codebase is approachable for international contributors.

## Next

Planned, reasonably well-scoped, but not yet started.

- **macOS signing/notarization, verified for real.** AU support exists in the build config,
  but hasn't been verified on an actual, unmodified macOS install. Without notarization,
  Gatekeeper will likely block a freshly built binary outright — this needs to be resolved
  and tested before "Mac support" can be claimed with confidence.

## Recently shipped

- ~~**Match Gain button.**~~ **Done.** A `MATCH` button next to the output gain knob
  auto-sets output gain so a ~2 s running average of post-EQ loudness matches the plugin's
  input loudness, so A/B comparisons aren't skewed by a boost/cut. See
  `MSEQ8AudioProcessor::matchGain()`.
- ~~**Correlation / width meter.**~~ **Done.** A small horizontal broadband phase
  correlation strip (-1..+1) now sits under the IN/OUT level meters, computed directly from
  the decoded L/R signal each block.
- ~~**Sidechain input for dynamic bands.**~~ **Done.** An optional stereo sidechain input
  bus plus a per-band `SC` toggle (in the right-click dynamics panel) lets any band's
  detector listen to an external signal instead of its own audio, falling back silently to
  the band's own signal if the host hasn't connected the sidechain bus.

## Later

Directional — worth pursuing, but scope and timing are loose.

- **Linear-phase mode.** Optional latency-for-phase-accuracy tradeoff, aimed at mastering
  use where zero phase distortion matters more than latency.
- ~~**Genre-oriented factory presets.**~~ **Done.** 38 new factory presets across 13
  genres (Pop/Vocal, EDM/Electronic, Rock/Band, Hip-Hop/Trap, Acoustic/Singer-Songwriter,
  Podcast/Voice, Mastering/Bus, R&B/Soul, Jazz/Acoustic Ensemble, Classical/Orchestral,
  Reggae/Dub, Funk/Disco, Latin/Afrobeats, Cinematic/Film), browsed via a two-level
  genre → preset `PopupMenu` (replacing the old flat dropdown). See
  `MSEQ8AudioProcessor::getPresetList()`.
- **Searchable preset browser.** Text search/tags across the (now much longer) preset
  list, for finding a specific preset without opening every genre submenu.
- **CLAP format support.** Modern, open plugin format — a natural fit alongside the
  project's AGPL-3.0 stance.
- **Expanded/named snapshots.** More than four A/B/C/D slots, with names, for people who
  want to compare more than four ideas at once.

## Not planned

Listed deliberately, so it's clear these were considered and set aside rather than
overlooked.

- **AAX.** Requires Avid's proprietary SDK and PACE Anti-Piracy signing — a large, closed
  commitment that sits awkwardly next to an AGPL-3.0 project.
- **Linux VST3.** No reliable way to build and test this without a maintainer who actually
  runs Linux day to day. Open to revisiting if that changes.
