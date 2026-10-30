# Crashes

Crashes seen while playing, what is known about each, and how to trace one. Add a new entry at the top of
[Known crashes](#known-crashes) for each new crash, even when its cause is unknown. A second sighting of the same
crash goes into its existing entry.

## Tracing a crash

What a crash leaves behind, beside the exe (`out/build/<preset>/`):

- `logs/mohea_<n>.log`: the exception, the guest registers and a scan of the guest stack for return addresses
  (`[esp+N] 00XXXXXX`). Release builds log only errors; play with the **dev** build (`cmake --build out\build\dev`),
  which is optimized and logs everything, the guard below included.
- `CrashDumps/MOHEA.<date>_<time>.dmp`: a minidump.

From the dump to the guest function:

1. The exception address and the base of `MOHEA.dll` come from the dump's exception and module lists (any minidump
   reader; WinDbg `.ecxr` and `lm`). The fault's RVA is the address less that base. A fault in `MOHEA.exe` is in the
   runtime (the SDK), not in the game's code.
2. `llvm-symbolizer --obj=out\build\dev\MOHEA.dll 0x<RVA>` names the generated function and line
   (`generated/game/x86_recomp.<n>.cpp`). The comments there are the guest's instructions, with their addresses.
3. The log's stack addresses are guest return addresses: the function each lands in, and the call just before it, are
   in the same comments. A flat listing helps: every `X86_FUNC(__imp__sub_...)` line and every `// XXXXXXXX ...`
   comment line of the generated files, in address order.
4. The dump must be read against the `MOHEA.dll` it crashed with: note the DLL's time (the dump lists it) before
   rebuilding.

Tools in `src/game/` for following a problem at run time:

- `pool_guard.cpp` (the `pool_guard` setting in `mohea.toml`: on by default in dev and debug builds, off in release;
  set it to `true` for a tester on a release build, and restart): checks the game's fixed-size pools at every block handed out and taken
  back, and logs a bad one as it happens, with the pool's last 64 operations. See the 2026-10-28 entry.
- `watch.cpp` (`MOHEA_WATCH=actions`): data breakpoints, and the call chain of each access.

## Known crashes

### 2026-10-29: out of sound buffers (mission 1_21)

**Status:** open. Seen twice, both times with more soldiers in the world than the game keeps: likely each soldier
holds sound buffers until it is taken away.

- **Second sighting (2026-10-30):** 267 s into a dev build session (log `mohea_152.log`, dump
  `MOHEA.2026.10.30_11.35.55.dmp`), the same fault (`MOHEA.dll+0x5134E1` in `sub_0019624E`, `ecx` `00523984`), with an
  experimental keep-the-fallen option keeping up to 20 dead soldiers as whole objects (dropped for it). The first
  sighting had `squad_immortal` on: a squad that never dies. Theory: a soldier's sounds (voice, effects) keep their
  DirectSound buffers until the soldier is taken away, so more soldiers about, dead or alive, empty the pools.

- **When:** 140 s into a dev build session (log `mohea_119.log`, dump `MOHEA.2026.10.29_19.04.25.dmp`), in mission 1_21,
  on guest thread 14. The new `unlimited_ammo` option was on for the first time, with `squad_immortal` and
  `player_invincible`. The pool guard reported nothing.
- **Fault:** `MOHEA.dll+0x5134E1`, in `sub_0019624E`, reading guest `00000008`:

  ```
  00196286 call 0x0019B727       ; pop a buffer from the chosen list: none (esi 0)
  0019628E push [esi+0x08]       ; faults
  ```

- **The buffers:** at a level's start (`sub_00197xxx`) the game creates two pools of DirectSound buffers (`sub_00196143`,
  48 kHz, Xbox ADPCM or 16-bit PCM), each a free list at `0x523990` and `0x52399C` (count at +8): `0x94` less
  `[0x523A10]` of one, 28 of the other. A sound takes a free buffer of its format. When its format has none,
  `sub_0019624E` takes one from the other list (the one with more), releases its DirectSound buffer and makes one of the
  needed format. With both lists empty it picks no list (`eax` -1, the "list" at `0x523984`), pops nothing and reads
  `[0+8]`.
- **Call chain:** `sub_00081190` → `sub_001AD630` → … → `sub_001957AE` → `sub_0019AFC8` → `sub_0019AB81` →
  `sub_0019F360` → `sub_0019624E`.
- **Theories:**
  1. Buffers are not given back: a sound's end goes unnoticed (for example, the SDK's DirectSound reporting a buffer as
     still playing), so the pool drains over time. Firing without reloads (`unlimited_ammo`) would drain it faster.
  2. Too many sounds at once. Unlikely on its own with a pool of over a hundred.
- **Next steps:**
  1. Log both lists' free counts every few seconds in a dev build: a steady fall points to a leak, a dip under heavy
     fire to a burst.
  2. If it leaks, find where the game gives a buffer back, and what it checks (a DirectSound status call) to decide a
     sound has ended.
  3. Whatever the cause, `sub_0019624E` could be made to fail without crashing. That needs its callers read first: they
     pop from the list it has just filled.

### 2026-10-28: corrupt free list in a game pool (mission 1_8)

**Status:** fixed by `src/game/requests.cpp` (a request list's node taken back twice), confirmed in play: on
2026-10-30 (log `mohea_145.log`, mission 1_21) it left alone two requests taken out again at 206 s and 274 s, both
from the completion callback (`0x3C7A9`), with no pool error after and no crash. Seen twice before the fix; the second
time, on 2026-10-29, the pool guard caught it (below).

- **When:** 744 s into a dev build session (log `mohea_111.log`, dump `MOHEA.2026.10.28_21.04.50.dmp`), playing
  mission 1_8, on guest thread 14 (the game's main thread). The build also had an experimental sprint feature, since
  removed. Its hooks skipped whole fire and weapon switch calls and moved the weapon's projection; none of them hands
  out or takes back memory, so it is unlikely but not ruled out.
- **Fault:** `MOHEA.dll+0x453881`, in `sub_001569E0`, reading guest `8BF18B56`:

  ```
  001569F2 mov eax, [esi+0x18]   ; the pool's free list head: 8BF18B56
  001569F5 mov ecx, [eax]        ; faults
  ```

  `ecx`/`esi` = `003A65A8`, the pool.
- **The pool:** `003A65A8` is a global pool of 0x28-byte blocks, 6 a chunk, set up at startup (`sub_002CF6E0`).
  Objects made by `sub_0003CD90` (vtable `0x302800`) take their blocks from it (`sub_0003D8E0`, through the pool's
  vtable).
- **The pool allocator:** vtable `0x3082A4`:

  | Function | What it does |
  |---|---|
  | `sub_001567B0` | Sets up a pool. |
  | `sub_001569E0` | Hands out the free list's head. |
  | `sub_00156550` | Takes a block back. |
  | `sub_00156800` | Tears a pool down. |

  | Offset | Field |
  |---|---|
  | `+0x04` | Blocks in use |
  | `+0x08` | Blocks the pool has |
  | `+0x0C` | Blocks a chunk |
  | `+0x10` | Block size |
  | `+0x14` | Chunk list |
  | `+0x18` | Free list |

  When the count of blocks in use reaches 0, `sub_00156550` gives every chunk back to the heap. So a block taken back
  twice does more than corrupt the free list: it empties the count too early, and the chunks are freed under blocks
  still in use.
- **What was running:** a weapon update fired an effect, which needed a block:

  `sub_00186E90` → `sub_00187540` → `sub_0010CCD0` → `sub_0003D0A0` → … → `sub_0003D310` → `sub_0003D8E0` → `sub_001569E0`

  The damage was done earlier, by whatever corrupted the list. This call only tripped over it.
- **First theory (2026-10-28):** two guest threads use the pool at once. With the guard, a run that day logged in the
  menus `pool 003A65A8: used by guest thread 14 too, first by 28 (from 0003D80B 0003DB8E 0003D3D4 0003E3B0)`. Thread
  0x28 starts at `0x1BFE7E`, the start routine several threads share (18, 24, 28, 34, 3C). The second sighting does
  not need a race: every operation on the block was on thread 14.
- **Second sighting (2026-10-29), caught by the guard:** mission 1_21 (log `mohea_121.log`, dump
  `MOHEA.2026.10.29_21.00.44.dmp`), with `unlimited_ammo`, `squad_immortal`, `player_invincible` and `no_shell_shock`
  on. At 198.5 s:

  ```
  pool 003A65A8: taken back but not live (twice?): block 83F32390 (blocks 46 of 120 in use, ...)
  ```

  The block's history, all on thread 14:

  | Step | Return addresses | Path |
  |---|---|---|
  | Out | `0003D80B 0003DB8E 0003D3D4 0003E3B0` | Through `sub_0003D800` |
  | Back | `… 0003C8D1` | A request that failed to start (`sub_0003C890`) |
  | Out, then back | `… 0003CD4C 0003C887` | Event 0x08, likely stop (`sub_0003C860`) |
  | Back again: **the double free** | `… 0003CD4C 0003C7A9` | Event 0x02, done (`sub_0003C6F0`) |

  At 205 s the game made an indirect call through a null pointer, at `0x12F730` with `ecx` = `83F32390`, the reused
  block. The runtime stopped it (`indirect call to 00000000`, exception `80000003` in `MOHEA.exe`).
- **Cause:** the pool's 0x28-byte blocks are nodes of a list of pending requests (vtable `0x302800`): +0x14 the
  count, +0x18 the pool, +0x1C the list head. `sub_0003C890` starts a request with the callback `sub_0003C6F0`. A
  request leaves its list through `sub_0003CD00` (vtable +4, `sub_001568D0` unlinks and frees the node) from three
  places:
  - its start failing (`0x3C8D1`);
  - event 0x08, likely stop (`sub_0003C860`, `0x3C887`);
  - its completion callback on event 0x02, done (`sub_0003C6F0`, `0x3C7A9`).

  Nothing checks the node is still listed. A request that is stopped and then completes is taken out twice. The
  request code is in the sound system's range (`0x1ADxxx`), so these are probably sounds. The game likely never got
  "done" after "stopped" on the Xbox: the SDK's DirectSound may report a sound's end differently, which would also fit
  the 2026-10-29 "out of sound buffers" crash.
- **Next steps:**
  1. Done: `src/game/requests.cpp` hooks `sub_0003CD00` and leaves alone a request no longer in its list, logging
     `requests: request ... taken out of list ... again`. If a new request reused the freed node in the same list, the
     late callback ends that one early (a sound cut short), but nothing is freed twice. Confirmed on 2026-10-30.
  2. Find which events reach a stopped sound, and whether the SDK's DirectSound reports a stopped buffer's end as the
     Xbox did.
