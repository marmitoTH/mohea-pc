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
- [x] **SDK fixes are patches in this repo**: `sdk/` stays pinned to upstream; `sdk_patches/NNNN-*.patch` (a short explanation above the diff, LF endings) are applied by `CMakeLists.txt` at configure time, in name order, each skipped when already in the checkout, a clear error when one no longer applies. `.gitmodules` has `ignore = dirty` so the patched submodule does not show as changed
  - New patch: edit under `sdk/`, then `git -C sdk diff > sdk_patches/NNNN-name.patch` (add the explanation on top)
  - On an SDK update: revert `sdk/` (`git -C sdk checkout .`), move the submodule, reconfigure, refresh any patch that fails
  - `0001-d3d8-SetViewport-accepts-null.patch`: the §7 boot fix
  - `0002-app-window-size-and-render-scale.patch`: windowed, the render resolution follows the window
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

- [x] Ran `xbe_symbols assets\default.xbe sdk\xdk --library MOHEA.dll --out config\game.toml` (built alone with `cmake --build out\build\release --target xbe_symbols`); it picked `xdk/5849`, entry `0x1EF095`, `code_sections = [.text, D3D, DSOUND, XGRPH, XPP]` (`DOLBY` left out)
- [x] Result: 127 of 243 functions, 10 of 12 data. The one ambiguous name (`IDirectSoundBuffer_SetFilter`) and 4 others were resolved through `.XTLID`
- [x] Reviewed the 116 misses. **113 are not linked into this XBE**:
  - Network (XNet, XOnline, Winsock): no such libraries. Bink: not used (VP6 movies)
  - DSound: every miss with an XTLID id is absent from the XBE's `.XTLID` (154 entries). `DirectSoundCreateStream` is absent, so the `CDirectSoundStream_*` functions and both `DSound_StreamVtable*` data symbols are unused: **the game uses no DSound streams**
  - D3D misses with signatures: none of the 33 unnamed D3D functions the game calls look like them (no `LoadVertexShaderProgram` 0x1E9C, `SwitchTexture`, `GetRenderTarget2` 0x1A04 or `DrawVerticesUP` pattern)
  - CRT `sin`/`cos`/`tan`/`atan`/`sqrt`/`log`/`log10`: not linked. The `fsin`/`fcos` stubs at `0x2B1345..0x2B13AA` are **EA's degree-based float helpers** (×π/180), recompiled as plain x87
- [x] **3 real misses, filled in by hand** (the SDK has no signature for them), commented in `config/game.toml`:
  - `D3DDevice_BeginPush = 0x2DDEB0`, `D3DDevice_EndPush = 0x2DDED0`: **EAGL draws through raw push-buffer commands** (`BEGIN_END` + `INLINE_ARRAY`; 4 BeginPush calls at `0x1D0CFC`…, 5 EndPush calls). The SDK's `RunCommands` handles exactly that
  - `D3DDevice_SetPalette = 0x2D9200`: 9 calls, so **P8 palettized textures are used**
- [x] Unnamed library code the game calls runs as recompiled guest code by design: D3D functions that write the push buffer (e.g. `SetVertexData2f/4f/Color`, the clear values at `0x2DDE40`) are read by the SDK's push-buffer walk; XPP's `XGetDevices`/`XGetDeviceChanges`/MU mounting read the `XPP_*Type` data the SDK fills; XInput core is matched
- [x] `[data]`: 10 of 12 named; only the two unused `DSound_StreamVtable*` are 0
- [x] `[abi]`: empty; `xbe_symbols` reported no changed conventions (no LTCG libraries)
- [x] Flat PE for Ghidra: `python sdk/tools/symbols/xbe2pe.py config/game.toml out/ghidra/mohea.exe` (130 exports now; rerun after §5 to export every `sub_XXXXXXXX`)
- [x] Commit `config/game.toml` (addresses only, no game code)
- Watch in §7/§9: EAGL's EndPush path. The SDK drops pushed vertex-array draws (methods 0x1720–0x1817) with a log line, and stops at non-method headers

## 5. First recompile (analysis + codegen)

- [x] `cmake --build out\build\release`: recompile + compile + link in **~1 minute** on this machine, exit 0
  - Analysis: 732 direct functions, +6777 pointer seeds (1650 rejected), +6739 gap fill → **22,759 functions, 900,275 instructions**, 2,882,214 of 3,069,352 code bytes (94%), **473 jump tables**, 13 callback entries, no C++ EH
  - 146 MB of C++ in `generated/game` (64 partitions)
  - Warnings: 77, all in third-party Dear ImGui (`-Wnontrivial-memcall`, `-Wdeprecated-declarations`); none in generated or our code
- [x] **19 overlapping decodes** reviewed: all harmless; none of the functions involved is called from generated code
  - `.text 0x8000D..0x80065`: the real `sub_0007FCA0` is decoded whole; pointer seeds made bogus entries `sub_0008000F` / `sub_00080061` / `sub_00080063` / `sub_00080065` mid-instruction
  - `D3D sub_002E77EF`: a float table decoded as code; `DSOUND sub_002F46FF`: `00 00` data decoded as code
  - Not marked as `data_ranges` (exact table extents unknown, nothing gained at run time)
- [ ] Fix analysis gaps found at run time (they only show when the game reaches them):
  - [ ] `[functions]` / `[[function]]` for entries reached only indirectly (FH needed 3, including its job thread `sub_00068D90`; expect EAGL render-method callbacks, thread entry points and vtables here)
  - [ ] `[[switch]]` for wrong jump-table targets
  - [ ] `[xbe] data_ranges` for data embedded in code sections
- [x] `DOLBY` (the DSP effects image) is **not** code: `code_sections = [.text, D3D, DSOUND, XGRPH, XPP]`
- [x] Linked `out/build/release/MOHEA.exe` (5.2 MB) + `MOHEA.dll` (12.8 MB, the recompiled game)
- [x] Ghidra PE regenerated with every recompiled function: `out/ghidra/mohea.exe`, 22,759 exports

## 6. Minimal `app::kGame` (`src/common/game_info.cpp`)

- [x] `title`, `short_title`, `name`, `log` (`mohea`), `settings` (`mohea.toml`), `splash`, `missing` message (done in §2)
- [x] `images = {{"default.xbe", "MOHEA"}}` (single image)
- [x] `options = {}` and every hook null for now; `init` gets the core-sharing setup only if §8 needs it
- [x] Console settings: the upstream kernel's `ExQueryNonVolatileSetting` (`sdk/runtime/kernel/xboxkrnl.cpp`) is fixed to what this disc needs, so no override is required (upstream has no `console_language` field; that is FH's fork)
  - Language English (1): the disc's `START_US.XSH` / `STF/US`
  - Game region North America (1) = the certificate's region `0x1`; AV region NTSC-M; DVD region 1
  - Video flags: 480p, plus widescreen when the output is 3:2 or wider (`app::Widescreen`). On a 16:9 monitor the game is told it has a widescreen TV; check in §9 what MoH:EA does with it
- [x] `mohea.toml` (written beside the exe): `display_mode` (fullscreen/borderless/windowed), `width`/`height` (0 = desktop), `render_percent`, `fps_cap`, `vsync`, `anisotropic_filtering`, `custom_textures`, `dump_textures`, `texture_dump_format`, `join_ip`

## 7. First boot: get past the kernel and into the game loop

- [x] Launch; read the log (`out/build/release/logs/mohea.log`) and `CrashDumps/` beside the exe
  - Release builds log only errors and reports (`NDEBUG`); the `debug` preset logs info/warnings too
  - Crash triage: `[esp+N]` return addresses in the log give the guest call chain; the host fault address → `MOHEA.dll` RVA (from the dump's module list) → guest function via the DLL's exported `X86FuncMappings` table → x64 disassembly around it, which carries guest constants. `-g` is link-only, so the PDB has no line tables
- [x] Dev loop: an exe-only relink left `MOHEA.dll` older than the exe, so its first start **recompiled the game code itself** (the release "build on the player's PC" path, ~35 s, then a "Start the game now?" prompt). `CMakeLists.txt` now touches the DLL and drops the stale stamp after each exe link
- [x] Boot crashes fixed (3):
  1. `D3DDevice_SetBackBufferScale` (`0x2DC070`, unnamed, guest code) → helper `0x2DACB0` read `CDevice_VertexShader` (0) flags: the library's device init stores the FVF shader object there. Fix: `[data] D3D_FvfShader = 0x2EBBC8` (from `0x2E0A5B`); the SDK's CreateDevice then sets it
  2. The same helper calls `D3DDevice_SetViewport(NULL)` ("set the current viewport again, scissors off"); **the SDK's HLE dereferenced NULL**. Fix: in `sdk/runtime/xdk/d3d8.cpp` (an SDK change, see §2's open item)
  3. `D3DDevice_Reset` (`0x2D8750`) ran as guest code and polled NV2A registers through the uninitialised miniport (`[0 + 0x3214]`). The SDK has a Reset HLE but **no 5849 signature** for it. Fix: `[symbols] D3DDevice_Reset = 0x2D8750`
- [x] No trapping stubs or missing kernel exports hit up to the front end
- [x] Drive mapping: `D:` = `assets/`; hard disk under `out/build/release/hdd/` (`E`, `C`, `X`, `Y`, `Z`). The game writes `T:` → `hdd/E/TDATA/4541005e` and saves to `U:` → `hdd/E/UDATA/4541005e/<save>/` (`DATA`, `SaveMeta.xbx`, `TitleMeta.xbx`, images)
- [x] Threads: 43 host threads; in the front end the process uses ~27% of one core, busiest thread 19%: **no busy-wait** so far (re-check in game, §8)
- [x] Milestone **M1**: window opens and the D3D8 device is created without a trap (RTX 4060, D3D12)
- [x] Beyond M1: THX logo movie plays, then the front end (Controller Settings screen) renders correctly by 40 s
- [x] Rumble stuck on (reported while testing): the game sets the left motor to `(strength << 8) + 0xFF` (`0xC5AD5`), so `0x00FF` is its "off"; the Xbox pad only uses the high byte. Fix: `kGame.rumble` in `src/common/game_info.cpp` keeps the high byte, scaled to full range

## 8. Single-core assumptions (lessons from Finest Hour; port-critical)

On the Xbox all threads time-sliced **one** core; on a PC they run truly in parallel. FH froze or burned a full core
until it fixed this ([`src/game/jobs.cpp`](https://github.com/crazyriddler/FinestHourRemastered/blob/main/src/game/jobs.cpp)).

- [ ] Profile for threads spinning at 100% CPU on a queue/flag (`XBE_PERF=1`, a profiler, or the debugger)
- [ ] For each spin loop: add a `[[hook]]` at the idle path that sleeps/yields (FH: `cod_job_thread_idle` at the job thread's empty-queue branch)
- [ ] For each spin-wait on another thread releasing something: yield the turn (FH: `cod_job_thread_wait`)
- [ ] If threads rely on time-sliced ordering (one finishes before the other looks), port FH's `kernel::ShareOneCore` (ticket lock, in FH's `sdk/runtime/kernel/threads.cpp`) and register the affected thread entry points from `kGame.init`
- [ ] Watch for **game bugs that only crashed rarely on hardware** (FH: RenderWare worlds freed under live objects, `src/game/worlds.cpp`). Use a poisoned heap (FH `debug_heap.cpp` pattern: fill freed blocks with `0xDD`) to surface use-after-free early.

## 9. Graphics (EAGL on the SDK's D3D8 → D3D12 renderer)

- [x] Milestone **M2**: first frame presented (THX `.vp6` at ~10 s, the front end at ~40 s)
- [ ] `EAGLRM.O` render methods: confirm every vertex program (`*__EAGLMicroCodeVS`) passes through `D3DDevice_CreateVertexShader`/`LoadVertexShaderProgram` and the SDK's NV2A VS → HLSL translator handles all of them (560 symbols, families: `Moh3_Cpt_*`, `Moh3_Msh_*`, `Moh4_*`, `Particle`, `SingleDraw*`)
- [ ] Pixel shaders (`*__EAGLMicroCodePS`): check the pixel combiner translation covers them (env-maps, gloss, lightmaps, refractive, alpha maps)
- [x] EAGL writes the **push buffer directly**: yes, through `BeginPush`/`EndPush` (named in §4; the SDK's EndPush runs `BEGIN_END` + `INLINE_ARRAY` draws)
- [ ] Verify at run time that nothing EAGL pushes is dropped (log lines `d3d: pushed commands ...` / `EndPush primitive ... dropped`)
- [ ] Textures (`.xsh`): swizzled/compressed Xbox formats decode correctly; palettes (P8) if used
- [ ] Render targets / back buffer / depth formats, `D3DDevice_Swap`/`Present`, VBlank waits (`BlockUntilVerticalBlank`), gamma ramp
- [ ] Frame pacing: lock to the game's native rate (verify 30 vs 60 Hz) so game logic tied to VBlank/frame count runs at the right speed
- [ ] Use `XBE_D3D12_DEBUG=1` and `XBE_CAPTURE_DIR` + F3 for frame captures when something draws wrong

## 10. Movies (VP6)

- [ ] VP6 is decoded by **game code** (EA's player inside the XBE), so it is recompiled like everything else. The SDK's Bink fixes do **not** apply.
- [x] `thxlogo.vp6` decodes and displays (first boot)
- [ ] Verify playback speed/timing (timer-based vs frame-count-based) and A/V sync for `eagmlogo`, `thxlogo` and the mission intros
- [ ] Subtitles (`STF/US/*.stf`) render

## 11. Audio

- [ ] DirectSound HLE → XAudio2: buffers, mix bins, Xbox ADPCM. **No DSound streams**: `DirectSoundCreateStream` is not linked (§4), so EA's streamed audio goes through buffers
- [x] `DOLBY` section = DSP effects image: the SDK loads it (`dsound: effects image ...`: reverb, gain, mixers, crosstalk; 5 effects "not reproduced")
- [ ] EA audio formats (`.abk` banks, `.ast`/`.mus` streams, `.mpf` music controller, `.bus`, `audiofx0.fx`) are parsed by game code; check they reach DSound and play at the right pitch/rate
- [x] Milestone **M3**: front-end music and menu SFX play; in-game sound correct (playtest 2026-10-27)

## 12. Input

- [ ] XPP / `XInputGetState` HLE maps an SDL3 controller to the Xbox gamepad (incl. analog buttons and triggers); hot plug
- [x] Rumble (`XInputSetState`): high-byte fix in place (§7); idle is silent (confirmed in the playtest)
- [ ] Memory unit enumeration (`XPP_MemoryUnitType`) reports none/empty without crashing
- [x] Keyboard and mouse (`src/common/pc_input.*`, a PC addition): keys and mouse buttons as the controller on port 0 (connected without a controller), rebindable in `mohea.toml` `[keys]`; cursor hidden and held at the window's centre while the game has the focus
- [x] Mouse look (`src/game/controls.cpp`): the mouse's motion each frame is added as an angle to the player's turn (`sub_001149E0` yaw, `sub_00114B60` pitch), past the stick's dead zones, the look handler's clamp, response curve and flick halving. Verified on foot, aiming down sights and on mounted weapons (2026-10-27); the controller keeps its own feel. The input chain is written up at the top of `controls.cpp`; `MOHEA_WATCH=actions` (`src/game/watch.cpp`) is the data-breakpoint tool that found it
- [x] Milestone **M4**: navigate the main menu with a controller

## 13. Saves, settings, storage

- [ ] Save games via XAPI (`XCreateSaveGame`/`XFindFirstSaveGame`…) land in `hdd/` and reload across restarts
- [ ] Profile/options save; `T:\` title data persists
- [ ] Settings file (`mohea.toml`) is written beside the exe

## 14. In-game boot milestones (definition of "bootable")

First playtest (2026-10-27, release build, windowed, controller): 15.5 min, mission 1 finished and mission 2 started, deaths and restarts, no crash, clean exit; graphics, sound and input as expected.

- [x] **M5**: start a campaign: the mission intro movie plays and the first level (`DATA/1/1_1`: `level_a.str`, `asyncstr.rez`, `loading.viv`) streams in
- [x] **M6**: in control of the player: rendering, audio, input all working in-level
- [x] **M7**: reach a checkpoint, die and restart (playtest: died and carried on; save/load across a restart still to check)
- [x] **M8**: finish a mission and transition to the next (level unload/reload: watch for freed-memory crashes like FH's)
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
