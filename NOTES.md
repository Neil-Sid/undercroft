# UNDERCROFT — engineering notes

A first-person, grid-based, turn-based roguelike in the style of a PS1 dungeon crawler.
Native C++20, Win32 + OpenGL 3.3 (own loader), GDI for fonts, GDI+ for PNG, waveOut for
sound. No third-party libraries. Ten floors; the Lich waits on depth 10.

## Build and run

```
build.bat                                  (PowerShell or cmd, in this folder)
build.bat out/me.exe out/me_sim.exe        (custom output names)
```

`build.bat` compiles the game (`undercroft.exe`) and the headless balance bot (`sim.exe`) with
MSYS2 ucrt64 g++ 16. Use custom output names when several builds may run at once. From Git Bash,
call it as `cmd //c "<full Windows path>\build.bat"`; a relative path fails there.

### WebAssembly build

```
powershell -File build_web.ps1        writes web/undercroft.js + web/undercroft.wasm (about 20 s)
```

Serve `web/` over http (launch config `undercroft-web`, port 5340) and open it in a browser. It uses the
MSYS2 Emscripten package; `tools/emxx.py` works around its refused `.exe` launchers, and
`BINARYEN_CORES=1` stops `wasm-opt` from deadlocking. The web build is the same game code with a
different platform layer:

- `web_main.cpp` replaces `main.cpp` (canvas, keys, `emscripten_set_main_loop`). Browser key codes
  equal Windows virtual keys, so `keys.h` only defines the constants.
- `gl.h` maps to WebGL 2. Shaders get their `#version` header in `compile()`; affine interpolation is
  done by multiplying varyings by w instead of `noperspective`, which GLSL ES lacks.
- `paint_web.cpp` reads `src/fonts.inc`, baked from the GDI fonts by `tools/bake_fonts.cpp`. Re-bake it
  if the fonts in `Renderer::init` change.
- `audio.cpp` synthesises without threads and Web Audio pulls blocks of the same mix via `audio_pull`.
- `sounds/` is preloaded into the page (`undercroft.data`, about 12 MB, mostly the music track).

### Publishing

`web/` is a complete static site (game, `undercroft.data`, `credits.html`); nothing else needs deploying.
From `web/`: `npx vercel login` once, then `npx vercel deploy --prod`. Share the production domain from
the project's Settings → Domains; Vercel's generated deployment URLs are login-protected.

### Recorded sounds

`sounds/sounds.txt` maps game slots (`door`, `ogre.hurt`, `step.crypt`, `music.floors`, ...) to WAVs in
`sounds/`, with an optional `@pitch` per file. Edit it to swap a sound, then copy the files in:
`python tools/import_sounds.py <ffmpeg.exe>` converts what it names from the CC0 packs unzipped under
`assets/download/` to 22 kHz mono WAV. Sources and licences: `sounds/CREDITS.md`. A slot that isn't
listed, or whose files are missing, keeps its synthesised sound; the desktop exe reads `sounds/` next to
itself.

### Screenshot harness (no window, fixed 60 fps, deterministic)

```
undercroft.exe --shot out/x.png [--at 1.5] [--seed 7] [--depth 3] [--keys wwqf>] [--spawn skeleton,chest]
               [--dist 1] [--anim walk|attack|hurt|die] [--screen title|inventory|map|help|dead|won]
               [--scale 1|2] [--reveal] [--torch 300] [--yaw 1] [--wield axe] [--give "wand of lightning"]
undercroft.exe --viewer skeleton [--anim walk] [--yaw 1] [--shot out.png]
```

- Output is the 640x480 frame times `--scale`. Read the PNG to look at it.
- `--keys`: one key every 0.3 s: `wasd` move, `qe` turn, `f` attack, `.` wait (and search), `>` descend
  or use, `i` pack, `m` map, `<` escape. In the pack a letter picks a row.
- `--spawn`: comma list of monster or item names (substring match), placed on free cells ahead. Also
  `chest`, `fountain`, `stairs`, `secret`, `trap` and `trap-dart|-alarm|-pit|-teleport[-hidden]`.
- `--anim` forces every live monster to loop that animation. `--viewer` shows one model on a dark stage.
- Freshly built exes are occasionally blocked by Windows Smart App Control ("Permission denied" from
  bash). Rebuild under another output name rather than touching the security setting.

### Balance bot

`sim.exe --runs 300 [--seed 1] [--verbose]`: a scripted bot plays whole runs and prints win rate,
depth reached, causes of death and per-floor tallies. `--check 300` validates the generator over 300
seeds x 10 floors, `--dump 7 5` prints the plan of seed 7 depth 5, `--replay 7` checks determinism.

## Architecture

| Layer | Files | Notes |
|---|---|---|
| Rules (no Windows, no GL) | `content.*` `world.h` `dungeon.cpp` `fov.cpp` `game.*` `sim.cpp` | Deterministic per seed. `Game::act/use/drop` mutate state and emit `Event`s. |
| Presentation glue | `app.*` `main.cpp` | Screens, input, camera, replays events as animation and sound. |
| Renderer | `render.*` | PS1 pipeline: 320x240 world, 640x480 UI layer, sharp-bilinear present. |
| Art | `textures.*` `models.*` `scene.*` | Procedural 64x64 textures in one atlas, segmented low-poly models, level mesh. |
| UI | `ui.*` | HUD, pack, automap, title/death/victory/help cards. |
| Audio | `audio.*` `synth.h` `audio_sfx.cpp` `audio_music.cpp` `sounds/` | CC0 recordings listed in `sounds/sounds.txt` replace their slots at startup; everything else is synthesised. `synth.h`: DSP toolkit. `audio_sfx.cpp`: effects, a voice set per monster (`playCreature(kind, Cue)`: notice, attack, impact, hurt, die, step), footsteps per floor, ambience beds. `audio_music.cpp`: one-note sampled instruments and five composed songs in a tiny text notation, played by a sequencer that pitches samples like the SPU. `audio.cpp`: mixer, hall reverb, limiter, waveOut/Web Audio. Offline check: `src/audio_test.cpp` writes WAVs and spectrograms to out/audio. |

### The PS1 look (keep it)

- World at 320x240; vertices snapped to that pixel grid in the vertex shader.
- `noperspective` UVs (affine warping); big faces are subdivided so it stays tolerable.
- Per-vertex Gouraud lighting (torch + braziers, max 8 lights), per-vertex depth fog.
- 15-bit colour with the PS1's 4x4 dither matrix. Nearest filtering, no mipmaps.
- Textures are 64x64, quantised to 16 colours (like 4-bit CLUTs). Texel alpha < 0.5 is cut
  out; 0.5–0.9 is emissive (ignores lighting).
- Models are rigid parts on a joint tree (no skinning), animated procedurally.

### Contracts between modules

- `MonsterKind`, `ItemKind`, `ModelId`, `Sound`, `EventType`, `Tex`, `Tile`, `DecorKind`, `TrapKind` are
  shared. Never reorder or remove entries; add new ones only immediately before `Count`
  (for `MonsterKind`, before `Lich`, and mirror it in `ModelId`).
- `ModelId` starts with monsters in `MonsterKind` order, then weapons/armour in `ItemKind` order.
- Event meanings are documented in `game.h`. The presentation must handle every event type.
- Shared headers are append-only: add fields/functions, never rename or change signatures that
  other modules use.

## Gotchas

- `windows.h` defines macros `near`, `far`, `OPAQUE`, `TRANSPARENT`, `min`, `max`, ... Don't use
  them as identifiers (render.h pulls in windows.h through gl.h).
- In the Bash tool, heredocs drop backslashes and cut at a bare ` & `. Edit source with the
  Write/Edit tools, not shell heredocs or `sed` with escapes.
- `cmd //c build.bat` fails with a relative path from Git Bash; use the full quoted path.
- Text uses GDI fonts rasterised without antialiasing (Palatino Linotype, Georgia). Glyphs exist for
  ASCII plus the middle dot `·`.
