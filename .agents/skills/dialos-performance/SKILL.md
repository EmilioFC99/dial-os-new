---
name: dialos-performance
description: >-
  Performance engineering playbook and hardware profiling workflow for DialOS
  on the FiiO Snowsky Disc (Ingenic X2000 dual-core MIPS32r2, 360x360
  framebuffer, ~64 MB RAM). Use when optimizing rendering, background I/O,
  artwork decoding, boot choreography, or diagnosing FPS drops and PERF logs.
---

# DialOS Performance Playbook

## Hardware Budget

| Resource | Target Limit | Notes |
| :--- | :--- | :--- |
| CPU | Ingenic X2000 dual-core MIPS32r2 | Core 0 renders UI; Core 1 handles background `-static -pthread` I/O & decode |
| Frame Budget | `16.6 ms` (`60.0 FPS`) | Target `avg_ms <= 14`, `max_ms <= 18`, `slow_frames == 0` |
| Framebuffer | `360×360` XRGB8888 (`180°` inverted) | Software rasterized; no GPU |
| Free RAM | `~64 MB` | Keep streaming decoders `< 250 KB` and bound LRU caches (e.g., `256` RGB565 slots `~7.4 MB`) |
| Cooperative Main-Thread Work | `<= 1 ms/frame` | Any main-thread scan/poll must yield well inside frame budget |

## 1. Software Rasterizer Patterns

- **Span-Vectorize Fills:** Compute `y` bounds, `180°` row addresses, and clip rectangles once per scanline; fill contiguous horizontal spans with integer arithmetic.
- **Integer Fixed-Point Alpha:** Precompute `alpha_256` and source terms outside the inner loop; avoid per-pixel `float` multiplies/divisions.
- **Row-Level Circle Bounds:** Compute `half_width = sqrt(r^2 - dy^2)` once per row for filled/AA circles; never call `sqrtf()` per pixel.
- **Fast RGB565 Blits:** Convert/blend pre-cached `120×120` RGB565 sleeves directly with integer shifts instead of decoding PNG/JPEG per frame.

## 2. Dual-Core `-static -pthread` & Cooperative I/O

- **Offload Heavy Work to Core 1:** Spawn a `-static -pthread` worker for cold cover precaching or heavy SD reads, passing completed items back via a short mutex-guarded handoff.
- **SD Remount Resilience:** Handle transient microSD unavailability with bounded remount backoff (e.g., `500 ms` retry) and a deadlock timeout (`2.5 s`) so background workers never hang boot or UI transitions.
- **No Blocking Calls on Core 0:** Never call `statvfs()`, recursive directory walks, or `fsync()` inside active animation/scroll frames.

## 3. Memory-Bounded Artwork & Cache Workflow

- **Stream-Downsample Large JPEGs:** Use a streaming 1/8-scale DC-only baseline JPEG decoder (`< 250 KB` RAM) before falling back, preventing OOM on `4000–5100 px` / `20 MB` embedded covers.
- **Pre-Cache to `.dial-os/`:** Persist `120×120` `.rgb565` thumbnails atomically (`temp` + `fsync` + `rename`) and preload a bounded RAM LRU (`~64` slots at boot, `256` max) so scrolling is pure memory blit.
- **Non-Destructive Cold-Cache Reset:** To re-benchmark cold boot/cover precaching without touching user music files, delete only DialOS cache artifacts:
  `rm -f /Volumes/MUSIC/.dial-os/library.idx /Volumes/MUSIC/.dial-os/covers/*.rgb565`

## 4. Boot Choreography & Warm-Catalog Continuity

- **Defer Heavy Work Past Act 01:** Run cover precaching and warm-boot delta checks in Boot Act 02 (after `aa24` signal lock) so initial wake animations stay locked at `~60 FPS` without progress jumps.
- **Preserve Warm Catalog State:** Never clear an already-populated catalog when entering Home; apply delta updates atomically.

## 5. Hardware Profiling & Acceptance Gate

Inspect `.dial-os/dialos.log` after on-device runs and verify:
- `BOOT` / `PERF`: `fps_x10 >= 580`, `avg_ms <= 14`, `max_ms <= 18`, `slow_frames == 0`.
- Cover precache summary: `failed == 0` across all albums.
- Visual smoothness: no skipped boot acts and no `"0 albums"` empty-state flashes on warm boot.
