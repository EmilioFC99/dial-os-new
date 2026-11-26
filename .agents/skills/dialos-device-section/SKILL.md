---
name: dialos-device-section
description: >-
  Use when implementing a new vertical DialOS UI section or screen in C11 for
  the FiiO Snowsky Disc (V2.57) from docs/north_star.md and simulator/. Guides
  parallel codebase/hardware/simulator gap analysis, TDD C11 implementation,
  MIPS32r2 cross-compilation, microSD deployment, and dialos.log 60 FPS
  optimization.
---

# DialOS Device Section Implementation Workflow

Follow this repeatable 5-stage pipeline whenever bringing a new section from `docs/north_star.md` and `simulator/` onto real FiiO Snowsky Disc (`V2.57`) hardware.

## Stage 1: Parallel Three-Way Discovery & Gap Analysis
Before writing C code, inspect all three sources of truth in parallel (delegating to `research` subagents when broad):
1. **Current C Firmware & Active Branches**: Check `git branch -a -v` and `git worktree list` (e.g., unmerged feature branches), `src/`, `Makefile`, and `tests/c/` to identify existing primitives, font styles (`tools/generate_fonts.swift`), and runtime state.
2. **Stock `V2.57` Hardware & IPC (`vendor/stock_v257/`, `docs/hardware.md`, `docs/benchmarks/`)**: Verify exact evdev devices, sysfs paths, and real `mq_player` IPC framing (`docs/benchmarks/m0-v257-ipc-trace.log`). Never assume uniform headers—check real traces (e.g., `a202` and `a103` use compact 8-byte headers).
3. **Target Experience (`docs/north_star.md` + `simulator/web/`)**: Extract exact geometry, timings, springs, hitboxes, transitions, and Dark/Light tokens, and flag any expensive software-rendering operations (per-pixel SDFs, rotated textures, full-screen overdraw).

## Stage 2: Resolve Gaps & Isolate Worktree
- Explicitly resolve any hardware/IPC gaps (e.g., missing peripherals, font sizes to bake, deferred decoders/indexers, volume/percentage normalization) before coding.
- Create an isolated sibling worktree (`../dial-os-new-<section>` on branch `<section>`) from the active baseline.

## Stage 3: TDD C11 Implementation (Layer by Layer)
For each layer (Fonts → Protocol/Input → Controller/State Machine → Renderer → Runtime/Platform):
1. **Write failing host C tests first** in `tests/c/` (nominal, boundary/stride canary, and negative/malformed cases).
2. **Implement minimal, clean C11** (`-Wall -Wextra -Werror`, zero single-letter variables, zero theme-name leakage, fixed-width types).
3. **Keep the UI thread 100% single-threaded and non-blocking**: Never call `pthread_create()` in `-static` musl on kernel `3.10.14` and never run blocking SD filesystem scans (`statvfs`, recursive `readdir`) on the UI thread during boot or 60 FPS animation.
4. **Verify after every layer**: `make test && make mq-ui`.

## Stage 4: Deploy & Collect On-Device Logs
1. Deploy to microSD without touching music files:
   ```sh
   ./dial-os-mac sd-update --binary ../dial-os-new-<section>/build/mq_ui
   ```
2. Exercise all states and transitions on the real FiiO Snowsky Disc, then shut down cleanly (Power Long-Press) so `boot.log` and `dialos.log` flush and `fsync()`.
3. Inspect `<SDCARD>/.dial-os/dialos.log`, `boot.log`, and `crash.log` (if any).

## Stage 5: Log-Driven 60 FPS Optimization
Check `PERF` lines in `dialos.log` for `fps_x10 < 600`, `avg_ms > 15`, `max_ms > 17`, or high `slow_frames`:
- **Ellipses / Circles**: Use exact tight bounding boxes and `norm_sq` early-reject before `sqrtf()`.
- **Faint `<= 1 px` Wireframes / Grids**: Use `render_draw_line_fast()` (integer Bresenham) instead of capsule-SDF AA lines.
- **Filled Polygons / Chamfer Plates**: Use `render_draw_filled_convex_polygon_fast()` (integer scanline span blending).
- **Large Transitions**: Cull occluded back/inner wireframe edges once expanded past `half_size > 110 px`, and lower polygon segment LOD on small distant neighbor slots.
- **Verify zero regression**: Re-run `make test && make mq-ui`, re-deploy via `sd-update`, and confirm `dialos.log` hits `~60.0 FPS` on hardware.
