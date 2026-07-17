# MSEQ 8 — 8-band Mid/Side parametrisk EQ (JUCE, VST3/AU)

## v19 — Frekvensberoende Q-tak

- **Nytt: det effektiva Q-taket skalar nu med frekvens istället för ett platt
  tak på 10.** Bandbredd ≈ frekvens/Q, så ett Q på 10 skär redan en försumbart
  smal notch på subbas, medan samma Q knappt gör avtryck på en resonans i
  mellan-/högregistret — att bara höja taket rakt av till 40 hade sprängt
  dynamikmotorns attack-/release-/knäformler (som delar samma Q) vid låga
  frekvenser utan att ge något musikaliskt värde där. Istället rampar
  `MSEQ8AudioProcessor::maxQForFreq()` det effektiva taket från 10 under
  150 Hz till 40 över 500 Hz (linjär rampning däremellan), tillämpat tyst
  överallt Q faktiskt används i signalbehandlingen: filterkoefficienter,
  dynamikdetektorn/knät, Ctrl+hover-avlyssning och Find Resonances egna
  föreslagna Q-värden. Den råa `Q`-parameterns intervall är nu 0.1–40
  (knoppens skew oförändrad) så det extra utrymmet nås genom att skriva in
  ett exakt värde eller via presets/automation; grafens scroll-hjuls-steg
  för Q respekterar samma frekvensberoende tak per nod.
- Lyssningstestat: mindre musikaliskt material filtreras bort som
  sidoeffekt när högre Q används för kirurgisk borttagning i
  mellan-/högregistret.
- **Fix: grafens Bell-/Notch-kurvor slutade bli visuellt smalare över
  Q=10 (Notch: Q=25), trots att själva filtret fortsatte smalna korrekt.**
  `EQGraphComponent`s `bandMagnitudeDb()` approximerar bandformen med
  `x = oktavavstånd / max(golv, bandbredd * 0.5)`; golvet (0.05 för Bell,
  0.02 för Notch) var harmlöst så länge Q hade ett hårt tak på platta 10
  överallt, men när det nya frekvensberoende taket tillät Q upp till 40
  började golvet träffas för varje Q över 10/25 och fryste tyst den
  ritade kurvans bredd från den punkten — formen på skärmen slutade följa
  ratten trots att ljudet fortsatte ändras under. Sänkte båda golven
  (0.005 / 0.002) så de hamnar under den smalaste bandbredd taket nu kan
  ge (0.0125 vid Q=40), så kurvan följer Q hela vägen upp till taket igen.

## v18 — Genre-grupperade fabrikspresets, header-/rutnätspolish

- **Ny: 38 nya fabrikspresets fördelade på 14 genrer**, utöver de ursprungliga 3
  (Vocal Clarity, Wide Master, Low-End Focus — oförändrade). Genrer: Pop/Vocal,
  EDM/Electronic, Rock/Band, Hip-Hop/Trap, Acoustic/Singer-Songwriter, Podcast/Voice,
  Mastering/Bus, R&B/Soul, Jazz/Acoustic Ensemble, Classical/Orchestral, Reggae/Dub,
  Funk/Disco, Latin/Afrobeats och Cinematic/Film. Varje preset är en genomtänkt
  uppsättning band-gain/Q/mode/type och dynamikmotor-deltan från neutralt
  standardläge (endast de tre ursprungliga fabriksfrekvenserna/-slottarna rörs
  någonsin, aldrig `freqID`). Se `MSEQ8AudioProcessor::getPresetList()` /
  `applyPreset()`.
- **Ny: preset-bläddrare i två nivåer.** Den platta preset-`ComboBox`en ersätts av
  en `TextButton` som öppnar en `PopupMenu` med en undermeny per genre (byggd från
  `getPresetList()`s ordning), "Default" på toppnivå, sedan "User Presets" och
  "Save preset..." längst ner — skalar bra nu när antalet presets vuxit från 4
  till 39 (+ användarpresets).
- **Ny: "Dialogue-Safe Mix" (Cinematic/Film) slår på sidechain som standard** på
  band4:s dynamikdetektor (`SC`-togglingen), tänkt att kombineras med en
  dialogstam kopplad till pluginets sidechain-input för att "duckа" musik under
  dialog; faller tillbaka till självdetektering om inget är inkopplat, enligt
  befintlig sidechain-fallback.
- **Fix: `CorrelationMeter` kompilerade inte med `setTooltip()`.** Den är en ren
  `juce::Component` och behöver `juce::SettableTooltipClient` inblandad, precis
  som `Button`/`TextButton` redan får gratis.
- **Ny: adaptivt dB-rutnätssteg.** EQ-grafens horisontella rutnätslinjer använder
  nu 2 dB-avstånd vid hård inzoomning (`displayMaxDb <= 9`), 3 dB vid medelzoom
  (`<= 15`) och 6 dB vid ursprunglig vid zoom — ersätter ett fast 6 dB-avstånd som
  var för grovt vid inzoomning, och som tyst slutade rita linjer bortom ±12 dB
  även när man zoomat ut längre än så.
- **Ändring: headerns kontrollkluster (presets/A-B-C-D/monitor/delta/gain/match/
  bypass) ankrat direkt efter "8-BAND MID/SIDE EQUALIZER"-undertexten**, uppmätt
  via `GlyphArrangement::getStringWidthInt` i stället för en gissad fast offset,
  för tightare användning av smala skärmar.

## v17 — Match Gain, korrelationsmeter, sidechain-input för dynamikbanden

- **Ny: Match Gain-knapp (MATCH, i headern bredvid GAIN-ratten).** Håller ett
  ~2 sekunders löpande medelvärde av ingångens effekt (`avgInPower`, mätt
  före all bearbetning) och av utgångens effekt precis före
  output-gain-steget (`avgOutPower`, mäts direkt på det avkodade L/R-
  paret). Ett klick sätter `output_gain` till `jlimit(-12, 12, inDb - outDb)`
  så att A/B-jämförelser inte förvrängs av att en boost eller cut i sig
  låter högre/lägre. Gör ingenting om det inte funnits tillräckligt med
  signal ännu (nära tystnad på endera sidan).
- **Ny: bredbandig fasrelationsmeter (CORR, under IN/OUT-metrarna).** Räknar
  ut en klassisk korrelationskoefficient (`Σ(L·R) / √(Σ(L²)·Σ(R²))`) direkt
  på det avkodade L/R-paret varje block, EMA-utjämnad över ~5 block för en
  stabil avläsning. -1 = helt urfas (mono-inkompatibelt), 0 = brett/
  okorrelerat, +1 = mono-kompatibelt. Billig förlängning av samma
  ljuddata mono-varningen redan tittar på, precis som föreslaget i
  roadmappen.
- **Ny: externt sidechain-input för dynamikbanden.** En valfri stereo
  sidechain-buss (`.withInput("Sidechain", ..., false)`, avstängd som
  standard tills värden kopplar in den) plus en `SC`-togglingsknapp per band
  i högerklicks-dynamikpanelen. När påslagen och värden faktiskt levererar
  sidechain-ljud läser bandets detektor det externa L/R-paret (kodat till
  mid/side på samma sätt som huvudingången) i stället för bandets egen
  bearbetade signal — själva gain-reduktionen appliceras fortfarande alltid
  på bandets eget ljud. Faller tyst tillbaka till bandets egen signal om
  värden inte kopplat in sidechain-bussen. `isBusesLayoutSupported()`
  kräver att sidechain-bussen antingen är avstängd eller stereo.
- **Fix: `output_gain`-ramp begränsad till kanal 0/1 explicit.** Med
  sidechain-bussen påslagen kan den delade `buffer` som `processBlock()` får
  nu innehålla fler än 2 kanaler; `applyGainRamp` utan kanalargument
  påverkar ALLA kanaler i buffern, vilket i onödan skulle ha rört
  sidechain-kanalerna också. Bytt till två explicita per-kanal-anrop
  (kanal 0 och 1).
- **Minsta fönsterbredd höjd 1020 -> 1140.** MATCH-knappen breddade
  headerns högerkluster med ~54px; eftersom det klustret är ankrat mot
  fönstrets högerkant medan preset/A-B-C-D-blocket skalar från mitten,
  gav en rak +54 till golvet noll marginal mellan dem vid minsta storlek.
  1140 återställer den ursprungliga ~24px-marginalen.

## v16 — GitHub-städning: CI, engelska källkodskommentarer, dokumentationsfix

- **Ny: GitHub Actions CI (`.github/workflows/build.yml`).** Bygger VST3 på
  Windows och VST3+AU på macOS vid varje push/PR, cachar JUCE:s
  `FetchContent`-nedladdning, kör `pluginval --strictness-level 5` på det
  byggda pluginet och (best-effort, `continue-on-error`) Apples `auval` för
  AU-formatet. Ger regressionsskydd och signalerar till potentiella bidrag-
  sgivare att projektet underhålls aktivt.
- **Fix: README:s "Known limitations" var inaktuell.** Avsnittet påstod att
  statiska band saknade parameterutjämning och allokerade på ljudtråden vid
  koefficientuppdatering - båda redan åtgärdade sedan v12 (se
  `SmoothedValue`-baserad utjämning och `ArrayCoefficients`-uppdateringen
  längre ner i denna logg). Ren dokumentationsfix, ingen kodändring krävdes.
- **Alla kommentarer i `Source/*.cpp` och `Source/*.h` översatta till
  engelska.** Källkodens identifierare (variabel-/funktions-/klassnamn) och
  alla UI-strängar var redan engelska sedan tidigare; det var bara de
  inline-kommentarer som skrevs under utvecklingen som var på svenska.
  Ingen logik eller kod ändrad - ren kommentaröversättning, gjord för att
  göra koden tillgänglig för internationella bidragsgivare nu när repot är
  publikt på GitHub.

## v15 (forts.) — Dynamikpanelens knobbar & placering

- **Fix: BandDynPanel:s (högerklick på en nod) Threshold/Dyn Range/Attack/
  Release-knobbar var synbart mindre än FREQ/GAIN/Q-knobbarna längst ner.**
  Orsak: de använde `TextBoxBelow`, vilket gör att JUCE:s inbyggda
  slider-layout stjäl höjd från själva vridreglaget innan det ritas -
  medan bandkolumnernas knobbar använder `NoTextBox` (värdet ritas för
  hand i `paint()`) och därför får hela sin tilldelade yta. Bytte
  BandDynPanel till exakt samma mönster: `NoTextBox` + fast knobbhöjd
  42px (identisk med BandColumn) + värdena ritade manuellt under
  etiketterna. Panelen är nu tajtare (280×152 i stället för 280×170).
- **Fix: panelen täckte bell-/shelf-kurvan för noden man just justerade.**
  `CallOutBox` ritar sig aldrig ovanpå sin `targetArea`, bara utanför den
  (vänster/höger/topp/botten - den sida med mest plats). Tidigare var
  `targetArea` bara en 8×8-punkt vid klicket, så panelen hamnade ändå
  precis intill/över kurvformen. Nu spänner `targetArea` hela plotytans
  höjd och ±140px kring nodens x-position, vilket tvingar `CallOutBox`
  ut till vänster eller höger om hela det området - kurvan syns nu
  live medan man drar i dynamik-parametrarna.

## v15 (forts.) — Ångra/gör om (5 steg) + Applied-meddelande döljs vid klick

- **Ny funktion: Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z för gör om) ångrar/gör om upp
  till 5 steg.** Bygger på hela APVTS-tillstånds-snapshots (samma mönster
  som A/B/C/D-slotarna och ANALYZE:s Apply/Undo redan använder), inte
  finkornig per-parameter-spårning. En `AudioProcessorValueTreeState::
  Listener` registreras för ALLA parametrar (även framtida tillagda, ingen
  handplockad lista); `parameterChanged()` gör bara en atomär inkrementering
  av en räknare - trådsäkert oavsett om anropet kommer från ljud-tråden
  (host-automation) eller meddelande-tråden (GUI). Allt tungt arbete
  (ValueTree-kopiering, `replaceState`) sker i stället i editorns
  `timerCallback()` (garanterat meddelande-tråden), som debouncar ~400 ms
  utan nya ändringar innan den committar ett snapshot - så en kontinuerlig
  knob-/nod-drag blir ETT ångra-steg, inte ett per millimeter musrörelse.
  Preset-byten, A/B/C/D-växlingar och ANALYZE Apply/Undo täcks automatiskt
  in också, helt gratis, eftersom de redan går via samma parameterändrings-
  mekanism.
- **"Applied — click UNDO"-affordancen döljs nu direkt vid klick** någon
  annanstans i pluginet, inte bara efter 10s-timeouten. Klicket fortsätter
  sedan göra sin vanliga sak (nod, legend, etc.) precis som förut.

## v15 (forts.) — Oberoende HP/LP-filter för mid och side

- **Ny funktion: HP och LP kan nu köras med OLIKA frekvens (och slope) för
  mid och side samtidigt**, i stället för bara en delad frekvens routad till
  Stereo/Mid/Side. Aktiveras via "Independent Mid/Side" i HP/LP-högerklicks-
  menyn. Av (default): exakt samma beteende som innan denna funktion fanns -
  en delad hp_freq/hp_slope, routad via hp_mode. På: hp_freq/hp_slope styr
  MID, nya hp_side_freq/hp_side_slope styr SIDE, båda alltid aktiva samtidigt
  (routing-menyn döljs då - den är meningslös när båda kanalerna ändå körs).
  Samma mönster för LP (lp_independent/lp_side_freq/lp_side_slope).
- **Nya, additiva APVTS-parametrar** (hp_independent, hp_side_freq,
  hp_side_slope, lp_independent, lp_side_freq, lp_side_slope) - bakåt-
  kompatibelt med gamla projekt/presets/automationslane, som bara känner
  till de befintliga hp_freq/hp_slope/hp_mode-parametrarna och därför faller
  tillbaka på identiskt beteende som innan.
- **DSP:** hpMid/hpSide (och lpMid/lpSide) var redan separata filter per
  kanal internt, bara konfigurerade med samma värden - så independent-läget
  krävde ingen ny filterinfrastruktur, bara att de två kanalerna nu kan få
  olika koefficienter (och därmed potentiellt olika antal kaskaderade
  sektioner vid olika slope, därav separata hpSections/hpSideSections).
- **UI:** två nya, villkorliga grafnoder (hpSideNode/lpSideNode) som bara
  existerar/går att klicka/dra när respektive independent-läge är på -
  färgade grönt (mid) resp. orange (side) för att matcha konventionen i
  resten av grafen, förskjutna en rad nedanför mid-triangeln så de går att
  skilja åt även när frekvenserna råkar vara desamma.

## v15 (forts.) — Tre nya funktioner + RES-etiketter/textfixar

- **Ctrl+hover bandaudition.** Håll Ctrl nedtryckt och hovra en bandnod ->
  bandets ungefärliga frekvensområde (bandpass kring dess freq/Q) isoleras i
  den slutliga L/R-signalen så man kan höra bara det bandet, plus två tunna
  vertikala linjer i grafen vid bandets ungefärliga -3dB-kanter. Påverkar
  bara monitorering, inte den faktiska EQ-kurvan. Ingen APVTS-parameter (ska
  inte automatiseras/sparas) - styrs av en atomär `auditionBand` som UI:t
  sätter varje timer-tick, med ~15 ms crossfade i DSP:n för klickfri av/på
  och en säkerhetsnollning i editorns destruktor om fönstret stängs mitt i.
- **Mono-kompatibilitetsvarning.** Om side-signalen i snitt är klart
  starkare än mid under 500 Hz en stund, visas texten "SIDE SIGNAL HIGH IN
  LOW FREQUENCIES" i grafens övre vänstra hörn (under ev. hårkors-readout,
  så de aldrig krockar). Återanvänder detDbMid/Side (samma data som RES),
  med hysteres (3 dB på / 1 dB av) och ~1,5 s persistens för att inte
  flimra på korta transienter.
- **RES-etiketter staplas** i stället för att skriva över varandra när flera
  resonanser ligger nära i frekvens: sorteras efter x-position, och en
  etikett som skulle krocka med föregående på samma rad flyttas till en ny
  rad under (markören/linjen ligger kvar på rätt frekvens, bara texten
  flyttas).
- **Legend-texten "ANALYZE"** bytt till **"FIND RESONANCES"** (versaler,
  matchar MID/SIDE/RES/ST-stilen).
- **Mojibake-fix:** "Applied — click UNDO" hade en trasig UTF-8-byte-escape
  för tankstrecket som gav skräptecken i UI:t. Bytt till vanligt bindestreck.

## v15 (forts.) — Fokus-baserad avlastning (3+ instanser gjorde FL Studio seg)

- **Med 3+ MSEQ 8-fönster öppna samtidigt blev FL Studio seg/oresponsiv, trots
  att 2 fönster fungerade fint efter tidigare fixar.** 60→30 fps-sänkningen
  räckte inte ensam. Varje öppet fönster kör sin FFT/utjämnings/resonans-kedja
  i full takt HELA TIDEN, oavsett om fönstret faktiskt syns/är aktivt just då
  — med flera samtidigt öppna, ofta i samma UI-tråd hos värden, adderas
  kostnaden rakt av tills tråden inte hinner med.
  **Fix:** ett plugin-fönster som saknar OS-fokus (`peer->isFocused()`) kör nu
  FFT/utjämning/resonansdetektering/repaint i reducerad takt (var 4:e tick,
  ~7,5 Hz i stället för 30 Hz) — tillräckligt för att grafen inte ska se
  fryst ut, men långt billigare i bakgrunden. UNDANTAG: en pågående ANALYZE-
  lyssning kör alltid i full takt oavsett fokus, så mätdatan aldrig blir
  ofullständig bara för att användaren klickat över till ett annat fönster.
- **Följdbugg: bakgrundsfönster fick hackig/stegande spektrumrendering.**
  `dt` (tid sedan senaste uppdatering, används av all exponentiell
  utjämning/detektering) sattes från `lastFrameMs` INNAN throttle-hoppet,
  så den alltid blev ~33 ms — trots att i bakgrundsläget (var 4:e tick)
  hade det egentligen gått ~133 ms sen senaste tunga uppdateringen. Kurvan
  hann därför bara röra sig en fjärdedel av vägen mot målet varje gång den
  väl uppdaterades. **Fix:** `dt`/`lastFrameMs` sätts nu efter throttle-
  hoppet, så den alltid speglar tiden sen senaste FAKTISKA uppdateringen.

## v15 (forts.) — ANALYZE gav falskt "NO SIGNAL" utan RES påslagen + CPU-sänkning

- **ANALYZE kunde ge "NO SIGNAL" trots riktigt ljud (DELTA av, IN-mätaren rörde
  på sig, RES hittade resonanser separat).** Rotorsak i `updateResonances()`:
  hela detektorkedjan (`updateDetector()` + `detectChannelResonances()`) hade
  en tidig retur om RES-visualiseringstoggeln (`resEnabled`) var avslagen —
  och den återställs till av vid varje ny editor-instans (sparas inte i
  presetet). Utan RES påslagen fylldes aldrig `detLevMid/Side` (ANALYZE:s
  signalcheck) eller `flagTimeMid/Side` (underlaget till `buildProposals()`),
  så ANALYZE fungerade bara av en slump om användaren råkat ha RES på.
  **Fix:** detektorkedjan körs nu alltid, oavsett RES-toggeln. Själva
  ritningen (`drawResonances`) har sin egen separata `resEnabled`-koll och
  påverkas inte — RES-visualiseringen är fortfarande av/på som förut.
- **Sänkt uppdateringstakt 60 → 30 fps** i grafkomponentens timer. Allt är
  redan dt-baserat (exponentiell smoothing), så resultatet är identiskt vid
  lägre takt — bara redraw/FFT-pollingen blir glesare. Sänker samtidigt den
  faktiska FFT-frekvensen (från produktionstaktens ~43/s till pollningens
  30/s), vilket minskar CPU-lasten per öppet fönster, särskilt märkbart med
  flera instanser öppna samtidigt.
- **Bytt legend-texten "ANALYZE" till "Find Resonances"** i grafens legend,
  för att tydligare signalera vad funktionen faktiskt gör.

## v15 (forts.) — Extrem CPU-belastning med flera instanser öppna samtidigt

- **Kritisk bugg: två öppna MSEQ 8-instanser i samma DAW (rapporterat i FL Studio)
  gav nästan 100% CPU och en helt orespons­iv editor, trots att en enda instans
  bara låg på ~3%.** Rotorsak: `juce::LookAndFeel::setDefaultLookAndFeel(&knobLnf)`
  i `PluginEditor`-konstruktorn sätter en **process-global** pekare, delad av
  ALLA plugin-instanser i samma värdprocess (inte en per-instans-inställning).
  Varje editor skrev om samma globala pekare till sin *egen* `knobLnf`-instans
  vid konstruktion och nollade den ovillkorligen vid destruktion — så fort
  instans A stängdes (eller bara skapades efter B), kunde instans B:s
  fortfarande öppna fönster stå kvar utan giltig LookAndFeel alls, vilket i sin
  tur gav kraftig extra belastning/upprepad ombyggnad i varje paint-anrop för
  alla PopupMenu/ComboBox/AlertWindow/CallOutBox som förlitade sig på den.
  **Fix:** bytt till instansbunden `setLookAndFeel()` på editorn själv (ärvs
  korrekt av barnkomponenter, ingen global state kvar). De fristående
  toppnivåfönstren som inte ärver via komponentträdet (PopupMenu i HP/LP-menyn,
  CallOutBox + dess BandDynPanel-innehåll, AlertWindow i "Save preset...") får
  nu var sin egen explicita `setLookAndFeel()`-koppling i stället för att
  förlita sig på en delad global. Detta var den enda delade/globala
  mutable-state:n i hela kodbasen — allt annat (FFT, detektering, DSP-cachar)
  är redan korrekt instansbundet per plugin-instans.

## v15 (forts.) — ANALYZE-band helt tysta i dynamikmotorn (rotorsak hittad)

- **ANALYZE-applicerade band gav inget hörbart resultat alls (bekräftat med DELTA:
  inget togs bort).** Manuellt skapade "gain 0, bara dynamik"-band fungerade,
  vilket pekade bort från själva dynamikmotorn och mot ANALYZE:s apply-väg
  specifikt. Rotorsak hittad i `updateFilters()`:s typbytesgren — `applyProposals()`
  sätter alltid bandtypen till Bell samtidigt som frekvensen ändras; om bandet
  råkade ha en *annan* typ sen tidigare (t.ex. testat Notch/Shelf tidigare under
  sessionen) triggade det den diskreta typbytesgrenen, som skrev `lastFreq[i]`
  till det nya målet **utan** att någonsin anropa `setTargetValue()` på
  frekvens-smoothern. Resultat: smoothern rampade aldrig, `isSmoothing()` blev
  aldrig sann, och detektorns bandpassfilter (som bara syncades när
  `dynamic && smoothing` var sanna samtidigt) frös permanent på bandets gamla
  frekvens — detektorn mätte fel del av spektrumet och triggade därför aldrig.
  **Fix (två delar, ska fungera oavsett vad användaren gjort med bandet innan):**
  1. Typbytesgrenen snap:ar nu smoothern direkt (`setCurrentAndTargetValue`) och
     uppdaterar *både* filter- och detektorkoefficienter omedelbart, i stället för
     att bara skriva cache-variabler och lämna smoothern/detektorn orörda.
  2. Detektoruppdateringen i `processBandChannel()` kräver inte längre att en
     freq/Q-ramp råkar pågå samtidigt som bandet är dynamiskt (`dynamic && smoothing`
     → `dynamic`) — och ett nytt `lastRange[]`-cache i `updateFilters()` tvingar
     fram en omedelbar koefficient-refresh (filter + detektor) så fort ett band
     går från statiskt till dynamiskt eller tvärtom, så det första blocket efter
     en apply/aktivering alltid synkar detektorn korrekt (annars kunde det första
     blocket sakna både en pågående ramp och en redan förändrad effektiv gain,
     och uppdateringsvillkoret triggade aldrig).
- **ANALYZE-chippens statusmeddelande ("N suggestions ready" m.fl.) krockade
  visuellt med APPLY/DISCARD/UNDO-knapparna.** Meddelandet ritades högerjusterat
  ända ut till kanten oavsett vilka knappar som var synliga. Nu beräknas
  högerkanten utifrån `analyzeState` (stannar vid APPLY:s vänsterkant i
  proposal-läge, UNDO:s vänsterkant i applied-läge) så de aldrig överlappar.

## v15 (forts.) — Dynamikpanelens etiketter

- **THRESHOLD/DYN RANGE/ATTACK/RELEASE-texten hamnade bakom vridreglagens båge,
  ej läsbar.** `paint()` ritade etiketterna på en fast y-position (76px) som aldrig
  hade uppdaterats när panelen växte från 2 till 4 knobbar — `resized()` reserverade
  ingen egen plats för raden, så etiketterna hamnade mitt inuti knobbarnas rityta i
  stället för ovanför. Fixat: `resized()` reserverar nu en egen 14px-rad för
  etiketterna innan knobbarna läggs ut, och `paint()` räknar ut samma y-position
  (8 inset + 26 titel + 24 typerad + 6 mellanrum = 64px) i stället för ett löst
  hårdkodat tal. Panelhöjden höjd 150 → 170px så knobbarna får plats bekvämt under
  den nya raden.

## v15 (forts.) — Byggverifiering

- **pluginval `--strictness-level 10`: SUCCESS.** Första fullständiga byggverifieringen
  av hela v15-omgången (FFT-kalibrering, ANALYZE-fixar, Q-mätning/dedup, GUI-städ).
  Alla tester gröna vid 44.1/48/96 kHz och buffertstorlek 64–1024: audio processing,
  automation (inkl. sub-block), state save/restore, editor-automation, trådsäkerhet
  för parametrar och bakgrundstråd, bus-hantering, samt fuzz-parametertestet. Kvar
  enligt `TESTING.md`: manuell DAW-matris (FL Studio/Reaper) och lyssningstesterna.

## v15 (forts.) — GUI-kvalitetsgenomgång

- **`Colours::white` borttaget.** Nodringens outline och bandnumret i `drawNodes()`
  använde ren vit (`Colours::white.withAlpha(...)`) — enda stället i hela UI:t som inte
  gick via `Theme::`. Bytt till `Theme::text` (samma ljust gråbeige som all annan text),
  ren vit/svart undviks nu konsekvent genomgående.
- **Inbäddat typsnitt (Montserrat + Bebas Neue) provades och reverterades.** Lades till
  via `juce_add_binary_data`, men krävde att typsnittsfilerna laddades ner manuellt och
  lades i `Assets/Fonts/` innan byggning — sandboxen kunde inte hämta dem åt sig själv,
  och det extra byggsteget (`juceaide`) kraschade med ett oläsligt fel
  (`MSB8066`/"Unhandled exception") första gången filerna saknades. Efter avvägning
  (extra byggkomplexitet och en manuell nedladdningsdelning mot marginell branding-vinst)
  valde Jan att gå tillbaka till JUCE:s systemsans (samma som innan denna ändring) —
  enklare, inga byggberoenden, inga licensfrågor. `CMakeLists.txt`, `LookAndFeel.h` och
  logotyp-texten i `PluginEditor.cpp` återställda; `Assets/Fonts/` borttagen.

## v15 — FFT-kalibrering, ANALYZE-fixar och UI-städ

- **Q-mätningen i ANALYZE frekvensutjämnas nu innan -3dB-bandbredden mäts** (en lätt
  grannpunkts-medelvärdesbildning, används bara för bandbreddsvandringen — själva
  peak-detekteringen är orörd). Tidigare kunde en enda brusig punkt göra en egentligen
  måttligt bred resonans falskt smal, vilket gav ett artificiellt högt Q som nästan alltid
  klamrade fast i taket (10) oavsett resonansens verkliga karaktär.
- **Dedup mellan föreslagna band bygger nu på faktisk -3dB-överlapp i stället för ett fast
  punktavstånd.** Två smala, distinkta resonanser nära varandra i frekvens kan nu bli två
  separata smala förslag i stället för att den svagare kasseras — flera smala filter är
  ofta bättre än en bred reduktion som tar med sig ljud man vill behålla mellan topparna.

- **Fast EMA(1s) borttagen — den överröstade av misstag SPEED-kontrollen** (SPEED-svaret
  försvann nästan helt eftersom den efterföljande fasta 1000 ms-utjämningen alltid blev
  flaskhalsen). Ersatt med fraktions-oktav-liknande frekvensutjämning: ett triangelviktat
  fönster över grannpunkter i frekvensled, samma etablerade metod som REW/SMAART m.fl.
  använder. Löser kantigheten på rätt axel utan att röra SPEED:s tidssvar.
- **`analyzePeakLevel` (styr "NO SIGNAL"-meddelandet) läser nu `detLevMid/Side`**
  (max-baserad) i stället för den nya medelvärdes-displayen — tröskeln (0.02) var
  kalibrerad mot de gamla max-nivåerna och gav felaktigt "NO SIGNAL" trots tydlig signal.
- **Attack, knä-bredd och tröskelmarginal görs nu Q-beroende — tre justeringar för att
  matcha örats faktiska uppfattning av smala kontra breda resonanser:**
  - **Attack** i ANALYZE:s förslag tar nu det största av "två perioder" (som förut) och
    en Q/f-term (τ ≈ Q/(π·f), detektorns bandpassfilters egen tidskonstant) — samma
    fysik som release redan byggde på. Smala/hög-Q-resonanser får längre attack så
    envelopen hinner bli en meningsfull mätning innan gainet rör sig.
  - **Knä-bredden** i dynamikmotorn (var fast 12 dB, gäller *alla* dynamiska band, inte
    bara ANALYZE) är nu Q-beroende: ~7 dB (brant, beslutsamt) vid hög Q, upp mot 18 dB
    (snällt) vid låg Q — smala toner sticker ut perceptuellt redan vid litet överskott
    (svagare maskering inom kritiskt band än för bredbandig energi), breda resonanser
    mår bättre av att inte hyvlas för hårt.
  - **Tröskelmarginalen** i ANALYZE:s förslag (var fast -2 dB) är nu Q-beroende:
    tightare (~2 dB) vid hög Q för konsekvent dämpning av tonen, lösare (~4-5 dB) vid
    låg Q för att undvika att choka bredbandigt, musikaliskt material.

- **FFT-magnituden kalibrerad till verklig dBFS-referens** (`processTap()`): den var tidigare
  helt onormaliserad (Hann-fönster + 4096-punkters transform utan kompensation), vilket läste
  allt material tiotals dB för högt. Det gav två separata buggar med samma rotorsak: spektrats
  display slog i taket vid normal lyssningsnivå, och ANALYZE:s Threshold-förslag hamnade på en
  skala den riktiga dynamikmotorns envelope-detektor aldrig når — så Apply skrev rätt
  parametrar men gav i praktiken ingen hörbar cut. En enda normalisering vid källan
  (`kFftMagRef = fftSize / 4`) fixar båda, ingen separat auto-gain-lösning behövdes.
- **ANALYZE:s Q-förslag klampat till 2–10** (var 2–12) så det matchar bandens riktiga
  Q-gräns — chippen och de applicerade värdena stämmer nu överens.
- **RES-flaggornas linjer går nu till 0 dB-linjen** i stället för en fast 8 px-stump vid
  grafens topp, så de visuellt hänger ihop med EQ-kurvans nolläge. Varje markör har nu
  också en egen frekvensetikett direkt till höger om sig (grön = Mid, orange = Side) i
  stället för en samlad "RES ..."-rad i hörnet.
- **Crosshair-readouten (frekvens/ton/dB) är fast placerad i grafytans övre vänstra hörn**
  (innanför plotten, inte i header-raderna). Den låg tidigare bakom ANALYZE-chipsen och
  försvann när ett förslag visades; ett mellansteg där den följde muspekaren visade sig i
  sin tur täcka MID/SIDE/RES-klicken, så den fasta positionen i grafen är den slutgiltiga
  lösningen.
- **UNDO-knappen efter Apply döljs automatiskt efter 10 sekunder** (de applicerade
  parametrarna påverkas inte, bara affordancen försvinner).
- **Minsta fönsterbredd höjd 900 → 1020 px**: under ~980 px kolliderade preset/A-B-C-D-
  blocket med monitor/delta/gain/bypass-blocket. Ett gammalt sparat, smalare `uiWidth`
  klampas nu upp vid laddning i stället för att återskapa överlappet.
- **Extra visuell utjämning (EMA, tau=1 s) av spektrumkurvorna** ovanpå den befintliga
  SPEED-styrda attack/release-smoothingen, för en lugnare kurva. Helt separat från
  detekteringens egen ~400 ms medelvärdesbildning — rör varken RES eller ANALYZE.
- **ANALYZE kan nu föreslå upp till 6 band** (var 4), fortfarande inom de befintliga 8 —
  minst 2 band lämnas alltid fria för manuellt arbete.
- **Högerklick på en nod öppnar Type & Dynamics-panelen direkt**, utan den tidigare
  enradiga mellanmenyn (rent extra klick utan funktion).
- **Attack och Release tillagda i panelen** (var bara Threshold + Dyn Range trots att
  v13-changeloggen beskrev fyra knobbar — parametrarna fanns och användes redan av
  DSP:n/ANALYZE, de hade bara aldrig kopplats in i just denna popup). Panelen är nu
  fyra knobbar i rad och något bredare.
- **Spektrumkurvan byggs nu av effekt-/RMS-medelvärde över varje punkts bin-intervall**
  i stället för max. Den tidigare kantigheten berodde inte på bin-bredden i sig utan på
  att max hoppar mellan olika enskilda FFT-bins bild för bild — EMA(1s) hjälpte därför
  inte mycket (tidsutjämning löser inget frekvensled-problem). RES/ANALYZE-detektorn
  fortsätter läsa max (egna `*Det`-arrayer), så känsligheten för smala resonanstoppar
  är oförändrad; bara den ritade kurvan (`*Disp`-arrayerna) blev jämnare.
- **HP/LP-menyn öppnas nu vid triangelns faktiska position** i stället för en odefinierad
  standardplats (syntes tidigare nere i vänstra hörnet, långt från kontrollen).
- **Global mörk LookAndFeel för PopupMenu/ComboBox/AlertWindow/TextButton**: tidigare
  stylades bara rotary-knobbarna, så högerklicksmenyer, presetväljarens lista och
  "Save preset..."-dialogen föll tillbaka på JUCE:s ljusa standardutseende med för stor
  font — påtagligt och störande i en mörk mixmiljö. Satt som applikationens default
  LookAndFeel i editorns konstruktor, så allt ärver samma tema automatiskt.

## v14 — Filtertyper per band + större fonter

- Varje band kan nu vara Bell (default), Low Shelf, High Shelf eller Notch. Typen väljs
  i en knapprad överst i högerklickspanelen; parametern ("bandN_type") är automatiserbar
  och ingår i presets/A-B-C-D. Typbyte är diskret (ingen ramp).
- Shelf-Q klampas till 0,3–1,2 i koefficientgenereringen så övergången alltid är
  civiliserad. Notch har ingen gain: noden ligger fast på 0 dB-linjen (drag = endast
  frekvens), gain-knobben gråas ut (värdet visas som —) och dynamiken inaktiveras.
- ANALYZE sätter alltid Bell på föreslagna band. Hover-readouten visar typnamnet.
- Läsbarhet: de minsta fontstorlekarna höjda 1–2 pt genomgående (axlar, legend,
  resonansetiketter, readouts, kolumnrubriker, fottext, panel).

## v13 — Attack/Release per band + skräddarsydda tider i ANALYZE

- Två nya parametrar per band: Dyn Attack (0,1–100 ms, default 5) och Dyn Release
  (20–1000 ms, default 150), logaritmiska och automatiserbara. Envelope-koefficienterna
  cachas per band och räknas bara om vid ändring. Dynamikpanelen (högerklick på nod)
  har nu fyra knobbar; hover-readouten visar tiderna.
- ANALYZE skräddarsyr tiderna per resonans: attack ≈ 2 perioder av resonansfrekvensen
  (transienten släpps igenom, ringningen fångas), release ≈ resonatorns ringtid
  t60 ≈ 2,2·Q/f ur uppmätt Q och frekvens (klampat 60–600 ms) — dämpningen släpper i
  takt med att ringningen dör ut, utan fladder eller pumpning.

## v12 — Konsolidering: RT-säkerhet + parameterutjämning

- Audiotråden är nu helt allokeringsfri: statiska bands koefficienter, detektorernas
  bandpass och HP/LP-kaskaderna (Butterworth-Q-tabeller ersätter FilterDesign) skrivs
  alla via ArrayCoefficients direkt in i befintliga koefficientobjekt.
- Parameterutjämning ~20 ms per band och kanal (Freq/Q multiplikativt, Gain i dB):
  ändringar rampas via sub-block-vägen — ingen zipper vid snabb automation.
  Preset-/state-laddning och A/B/C/D hoppar direkt utan ramp (avsiktligt).
- Kodgranskning: ANALYZE-chipsen flyttade under badge-raden (krockade med legenden
  vid smala fönster). TESTING.md tillagd med pluginval-, DAW-, CPU- och
  lyssningstest-checklista inför release.

## v11 — Delta-lyssning

- Δ-knapp i headern (parameter "delta", automatiserbar): spelar det som tagits BORT,
  dvs. dry − wet, med ~10 ms crossfade. Beräknas i M/S-domänen före monitor-decode,
  så MONITOR M/S solar deltats mid- respektive side-del. Output gain appliceras efter
  (lyssningsnivån styrs som vanligt). DELTA-badge i grafen när aktiv.
- Obs: spektrat och detekteringen visar det du hör — alltså delta-signalen när Δ är
  aktiv. Kör inte ANALYZE med delta på.

## v10 — Detekteringssteg 1–5 + ANALYZE-assistent

Detekteringskedjan uppgraderad i fem steg:
1. Eget RMS-detektorspektrum (effektmedelvärde ~400 ms, oberoende av SPEED-inställningen
   och displayens max-binning) — detekteringen ser energi över tid, inte enstaka peaks.
2. ERB-skalade grannskapsfönster (kritiska band): breda i basen, smalare relativt sett
   högre upp — "sticker ut ur sitt sammanhang" matchar örats hörselfilter.
3. Frekvensstabilitet: parabolisk peak-offset-jitter per punkt gate:ar vandrande innehåll
   (formanter, vibrato) — bara stationära toppar kvalificerar.
4. Maskeringsgate: kandidater under grannenergins maskeringströskel ignoreras
   (spridning ~10 dB/ERB uppåt, ~25 dB/ERB nedåt).
5. Harmonisk diskriminering: toppar med stark subharmonik (f/2, f/3, f/4) straffas som
   sannolikt musikaliska övertoner.

ANALYZE-assistenten (klickbar i legenden): lyssnar 8 s medan musik spelar, samlar
tidsstatistik över flaggade resonanser, mäter bandbredd (→ Q, 2–12), allvarlighetsgrad
(→ cut 2–9 dB, ~60 % av överskottet) och nivå (→ threshold). Föreslår upp till 4
dynamiska band (gain 0, negativ range — dämpar bara när resonansen ringer) som ritas
som förslag i grafen. APPLY skriver till lediga band (rör aldrig band du ställt själv)
och sparar undo-state; UNDO återställer. Mid/side-resonanser routas till M- resp S-läge.

## Struktur

```
CMakeLists.txt              CMake + JUCE (FetchContent), targets VST3 (+ AU på macOS)
Source/
  PluginProcessor.h/.cpp    DSP: M/S-encode/decode, 8 peak-band (juce::dsp::IIR),
                            APVTS-parametrar, peak/RMS-meters, A/B-snapshots, presets
  PluginEditor.h/.cpp       Editor: header (preset, A/B, meters, bypass),
                            8 bandkolumner (M/MS/S, Freq/Gain/Q-knobbar, bypass)
  EQGraphComponent.h/.cpp   Frekvenskurva: separat Mid- (grön) och Side-kurva (orange),
                            draggbara noder (drag = freq/gain)
  LookAndFeel.h             Färgtema, KnobLookAndFeel (rotary), LevelMeter
```

## Bygga

Kräver CMake ≥ 3.22 och en C++17-kompilator (MSVC/Xcode/Clang).
JUCE 8.0.4 hämtas automatiskt via FetchContent vid första konfigureringen.

```
cmake -B build
cmake --build build --config Release
```

`COPY_PLUGIN_AFTER_BUILD TRUE` installerar pluginen till systemets pluginmapp.
AU-target byggs endast på macOS.

Har du JUCE lokalt: byt FetchContent-blocket mot `add_subdirectory(path/to/JUCE)`.

## DSP-design

- Encode: `mid = (L+R)*0.5`, `side = (L-R)*0.5`; decode: `L = mid+side`, `R = mid-side`.
- Per band: ett mono `juce::dsp::IIR::Filter<float>` (peak/bell) för mid och ett för side.
  Bandets M/S-läge (Mid / Mid+Side / Side) avgör vilka som processas.
- Koefficienter räknas bara om när Freq/Gain/Q ändrats (cache i `updateFilters()`).
- Alla parametrar (`bandN_freq/gain/q/mode/bypass`, `global_bypass`) ligger i
  `AudioProcessorValueTreeState` → DAW-automation och total recall via get/setStateInformation.

## HP/LP och spektrum (v2)

- HP/LP: Butterworth med valbar lutning 12/24/48 dB/okt (kaskaderade biquad-sektioner via
  `FilterDesign`), routing Stereo/Mid/Side per filter. Styrs via noderna i grafen:
  drag = frekvens, högerklick = meny (enable/slope/routing), dubbelklick = on/off.
- Två spektrum-tappar (mid/side, pre-EQ), FFT 4096 + Hann, ritade som fyllda ytor i
  mid/side-färger med +3 dB/okt display-tilt (musik ser platt ut). Interpolation mellan
  bins i basen, max-binning i diskanten, temporal utjämning.
- Y-axeln zoomas med mushjul över vänsterkanten (±6…±30 dB, endast visuellt).
- Fönstret är resizable (900×550–1800×1100); storleken sparas i plugin-state.

## Post-EQ-spektrum m.m. (v4)

- Spektrum-tapparna sitter nu EFTER banden + HP/LP (före decode), så bakgrunden
  reagerar direkt när EQ:n justeras. Output gain ingår inte i analysbilden (avsiktligt).
- STEREO-spektrum (effektsumman √(mid²+side²), lila-grå) togglas i legenden, av som default.
- FREEZE i legenden fryser synliga spektra som referenskonturer.
- Frekvens/not-texten och resonansetiketterna ligger på fasta rader högt upp i grafen.
- Vid fönster-resize pausas FFT/spektrum/text-rendering (återupptas 200 ms efter sista
  ändringen); ljudprocesseringen påverkas aldrig av resize.
- Dubbelklick på valfri knob (Freq/Gain/Q/Output) öppnar textinmatning; "1.5k" = 1500 Hz.

## v9 — Hörselviktad resonansdetektering

- Detekteringen använder nu en perceptuell viktkurva (grov inverterad equal-loudness,
  ~80 fon): presensområdet 2–5 kHz (+6 dB kring 3,5 kHz) prioriteras upp, basen
  (−12 dB vid 20 Hz) och yttersta diskanten (−6 dB vid 20 kHz) ned. Vikten påverkar
  både kvalificeringsgolvet och topp-3-rangordningen, så markörerna hamnar närmare
  det som faktiskt låter störande.
- Grannskapsnivån beräknas med median i stället för medel — robustare när flera
  resonanser ligger nära varandra.

## v8 — Jämnare spektrum

- Överlappande FFT: ringbuffert publicerar block var (fftSize/4):e sample (75 % överlapp)
  → ~43 uppdateringar/s i stället för ~11, utan förlorad basupplösning. Publicering via
  seqlock så att UI:t alltid läser senaste kompletta block (inga tappade block).
- Frame rate-oberoende utjämning: targets (från FFT) separeras från visade nivåer;
  attack-interpolation uppåt och exponentiell decay nedåt skalas med uppmätt frame-tid.
  SPEED-lägena är nu tidskonstanter (release ~0,12/0,3/0,8 s).
- Grafen ritar i 60 fps; resonans-scoren är också dt-baserad så RES beter sig likadant
  oavsett frame rate.

## v7 — Dynamisk EQ

- Alla 8 band kan göras dynamiska: högerklicka på en bandnod för Threshold (−60…0 dB)
  och Range (±18 dB, 0 = statiskt band). Negativ range sänker bandet när nivån i dess
  frekvensområde överstiger threshold (frekvensselektiv kompression); positiv höjer.
- Detektering per kanal: bandpass-detektor (samma freq/Q som bandet) + envelope
  follower (~5 ms attack, ~150 ms release), mjukt 12 dB-knä.
- Koefficienterna uppdateras per 32-samples sub-block via `ArrayCoefficients`
  (allokeringsfritt i audiotråden, uppdateras bara vid ändring > 0,05 dB).
- Ghost-noder i grafen visar bandets effektiva gain i realtid (grön = mid, orange = side)
  med en linje till den statiska noden — du ser kompressionen arbeta.
- Hover-readout visar DYN-inställningarna; parametrarna är automatiserbara och ingår
  i presets/A-B-C-D.

## v6

- Monitor-val i headern (MONITOR: ST / M / S): lyssna på hela stereosignalen, enbart
  mid eller enbart side (mono i båda öronen). Klickfri växling via ~10 ms crossfade av
  avkodningskoefficienterna. Automatiserbar parameter ("monitor"); aktivt solo visas
  med badge i grafen. Spektrum och resonansdetektering visar alltid båda kanalerna.

## v5

- Resonansdetekteringen visar nu de 3 starkaste resonanserna per kanal: mid (grön,
  övre etikettraden) och side (orange, undre raden), med hysteres så att listan är
  stabil. Körs på post-EQ-spektrat: dämpa en resonans och nästa tar dess plats.
- IN/UT-metrarna är stora vertikala staplar med dB-skala (0…−60) till höger om grafen.
- Global bypass har tydlig på/av-färg på knappen + "BYPASSED"-badge i grafen.
- A/B-jämförelsen utökad till fyra snapshots: A/B/C/D.
- Egna presets: "Save preset..." i preset-menyn sparar aktuell inställning som XML i
  användarens appdata-mapp (MSEQ8/Presets); sparade presets dyker upp i menyn.

## Interaktion & analys (v3)

- Legend uppe till vänster: MID/SIDE-toggles, RES (resonansdetektering) och
  SPEED FAST/MED/SLOW (spektrumets attack/decay); RES och SPEED sparas i plugin-state.
- Resonansdetektering: punkter som ihållande ligger ~7 dB över sitt spektrala grannskap
  markeras med vertikal linje + frekvens/not-etikett.
- Hårkors med frekvens + not (A4 = 440 Hz) + cent följer muspekaren.
- Hover på nod: readout med bandets alla värden; motsvarande bandkolumn markeras.
- Mushjul över bandnod justerar Q (upp = smalare).
- Output gain ±12 dB i headern (parameter `output_gain`, rampad, automatiserbar).

## UI

- Grafen visar summerad dB-respons per kanal; noder färgas efter M/S-läge
  (grön = Mid, lila-grå = Mid+Side, orange = Side). Drag av nod ändrar freq (log-x) och gain (y)
  med korrekta begin/endChangeGesture för host-automation.
- A/B: två parameter-snapshots (ValueTree-kopior) i processorn; växling sparar aktiv slot
  och laddar den andra.
- Presets: hårdkodade i `applyPreset()` — enkelt att byta till XML-filer senare.

## Kända förenklingar (medvetna, för tydlighet)

- `makePeakFilter` i `updateFilters()` allokerar; för produktions-RT-säkerhet, byt till
  `ArrayCoefficients` (JUCE ≥ 7.0.3) eller räkna koefficienter till en lock-free-buffer.
- Koefficientbyten är osmoothade — snabb automation kan ge små klick; lägg till
  `SmoothedValue` per parameter vid behov.
- Metrarna är peak med enkel release-smoothing; RMS exponeras också i processorn.
- Koden är inte kompilerad här — förvänta ev. någon mindre justering beroende på exakt
  JUCE-version (skriven mot JUCE 8.x, `FontOptions`-API).
