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
| 1 | Upstream `STBI_WRITE_NO_STDIO` (#4282) not adopted; Duo defines `STBI_ONLY_PNG` there instead | `src/common/stb.h` (line ~10) | Duo still writes PNG files with `stbi_write_png` to files (`src/core/mods/`, eden-cli screenshots in `src/yuzu_cmd/yuzu.cpp`, golden tests) | Conflicts whenever upstream edits `stb.h` |
| 2 | `unordered_dense` kept after upstream dropped it (#4326) | `cpmfile.json` (`unordered_dense` entry), `externals/CMakeLists.txt` (`AddJsonPackage(unordered-dense)`), `.patch/unordered-dense/`, `src/core/CMakeLists.txt` (`target_link_libraries(core ... unordered_dense::unordered_dense)`) | The dual-screen runtime's per-tick snapshots use the pinned dense map | Conflicts on the next sync: upstream edits the same `core` link line (`frozen::frozen-headers`) |
| 3 | eden-cli keeps its own `getopt_long` table and `PrintHelp` instead of upstream's unified parser (#4424, `Core::ParseLaunchParams`); about 117 lines upstream removed are restored | `src/yuzu_cmd/yuzu.cpp` (`PrintHelp` ~91-117, option loop ~897-1104) | eden-cli needs Duo-only harness flags (dump, install, scripted input, aux window) | High: upstream parser changes and new flags never reach eden-cli. Planned fix: parse Duo flags first, pass the rest to `ParseLaunchParams` |
| 4 | Kernel `application_process` guarded by a lock plus an epoch counter | `src/core/hle/kernel/kernel.{h,cpp}` (`SetApplicationProcess`, `RetainApplicationProcess`, `IsApplicationProcess`, `application_process_epoch`) | The companion runtime reads the application process from other threads and must detect a process swap; also closes an upstream race | Medium |

## Generic fixes carried on top of upstream

Not Duo features. Each should stay a separate, self-contained change so a sync can
drop it as soon as upstream fixes the same problem.

| # | Fix | Where | Sync risk |
|---|---|---|---|
| 5 | Screenshot request race: renderers claim a pending request via `RendererBase::TakePendingScreenshot` | `src/video_core/renderer_base.{h,cpp}`, `renderer_opengl.cpp`, `renderer_vulkan.cpp` (`RenderScreenshot`) | Medium; the aux window depends on it |
| 6 | ThreadSanitizer fiber annotations (`EDEN_HAS_TSAN`) | `src/common/fiber.cpp` | Low |
| 7 | Debugger `ConnectionState` constructs the Boost.Process signal pipe in place (copying the v1 pipe could assign invalid descriptors) | `src/core/debugger/debugger.cpp` (`ConnectionState`) | Medium |
| 8 | `HostMemory::Unmap` range check as an overflow-safe `ASSERT_MSG` with the offending range | `src/common/host_memory.cpp` | Low |
| 9 | Test fixes | `src/tests/core/core_timing.cpp`, `src/tests/video_core/memory_tracker.cpp` | Low |
| 10 | Dynarmic code-page cache invalidated through an atomic `code_gen` counter (bumped by `ClearInstructionCache`/`InvalidateCacheRange`, possibly from another thread) instead of a cross-thread write to `last_code_addr` | `src/core/arm/dynarmic/arm_dynarmic_{32,64}.{h,cpp}` (`DynarmicCallbacks*::MemoryReadCode`) | Medium; regenerate the ledger, the headers become upstream hooks |

## Accidental drift (to revert in a source change)

Formatting-only differences with identical tokens. They have no purpose and cause
conflicts; restore upstream's formatting the next time these files are edited.

* `src/dynarmic/src/dynarmic/backend/x64/emit_x64_memory.h` (~lines 97, 100): tabs
  converted to spaces. Upstream edits this file often.

Rule: format only Duo-owned paths; never run `clang-format` over whole upstream files.

## Known upstream API changes to handle at the next sync

* Upstream #4480 replaced `static const FunctionInfo functions[]` +
  `RegisterHandlers(functions)` with `CreateStaticMap` / `FindRequest` overrides.
  `src/core/hle/service/dsm/dsm.cpp` (Duo-only, so not in the conflict list) uses
  the old form and must be converted when syncing past upstream `218c670`. Only the
  build catches this.
