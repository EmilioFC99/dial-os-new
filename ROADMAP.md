# DialOS Roadmap — Foundations to 1.0 and Beyond

> **How this roadmap works:** DialOS is built **one milestone at a time**. Each milestone is its own focused cycle (`brainstorm -> draft -> build -> hardware-verify`). Milestone `N+1` does not start until Milestone `N` works reliably on a real **FiiO Snowsky Disc (V2.57)** — never "done" in the simulator alone.
>
> This file is the **single source of truth** for milestones and guardrails. Background V2.57 binary RE findings, hardware constraints, and review notes live in [docs/dialos-foundation-roadmap/01_brainstorm/01_concepts_brief.md](docs/dialos-foundation-roadmap/01_brainstorm/01_concepts_brief.md).

## Guardrails (Apply to Every Milestone)

- **Host & Target:** Apple Silicon macOS development only (`zig cc` targeting `mipsel-linux-musl`); FiiO Snowsky Disc stock firmware `V2.57` only; GPL-3.0-or-later.
- **Clean Architecture & Strict Reuse Rule:** Direct custom `360x360` XRGB8888 2D software renderer (no LVGL, no `fb_pan.c`), strict HAL boundaries, and **zero `diskOS` or `../dial-os` spaghetti copied into `src/` or `tools/`**:
  - *Hardware blobs reused only where unavoidable:* The verified Ingenic X2000 Mask-ROM hardware blobs (`disc_spl_lpddr3.bin`, `my_write6_dram.bin`) and `usbboot` USB transport in `vendor/`, plus (optionally) an isolated out-of-process cover decoder helper.
  - *Written clean & unit-tested from scratch:* The macOS flasher / microSD updater CLI and all runtime C modules (`ipc`, `playstate`, `power`, `sdio`, watchdog/crash logging, `library.db`, renderer, gestures, UI) are built fresh using `diskOS` and `../dial-os` strictly as reference checklists for V2.57 hardware invariants (`Q01–Q18`, `buttons.md`, `fstat` queue recovery, `0664=0` + `sync`, BT pre-stop, `0649` gain).
- **No Theme Leakage:** Zero theme names in file names, identifiers, or logic. Themes are **Dark Mode** and **Light Mode** driven by semantic tokens (visual identity designed fresh in M2).
- **Unbounded Library:** No track/album/artist/playlist/favorite caps. Memory is bounded by the screen (`64 MB` RAM budget), never by library size.
- **Hardware-Verified Only:** Every setting, gesture, and playback behavior must pass on the real Disc before a milestone closes.

---

## Part 1: Foundations (No Product Screens Yet)

### M0 — V2.57 Hardware Access, Clean Flasher & Feasibility Spikes
*Goal: Build a clean, tested macOS flash/SD-update CLI and prove the physical limits on real V2.57 hardware before writing product code.*
- Rewrite the macOS flasher & microSD updater CLI cleanly from scratch (unit-tested with dry-run / mocked USB & SquashFS) wrapping only the verified X2000 Mask-ROM blobs (`disc_spl_lpddr3.bin`, `my_write6_dram.bin`, `usbboot`): supports `flash`, `sd-update`, and `restore-stock`, patches V2.57 `vendor/stock_v257/rootfs.squashfs` with the 10-second microSD hot-reload hook (`/tmp/sdcard/.dial-os/mq_ui` + `.dial-os/use_stock` escape hatch, preserving `/usr/bin/mq_ui.stock`) — **no `diskOS` flash required**.
- Stand up `zig cc` (`mipsel-linux-musl`) cross-compilation + microSD hot-reload deploy.
- Benchmark on real hardware under active `mq_player` playback (`24/192` FLAC + EQ + BT):
  - `360x360` fixed-point 2D rotation fps (`>=30 fps` gate; **Fallback:** static cover art + rotating progress ring) and `FBIOPAN_DISPLAY` tearing.
  - `cst816t` rim touch sample rate (`>=60 Hz` continuous without dropouts).
  - Rolling-slice playback handoff (`CUSTOM_PLAYLIST` / `comm_folder_play_control`) — **Fallback (Plan B):** if rolling slices stutter or break gapless/shuffle at slice boundaries, pass the full active album/playlist/folder to `mq_player` and slice `All songs` only above a safe track threshold.
  - V2.57 EQ/PEQ write behavior (`filterType` acceptance).
  - Capture golden V2.57 `/ui` + `/player` IPC traces (including `Q14`/`Q17`/`Q18` EOF and seek behavior).
- **Exit Gate:** Real benchmark numbers and fallback decisions recorded on V2.57 hardware; cross-compiled spike binary deploys via microSD hot-reload and runs cleanly on the Disc.

### M1 — Core Platform & Walking Skeleton
*Goal: Build the clean, testable engineering foundation and safety net.*
- Initialize git repo and modular layout (`src/bsp/`, `src/hal/`, `src/core/`, `src/ui/`, `src/media/`, `src/app/`, `tests/`).
- Build clean-room, unit-tested hardware safety modules in `src/bsp/` enforcing V2.57 invariants: watchdog `argv[0]` / `PR_SET_NAME` guard, async-signal-safe `crash_log()`, `45` s boot alarm, SD-lease lifecycle, `0664=0` + `sync` shutdown safety, startup V2.57 version guard, IPC transport (`fstat` queue-recreation recovery + serialized BT pre-stop guard), and `playstate` inference.
- Build the Device Abstraction Layer (direct `/dev/fb0` 180° blit, `event0`/`event1` input, IPC transport) and native macOS SDL2 simulator (circular bezel mask, `~60 px` touch cursor, virtual `clock_ms`, framed fake player replaying M0 V2.57 traces).
- Set up the Mac TDD unit-test harness (ASan/UBSan) and explicit `64 MB` RAM / `83 MB` `/usr/data` budgets.
- **Exit Gate:** Blank app passes Mac unit tests, cross-compiles via `zig cc`, deploys via microSD hot-reload, survives the watchdog, logs crashes cleanly, and toggles real play/pause (`0201`) on the Disc.

### M2 — Visual Design System, 2D UI Kit & Gesture Engine
*Goal: Establish DialOS's fresh visual identity, drawing primitives, and shared input model.*
- Design DialOS's fresh **Dark Mode** and **Light Mode** visual identity (semantic color, spacing, typography, and motion tokens).
- Build the direct 2D drawing primitive kit + FreeType glyph renderer with `1 MiB` LRU cache over rootfs `SourceHanSans-Regular.ttc` (CJK/Cyrillic coverage).
- Build the screen/overlay lifecycle host and the shared polar gesture + physical-key (`buttons.md`) engine (disjoint polar zones, zero `250–300 ms` tap lag, upper-arc scrub readout).
- **Exit Gate:** Gesture unit tests and deterministic simulator screenshot tests (Dark Mode, Light Mode, CJK text) pass; interactive empty shell runs smoothly on the Disc.

---

## Part 2: Product (`1.0` — Built & Verified One Feature at a Time)

### M3a — Playback Engine & Minimal Now Playing (Reliability First)
*Goal: Make playback rock-solid before adding visual complexity.*
- Build a single deterministic playback / EOF / seek / play-mode (sequential, repeat, shuffle) state machine handling V2.57 quirks (`Q14`, `Q17`, `Q18`) so music never randomly stops.
- Build a bare-bones text + progress-bar `Now Playing` view (play/pause, next/prev, seek, play mode, physical-key + rim volume) with zero visual polish so playback bugs cannot hide behind UI code.
- **Exit Gate:** Unit tests for all EOF/seek/play-mode transitions + **multi-hour on-device playback soak test** (multi-track advance, shuffle/repeat/sequential, `95%–99%` seek, zero unexpected stops, zero crashes).

### M3b — Now Playing Visuals, Cover Art & Lyrics
*Goal: Deliver the centerpiece screen on top of the proven playback engine.*
- Build full `Now Playing` visuals: spinning vinyl (or M0 fallback ring), center progress ring & scrub (timestamp rendered above thumb), volume HUD, translucent Quick Actions (frozen spin + `90x90` cached blur), Track Details sheet, Lyrics (embedded + `.lrc`), isolated out-of-process cover decoding (`32 MiB` heap / `8 MiB` file / `4096 px` caps) + byte-capped art cache, and Not Playing state.
- **Exit Gate:** Unit tests (lyrics, art cache eviction, time-based angle) + simulator screenshot tests + on-device verification (`>=30 fps`, RAM under `64 MB` budget, zero regressions on M3a playback reliability).

### M4a — Library Core, Scanner & Basic Playback (2 TB / Unbounded Scale)
*Goal: Index and play massive libraries with constant screen-bounded RAM.*
- Build SD-resident `.dialos/library.db` (`journal_mode=TRUNCATE`, canonical path keys, keyset-paged SQLite queries, zero `OFFSET`, zero full-library RAM array), incremental `(path, size, mtime)` scanner, and M0-verified playback handoff (rolling-slice or Plan B) to `mq_player`.
- Build flat `All songs` and `Albums` list views + `0`-track empty state wired end-to-end into `Now Playing`.
- **Exit Gate:** Mac `EXPLAIN QUERY PLAN` assertions (zero `SCAN`/`SORT`) at `0`, `1`, `10k`, and `1M` synthetic tracks; on-device scan and playback benchmark over a real SD card under RAM budget.

### M4b — Library Views, Letter Seek, Favorites & Playlists
*Goal: Complete the full Library browsing and collection experience.*
- Build top-level curved list (`Favorites`, `Playlists`, `Artists`, `Albums`, `Genres`, `All songs`), `Albums` cover carousel, `Artists`/`Genres` drilldown, explicit-handle `Letter Seek` (`#`, `A`–`Z`, CJK/Cyrillic), `Favorites`, and `Playlists` persistence + playback.
- **Exit Gate:** Unit tests for Letter Seek bucket queries and Favorites/Playlists persistence; simulator screenshot tests; on-device browse and playback verification across all views.

### M5 — Home & Global Chrome
*Goal: Connect the apps with the tactile round-screen launcher and Control Center.*
- Build the 4-app curved `Home` arc (`Now Playing`, `Library`, `Modes`, `Settings`) with rotating detent ticks.
- Build the top-rim pull-down icon-only `Control Center` puck and `Home`/`Library -> Now Playing` transitions.
- **Exit Gate:** Simulator screenshot tests and on-device transition fps verification.

### M6a — Settings (Audio, Display, Power & System)
*Goal: Every local setting is proven to work on real V2.57 hardware.*
- **Audio:** DAC filter, output gain (`0649`), gapless, channel balance, volume limit, hardware-verified fixed EQ presets + graphic EQ.
- **Display:** Brightness slider, Dark Mode / Light Mode toggle, spinning vinyl toggle, screen timeout.
- **Power & System:** Battery status, USB host power behavior, idle power-off timer, non-blocking microSD rescan with progress window.
- Clean-room atomic versioned settings persistence (`fsync` + `rename`, schema version).
- **Exit Gate:** Unit tests for persistence/rollback and `0649`/EQ IPC sequences; **every single setting verified end-to-end on real V2.57 hardware** with zero crashes or MCU reboots.

### M6b — Settings (Bluetooth)
*Goal: Reliable Bluetooth management without crashing `mq_player`.*
- Scan, pair, connect, disconnect, and per-device volume enforcing serialized local-output pre-stop (`Q09`/`Q11`/`Q13`/`Q15`).
- **Exit Gate:** Unit tests for BT pre-stop state machine; on-device BT scan/pair/playback/disconnect smoke test with zero `mq_player` crashes or MCU reboots.

### M7 — Modes (Mass Storage)
*Goal: Safe USB file transfer without corrupting the SD card or database.*
- Build `Modes` with **Mass Storage** only (honoring V2.57 `Q06`/`Q10` USB gadget quirks).
- Full SD-lease handle teardown across `.dialos/library.db`, art cache, and lyrics before USB export; abrupt-unplug error state; post-USB incremental `(path, size, mtime)` rescan.
- **Exit Gate:** Integration test proving zero open SD file descriptors during export; real USB mount/copy/eject/rescan smoke test on the Disc.

### M8 — `1.0` Packaging & Soak Release
*Goal: Safe installation, stock restore, and release-grade stability.*
- Finalize and package DialOS's clean-room macOS USB Mask-ROM flasher (`flash` / `restore-stock`), microSD hot-reload (`sd-update`), and `use_stock` escape hatch.
- Full memory/leak audit (`VmHWM`) and multi-hour hardware soak test (playback + play modes + BT + Mass Storage + sleep/wake).
- **Exit Gate:** Clean install from stock V2.57, microSD update pass, `use_stock` escape + stock-restore round-trip, and multi-hour soak test with zero unexpected stops, zero watchdogs, and zero MCU reboots.

---

## Part 3: Future (`1.x` — Separate Future Efforts)

Each `1.x` release starts as its own `/egm:egm-brainstorm` once `1.0` is stable on hardware:

- **`1.1` — Queue, Folder Browser & Daily-Driver Polish:** Up Next queue view, Folder Browser, Sleep Timer, ReplayGain, and quick search/filter.
- **`1.2` — Wi-Fi & Easy Provisioning:** Wi-Fi Settings UI, SD `wifi.conf` / SoftAP phone provisioning (and/or 2-tap radial keyboard), plus on-device Debug SSH toggle.
- **`1.3` — Signed Wi-Fi OTA Updates:** Self-updates with signature verification and trial-boot health-check rollback.
- **`1.4` — On-Device Music & Metadata Sync:** Downloading music over Wi-Fi into `.dialos/library.db` (and optional manual `.lrc`/cover fetch).
- **`1.5` — Receiver Modes:** USB DAC, Bluetooth Receiver, and AirPlay dashboards.
- **`1.6` — Games & Extras:** Dedicated fixed-timestep render loop and mini-games.
