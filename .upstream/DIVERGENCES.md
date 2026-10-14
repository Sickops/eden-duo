# Eden Duo: deliberate divergences from Eden upstream

Base: the Eden nightly commit in `.upstream/eden.json` (`source_commit`).
Ledger of every upstream file Duo touches: `.upstream/duo-touched.tsv`
(regenerate and check with `tools/duo/duo-touched-ledger.sh`).

This file lists the places where Duo intentionally differs from upstream in code
upstream owns, beyond the dual-screen hooks themselves. A sync that silently "fixes"
one of these breaks Duo, and a sync that keeps one after upstream fixed the same
problem keeps dead code. On every upstream sync, go through the list:

* still needed: keep it, update the location if it moved;
* upstream now does the same: drop the Duo change and remove the entry;
* upstream changed the surrounding code: re-check that the divergence still applies.

Line numbers are as of the Eden Duo 1.3.0 betas and drift; the symbols are the
stable reference.

## Kept on purpose

| # | Divergence | Where | Why | Sync risk |
|---|---|---|---|---|
| 1 | Upstream `STBI_WRITE_NO_STDIO` (#4282) not adopted; Duo defines `STBI_ONLY_PNG` there instead | `src/common/stb.h` (line ~10) | Duo still writes PNG files with `stbi_write_png` to files (`src/core/mods/`, eden-cli screenshots in `src/yuzu_cmd/dsmod_harness.cpp`, golden tests) | Conflicts whenever upstream edits `stb.h` |
| 2 | `unordered_dense` kept after upstream dropped it (#4326) | `cpmfile.json` (`unordered_dense` entry), `externals/CMakeLists.txt` (`AddJsonPackage(unordered-dense)`), `.patch/unordered-dense/`, `src/core/CMakeLists.txt` (`target_link_libraries(core ... unordered_dense::unordered_dense)`) | The dual-screen runtime's per-tick snapshots use the pinned dense map | Conflicts on the next sync: upstream edits the same `core` link line (`frozen::frozen-headers`) |
| 3 | eden-cli keeps its own `getopt_long` table and `PrintHelp` instead of upstream's unified parser (#4424, `Core::ParseLaunchParams`); about 117 lines upstream removed are restored | `src/yuzu_cmd/dsmod_harness.cpp` (`PrintHelp`, `ParseCommandLine`); `src/yuzu_cmd/yuzu.cpp` only calls `DSModHarness::ParseCommandLine` where upstream calls `ParseLaunchParams` | eden-cli needs Duo-only harness flags (dump, install, scripted input, aux window), and harness scripts rely on the pre-#4424 semantics that upstream's parser changed. Layering the Duo flags on `ParseLaunchParams` was evaluated for 1.3.0 and rejected, see "eden-cli option parser" below | Medium: the parser no longer conflicts (it is in a Duo file), but upstream parser changes and new flags never reach eden-cli. Re-check on each sync whether upstream fixed the items below |
| 4 | Kernel `application_process` guarded by a lock plus an epoch counter | `src/core/hle/kernel/kernel.{h,cpp}` (`SetApplicationProcess`, `RetainApplicationProcess`, `IsApplicationProcess`, `application_process_epoch`) | The companion runtime reads the application process from other threads and must detect a process swap; also closes an upstream race | Medium |

## Generic fixes carried on top of upstream

Not Duo features. Each should stay a separate, self-contained change so a sync can
drop it as soon as upstream fixes the same problem.

| # | Fix | Where | Sync risk |
|---|---|---|---|
| 5 | Screenshot request race: renderers claim a pending request via `RendererBase::TakePendingScreenshot` | `src/video_core/renderer_base.{h,cpp}`, `renderer_opengl.cpp`, `renderer_vulkan.cpp` (present path) | Medium; the aux window depends on it |
| 6 | ThreadSanitizer fiber annotations (`EDEN_HAS_TSAN`) | `src/common/fiber.cpp` | Low |
| 7 | Debugger `ConnectionState` constructs the Boost.Process signal pipe in place (copying the v1 pipe could assign invalid descriptors) | `src/core/debugger/debugger.cpp` (`ConnectionState`) | Medium |
| 8 | `HostMemory::Unmap` range check as an overflow-safe `ASSERT_MSG` with the offending range | `src/common/host_memory.cpp` | Low |
| 9 | Test fixes | `src/tests/core/core_timing.cpp`, `src/tests/video_core/memory_tracker.cpp` | Low |
| 10 | Dynarmic code-page cache invalidated through an atomic `code_gen` counter (bumped by `ClearInstructionCache`/`InvalidateCacheRange`, possibly from another thread) instead of a cross-thread write to `last_code_addr` | `src/core/arm/dynarmic/arm_dynarmic_{32,64}.{h,cpp}` (`DynarmicCallbacks*::MemoryReadCode`) | Medium; regenerate the ledger, the headers become upstream hooks |
| 11 | SDL frontend lifetime fixes: `*appstate = state`, `SDL_APP_CONTINUE` from `SDL_AppInit`/`SDL_AppEvent`, the input subsystem kept in `SdlState` (upstream's is a local that dies when `SDL_AppInit` returns) and passed to `System::SetInputSubsystem`, the shader-reload callback capturing `state` by value, and `SDL_AppQuit` tolerating early exits (`IsPoweredOn` guard) | `src/yuzu_cmd/yuzu.cpp` (`SdlState`, `SDL_AppInit`, `SDL_AppEvent`, `SDL_AppQuit`) | Low; drop each part once upstream fixes it |

## eden-cli option parser

The eden-cli harness (option parsing, `--dump-il2cpp`/`--dump-romfs`/`--install`, screenshots,
scripted input, aux window) lives in `src/yuzu_cmd/dsmod_harness.{h,cpp}`. `yuzu.cpp` keeps
upstream's layout and calls `DSModHarness::ParseCommandLine`, `ResolveSelectedUser`,
`OnGameLoaded`, `Iterate` and `HandleEvent`.

Putting the Duo flags in front of upstream's `Core::ParseLaunchParams` (Duo flags consumed
first, the rest passed through) would change flags the harness scripts use. In upstream's
parser at the recorded base:

* `-d/--debug` is `no_argument` but reads `optarg`: `-d <port>` (the BOTW harness) passes a
  null pointer to `atoi`.
* `-i/--input-profile` is `no_argument` but builds a `std::string` from `optarg` (null).
* `-x/--filter` is `no_argument` and takes the next `argv` entry; `--filter=<spec>` is rejected.
* `-p/--program` is `optional_argument` and also takes the next `argv` entry, so
  `--program=<args>` swallows the following argument.
* `-h/--help` and `-v/--version` only log and do not exit; the run then fails with "No ROM
  specified" (the `cli_help`/`cli_version` tests expect exit 0 and the text on stdout).
* `-u <name>` is looked up before the config is loaded; Duo resolves it after `SdlConfig`.
* A malformed `--multiplayer` is ignored instead of printing the usage and failing.

Fixing those means editing upstream's `src/core/launch_params.cpp`, which is shared with the Qt
frontend; that is a separate upstream fix, not a Duo change. Revisit when upstream's parser
handles the cases above; then `ParseCommandLine` can strip the Duo-only flags (`-a`, `-V`,
`-D`, `-R`, `-I`, `-S`) and pass the rest to `ParseLaunchParams`.

## Accidental drift (to revert in a source change)

Formatting-only differences with identical tokens. They have no purpose and cause
conflicts; restore upstream's formatting the next time these files are edited.

* `src/dynarmic/src/dynarmic/backend/x64/emit_x64_memory.h` (~lines 97, 100): tabs
  converted to spaces. Upstream edits this file often.
* `src/video_core/renderer_vulkan/renderer_vulkan.cpp` (~lines 189-219): the
  `RendererVulkan` constructor initialiser list was reflowed; upstream uses
  comma-leading layout.

Rule: format only Duo-owned paths; never run `clang-format` over whole upstream files.

## Known upstream API changes to handle at the next sync

* Upstream #4480 replaced `static const FunctionInfo functions[]` +
  `RegisterHandlers(functions)` with `CreateStaticMap` / `FindRequest` overrides.
  `src/core/hle/service/dsm/dsm.cpp` (Duo-only, so not in the conflict list) uses
  the old form and must be converted when syncing past upstream `218c670`. Only the
  build catches this.
