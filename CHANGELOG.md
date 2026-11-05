# Changes

What each release of Ravel adds and fixes. The release workflow puts a version's section at the top of its
GitHub release; a version with no section here gets its commits listed instead. Newest first.

## 1.18.0

Faster
- **WEFT runs its blocks sixteen samples at a time.** Each block works through a whole run before the next, wherever the cords allow; blocks in a loop of each other (through a cord from a later block, a sample late) still run sample by sample together. The sound is the same, sample for sample (checked bit for bit on every factory patch and 300 random ones). Across the twenty-eight factory patches the work is 37 % less on the simulator's count of instructions: most patches 35–60 % less (INIT 49 %, PHASE DIST 52 %, KS STRING 54 %, VIOLENCE 48 %), the ones built on loops less (TANGLE 8 %, BENJOLIN 15 %), and THREE LOOPS, nearly all loop, about the same. The FM-1's own time will differ from the count; GLO › CPU shows it.
- DIFFUSE, SHIFT and DIODE do less work a sample; the six-mode bodies, CHEBY and FORMANT run each filter through the run in turn.
- **Two cores share whole tracks.** A track the second core takes now has its effects (SKIN, the INSERT, RING) there too, not just its voices; the tracks are shared by what their voices and effects cost, then single voices where that helps. With one core the sound is the same, bit for bit.

New
- **WEFT RATE** (a new page after WEFT MAP): FULL, or HALF: the patch at 22 kHz, about half the time on the FM-1, its top octave gone and a little grit. Saved with the patch and in its file; also in the emulator's patcher.

Changed
- WEFT's random blocks (DUST, TICK, the switches, CHAOS, NOISE, RUNGLER) each have their own dice: the same kind of randomness, a different draw from before.

Fixed
- The new release notes on GitHub list what each release adds and fixes (CHANGELOG.md).

## 1.17.1

Fixed
- After installing an update, the second core always gets a fresh start: if it had been held after a failure during the last session, the FM-1 no longer comes back on one core (CORES 1) until it is switched off and on.

Changed
- The start screen opens on real TV static: dense streaks across, specks of violet, blue and green, rows drifting brighter and darker, a bright band rolling down.
- The start screen runs smoothly to the end: the melt draws about six times faster, and the crash no longer freezes the picture. The tape now warps in waves, tears sideways in bands and breaks into sliding blocks every frame.

## 1.17.0

New
- **Overload relief.** When fading voices is not enough (the guard is down to two, or the cost is a big WEFT patch, the delay's GRAINS or the effects), the guard eases the rest in steps instead of falling behind into crackles: EASED 1 halves the delay's new grains (six at most), EASED 2 quarters them (four at most) and keeps new WEFT notes to one voice, EASED 3 allows two grains and one voice. Nothing playing is cut off; it steps back once the load is calm. Shown on GLO › CPU.
- **SKIP SILENT** (GLO › CPU, SILENT: SKIP or PLAY): a track muted or at LEVEL 0 for half a second stops computing its voices, unless something else uses its sound (an influence route, the GLITCH LOOPER, the COMP's sidechain, a WEFT output or RUN ALWAYS). Its next notes play as soon as it is heard again.
- **Per-track CPU** (GLO › CPU, VIEW: TRACKS): what each track's voices and effects cost; skipped tracks read SKIP.
- **COMP sidechain** (COMP 2, KNOB 4, S.CHAIN): the compressor listens to the mix or to any of T1–T7, muted or not (a silent "ghost" kick can pump the mix). The picture says KEYED BY T1 and so on.

Faster
- WEFT's whole voice loop runs from RAM instead of the flash. To make room, the screen is sent in 8-row pieces (an unchanged piece is not re-sent), WEFT's delay lines moved, and the RING influence is summed in the chain.

## 1.16.2

Faster
- Two cores: the voices are shared out by what each one cost a moment ago, dearest first, so both cores finish together. A heavy WEFT or HYBRID FM voice no longer piles onto the first core.

## 1.16.1

Fixed
- Installing an update no longer leaves the second core off (CORES 1) afterwards.

## 1.16.0

New
- **WEFT RUN** (WEFT MAP, KNOB 4: KEYS or ALWAYS): a patch can run with no key held, for drones, self-clocking networks and patches that drive other tracks.

Faster
- WEFT's bigger blocks (the six-mode bodies, CHEBY, FORMANT, VOCODER, SHIFT, DIFFUSE, STRING and the switches) run from RAM.
- SHIFT's carrier is cheaper (the same sound).

Removed
- The hidden game, to make room (about 48 KB of flash and 4.7 KB of RAM).

## 1.15.4

Fixed
- WEFT LIBRARY: SAVE saves where you point it, and the bank's own patches are told apart from the factory ones (a bank no longer looks full of the same patches).

## 1.15.3

Fixed
- ARP: turning HOLD off stops the arpeggio.
- Short lists (a few choices) step one choice per knob detent.

## 1.15.2

Faster
- HYBRID FM: the formants and the breath run at half the sample rate.

## 1.15.1

Faster
- HYBRID FM: the formant code runs from RAM, and is lighter.

## 1.15.0

New
- WEFT: a frequency **SHIFT** block and a **DIODE** clipper; the **THREE LOOPS** factory patch.

## 1.14.0

New
- **HYBRID FM**: the FM engine gains an envelope on its modulators and FS1R-style formants, with formant sequences.
- WEFT: **AUDIO OUT** and **SEQ OUT** blocks (SEQ OUT steps, jumps, restarts, gates or changes another track's pattern); OUT AUTO; the VIOLENCE sources (V TAP, V HELD, V TURN: how hard the panel is being played); the VIOLENCE factory patch.

Faster
- WEFT: lighter on the CPU (blocks nothing heard depends on don't run, slow LFOs worked out every eighth sample, cached coefficients).

## 1.13.0

New
- The start screen as a failing VHS tape, with the key lights following it; the new logo.
- WEFT LIBRARY: SAVE always works; INIT and CLEAR.

## 1.12.0

New
- WEFT blocks: FOLLOW, SHAPE, CHEBY, FORMANT, VOCODER, RINGMOD, NOISE, RUNGLER, LISSA, the sequential switches (SW 1>4, SW 4>1), STRING, CHAOS, CRUSH and EDGE.

## 1.11.0

New
- WEFT: twelve blocks a patch; DUST, TICK, the resonators, SAT, ABS, DIFFUSE; CLK OUT and MOD OUT; B MOVES (input B moves a block's second setting).

## 1.10.0

New
- The WEFT LIBRARY page; the start screen melts like a corrupted system.

## 1.9.0

New
- **WEFT**: a track you patch from blocks, on the FM-1's pages and in the emulator's drag-and-drop patcher.
- The emulator page works on a phone.

## 1.8.0

New
- A start screen: a storm of dust that gathers into RAVEL and drips away.

## 1.7.0

New
- **GLITCH LOOPER**: one track becomes four lanes of looped audio, with rec and play trigs, slices and a LOOPER knob (OFF, ON, +SPACE, +DELAY).

Fixed
- Voice stealing spares lone held notes.

## 1.6.0

New
- GLO › MUTE / SOLO, after the mixers.
- Step recording: STAY, for note input.

Fixed
- A track of an unknown kind loads as a blank synth.

## 1.5.0

New
- STEP: PRESETS scrolls the steps; note length while step recording; hold HOME for help; TAKE keeps the last loop played.

## 1.4.1

Fixed
- HOLD switched on takes the keys already down.

## 1.4.0

New
- MIDI control of the knobs: CC 70–73 turn the four knobs on screen, and MIDI LEARN ties any control to any parameter.
- PANEL: the buttons' backlight.

## 1.3.0

New
- BEND: MIDI pitch bend as a matrix source.

## 1.2.1

New
- SAVE › PROJECT › CLEAR empties a project slot.

## 1.2.0

New
- DUST: SCATTER, and strike tracks can take its grains.
- MIDI IN's settings belong to the FM-1 and are kept across power-off.

## 1.1.0

New
- TOUCH: MIDI aftertouch as a matrix source.
- The installer lists the last ten releases, newest first.

## 1.0.0

- The first 1.0: the same as 0.10.0 (32 compressed project slots, your own sounds on PRESETS, MIDI in and step recording).
