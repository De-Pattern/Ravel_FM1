# Handoff: put the redesign on the live web emulator

For the chat that builds and deploys Ravel. This is a restyle of the **public web emulator** — the page people
open at `https://de-pattern.github.io/Ravel_FM1/` to play the firmware in their browser, as it runs on the FM-1.

## Scope

- **Change:** `web/emu/index.html` only (its markup, CSS and the small amount of page script needed for the new
  visuals). It is the page `pages.yml` bundles and `tools/make_site.py` copies to the site root (`index.html`
  and `emulator.html`).
- **Do not change:** the engine (`ravel_web.c`, firmware sources), `web/emu/build.sh`, `bundle.py`,
  `test.mjs`, `xfer.js`, the installer, the manual, `/software/` (Ravel Software is built from another
  checkout), any test or development emulator, or `RELEASE` / `CHANGELOG` (a design change is not a release).
- **Keep working exactly as now:** every control, every keyboard shortcut, mouse and touch play, Panic, MIDI in,
  audio-file input, project/sound export, import, Send to FM-1 / Get from FM-1, the WEFT patcher, the guides,
  and the `__RAVEL_ICON__`, `__RAVEL_MANUAL__`, `__RAVEL_TOUCH__`, `__RAVEL_WASM__` placeholders.
- **Keep every element `id`** the page script uses (move and restyle them freely, don't rename or drop them):
  `ver sdot status manualbtn pdflink installlink repolink panic proj demo loaddemo play rec pos tracks8 kind
  engine preset pprev pnext bsave bclear pageback pagefwd gotos infile midiin midistate xexport ximport xsend
  xget xfile xstate outm vol mnote mnotex lefthand screen knobs4 btnrow1 btnrow2 blackrow whiterow wfcard
  wfwho wfnone wfmake wfon wfpre wfout wfpoly wfrun wfrate wfexp wfimp wffile wfsvg wfins wfmap manualbtn2
  gtabs gbody tlist` and the generated `wfsc…`, `wfwire`, `wfity`, `wfbmove`, `wfisout`.

## The design to match

Everything is in this folder. Open the PNGs; read values from the code.

| File | Use it for |
| --- | --- |
| `desktop-1440.png` | The whole page at 1440 wide: layout, spacing, every section |
| `components.png` | Colour tokens, type, button / lamp / key / readout / track-cell states |
| `screen-motion.png` | Bezel glow pulse and glare keyframes |
| `src/lib.js` | Exact values: the `C` palette, `WINE` + `BEZEL` gradient, `F` fonts, shadows in each helper |
| `src/desktop.js` | Exact positions and sizes per section |
| `preview/screen-motion.html` | Working CSS + JS for the screen bezel, glow pulse and glare — copy it |
| `assets/` | Textures. Use `speckle-device.png` cropped to a 512 px tile (≈210 KB) as a repeating CSS background, `scanlines`/`vignette` as CSS gradients (see the preview) |

### Look

- **Case and header:** near-black speckled matte plastic (microscopic pale flecks), soft top-light gradient on
  top, 1 px cool highlight on the top edge, crisp dark outline, deep drop shadow. Engraved labels are cool
  light ink `#a7b0b7` with a 1 px dark shadow above.
- **Screen:** 4 px bezel, deep maroon-red diagonal gradient (oxblood → crimson → faint violet-wine →
  copper-red glint → oxblood, stops in `BEZEL`), rim `#c23a3f` at 45%, red glow `#b3202c` that breathes on a
  2.4 s ease-in-out loop (opacity 0.30→0.60, blur 6→11 px). A soft glare crosses the glass top-left →
  bottom-right in 1.1 s every 7–12 s at random (peak 0.16). Scanlines + vignette over the canvas. Respect
  `prefers-reduced-motion` (no pulse, no glare).
- **Readouts** (values): sunken pale panels `#c8ceb6`, dark mono text `#1a1e18`, sized to their text.
- **Live things** glow phosphor green `#6dffa6`. **Held keys** are pale deep teal (`#4f8682` → `#2c5755`,
  black keys `#457a76` → `#244a48`) with a brighter teal glow `#6fe3d4` and pale teal label `#d4f4ef`.
- **Maroon** (`#6b1218` / `#a3161c`) only for selection, recording, Panic.
- **Fonts:** Barlow Semi Condensed 600/700 for labels and buttons, IBM Plex Mono 400/500 for readouts and notes
  (Google Fonts, with fallbacks).

### Layout (old → new)

1. **Header** → one nameplate bar: wordmark, "FM-1 EMULATOR", firmware version (`#ver`), one sunken status
   readout (`#status`, `#sdot` as its lamp), links Manual / Software / Source / Install on FM-1. PDF link stays
   reachable (in the header or next to Manual). **Panic** moves to its own maroon pill under the header's right end.
2. **Left panel** → stacked modules with header strips: **Session** (project readout, demo select + Load,
   Play / Record with lamps, position and tempo readouts), **Track** (the 8 track cells, `#tracks8`),
   **Sound** (kind, engine, preset, Prev / Next / Save / Delete), **Connections** (audio in, MIDI in, the four
   transfer buttons), **Output** (volume, output meter).
3. **GO TO** (`#gotos`) → the horizontal page rail directly under the device, all 11 pages, current one lit
   maroon, Back / Forward at its right end (`#pageback`, `#pagefwd`). Keep every page.
4. **Device** → case as above; left knobs + OCT, screen centre, KNOB 1–4 and the 12 buttons right, the "Now"
   readout (what the knobs do) under the buttons, keybed below.
5. **Monitor strip** (new, under the rail) → **Output scope** (L/R) and **Levels**, fed from a Web Audio
   `AnalyserNode` on the existing output; **Influence** matrix (8×8, from → to, dot size = amount) only from
   firmware state the emulator already exposes — if it doesn't, leave the matrix out for now and say so.
   Don't add new hooks to the engine in this pass.
6. **Eight tracks** → channel strips with kind · engine, sound name, level, mute/solo, a lamp. Per-track traces
   only if per-track signal is already available; otherwise use the strip without the trace.
7. **WEFT patcher, How it fits together, Playing it, Controls guide, footer** → restyled as in the PNG, same
   content and behaviour.

At phone width the page stacks to one column with no sideways scroll, as it does now.

## Done means

1. `sh web/emu/build.sh && node web/emu/test.mjs && python3 web/emu/bundle.py` pass locally.
2. The built page opened in a browser: plays from the keys, every button and GO TO page works, Load demo + Play
   works, no console errors, looks like `desktop-1440.png`, and works at ~400 px wide.
3. Pushed to `main` on De-Pattern/Ravel_FM1 (no `RELEASE` change); the **Pages** workflow run succeeds; the live
   site at the root shows the new look. `/software/` unchanged.
4. Screenshot of the live page back to the user.
