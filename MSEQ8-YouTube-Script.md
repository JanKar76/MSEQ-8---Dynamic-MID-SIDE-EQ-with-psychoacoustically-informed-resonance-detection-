# MSEQ 8 — YouTube Launch Video

**Working title:** "This Free EQ Plugin Finds Resonances For You (MSEQ 8)"
**Alt titles:** "I Built a Free Mid/Side EQ That Listens For You" / "MSEQ 8: Free Dynamic EQ with Automatic Resonance Detection"

**Target length:** ~9–11 minutes
**Tone:** Confident, practical, demo-driven. Talk over real audio (piano/drum loop/full mix), not silence. Show, don't just tell — every feature claim should be followed within seconds by you clicking it live.

**Thumbnail idea:** Split screen — left half a messy/muddy waveform with red "resonance" markers, right half the same after MSEQ8 cleaned it up. Bold text: "FREE" in the corner.

---

## 0. Pre-production notes

- Record in FL Studio (or your DAW of choice) with a real source: a full mix works best for the "wow" moments (RES/ANALYZE), a solo piano or vocal works best for showing precision.
- Keep DELTA on standby for every feature — "hear what's being removed" is your best selling point and costs you nothing to demonstrate.
- Zoom in on the graph when you drag nodes, right-click menus, and when RES/ANALYZE markers appear — viewers need to actually see the UI react.
- Mention **free and open source (AGPLv3, full source on GitHub)** at least twice: once early (removes the "what's the catch" hesitation) and once at the end (call to action).

---

## 1. Hook (0:00–0:20)

**On screen:** A mix playing with an obvious harsh resonance (a boxy low-mid or a shrill high-mid). Cut to MSEQ8 open, RES already flagging it with a marker.

**Script:**
> "This EQ just found the exact frequency that's making my mix sound harsh — I didn't touch a single knob. It's called MSEQ8, it's a free 8-band Mid/Side EQ with automatic resonance detection and a dynamic EQ assistant that listens to your track and builds the bands for you. And yeah — it's completely free and open source. Let's get into it."

---

## 2. What MSEQ8 is (0:20–1:00)

**On screen:** Full plugin window, calm zoom-out, cursor idle so viewers can read the layout.

**Script:**
> "MSEQ8 is an 8-band parametric EQ built around Mid/Side processing — every band can work on the mono center of your mix, the stereo sides, or both. Each band can also become a dynamic band, meaning it only cuts when a problem frequency actually gets loud. On top of that there's a resonance detector that runs constantly in the background, and an assistant that listens for 8 seconds and proposes the dynamic bands for you. It's free, it's open source under AGPLv3, and I'll drop the GitHub link below."

---

## 3. Interface tour (1:00–2:00)

**On screen:** Point out each area as you mention it — don't rush, this is orientation for new viewers.

**Script:**
> "Quick tour. This is the frequency graph — drag a node to set frequency and gain, scroll on a node to change Q. Right-click a band gives you type — bell, shelf, notch — and the dynamics panel. Down here are the eight band columns with knobs if you prefer dragging knobs to dragging nodes. Up top: monitor solo for mid or side, delta listening, output gain, global bypass. And these two triangles at the edges are your high-pass and low-pass filters."
>
> "Every band also has an M / M+S / S switch — that's the whole point of a Mid/Side EQ. You can cut mud out of just the sides without touching the center, or tighten up the mono bass without narrowing your stereo image."

---

## 4. Core EQ + Mid/Side basics (2:00–3:00)

**On screen:** Drag a band into a boomy low end. Switch it between Mid / Mid+Side / Side live so viewers hear the difference.

**Script:**
> "Let's do something practical. This mix has some low-end mud. I'll drop a band here around 250 Hz — set it to Mid only, since that's usually where bass buildup lives in the center. Listen to the difference with delta on — that's exactly what's being pulled out."
>
> "Now watch what happens if I switch the same band to Side instead — completely different result, because the stereo content up here is totally different from the mono content. That's the whole value of Mid/Side over a normal stereo EQ."

---

## 5. RES — automatic resonance detection (3:00–4:30)

**On screen:** Toggle RES on in the legend. Let it run over 5–10 seconds of playback so markers appear and settle. Zoom into the frequency labels.

**Script:**
> "Now the part that got your attention in the intro. This is RES — click it on, and it starts analyzing your mix in real time. It's not just finding loud peaks — it's using a psychoacoustic model: it checks whether a peak actually stands out from its neighboring frequencies by a masking threshold, it discards peaks that are just noise or jitter, and it discriminates harmonics so a single resonant note doesn't get flagged three times as separate problems."
>
> "See these markers with the frequency labels? Those are live, sustained resonances — mid in green, side in orange. If two are close together in frequency, they stack instead of overlapping so you can still read both."
>
> "This is purely informational right now — it's showing you where the problems are, you still fix them manually. But it already saves you from hunting around with a narrow bell and your ears for ten minutes."

---

## 6. ANALYZE / Find Resonances — the assistant (4:30–6:30)

**This is the centerpiece. Give it the most screen time.**

**On screen:** Click "FIND RESONANCES" in the legend. Let the full 8 seconds play with the countdown visible. Show the proposal chips appearing. Click APPLY. Immediately toggle DELTA so viewers hear exactly what got removed. Then click UNDO to prove it's non-destructive.

**Script:**
> "RES tells you where the problems are. This next feature actually fixes them for you. Click 'Find Resonances,' and it listens for 8 seconds while you play your track."
>
> [let it run, narrate briefly while it counts down]
> "It's measuring bandwidth to figure out the right Q, how far each peak sticks out to decide how much to cut, and the natural decay time of each resonance to set attack and release automatically."
>
> "And there we go — it's proposing bands, up to six of them, only on parts of the EQ I haven't already touched myself. I'll hit Apply."
>
> [bands appear, click DELTA]
> "Listen to delta — that's literally only the problem frequencies being pulled out, nothing else touched."
>
> [click UNDO]
> "And if I don't like it, one click undoes the entire thing — back to exactly where I was before."
>
> "These aren't static cuts either — they're dynamic bands, so they only engage when that frequency actually gets loud enough to be a problem. The rest of the time they're doing nothing."

---

## 7. Dynamic EQ per band, manually (6:30–7:30)

**On screen:** Right-click a band, open the dynamics panel, show Threshold / Range / Attack / Release live while adjusting one.

**Script:**
> "You don't need the assistant to use dynamic EQ — any band can do this manually. Right-click a band, and you get threshold, range, attack, and release. Set a threshold, and the band only cuts once the signal crosses it — like a compressor that only affects one frequency range instead of the whole signal."
>
> "You can see it react live in the graph — this ghost ring around the node shows the actual gain being applied in real time."

---

## 8. Independent Mid/Side HP/LP (7:30–8:30)

**On screen:** Right-click the HP triangle, show the "Independent Mid/Side" toggle, drag the two resulting nodes to different frequencies.

**Script:**
> "One more Mid/Side-specific feature: the high-pass and low-pass filters can run at completely different frequencies for mid and side. Right-click, turn on Independent Mid/Side, and now I get two nodes — green for mid, orange for side. I can high-pass the sides much more aggressively than the center, which is a really clean way to keep your low end mono and tight without touching the mid channel's bass at all."

---

## 9. Quality-of-life features, quickfire (8:30–9:30)

**On screen:** Quick cuts, each feature gets 10–15 seconds max.

**Script:**
> "A few more things worth knowing about. Hold Ctrl and hover any band — it solos just that frequency range so you can hear exactly what a band is sitting on before you touch anything."
>
> "If your side channel gets too strong in the bass, MSEQ8 warns you — that's a common cause of phase cancellation in mono playback, like phone speakers or club systems."
>
> "A, B, C, D — four full snapshots you can flip between instantly to compare settings."
>
> "And Ctrl+Z, Ctrl+Y — five steps of full undo/redo, covering literally everything, including Analyze and preset changes."

---

## 10. Real-world example — before/after (9:30–10:30)

**On screen:** Play the same 15–20 second clip fully unprocessed, then fully processed with everything you set up during the video. No talking during either playback — let it speak for itself. Optional: show a spectrum-analyzer-style split screen if you have one.

**Script (before playback):**
> "Let's hear the whole thing, start to finish — unprocessed first, then with everything we just set up."

**Script (after playback):**
> "That's RES, Find Resonances, manual dynamic EQ, and independent Mid/Side high-pass, all in one free plugin."

---

## 11. Outro / CTA (10:30–11:00)

**On screen:** Plugin window one last time, then cut to a slide with GitHub link + license.

**Script:**
> "MSEQ8 is completely free, open source under AGPLv3 — full source code is on GitHub, link in the description. VST3 and AU, Windows and... [confirm Mac support before saying this] Grab it, try it on a mix you know well, and let me know in the comments what you'd want added next. If this was useful, subscribe — I'm building this in the open and there's more coming."

---

## Notes for you before recording

- **Confirm before scripting further:** Mac/AU support and testing status — the outro currently has a placeholder because I don't have confirmation you've tested on macOS. Don't claim a platform you haven't verified.
- Consider replacing the piano/mix example with whatever source material best shows off RES/ANALYZE dramatically — a busy full mix with a known problem frequency will "sell" this feature far better than a clean single instrument.
- If you want a shorter cut for socials (60–90s), section 6 (ANALYZE) alone is the strongest standalone clip.
