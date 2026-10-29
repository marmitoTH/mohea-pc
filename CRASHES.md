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

### 2026-10-28: corrupt free list in a game pool (mission 1_8)

**Status:** open, cause unknown. Seen once.

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
- **Leading theory:** two guest threads use the pool at once. The Xbox ran every thread on one core, where such a
  race almost never hit; on a PC they run in parallel (CHECKLIST.md §8). Supporting it: with the guard, a dev build
  run later on 2026-10-28 logged at 48 s, in the menus:

  ```
  pool 003A65A8: used by guest thread 14 too, first by 28 (from 0003D80B 0003DB8E 0003D3D4 0003E3B0)
  ```

  Thread 0x28 starts at `0x1BFE7E` (the start routine several threads share: 18, 24, 28, 34, 3C), so which subsystem it
  runs is not known yet. The other possibility is a plain double free in the game, which the guard would also catch.
- **Next steps:**
  1. Play the dev build, mission 1_8 if possible, until the guard logs an error for `003A65A8`. The error and the
     history under it give the thread and the guest callers of the bad operation.
  2. Find what thread 0x28 is: its start parameter, the `0x3Cxxx` subsystem's other entry points.
  3. If it is a race, serialise the pool's users. Two ways: a lock around `sub_001569E0` and `sub_00156550` for this
     pool, or `kernel::ShareOneCore` for the threads involved, as Finest Hour did (CHECKLIST.md §8).
