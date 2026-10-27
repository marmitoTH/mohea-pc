# Medal of Honor: European Assault — Xbox Recomp / PC Port Checklist

Goal of this checklist: get the original Xbox game **booting and playable** as a native Windows executable, built with
[Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp) (static x86 → C++ recompilation, HLE of the kernel/XDK,
D3D8 → Direct3D 12, DirectSound → XAudio2, SDL3 input).

Reference port: [FinestHourRemastered](https://github.com/crazyriddler/FinestHourRemastered) (Call of Duty: Finest
Hour). Only the parts of that port needed to make the game *run* are used here. Its enhancements (remaster pack,
renderer extras, widescreen/HUD, lights, cheats, Vulkan, extra languages, installer/updater) are out of scope; see the
end of this file.

---

## 0. What we know about our disc (already verified)

| Item | Value |
|---|---|
| Image | `game/Medal of Honor - European Assault (USA).xiso.iso` (XISO, XDVDFS at offset 0, game partition only) |
| Executable | `/DEFAULT.XBE`, 3,686,400 bytes, SHA-1 `f194e90b50f9f2a5259ed941971b0b4c11583eee` |
| Title | "MOH European Assault", title ID `0x4541005E`, region `0x1` (NA), version `0x1` |
| Base / entry | base `0x10000`; entry (retail-XOR'd) `0xA8E2A73E` → `0x001EF095` |
| XDK libraries | XAPILIB, D3D8, DSOUND, XGRAPHC, XBOXKRNL, LIBCMT, LIBC: **all 1.0.5849** ✅ the SDK ships `xdk/5849` |
| Sections | `.text`, `D3D`, `DSOUND`, `XGRPH`, `XPP`, `.rdata`, `.data`, `DOLBY`, `$$XTINFO`, `$$XTIMAGE`, `$$XSIMAGE`, `.XTLID` |
| Other XBEs | none (single image, no XONLINE/XNET: no Live/multiplayer XBE to switch to) |
| Engine | EA's **EAGL** renderer (not RenderWare like Finest Hour) |
| `/EAGLRM.O` | ELF relocatable (`e_machine` 8), **data only** (`.data` + `.rel.data`): EAGL "render methods" with NV2A vertex/pixel shader microcode (`*__EAGLMicroCodeVS/PS`, 560 symbols). The game loads it at runtime ("forgot to load eaglrm.o before a model ELF"). No x86 code, so static recompilation still covers all executable code. |
| Data | 394 files: `.viv` (EA BIG archives), `.str`/`.rez` (streams), `.xsh` (textures), `.xfn` (fonts), `.abk`/`.ast`/`.mus`/`.mpf`/`.bus`/`.fx` (EA audio), `.vp6` (EA VP6 movies, **not Bink**), `.stf` (subtitles), `game.cfg` |

---

## 1. Toolchain

- [x] Visual Studio 2022 Build Tools 17.12 with the C++ workload (MSVC 14.42, Windows SDK 10.0.22621)
- [x] LLVM **21.1.0** (`C:\Program Files\LLVM`, same version as FH's release toolchain); `build.bat` puts it first on PATH
- [x] CMake 3.29.5 + Ninja 1.12.1 (from the Build Tools' **C++ CMake tools** component, available inside `VsDevCmd`)
- [x] Smoke test passed: clang++ + lld, C++20, linked D3D12/DXGI/XAudio2/d3dcompiler; D3D12 device and XAudio2 created
- [x] Python 3.13.0 (for `sdk/tools/symbols/xbe2pe.py` and our own scripts)
- [x] Git 2.47.1
- [x] Ghidra 12.2 (`%LOCALAPPDATA%\Programs\ghidra_12.2_PUBLIC`, SHA-256 verified) + Temurin JDK 21.0.12 (`C:\Program Files\Eclipse Adoptium`)
- [x] XISO extraction: our Python XDVDFS reader (moves into `tools/` in §2); extract-xiso not needed
- [ ] Optional: xemu (only as a behaviour reference to compare against, not as part of the port)

## 2. Repository scaffolding

- [x] `git init` (branch `main`); template `.gitignore` plus `/game/`, `*.iso`, `*.xbe`, `mohea.toml`. **No game files, keys or generated code are committed.**
- [x] SDK added as `sdk/`: git submodule of `TGP482/XboxXBERecomp` at `0fd00442` (SDK 0.1.0, 2026-10-26)
  - Decision: **upstream SDK**. FH ships a *forked* `sdk/` with extra runtime pieces (see §8). Cherry-pick only what we need, e.g. `kernel::ShareOneCore`.
- [x] Copied `sdk/templates/game/*` into the repo root (`CMakeLists.txt`, `CMakePresets.json`, `build.bat`, `release.bat`, `config/game.toml`, `src/common/game_info.cpp`, `src/game/hooks.cpp`, `README.md`, `.gitattributes`)
- [x] `build.bat` / `release.bat`: `GAME_TITLE=Medal of Honor European Assault`, `GAME_XBE=default.xbe`, `GAME_EXE=MOHEA`
- [x] `CMakeLists.txt`: `project(mohea ...)` and `add_xbe_image(MOHEA default.xbe game.toml ${GAME_PARTITIONS} game)`; placeholder `config/game.toml` has `library = "MOHEA.dll"`
- [x] `src/common/game_info.cpp`: MOHEA names, `mohea.toml`, `mohea` log prefix (the §6 basics)
- [x] `tools/xiso.py`: `list` / `info` / `extract` for the disc image (in Git Bash, set `MSYS_NO_PATHCONV=1` so `/DEFAULT.XBE` isn't rewritten)
- [x] `cmake --preset release` configures: Zydis, toml++, Dear ImGui and SDL3 fetched into `out/build/release/_deps`
- [ ] Later: FH's CMake extras (`-fno-rtti`, `/Brepro`). `XBE_SYMBOLS` (PDB in dev builds) exists only in FH's SDK fork; use the `relwithdebinfo` preset until then

## 3. Game files in `assets/`

- [x] Extract `DEFAULT.XBE` to `assets/default.xbe` (`build.bat` refuses to run without it, and CMake hashes it at configure time)
- [x] SHA-1 matches `f194e90b50f9f2a5259ed941971b0b4c11583eee`
- [x] SHA-1 check in `build.bat` (`GAME_SHA1`, via `certutil`): stops the build with both hashes on another release; tested with the real XBE and a modified copy
- [x] Whole disc extracted to `assets/` (`DATA/`, `EAGLRM.O`, `default.xbe`; 394 files, 1.7 GB, sizes verified against the image)
  - Why not the ISO: `app::FindGame` (`sdk/runtime/app/game_files.cpp`) tries `assets/default.xbe` first and, when it matches, uses the `assets` folder as `D:` and mounts no image. The build needs `default.xbe` in `assets`, so a dev tree must have the data extracted. The ISO route is for release players, who have no `default.xbe` there.
- [x] Case-insensitive paths: host folders go through `MatchCase` (`sdk/runtime/kernel/files.cpp`), disc images through `Disc::Find` ("any case")
- Hard disk: `MountDefaults` maps `\Device\Harddisk0\Partition1..5` to `E`, `C`, `X`, `Y`, `Z` folders under the data dir; check where XAPI's `T:`/`U:` land in §7

## 4. XDK symbol matching (`xbe_symbols`)

- [ ] Run `build.bat` (first run calls `xbe_symbols assets\default.xbe sdk\xdk --library MOHEA.dll --out config\game.toml`); confirm it picks `xdk/5849`
- [ ] Review the printed **misses and ambiguous names**: each is left `= 0` and becomes a *trapping stub*. FH still had ~50 zeros, which is normal when the game never calls a function.
- [ ] Write the flat PE for Ghidra: `python sdk/tools/symbols/xbe2pe.py config/game.toml mohea.exe`
- [ ] For each miss the game **does** call (D3D, DSound streams, XAPI save/file, crt math): find it in Ghidra and fill in the address by hand
- [ ] Check `[data]` (`D3D_Device`, `D3D_pDevice`, `D3D_RenderState`, `D3D_DirtyFlags`, `D3D_Streams`, `DSound_StreamVtable*`, `XPP_GamepadType`, `XPP_MemoryUnitType`…). Every entry must be non-zero or proven unused.
- [ ] Check whether any library was built with LTCG (register arguments); if so, fill `[abi]`
- [ ] Commit `config/game.toml` (addresses only, no game code)

## 5. First recompile (analysis + codegen)

- [ ] `cmake --build out\build\release` until `generated/game/*` is produced and compiles (first build can take 30–90 min)
- [ ] Fix analysis gaps reported or found later at run time:
  - [ ] `[functions]` / `[[function]]` for entries reached only indirectly (FH needed 3, including its job thread `sub_00068D90`; expect EAGL render-method callbacks, thread entry points and vtables here)
  - [ ] `[[switch]]` for wrong jump-table targets
  - [ ] `[xbe] data_ranges` for data embedded in code sections
- [ ] Check the `DOLBY` section (flags `0x26`, the DSP effects image) is **not** treated as code; only `.text`, `D3D`, `DSOUND`, `XGRPH`, `XPP` should be in `code_sections`
- [ ] Link `MOHEA.exe` + `MOHEA.dll`

## 6. Minimal `app::kGame` (`src/common/game_info.cpp`)

- [ ] `title`, `short_title`, `name`, `log`, `settings` (`mohea.toml`), `splash`, `missing` message
- [ ] `images = {{"default.xbe", "MOHEA"}}` (single image)
- [ ] Leave `load/save/init/loaded/window/message/ui/pad/rumble` null at first; add `init` only for the core-sharing setup from §8
- [ ] Console language/region: NA disc. Make sure the emulated EEPROM gives a language the disc has (English; `START_US.XSH`, `STF/US`)

## 7. First boot: get past the kernel and into the game loop

- [ ] Launch; read the log (`<log>*.txt`) and `CrashDumps/` beside the exe
- [ ] Fix every **trapping stub** hit, either by finding the real symbol (§4) or by implementing/stubbing it in `src/game`
- [ ] Note any **kernel exports** the SDK does not implement yet (the game imports from XBOXKRNL 5849) and implement them in the runtime
- [ ] Drive mapping: `D:\` → game data (ISO or folder), `T:\` (title data) / `U:\` (user saves) → `hdd/` beside the exe, `Z:\` → scratch/cache
- [ ] Threads: verify all game threads start (main, loading/streaming, audio). Check none are stuck in a busy-wait (see §8).
- [ ] Milestone **M1**: window opens and the D3D8 device is created without a trap

## 8. Single-core assumptions (lessons from Finest Hour; port-critical)

On the Xbox all threads time-sliced **one** core; on a PC they run truly in parallel. FH froze or burned a full core
until it fixed this ([`src/game/jobs.cpp`](https://github.com/crazyriddler/FinestHourRemastered/blob/main/src/game/jobs.cpp)).

- [ ] Profile for threads spinning at 100% CPU on a queue/flag (`XBE_PERF=1`, a profiler, or the debugger)
- [ ] For each spin loop: add a `[[hook]]` at the idle path that sleeps/yields (FH: `cod_job_thread_idle` at the job thread's empty-queue branch)
- [ ] For each spin-wait on another thread releasing something: yield the turn (FH: `cod_job_thread_wait`)
- [ ] If threads rely on time-sliced ordering (one finishes before the other looks), port FH's `kernel::ShareOneCore` (ticket lock, in FH's `sdk/runtime/kernel/threads.cpp`) and register the affected thread entry points from `kGame.init`
- [ ] Watch for **game bugs that only crashed rarely on hardware** (FH: RenderWare worlds freed under live objects, `src/game/worlds.cpp`). Use a poisoned heap (FH `debug_heap.cpp` pattern: fill freed blocks with `0xDD`) to surface use-after-free early.

## 9. Graphics (EAGL on the SDK's D3D8 → D3D12 renderer)

- [ ] Milestone **M2**: first frame presented (EA logo, then THX `.vp6`, or `DATA/LOADING/START_US.XSH` loading screen)
- [ ] `EAGLRM.O` render methods: confirm every vertex program (`*__EAGLMicroCodeVS`) passes through `D3DDevice_CreateVertexShader`/`LoadVertexShaderProgram` and the SDK's NV2A VS → HLSL translator handles all of them (560 symbols, families: `Moh3_Cpt_*`, `Moh3_Msh_*`, `Moh4_*`, `Particle`, `SingleDraw*`)
- [ ] Pixel shaders (`*__EAGLMicroCodePS`): check the pixel combiner translation covers them (env-maps, gloss, lightmaps, refractive, alpha maps)
- [ ] Check whether EAGL writes the **push buffer directly** (raw NV2A methods, `D3DDevice_BeginPush`/`EndPush`, `RunPushBuffer`, or writes through `CDevice_Push`). If so, those paths need HLE support or a game hook.
- [ ] Textures (`.xsh`): swizzled/compressed Xbox formats decode correctly; palettes (P8) if used
- [ ] Render targets / back buffer / depth formats, `D3DDevice_Swap`/`Present`, VBlank waits (`BlockUntilVerticalBlank`), gamma ramp
- [ ] Frame pacing: lock to the game's native rate (verify 30 vs 60 Hz) so game logic tied to VBlank/frame count runs at the right speed
- [ ] Use `XBE_D3D12_DEBUG=1` and `XBE_CAPTURE_DIR` + F3 for frame captures when something draws wrong

## 10. Movies (VP6)

- [ ] VP6 is decoded by **game code** (EA's player inside the XBE), so it is recompiled like everything else. The SDK's Bink fixes do **not** apply.
- [ ] Verify playback speed/timing (timer-based vs frame-count-based) and A/V sync for `eagmlogo`, `thxlogo` and the mission intros
- [ ] Subtitles (`STF/US/*.stf`) render

## 11. Audio

- [ ] DirectSound HLE → XAudio2: buffers, **streams** (`CDirectSoundStream_*`), mix bins, Xbox ADPCM
- [ ] `DOLBY` section = DSP effects image: confirm the upstream SDK's `dsp_image` support loads it (or stub it cleanly)
- [ ] EA audio formats (`.abk` banks, `.ast`/`.mus` streams, `.mpf` music controller, `.bus`, `audiofx0.fx`) are parsed by game code; check they reach DSound and play at the right pitch/rate
- [ ] Milestone **M3**: front-end music and menu SFX play

## 12. Input

- [ ] XPP / `XInputGetState` HLE maps an SDL3 controller to the Xbox gamepad (incl. analog buttons and triggers); hot plug; rumble (`XInputSetState`)
- [ ] Memory unit enumeration (`XPP_MemoryUnitType`) reports none/empty without crashing
- [ ] Milestone **M4**: navigate the main menu with a controller

## 13. Saves, settings, storage

- [ ] Save games via XAPI (`XCreateSaveGame`/`XFindFirstSaveGame`…) land in `hdd/` and reload across restarts
- [ ] Profile/options save; `T:\` title data persists
- [ ] Settings file (`mohea.toml`) is written beside the exe

## 14. In-game boot milestones (definition of "bootable")

- [ ] **M5**: start a campaign: the mission intro movie plays and the first level (`DATA/1/1_1`: `level_a.str`, `asyncstr.rez`, `loading.viv`) streams in
- [ ] **M6**: in control of the player: rendering, audio, input all working in-level
- [ ] **M7**: reach a checkpoint, die and restart, save and load
- [ ] **M8**: finish a mission and transition to the next (level unload/reload: watch for freed-memory crashes like FH's)
- [ ] **M9**: full campaign playthrough without crashes; play every movie once

## 15. Packaging (after it boots)

- [ ] `release.bat`: release builds recompile `<exe>.dll` on the **player's PC** from their own XBE (ships no game code)
- [ ] XBE hash/title-ID check at startup, with a clear message for unsupported releases (PAL/other dumps)
- [ ] README with build steps and the "provide your own legally acquired copy" notice

---

## Out of scope (FH enhancements, not needed to boot)

Remaster texture/model pack, per-pixel lighting/shadows/SSR/HDR/AA, Vulkan backend, widescreen Hor+ and HUD layout,
dynamic lights from sprites, cheats, extra translations, keyboard and mouse look (`controls.cpp`, nice to have after M9),
skip-intros, installer/updater. FH-specific: `offline.cpp` (redirects its Live/System Link menu). MoH:EA's XBE has no
online libraries, but check the main menu for any multiplayer entry that would try to launch something missing.
