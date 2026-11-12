# Changes

What each release of Ravel adds and fixes. The release workflow puts a version's section at the top of its
GitHub release; a version with no section here gets its commits listed instead. Newest first.

## 1.35.0

New
- WEFT blocks VCA (A as loud as LEVEL plus CV, linear to exponential) and ENV (a two-stage attack and release on the gate, or the note's). Existing patches play exactly as before.

## 1.34.0

New
- The matrix and the macros reach the LFOs' SHAPE and SYM (GLOBAL: L1 SHAPE, L2 SHAPE, L1 SYM, L2 SYM). SHAPE moved blends one shape into the next, so an LFO morphs (SINE, TRI, RAMP, SAW, SQUARE, then S&H and DRIFT); SYM skews it, as its knob does. With nothing moving them, the LFOs are exactly as before.

## 1.33.1

Fixed
- LADDER and SH-101 filters: with RESO past about a fifth and the cutoff (with ENV>CUT) high, past about 3 kHz, they broke into a hollow, piercing ring of tones unrelated to the notes. Their feedback came from a stage's memory, which at high cutoffs swings from sample to sample; it now comes from the output the filter is about to make, so the resonance stays clean all the way up. Lower down they sound as before (within a few percent).

## 1.33.0

New
- HARMONIC, a fifteenth synth engine, after Make Noise's Telharmonic: H (24 harmonics, the loudest at CENTRD, FLUX how fast the rest fall away, LOCK one held at full, EVEN the odd against the even), P (a carrier phase-modulated by two locked sines) and N (noise in two sidebands around the note), each at its own level, stacked in chords (INTRVL, STACK, DETUNE). Presets: TONEWHEELS, SPECTRAL TRIAD, PM MINOR, NOISE CHOIR.
- WEFT blocks IMPULSE (after Mannequins' Mangrove: a rise and fall each cycle, FORMANT and BARREL; split tones, the pitch dividing), CREASE and LOCATE (after Cold Mac's CREASE and LOCATION: a cut at zero; a clipped integrator). Factory patches MANGROVE, SPLIT TONE, CREASED RAMP.
- The start screen: the tube warms up (a dot, a line, opening into the picture), and three cloaked figures stand in the snow where the logo comes.

Changed
- The start screen's specks, rain, drips and melt are gone (the snow, the logo and the tape's glitches stay), making room in the flash for the above.

## 1.32.0

New
- GUITAR, a fourteenth synth engine: an electric guitar and its rig on one track. A plucked string per voice (PICK, POSITN, DECAY, MUTE for palm mutes, the PICKUP from bridge to neck, STRUM to spread a chord), then for the whole track a drive pedal (BOOST, OD, DIST, FUZZ, with DRIVE and TONE), an amp (CLEAN, CRUNCH, HIGH, LEAD, with GAIN, BASS, MID, TREBLE, PRESENCE) and a cabinet (1X12, 2X12, 4X12). The pedal and amp follow M-PYRE's (History Of Sky); synthesized, no samples. The amp takes the INSERT's place on a GUITAR track. One voice with GLIDE slides. Presets: CLEAN CHIME, CRUNCH RIFF, PALM CHUG, SINGING LEAD, FUZZ WALL.

Changed
- The interface's four largest functions are built for size on the FM-1, as the interface was meant to be (the toolchain had been ignoring the setting): room in the flash for GUITAR.

## 1.31.0

New
- WEFT block TRK IN: another track's sound into a patch, to filter, fold, resonate or gate it (a STRIKE track through a patch's LP, BANDS or FORMANT). Taken before that track's LEVEL and mute, at its own level, so it works as a wet send; about 3 ms late. While a patch takes it, that track plays only through the patch (its sends too); muting the WEFT track gives it back. One per project. The patch hears it while its voices play: on its notes, or always with RUN ALWAYS.

## 1.30.0

New
- REPEAT punch-in (FX + white 4, in place of the delay freeze, which stays on DELAY 2 › FREEZE): the last slice of the master, a tempo division long, repeated while held. FX + black keys 1–5 set the division (1/16, 1/16T, 1/32, 1/32T, 1/64), at once while REPEAT is held, so it can be rolled faster or slower as you play. A division longer than 93 ms at the tempo is halved until it fits, so it stays on the beat.

Changed
- LOW PASS and HIGH PASS punch-ins: each press sweeps from the start, over one bar at the tempo, and further: low pass 8 kHz to 300 Hz, high pass 40 Hz to 6 kHz.
- THROW: the dry sound fades out while it is held (about 80 ms), so only the echoes and the space play.
- The FX hint fits the screen (LOOP REV STT RPT LO HI THR FIL).

Fixed
- The punch-in filters' right channel took the left's state every block (not truly stereo, with a faint click); a sweep after the other filter started from where that one had stopped.

## 1.29.0

New
- A QUANTZE for each drum lane: SEQ › DRUM FEEL on a kit. SONG (the default) follows the song's QUANTZE as before; 0–100 % gives the lane its own, so a kick can play on the grid while the hats keep the timing they were played with. It changes playback only. Kept in the project (format 27; older projects load with every lane on SONG, so they play as they did).

## 1.28.0

New
- Play a kit in live from SEQ. With REC lit on a PERC kit, the white keys on the SEQ pages play the lanes (1–6 and again) instead of entering steps, and while the transport runs they are recorded into the lanes as played. With REC unlit the keys put trigs on the steps as before; holding SEQ makes them steps for a moment either way.
- QUANTZE on the SEQ pages: SEQ › TRACK on a kit, SEQ › DIRECTION on a note track. It is the same setting as SONG › CLOCK FEEL: 0 % plays recorded notes and hits as played, 100 % snaps them to the grid. It changes playback only, so the timing as played is kept.

Changed
- The footer while the keys play on SEQ reads KEYS PLAY  HOLD SEQ: STEPS (it ran off the screen on a kit).
- Install page: "Back to the stock firmware" sends V15's firmware with Ravel's own loader instead of M-VAVE's (which switches the FM-1 off when Ravel hands over to it), resets the stock settings to a new unit's, and shows its progress beside its button. Confirmed on hardware (a unit on Ravel 1.28.0 back to stock V15).

## 1.27.1

Fixed
- Newer FM-1s save, load and update. Later units have a different flash chip (a UC25HQ80, ID B36014; the first ones have Puya's P25Q80, 856014), and Ravel trusted only the first: on a newer unit it ran but kept nothing (projects, sounds, settings) and refused updates from the installer. Both chips take the same plain flash commands Ravel uses, and every write is still read back. A newer unit running an older Ravel can't take this from the page: hold OCT− and OCT+ for 5 seconds (the chip's update mode), put M-VAVE's firmware back with the rescue tool (tools/fm1_rescue.sh), then install. (Corrected: the first notes said to install from the page in that mode, which the page can't reach.)
- GLO › FLASH names the newer chip (UC25HQ80 1 MB). The rescue tool (tools/fm1_rescue.py) accepts it too.

## 1.27.0

New
- WEFT, ten new blocks:
  - **RIFT** passes or cuts A only at its zero crossings: click-free stutters, and SKIP's cycle-tearing (1 IN 2: an octave down).
  - **DRIP** is a bouncing drop: clicks that come faster and softer until they run together, to strike rings and bodies or open an LPG.
  - **CATCH** catches one, two or four cycles of A and loops them at the note: a waveform frozen.
  - **BEND** reads A back from its own few milliseconds, the read moved by B and a slow wobble: tape and Doppler swoops that come back to pitch.
  - **FLEXURE** folds A at each note's start and relaxes back to clean as the note goes on.
  - **LAMINA** slides A's lows, mids and highs apart in time.
  - **FASCIA** puts B's loudness and brightness on A, so one sound wears another's skin.
  - **VISCERA** is a soft resonant body that gets duller, shorter and lower the harder it's hit.
  - **SNAP** keeps a gesture (a knob's movement, a played rhythm) for 1/4 to 4 bars and loops it in time with the transport.
  - **TRACK** reads another track's level, envelope, pitch, velocity or hits as a source inside the patch.
- CATCH, BEND, LAMINA and SNAP each use one of a patch's two delay memories, as DELAY, DIFFUSE and STRING do.
- WEFT factory patches **DRIP BODY**, **FROZEN TEAR** and **LAYERS**.
- Emulator: the patcher's type menu has a CAPTURE & REFORM group; DRIP is under clocks, SNAP and TRACK under envelopes and motion, VISCERA under resonators.

The existing factory patches sound the same, sample for sample. The new blocks run from the flash, so the RAM the existing ones run from is unchanged; a patch costs nothing more unless it uses them.

## 1.26.0

New
- WEFT: **CLK DIV**, a clock divider and multiplier: a pulse on the first of each RATIO of A's rises (/2 to /64), or that many evenly between two (x2 to x4); WIDTH from a one-sample click to a gate the length of the gap; B resets it.
- WEFT: **LPG**, a low pass gate after the vactrol gates of Buchla's 292: B (or the note) opens it, it lets go over DECAY with a vactrol's long tail and darkens as it closes; a one-sample click opens it all the way, for struck, woody pings with no envelope. MODE: LPG, VCA or LP.
- WEFT factory patch **GATED BONGO**: sixteenths divided by three and by four, pinging LPGs over a sine and a ring.
- Emulator: the WEFT patcher's type menu has a CLOCKS group (TICK, CLK DIV, EDGE, FLIP); LPG is under envelopes and filters both.

The existing factory patches sound the same, sample for sample; a patch without the new blocks costs the same.

## 1.25.0

New
- Projects can be deleted from a computer: the install page's backup and restore (below) shows × beside each project on the FM-1, and Ravel empties the slot as SAVE › PROJECT › CLEAR does (DELETED PROJECT n on the top line; the project open stays as it is). Older computer tools are unaffected.

Changed
- The install page (not the firmware): a dark theme; more room around its lists; files' projects and banks go into free slots on the FM-1 (their own when free), so importing never replaces what is there unless you choose a slot in use (it says "replaces", and asks); × deletes a project (with this version) or a single sound from a bank (with any).

## 1.24.1

Fixed
- Holding HOME for help on SOUNDS with an empty bank slot could crash: the help named a knob that has no name.
- WEFT: a cord from a block with a long name ("10 VOCODER") overran its text and could overwrite other memory; the names ran together on WEFT PATCH and WEFT MAP.
- Loading a project, a demo or a new project keeps the sound silent while it happens, so nothing half-loaded plays. A saved project found damaged as it is read starts a new project (DAMAGED: A NEW PROJECT) instead of leaving a mix of old and new that a later SAVE could keep.
- A sound bank that can't be read (damaged, or saved by a newer Ravel) is never written over: it shows empty, and SAVE, CLEAR and the WEFT LIBRARY's SAVE and DELETE say BANK A UNREADABLE: KEPT. Before, storing one sound into it replaced all 32. A bank save during a computer transfer into the same bank ends the transfer instead of being written over by it.
- A step key held while OCT or the track encoder turned toggled a step on another page or track when let go; OCT and the track now wait while a step is held, and a shorter track opens on a page it has.
- MIDI learn no longer takes KIND or ENGINE (LEARN: NOT THAT KNOB); a control learned on one before does nothing now. Moving one skipped KIND's question, the clearing of steps and the one-GLITCH-track rule.
- Changing a track's sequencer settings (LENGTH, RATE and the like) no longer counts as editing its sound, so KIND doesn't ask TURN AGAIN: NEW SOUND after them.
- Moving a WEFT block past another SW 1>4 or LISSA now says which block SW 1-4 or LIS Y read from then on.
- Stuck notes: a full note queue (chords and ratchets on many tracks at once) could keep a note-on and drop its note-off. The last places in the queue are kept for note-offs.
- The sine and wavetable lookups could read one past their table at a phase a hair below a whole turn.
- If the second core failed mid-step, the delay, feedback and space ran twice for that step (a jump in them).
- A WEFT voice that went bad (NaN, or blown up) stays silent no more: it ends, and the next note starts it clean. A NaN can no longer reach a WEFT delay's or string's read position.
- Two WEFT tracks with SEQ OUT blocks, each on its own core, could lose a step pulse or a gate between them.
- A held note leads the track's envelope (the ENV source, the screen) again once a later note has ended.
- Early micro timing (below −6 ticks, QUANT under 50 %) is worked out the same on the FM-1 as in the emulator.
- A sound saved into an empty slot is named for what it is: TINE was named VA, and WEFT, GLITCH and COMP sounds PERC. Loading a project, a demo or a new project forgets the last one's patch names and presets, and a WEFT sound loaded from SOUNDS or PRESETS shows its name.
- The start screen's melt draws in strips as meant, not whole letters at each strip's drop.
- Text that ran off the screen or was cut short: long messages show whole, in the smaller letters when they need to, and stay up a little longer; long knob names stay in their cell; a few hints and messages are shorter.

## 1.24.0

New
- WEFT: move a block. On WEFT PATCH, hold EDIT and turn KNOB 1 (BLOCK): the block moves a place a detent and the others shift along. Its cords, the cords reading it, OUT, B MOVES, WEFT PLAY's knobs and its settings (their values, step locks, DEST settings, matrix rows, macros, influence MOD routes, the scenes' copies) all follow it. The footer shows the shortcut while EDIT is held.

## 1.23.1

Changed
- KIND asks before it replaces a sound you have changed by hand: the first turn shows TURN AGAIN: NEW SOUND (or SOUND+STEPS when it would clear steps too) and a second turn within a couple of seconds switches. A fresh sound, or one just loaded (PRESETS, a bank, WEFT LIBRARY, a project, a demo), still switches at once, so turning through the kinds stays quick.

## 1.23.0

New
- WEFT: **black key 11** twice on WEFT PATCH or WEFT BLOCK unpatches the block: every cord into it and out of it comes off (its A and B, and every input reading it), the block, its settings and amounts kept; the top line says how many came off, and the footer shows the key on both pages.

## 1.22.2

Fixed
- With REC armed on a SEQ page, the keys of a WEFT track, and of a PERC track with one machine, played and recorded notes the way a synth track's do; they used to stay steps, so a key turned a step on or launched a pattern instead. Stopped and armed, step recording now takes their notes too. A kit's keys stay its steps, as before.

## 1.22.1

Faster
- The tracks' mix adds nothing for a send at 0 (the FEEDBACK's almost always is, the DELAY's often), and the main mix is captured for a LOOP track only when there is one. The step's clears, the excitation's sums and the mix's loudness run from RAM, and the clock works out only the wave its shape uses. Measured with the FM-1's instruction costs in an emulator (TANGLE on T1, DEMO 1 playing): the step from 51.3 % to 50.0 % of its time, the part after the voices that only core 1 can do from 5.2 % to 3.3 %. The sound is the same, sample for sample.

Changed
- Bench builds for the emulator time each part of a step on both cores (`rvb_ph`, `docs/fm1-emu.md`); normal builds leave it out.

## 1.22.0

Faster
- More of the render's per-sample work runs from RAM instead of the flash: the clock (its two waves), the delay's CUTS ring, the buses' sum into the main mix (one pass instead of four) and the blowup check on the mix. The master's DC block and gain keep their state in registers through the block. Measured with the FM-1's own instruction costs in an emulator (TANGLE on T1, DEMO 1 playing): the audio's share of core 1 down from 53.5 % to 50.6 %. The sound is the same, sample for sample.

Changed
- The modulation's once-a-step parameter work (`eff_compute`) moved from RAMTEXT to the pool, same speed, to make room for the above; the pool's required spare room for later code is now 2 KB (was 6 KB).

## 1.21.1

(1.21.0 was tagged but not released: its new release check could not build the emulator on GitHub. 1.21.1 is the same firmware.)

Faster
- The tracks' mix into the main mix and the sends runs from RAM (in the flash it was the render's busiest loop), and so does a track's chain when it has no SKIN, INSERT or RING (most tracks): the DC block alone. The step's clears and copies run from RAM, four samples at a time. Measured with the FM-1's own instruction costs in an emulator: the audio's share of core 1 down from 58.5 % to 54.6 %, of core 2 from 38.6 % to 34.7 %. The sound is the same, sample for sample.
- A track nothing excites skips looking for the peak of its silence.

Fixed
- A WEFT track reads WFT on the track list and the TRACKS page, not CMP.

Changed
- Each release is booted and played in fm1-emu, an emulator of the FM-1, before it goes out: as it is, with a second core that misreads RAM, and with the FM-1's measured timing (`docs/fm1-emu.md`).

## 1.20.0

New
- **BUFFER SAFE** (GLO › CPU 2, the setting at start): one audio block rendered ahead, so a busy block (a bar's note-ons, a chord of new WEFT voices) may take up to twice its time without a dropout; everything is heard 11.6 ms later. TIGHT is the old way. With SAFE the overload guard reads two blocks at a time and allows 90 % instead of 80 %, so it fades far fewer voices.
- **GLO › CPU 2**, with three tests for finding what holds the FM-1 up: GUARD OFF, SCREEN 1 FPS (the screen drawn once a second: what its drawing costs the audio) and CORE TEST (the same work timed on each core, in RAM and from the flash).

Faster
- WEFT: the plan of a patch (who feeds whom, the loops, the order the blocks run in) is kept from step to step and worked out again only when the patch changes; about 5 % less work a voice, more on the FM-1 (it ran from the flash).
- WEFT: a DELAY of 18 samples or more with nothing on its B breaks a loop: its run is read before the run's blocks and written after them, so a loop through it runs a run at a time instead of sample by sample. THREE LOOPS does about 18 % less work. The sound is the same, sample for sample.
- SPACE: MOTION's sway is worked out at each step's ends and run straight between (it moves a tenth of a thousandth of a sample a step), and the combs go two at a time, so the FPU overlaps them: about 18 % less work, more on the FM-1. The sound differs by about 55 dB under the signal, the reverb's own 12-bit grain.
- The buses' peak and blowup checks in one pass; the voices' check without an early exit, so it pipelines; the SEM filter's BP gain once a step; the delay's CUTS ring without a division.

Changed
- The screen goes to the panel in bands of 4 rows (not 8), to make room in RAM for the audio's spare block.

## 1.19.0

Faster
- **The buses are shared between both cores.** The BED with the SPACE, FEEDBACK and DELAY are three jobs, each going to whichever core has room (they play the last step's sends, so the first core can run one while the second plays its voices). Before, the second core ran all of them and took few voices. The sound is the same, bit for bit.
- SPACE does about 18 % less work (about 43 % with MOTION at 0), and DELAY about 25 % less with WOW and FLUTTER at 0; the sound is the same, sample for sample.

Changed
- **The BED rests when nothing plays.** With the sequencer stopped and every track silent for four seconds, it fades out and stops computing, and the SPACE it sends into can rest too; it fades back in with the next sound or PLAY.

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
