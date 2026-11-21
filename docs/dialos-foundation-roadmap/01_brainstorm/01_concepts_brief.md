# DialOS 1.0 — Foundation and Roadmap

* **Author:** Emilio Flores
* **Date:** 2026-11-16

## Overview

DialOS is a GPL-3.0 replacement user interface (`mq_ui`) for the FiiO Snowsky Disc digital audio player (`360x360` round touchscreen, Ingenic X2000 dual-core 1.2 GHz MIPS32r2 CPU, no GPU, `117.5 MiB` total RAM with `~64 MB` free while `mq_player` runs, `83 MB` writable `/usr/data`, `360x360` XRGB8888 `/dev/fb0` mounted 180 degrees, `cst816t` single-finger `MT type-B` touch on `/dev/input/event1`, physical keys on `/dev/input/event0`). DialOS leaves the stock audio engine `mq_player` intact and drives it over POSIX message queues (`/player` and `/ui`).

The target is a clean-room modular C codebase with a **direct custom 2D software renderer (no LVGL)** built on Apple Silicon macOS (native Mac SDL2 simulator + `zig cc` `mipsel-linux-musl` cross-compile) targeting stock firmware `V2.57` (`SNOWSKY_DISC_update_20260909_v257`), with a clean-room macOS USB Mask-ROM flasher and microSD hot-reload boot hook (`use_stock` escape hatch).

> **Canonical Roadmap:** [ROADMAP.md](../../../ROADMAP.md) is the single source of truth for milestones and guardrails. This document preserves the V2.57 binary RE findings, Goldfish review analysis, and hardware checklists that inform it.

Seven independent Goldfish lenses reviewed the feature concept (`feature_concept.md`) against hardware limits, UX/ergonomics, implementation readiness, migration/compatibility, long-term architecture, contradiction analysis, and the live `b0hemia/diskOS` repository. The review uncovered major mismatches in storage capacity, `mq_player` queue/EQ behavior on `V2.57`, touch ergonomics, and the `diskOS` module map that must be resolved before or during Stage 0.

## Analysis

### Blockers

1. **Unbounded library (`400k`–`1M` tracks) and DialOS index DB collide with `83 MB` `/usr/data`, FAT32/exFAT, `0642` Mass Storage, and `mq_player` `0100` queue ownership — `[Resolved: Option A]`**
   * *Lenses:* `design_critic`, `codebase_details`, `zoom_out`, `implementation_readiness`, `structural`, `inventive_catalyst`
   * *Verified V2.57 RE findings (`usr/bin/mq_ui` & `usr/bin/mq_player` extracted from `SNOWSKY_DISC_update_20260909_v257`):* Stock `mq_ui` (`db_song_ctrl.c`) never loads the full library into RAM; it queries `/usr/data/fiio/db/song.db` with `LIMIT`/`OFFSET` and uses precomputed integer sort/bucket codes (`TITLE_CODE`, `ALBUM_CODE`, `ARTIST_CODE`, `GENRE_CODE`, plus `_JP` variants) where `code >> 16` is the Letter Seek bucket. Stock `mq_player` has no hardcoded local track cap (its capacity warning belongs to `g_qplay_queue`), and creates no `CREATE INDEX` statements. However, `/usr/data/fiio/db/song.db` is hardcoded on the `83 MB` `/usr/data` partition, and IPC `0100` duplicates the active list into `LIST_SONG_0` inside `song.db`, creating a hard physical ceiling (~40k–80k tracks). `diskOS`'s `musicdb.c` ignores its leftover `MDB_MAX_SONGS 1500` macro and `realloc()`s `g_songs` to `COUNT(*)`, which still OOMs at ~60k–70k tracks in `~64 MB` free RAM.
   * *Decision (Option A):* Store DialOS's indexed SQLite catalog on the microSD card (`.dialos/library.db`, `journal_mode=TRUNCATE`, keyed by canonical SD path, guarded by `sdio.c` handle teardown during `0642` Mass Storage) and push only the active playback slice (current album, playlist, or rolling window) to `mq_player` via reserved `CUSTOM_PLAYLIST` (e.g. `LIST_ID = -1000`) or `comm_folder_play_control`. Verify rolling-slice handoff and gapless/next-track boundary behavior in Stage 0a.

2. **30 fps spinning art, 60 fps transitions, and live frosted blur exceed X2000 CPU/bus capacity and misdescribe `fb_pan`**
   * *Lenses:* `design_critic`, `codebase_details`, `zoom_out`, `inventive_catalyst`
   * *Finding:* Section 7 specifies frosted tint over spinning art (`Lyrics`) and translucent overlays over dimmed art (`Quick Actions`), contradicting Section 5 rule 5 ("Blur computed once per track"); re-blurring `360x360` pixels at `30 fps` in software is impossible on X2000. Furthermore, `ui/fb_pan.c` deliberately disables triple-buffer page flipping (`FBIOPAN_DISPLAY`), pans to page `0`, and uses LVGL `PARTIAL` mode with two `60`-row band buffers (`FBPAN_BAND_ROWS 60`), warning that direct writes to the shown page tear on full-screen updates. `ui/lv_conf.h` also locks `LV_DEF_REFR_PERIOD 33` (`30 fps` cap) and `LV_DRAW_SW_DRAW_UNIT_CNT 1`, blocking `60 fps` transitions. Generic LVGL `lv_draw_sw_transform` across `~101,788` circular pixels (`3.05M` pixels/sec at `30 fps`) plus `180`-degree flip and `mq_player` FLAC/DSD + EQ + BT contention risks stutter or audio starvation.
   * *Recommendation:* Benchmark rotation and `FBIOPAN_DISPLAY` tearing in Stage 0 under active `mq_player` playback (`24/192` FLAC + EQ + BT). Either use a pre-baked polar ring texture + static LUT (with `180`-degree flip baked in) + hardware LCD overlay plane split (`inventive_catalyst`), freeze rotation when overlays open and blur a `90x90` downsample once (`design_critic`), or make static cover art + rotating progress ring the `1.0` baseline (`zoom_out`).

3. **Not forking `diskOS` creates a permanent backport tax while still relying on `diskOS` to boot and adapt ~25+ modules**
   * *Lenses:* `zoom_out`, `inventive_catalyst`
   * *Finding:* The concept rejects forking `diskOS` because `ui/` contains `312` flat files (`195 .c` + `110 .h` + `7` build/doc/asm/inc; `214` unique stems), yet relies on `diskOS` for dev booting/recovery and plans to copy-paste and adapt ~25+ low-level `diskOS` C modules (`playstate`, `fb_pan`, `controls`, `ipc`, `musicdb`, `scanner`, `art`, `artcache`, `lrc.h`, `ota`, `debug_ui`, `main`, `lvgl/`). Severing `git` ancestry forces manual backporting of every upstream `V2.57` and hardware fix over the next two years, while adapting global-heavy C modules clashes with zero-global/TDD rules unless quarantined.
   * *Recommendation:* Either fork `diskOS` (or keep it as a `git` remote), delete unused screens, and reorganize the retained modules into `src/` subdirectories (`zoom_out`), or enforce a strict BSP/HAL anti-corruption boundary around all copied `diskOS` C code so DialOS's core stays stateless and testable (`inventive_catalyst`).

4. **Misclassified and omitted `diskOS` dependencies (`touchdisc`/`touchcap`, `controls`, `diskos_artdec`, `sdio`, `power`, `fwcaps`)**
   * *Lenses:* `codebase_details`, `structural`, `implementation_readiness`
   * *Finding:* Live verification of `b0hemia/diskOS` uncovered five errors in Section 8:
     * `ui/touchdisc.c` (`37` lines) and `ui/touchcap.c` (`30` lines) are standalone `int main()` CLI debug tools, not UI modules; `mq_ui` uses `lv_evdev_create`, `controls.c` (`ctl_rot_touch_cal()`), and `main.c` (`g_touch_raw` wake fd).
     * `ui/controls.c/.h` is missing from Section 8 and is a hard compile-time dependency of `fb_pan.c` plus handles `0820`/`0821`/`0822` physical volume keys.
     * `ui/art.c` delegates all decoding/blur to `/opt/diskos/bin/diskos-artdec` (`ui/tools/diskos_artdec.c` + `stb_image.h`) because stock `V2.57` `ffmpeg` has no image decoders; `ui/artcache.c` caches `148x148`, `42x42`, and `360x360` blurred, **not** `360x360` sharp covers for `Now Playing`.
     * `ui/sdio.c` (SD I/O lease/fault guard, zero IPC) and `ui/power.c` (forces `0664 = 0` so `mq_player`'s idle power-off does not call `poweroff -f` without `sync`, which corrupted SD cards on `2026-09-21`) were wrongly marked `Re-derive`.
     * Skipping `ui/fwcaps.c` hides a direct `1.0` blocker: **`fw_custom_peq_writable()` is `0` on `V2.57`** (`mq_player` rejects/repairs bands above half its processing rate), and `ui/eqcustom.c` only implements fixed graphic EQ (`filterType: 0`, not parametric EQ). `fwcaps.c` also documents `V2.57` direct `/dev/mmcblk0p1` mounting, `0642 0` + `0666 6` + `0657 8` init, radio boot arbitration, `MY_LOVE.ID` for `0100` favorites, and `"0649"` gain tag (vs. `V2.09` `"0645"`).
   * *Recommendation:* Update the module map (see `## diskOS Module Map`), verify `V2.57` PEQ/filterType in Stage 0 or scope `1.0` EQ to fixed presets + graphic EQ, and adopt `sdio`, `power`, `controls`, and `diskos_artdec` early.

5. **Zero on-device observability, crash capture, or trial-boot health checks behind a silently respawning watchdog**
   * *Lenses:* `design_critic`, `codebase_details`, `implementation_readiness`, `structural`
   * *Finding:* `fiio_init.sh` immediately respawns stock `/usr/bin/mq_ui` if `mq_ui` exits, and killing/crashing `mq_player` reboots the MCU, wiping `/tmp`. Without crash handlers, an IPC flight recorder, or frame/RSS telemetry, crashes and stutters vanish with no diagnostics. Additionally, OTA trial-boot rollback relies on an undefined "health check"; process liveness alone permanently commits deadlocked or black-screen updates. `diskOS` already ships `prof.c/.h`, `crash_log()` (`SA_ONSTACK` to `/usr/data/diskos_boot.log`), `boot_alarm_handler` (`45` s `SIGALRM`), `fbshot.c`, `mqcap.c`, `mqfeed.c`, `tinj.c`, and `diskos-deploy.sh`, all omitted from Section 8.
   * *Recommendation:* Move async-signal-safe crash logging, `45` s boot alarm, IPC ring-buffer capture (flushed before `bt`/`0642`), and `prof` telemetry into Stage 1, and define the explicit trial-boot readiness criteria (display init + input loop ticks + `mq_player` IPC handshake + N seconds stable).

6. **Stage 0 / Stage 1 circular dependency, unverified `V2.57` encrypted OTA / installer migration, and late Stage 8 coupling**
   * *Lenses:* `implementation_readiness`, `structural`, `zoom_out`
   * *Finding:* Stage 0 schedules on-device spikes on `V2.57` before Stage 1 builds the Docker MIPS toolchain, `argv[0]` watchdog guard, Debug SSH, and deploy script. Meanwhile, Section 2 hardware specs were measured on `V2.09`, and Section 1 states `V2.57` OTA files are OpenSSL-encrypted (`Salted__`) with an ECDSA-signed manifest. How a standalone DialOS installer obtains/patches a `V2.57` `rootfs`, migrates `diskOS`-booted dev devices (cleaning `/usr/data`), restores stock firmware, and fits dual A/B OTA slots inside `83 MB` `/usr/data` is deferred to Stage 8. Furthermore, `ui/ota.c` and `ui/debug_ui.c` depend on omitted `payload/` scripts (`diskos-verify.sh`, `S97diskos_install`, `S96diskos_select`, `diskos_launch.c`, `diskos-debug.sh`, `dropbearmulti`).
   * *Recommendation:* Split Stage 0 into `0a` (V2.57 access, `song.db` diff, `rootfs`/installer feasibility, toolchain + `argv[0]` spike runner) and `0b` (performance/touch/IPC spikes). Define `/usr/data` layout and boot/OTA slot contracts in Stage 1 (or cut custom installer and Wi-Fi OTA from `1.0` per `zoom_out`).

7. **Now Playing touch UX overload: invisible gestures, unmapped physical keys (`/dev/input/event0`), `250`–`300 ms` tap lag, and round-screen collisions**
   * *Lenses:* `comprehension`, `inventive_catalyst`, `zoom_out`
   * *Finding:* `Now Playing` assigns `7` local gestures and `2` global gestures onto a featureless `360x360` single-finger `cst816t` circle with zero text (even song title requires tapping). Hardware keys on `/dev/input/event0` are unmapped in Section 7, forcing play/pause onto touch double-tap and inserting a `250`–`300 ms` delay on every single tap while leaving pocket/eyes-free playback unspecified. With an adult fingertip (`~70`–`100 px`), left-edge back collides with swipe-right previous track/album carousel; right-rim volume collides with horizontal swipes and top-rim Control Center; center-ring scrub hides the expanded spindle timestamp directly under the thumb.
   * *Recommendation:* Inventory `/dev/input/event0` buttons and map play/pause, volume, and screen lock to physical keys (`comprehension`, `zoom_out`, `inventive_catalyst`); eliminate the `250`–`300 ms` delay; separate swipe/rim zones using disjoint polar bands `(r, theta)` (`inventive_catalyst`) or remove edge-back on swipeable screens (`comprehension`); render scrub time in the upper arc above the finger; add a dismissible first-run gesture guide.

8. **Wi-Fi password round-screen keyboard is an undesigned `1.0` dependency**
   * *Lenses:* `comprehension`, `implementation_readiness`, `inventive_catalyst`, `zoom_out`
   * *Finding:* On-device Wi-Fi login and Wi-Fi OTA require typing WPA2/WPA3 passphrases on a `360 px` circle where 10-column QWERTY shrinks keys to `~20`–`36 px`, yet the keyboard is explicitly undesigned.
   * *Recommendation:* Specify a `2`-tap radial sector keyboard (`>=70 px` targets) or paged grid (`>=44 px` targets) plus `wifi.conf` on SD or SoftAP phone provisioning before Stage 6 (`comprehension`, `inventive_catalyst`), or cut Wi-Fi UI and Wi-Fi OTA from `1.0` (`zoom_out`).

### Majors

1. **Unbudgeted `~64 MB` free RAM vs. cover decode spikes, CJK fonts, and wireless/TLS stacks** (`design_critic`, `implementation_readiness`, `codebase_details`)
   * Large embedded covers (`2000x2000` -> `16 MB` RGBA), uncapped cover/blur caches, static CJK bitmap tables (`10`–`15 MB` `.rodata`), and Wi-Fi + TLS spikes risk OOM-killing `mq_player` (rebooting the MCU).
   * *Reuse missed:* `diskOS`'s `ui/fontft.c` + `freetype-2.13.3` loads `/usr/project/config/fonts/SourceHanSans-Regular.ttc` straight from the read-only rootfs with a `1 MiB` / `4,096`-entry LRU glyph cache; `diskos_artdec.c` isolates decoding in a child process capped at `32 MiB` heap, `8 MiB` file size, and `4096 px`. Define an explicit `64 MB` RAM budget table (`VmHWM` ceiling, byte-capped cover cache, fixed `LV_MEM_SIZE` in `lv_conf.h`).

2. **`ipc` should be `Adapt` (not `Re-derive`), and `mq_player` crash invariants need a serialized state machine** (`codebase_details`, `implementation_readiness`)
   * `ui/mqsend.c` and `ui/psend.c` are `14`-line standalone CLI binaries. `ui/ipc.c` (`1,678` lines) implements critical invariants absent from `docs/COMMAND_MAP.md`: `mq_identity()` `fstat()` `(dev, ino)` recovery when `mq_player` restarts and unlinks `/ui`/`/player`, dual-mutex 64-bit stat locks on 32-bit MIPS, `V2.57` `a607` unsolicited-only reporting, `jsmn.h` object-key guards, and `raw_id()` 0-inclusive parsing. Wrap queue I/O behind the DAL while adapting `ipc.c`, and enforce a serialized command state machine for Bluetooth local-output pre-stop and length validation.

3. **macOS dev loop (`fake player`, mouse, Apple Silicon speed, `OFFSET` queries, and screenshot diffs) hides device failure modes** (`design_critic`, `zoom_out`, `comprehension`, `implementation_readiness`, `inventive_catalyst`)
   * Calling an in-process `fake player` skips `mq_*` framing, `EAGAIN`, and `V2.57` state quirks (including `A2` unreliable `state`, where `ui/playstate.c` infers play/pause from advancing `position_ms`). Apple Silicon NVMe masks `OFFSET` $O(K)$ SQLite scans over SDIO; a `1 px` mouse hides fingertip width; time-based rotation breaks screenshot diffs or creates golden-image churn (`zoom_out`).
   * *Fix:* Drive the fake player over an in-memory byte stream using recorded `V2.57` `/ui` + `/player` traces; assert `EXPLAIN QUERY PLAN` (zero `SCAN`, keyset paging, precomputed Letter-Seek index) on Mac + run `qemu-mipsel` tests in Docker + run on-device SSH `--bench` for `1M` tracks; inject virtual `clock_ms` and a `360x360` circular mask + `~60 px` touch cursor in SDL (or defer golden screenshot diffs until `1.0` layouts stabilize).

4. **Missing screen/overlay lifecycle host, inverted Stage 3–7 dependencies, and unsplit Stages 4–9** (`implementation_readiness`, `structural`, `inventive_catalyst`)
   * `screenmgr` is marked `Reference only`, yet no stage creates the replacement screen stack, overlay host (Control Center puck, Quick Actions, Track Details, dialogs), or global gesture router. Building `Now Playing` (Stage 3) before `Library` (Stage 4) and `Home` (Stage 5) orphans entry transitions and `Browse Library`; deferring `Modes: Mass Storage` to Stage 7 risks open-FD leaks across Stages 3–6; Stages 4–9 lack breakdown and exit criteria.
   * *Fix:* Define the SD-lease (`sdio`) and boot/storage contracts in Stage 1, build the screen/overlay shell and global gestures in Stage 2, run both rotation and carousel/SQLite rendering spikes in Stage 0b, split Stages 4 and 6, and defer cross-screen hero morph animations to a later polish pass (`inventive_catalyst`).

5. **User settings, EQ, Wi-Fi/BT pairings, Favorites, and Playlists lack persistence format, ownership split, and migration/rollback safety** (`codebase_details`, `implementation_readiness`, `structural`)
   * Section 7 lists ~20 settings plus Favorites and Playlists without specifying storage format, `schema_version`, forward-tolerant rollback parsing, playlist representation (M3U vs. `song.db` vs. DialOS DB + `0100` playback), or ownership split vs. `mq_player`/BlueZ/`wpa_supplicant`.
   * *Reuse missed:* `ui/config.c/.h` + `ui/tests/config_test.c` already implement atomic temp-file + `fsync` + `rename` + multi-key transactions tested under ASan/UBSan.

6. **Round-screen geometry breaks 10-band EQ sliders, Letter Seek, and Lyrics legibility, and misses error/handedness/MSC UX states** (`comprehension`, `zoom_out`)
   * Ten vertical sliders across `<=360 px` leave `<30 px` per band; auto-triggering Letter Seek hijacks flick scrolling (`~12`–`13 px`/letter) and ignores `CJK`/Cyrillic collation; white Lyrics text over frosted bright art fails contrast while bottom chords clip wrapped text. Missing states: `Quick Actions` auto-hide timer reset, `>=4.5:1` scrim floor, left-handed rim option, BT/Wi-Fi/OTA error banners, accidental USB unplug during Mass Storage, and `0`-track empty card vs. `Not Playing`.

7. **SSH deploy loop has `fiio_init.sh` watchdog races, early-crash lockout, and Apple Silicon Docker architecture ambiguity** (`comprehension`, `codebase_details`, `inventive_catalyst`)
   * Restarting `mq_ui` over SSH races with `fiio_init.sh` (two UIs fighting over `/dev/fb0` and `/ui`), early boot crashes force manual `diskOS` reboot recovery, USB/Wi-Fi tests cut off SSH logs, and Docker `linux/arm64` vs. `linux/amd64` emulation on Apple Silicon is unstated.
   * *Fix:* Adapt `tools/diskos-deploy.sh` with watchdog-safe atomic handoff, automatic fallback to the prior working `mq_ui` (or a minimal supervisor shim) on immediate crash, persistent ring-buffer logs on `/usr/data`, and documented Docker platform flags.

8. **Skipping `fwcaps`/`version` risks silent firmware skew against `V2.09` assumptions; `lrc.h` vs. `lyrics.c` and `lvgl/` patches** (`codebase_details`, `zoom_out`, `structural`, `implementation_readiness`)
   * Hardware/IPC notes came from `V2.09` while DialOS targets `V2.57` only and skips `fwcaps`/`version`. Without a minimal startup version/`rootfs` fingerprint check, booting on `V2.09` or `V2.58+` risks silent corruption or MCU reboots.
   * `ui/lrc.h` already provides embedded-tag extraction (`lyr_embedded_read()`) alongside `.lrc` parsing; `ui/lyrics.c` is LVGL UI + `lrclib.net` `wget` fetch. Document all `diskOS` patches inside `lvgl/` before freezing Stage 1.

### Nits

1. **Scope TDD boundary (`implementation_readiness`, `zoom_out`, `comprehension`):** Enforce strict TDD on pure domain logic (IPC codec, `playstate`, `lrc`, SQLite query builders, gesture/power/OTA state machines) and verify thin `/dev/fb0`/evdev HALs and LVGL layout via screenshot tests (with injected `clock_ms`) and on-device smoke tests.
2. **Stage 0 thresholds and fallbacks (`implementation_readiness`, `structural`):** Specify numeric pass/fail gates for `cst816t` sample rate (`>=60 Hz`, no rim dropouts), IPC round-trip (`<=50 ms`), `mq_player` track cap, `song.db` `V2.57` schema diff, and `V2.57` `rootfs` extraction.
3. **`83 MB` `/usr/data` partition budget (`design_critic`, `implementation_readiness`, `structural`):** Keep CJK fonts external (`fontft` on rootfs `SourceHanSans-Regular.ttc`), cap crash/telemetry logs (`<=2 MB`), and check `>=2x` bundle free space before OTA unpack.
4. **Consolidate Non-Goals & add `ui/Makefile` / `ui/tests/Makefile` (`structural`, `codebase_details`):** Strip `STOCK_AV_HEADERS` / `STOCK_SYSROOT` (`audio_stock`) from `ui/Makefile` while preserving `check-theme`, `-ffp-contract=off`, `-Wl,--gc-sections`, and `ui/tests/Makefile`'s ASan/UBSan host tests.
5. **Terminology, module count, and placement (`codebase_details`, `comprehension`):** Correct `221` modules to `214` unique `.c`/`.h` stems (`312` top-level files in `ui/`). Define `Debug Mode SSH`, `MIT installer`, `argv[0]` normalization, `local-output pre-stop`, `MCU`, `0100` list types, `puck` Control Center, `tile-fill slider`, and `trial boot` health check; expand `DAP`, `NOS`, `mcm`; clarify `MT type-B` single-finger, `sdio`, and `Home` arc pagination; move `File Transfer (Mass Storage)` out of the `Now Playing` paragraph into `Modes`.

### Inventive Contradictions & Ideal Final Result (`inventive_catalyst`)

| Contradiction | Proposed Transform | Ideal Final Result (IFR) |
|---|---|---|
| Full-bleed spinning art (`>=30 fps`) vs. no GPU & `180`-degree panel | Pre-baked polar texture (`180x1024`) + static circular LUT (with `180`-degree flip) + hardware LCD overlay plane split for non-rotating chrome | Zero trig and zero LVGL widget redraws per frame, hitting `60 fps` at single-digit CPU load while overlays sit static on a hardware plane |
| Unlimited library (`400k`–`1M` tracks) vs. `mq_player` caps, `83 MB` `/usr/data`, and Mass Storage | Decouple indexer via rolling queue/path playback (if Stage 0 confirms) + SD-resident rebuildable index (`TRUNCATE`, stat walk) + album-keyed 2-mip cover cache | Library capacity scales with microSD capacity, `/usr/data` stays constant-size, and `mq_player` holds only the active playback slice |
| Zero on-screen controls vs. `250`–`300 ms` tap lag, collisions, and discoverability | Map Play/Pause to `/dev/input/event0` (or `0 ms` Quick Actions reveal with tap-2 override) + disjoint polar bands `(r, theta)` + touch-down ghost ticks | Every gesture responds in `<=16 ms` with zero ambiguous zones and zero persistent screen clutter |
| Clean modular TDD rewrite vs. coupled `diskOS` C modules & watchdog risk | Strict BSP/HAL boundary quarantining adapted `diskOS` code + minimal crash-proof `mq_ui` supervisor managing SSH/OTA and spawning `dialos_app` | UI crashes never sever SSH recovery or trigger firmware watchdogs, and vendor/diskOS quirks stay behind one swappable HAL boundary |
| macOS-only host vs. real `V2.57` hardware/IPC verification | Recorded `V2.57` IPC trace fixtures in fake player + Docker `qemu-mipsel` gate + permanent SSH `--bench` telemetry | macOS TDD verifies real `V2.57` state machines in ms, Docker `qemu-mipsel` gates MIPS/memory ceilings automatically, and SSH benchmarks track real frame times |
| Standalone Wi-Fi OTA vs. round-screen keyboard & roadmap coupling | `2`-stage radial sector keyboard (`>=70 px` targets) + SoftAP / SD `wifi.conf` provisioning; run both rotation and carousel/SQLite spikes in Stage 0 and defer hero morphs to a polish pass | Users connect to Wi-Fi quickly without microscopic keys, and Stage 0 settles all rendering and library bottlenecks before screen construction |

## Goals vs. Non-Goals

### Goals (DialOS 1.0)
* Replace stock `mq_ui` on FiiO Snowsky Disc (`V2.57`) while keeping `mq_player` untouched.
* Build a clean-room, modular C codebase with a direct custom 2D software renderer (no LVGL, zero `diskOS` or `../dial-os` spaghetti copied into `src/`), testable on Apple Silicon macOS (pure-logic TDD + SDL2 simulator) and cross-compiled via `zig cc` (`mipsel-linux-musl`).
* Deliver tactile round-screen UX across `Home` (4-app curved arc), `Now Playing` (spinning art or fallback ring, scrub, volume, Quick Actions, Track Details, Lyrics), `Library` (Favorites, Playlists, Artists, Albums carousel, Genres, All songs, Letter Seek), `Settings` (Audio with hardware-verified presets/graphic EQ, Display with Dark Mode & Light Mode fresh semantic tokens, Bluetooth, Power & System + rescan), and `Modes` (`Mass Storage` file transfer).
* Support large microSD libraries (target up to `400k+`–`1M` tracks via SD-resident `.dialos/library.db` + M0-verified playback handoff) with memory bounded by the screen, CJK/Cyrillic glyph coverage, `>=30 fps` spinning art (subject to M0), and on-device crash/frame observability.
* Build a clean-room macOS USB Mask-ROM flasher, microSD hot-reload hook, and `use_stock`/stock-restore flow (wrapping only the verified X2000 Mask-ROM hardware blobs) for `1.0` installation and updates; defer Queue/Folder Browser polish (`1.1`), Wi-Fi & easy provisioning (`1.2`), signed Wi-Fi OTA (`1.3`), music downloads (`1.4`), receiver modes (`1.5`), and games (`1.6`) to `1.x`.

### Non-Goals & Anti-Goals (Out of 1.0 / Failure Modes to Prevent)
* **Anti-Goals (lessons from `../dial-os/` and `diskOS`):**
  * **No simulator-only "done":** No setting, mode, or playback feature is considered complete until verified on real V2.57 Disc hardware. If a control cannot be hardware-verified, it is cut from `1.0`.
  * **No flaky playback:** Music randomly stopping, broken play modes (shuffle/repeat/sequential), or `mq_player` EOF/seek races (`Q14`, `Q17`, `Q18`) are release blockers; playback reliability is soak-tested on hardware (M3a) before adding visual polish (M3b) or Settings/Modes.
  * **No hardcoded theme names in code:** Theme names never leak into file names, symbols, or logic; visual identity lives strictly in **Dark Mode** and **Light Mode** semantic tokens.
  * **No spaghetti code copied into `src/` or `tools/`:** Reuse only the unavoidable low-level X2000 Mask-ROM hardware blobs (`disc_spl_lpddr3.bin`, `my_write6_dram.bin`, `usbboot` transport) and optionally an isolated out-of-process cover decoder. Write the macOS flasher CLI and all `src/` runtime modules clean and unit-tested, using `diskOS` and `../dial-os/docs/hardware/` (`Q01–Q18`, `buttons.md`) strictly as reference checklists.
* Linux or Windows development host support (macOS Apple Silicon only).
* Firmware versions other than stock `V2.57` (beyond a startup version-mismatch guard).
* Replacing or modifying `mq_player` audio decoding/DSP binaries.
* **Deferred to `1.x`:** Wi-Fi Settings UI, round-screen keyboard, signed Wi-Fi OTA self-updates, and on-device music downloads.
* Games (`out of 1.0`).
* Post-1.0 receiver dashboards: USB DAC, Bluetooth receiver, AirPlay (`airplay_*`), Cast (`cast_*`), remote control (`remote_*`).
* Additional `diskOS` features skipped for `1.0`: `lastfm*`, `weather`, `netart`, `books`/`bookmeta`, `custom_saver_*`/`saver`, `search`, `upnext`, `folderbrowser`, `albumwall`, `audio_*`, `opustitle*`, `device_names*`, `colorpick`, `quicksettings`, `lrclib.net` online lyric fetch, `8` `diskOS` themes (`theme_*`), and multi-language `i18n*`.

## Alternatives Weighed

* **Forking `diskOS` vs. fresh DialOS repo (rejected by author; challenged by `zoom_out`):** Author rejected forking `diskOS` because `ui/` has `312` flat files with no module structure. `zoom_out` argues that copying ~25+ GPL `diskOS` modules while depending on `diskOS` to boot incurs a permanent manual backport tax without `git` ancestry, recommending either pruning a fork of `diskOS` into `src/` subdirectories or (`inventive_catalyst`) isolating adapted `diskOS` modules behind a strict BSP/HAL boundary.
* **Rust or Zig (rejected by author):** Cancels GPL-3.0 C code reuse from `diskOS` and lacks Tier-1 `mipsel` support in Rust.
* **Library-first vs. Now Playing-first (raised by author, kept Now Playing-first pending spikes; revised by `implementation_readiness`, `structural`, `inventive_catalyst`):** Author kept `Now Playing` first as the highest-risk rendering slice, re-evaluating after Stage 0 spikes. Reviewers add that a thin Screen/Overlay Shell + SD-lease contract must land in Stages 1–2 before either screen, and Stage 0b should benchmark both spinning art and the 5-cover carousel + `1M`-track SQLite queries so screen ordering is decoupled from risk.
* **Games in `1.0` (rejected by author):** Excluded from `1.0` scope.
* **All receiver dashboards in `1.0` (reduced by author to File Transfer):** USB DAC, Bluetooth receiver, and AirPlay dashboards deferred past `1.0`; `Mass Storage` (`0642`) kept in `Modes`.
* **In-process fake player vs. recorded `V2.57` trace / framed byte-stream simulator (`zoom_out`, `design_critic`, `inventive_catalyst`, `implementation_readiness`):** Reviewers recommend replacing an idealized in-process fake with a framed byte-stream simulator replaying recorded `V2.57` `/ui` + `/player` traces to catch length-prefix, `EAGAIN`, and `A2` state quirks.
* **Custom installer, Wi-Fi OTA, and round keyboard in `1.0` vs. `1.x` (`zoom_out`):** `zoom_out` proposes cutting the mask-ROM installer, Wi-Fi Settings UI, round keyboard, and Wi-Fi OTA from `1.0`, shipping over `diskOS`'s base flash + SD/SSH updates first to eliminate bricking and cryptographic/keyboard scope.
* **DialOS shadow index DB & replacement scanner vs. direct `song.db` keyset paging (`zoom_out`, `structural`, `implementation_readiness`):** Reviewers warn that a shadow DB risks `0100` index drift and FAT32 corruption; `zoom_out` advises querying `song.db` directly via keyset paging and accepting `mq_player`'s track ceiling for `1.0`.
* **30 fps generic LVGL rotation vs. polar LUT + LCD overlay planes vs. static cover baseline (`inventive_catalyst`, `zoom_out`):** `inventive_catalyst` proposes pre-baked polar textures + LUT + overlay planes to achieve `60 fps` without trig; `zoom_out` proposes shipping static art + rotating progress ring as the `1.0` baseline if CPU headroom is tight.
* **Touch double-tap & edge gestures vs. `/dev/input/event0` hardware keys & polar zones (`comprehension`, `zoom_out`, `inventive_catalyst`):** Reviewers recommend moving Play/Pause and Volume to physical keys (removing the `250`–`300 ms` tap lag) or partitioning touch into disjoint polar `(r, theta)` bands.
* **Miniaturized QWERTY vs. 2-tap radial sector keyboard / SoftAP / `wifi.conf` (`comprehension`, `inventive_catalyst`):** Proposed to make `360x360` Wi-Fi passphrase entry viable with `>=44`–`70 px` targets.

## diskOS Module Map

Verification against `b0hemia/diskOS` `main` (`312` top-level files in `ui/`, `214` unique `.c`/`.h` stems). Corrections and additions from `codebase_details` (and `structural`/`zoom_out`/`implementation_readiness`) are marked inline.

| `diskOS` module / path | Purpose | Action | Reason & Reviewer Corrections |
|---|---|---|---|
| `ipc` (+ `jsmn.h`) | Player command encode/decode over POSIX queues | **`[Corrected]` Adapt** (was `Re-derive`) | `1,678` lines of `fstat` `(dev, ino)` queue-recreation recovery, dual-mutex 64-bit stat locks, `V2.57` `a607` quirks, `jsmn.h` key guards missing from `COMMAND_MAP.md` (`codebase_details`) |
| `mqsend`, `psend` | Standalone `14`-line CLI queue debug tools | **`[Corrected]` Skip** (was `Re-derive`) | Standalone `int main()` CLI binaries, not UI modules (`codebase_details`) |
| `playstate` | Infers play/pause state from `position_ms` advancing | **Adapt** (`[Clarified]`) | Does not parse JSON frames (`ipc.c` does); compensates for `mq_player` `A2` `state` returning `0` while playing; has `tests/playstate_test.c` (`codebase_details`) |
| `fb_pan` | Framebuffer flush, `180`-degree panel | **Adapt** (`[Corrected]`) | Disables page-flipping, runs two `60`-row `PARTIAL` bands; direct writes tear on `360x360` updates (spike `FBIOPAN_DISPLAY`); requires `controls.c` (`codebase_details`) |
| `touchdisc`, `touchcap` | Standalone `cst816t` raw diagnostic binaries | **`[Corrected]` Skip** (or `Reference only`, was `Adapt`) | Standalone `int main()` CLI tools; `mq_ui` uses `lv_evdev_create` + `controls.c` `ctl_rot_touch_cal()` + `main.c` `g_touch_raw` wake fd (`codebase_details`) |
| `controls` (`controls.c/.h`) | Rotation blit, touch calibration, `0820`/`0821`/`0822` volume keys | **`[Added]` Adapt** | Missing in concept; hard compile-time dependency of `fb_pan.c` and owns physical volume-key mappings (`codebase_details`) |
| `musicdb` | `song.db` queries, `0100` play plans (`mdb_plan_*`) | **`[Corrected]` Split: Re-derive queries / Adapt `0100` plans** | `g_songs` in-RAM array (`~328 MB` at `400k` tracks) and `MDB_MAX_*` caps break "no limits"; `ORDER BY` parity and `V2.57` `MY_LOVE.ID` contract are mandatory (`codebase_details`, `design_critic`) |
| `scanner` (+ `audiodur.h`) | Library rescan, duration parsing, `iconv` legacy tags | **Adapt** (`[Added]` `audiodur.h`) | Note `0622` is a no-op on `V2.09`; relies on `audiodur.h` (`802` lines) for header-authoritative durations (`codebase_details`) |
| `art`, `artcache` | Cover extract/scale/cache | **Adapt** (`[Corrected]`) | `art.c` forks `diskos-artdec`; `artcache.c` caches `148x148`, `42x42`, `360x360` blurred, **not** `360x360` sharp covers (`codebase_details`) |
| `tools/diskos_artdec.c`, `tools/stb_image.h` | Sandboxed child-process image decode & `3`-pass blur | **`[Added]` Adapt** | Stock `V2.57` `ffmpeg` has no image decoders; enforces `32 MiB` heap / `8 MiB` file / `4096 px` safety caps (`codebase_details`) |
| `lrc.h`, `lyrics.c` | `.lrc` + embedded tag parsing vs. LVGL view + `lrclib.net` | **`[Corrected]` Split: Adapt `lrc.h` / Reference `lyrics.c`** | `lrc.h` already includes `lyr_embedded_read()`; `lyrics.c` adds LVGL view + online `wget` fetch excluded from `1.0` (`codebase_details`) |
| `sdio` | Mutex-guarded SD I/O lease & boot fault marker | **`[Corrected]` Adapt** (was `Re-derive`) | Sends zero IPC; prevents `scanner`/`artcache`/DB writes during `0642` USB Mass Storage (`codebase_details`) |
| `power` | `0664 = 0` takeover, idle timer, `sync` + `0806` shutdown | **`[Corrected]` Adapt** (was `Re-derive`) | Prevents `mq_player`'s unsynced `poweroff -f` from corrupting the SD card (data loss `2026-09-21`) (`codebase_details`) |
| `eqcustom`, `bt`, `wifi`, `radio_*`, `modes` | Settings and Mass Storage command sequences | **Re-derive** (`[Clarified]`) | Keep command ordering (BT pre-stop); `eqcustom` is fixed graphic EQ (`filterType: 0`) and `V2.57` blocks custom PEQ writes (`codebase_details`) |
| `config` (+ `tests/config_test.c`) | Atomic `fsync` + `rename` transactional key-value store | **`[Added]` Adapt** | Tested under ASan/UBSan; ready-made persistence layer (`codebase_details`) |
| `fontft` (+ `third_party/freetype-2.13.3`) | FreeType renderer over rootfs `SourceHanSans-Regular.ttc` | **`[Corrected]` Adapt** (was `Skip font_*`) | `1 MiB` / `4,096`-entry LRU cache gives CJK/Cyrillic at `10`–`140 px` with zero bundled font bloat (`codebase_details`) |
| `kbinput` | On-screen keyboard | **Reference only** | Needed only if Wi-Fi password entry stays on-device; round-screen design is new (`comprehension`, `zoom_out`) |
| `ota`, `ota_policy` | Signed update, trial boot, rollback | **Adapt (late)** (`[Clarified]`) | Must pair with `payload/` verify/boot scripts and explicit trial-boot readiness check (`codebase_details`, `structural`) |
| `main` | Watchdog `argv[0]`, `WORK_MODE=0`, SD mount, crash log, touch wake | **Adapt** (`[Corrected]`) | Does more than `argv[0]`: resets `SYSCONFIG.WORK_MODE=0`, mounts `/tmp/sdcard` on `V2.57`, opens `g_touch_raw`, runs `crash_log()` + `45` s `boot_alarm_handler` (`codebase_details`) |
| `debug_ui` | Debug Mode SSH enablement | **Adapt (early)** (`[Clarified]`) | Requires `payload/diskos-debug.sh` + `payload/dropbearmulti` (`codebase_details`) |
| `payload/diskos-debug.sh`, `dropbearmulti`, `diskos-verify.sh`, `S97diskos_install`, `S96diskos_select`, `src/launcher/diskos_launch.c` | Runtime SSH/OTA verify scripts & boot launcher | **`[Added]` Adapt / Reference only** | Required runtime plumbing behind `debug_ui`, `ota`, and trial-boot slot switching (`codebase_details`) |
| `prof`, `fbshot`, `mqcap`, `mqfeed`, `tools/tinj.c`, `tools/diskos-deploy.sh`, `tools/diskos-shot.sh` | Frame/CPU profiler, screen capture, IPC capture/replay, touch inject, deploy | **`[Added]` Adapt** | Ready-made on-device observability, IPC trace capture, and safe SSH deploy (`codebase_details`) |
| `Dockerfile`, `toolchain/`, `lv_conf`, `lvgl/`, `ui/Makefile`, `ui/tests/Makefile` | Build toolchain, LVGL `9.2`, Makefiles | **Adapt** (`[Corrected]`) | Add Makefiles (strip `STOCK_AV_HEADERS`/`STOCK_SYSROOT` for `audio_stock`, keep `check-theme`, `-ffp-contract=off`, `-Wl,--gc-sections`, ASan/UBSan tests); inventory `lvgl/` patches; review `LV_DEF_REFR_PERIOD 33` (`codebase_details`, `implementation_readiness`) |
| `screenmgr`, `ui`, `home`, `library`, `settings`, `anim`, `toast`, `ui_dialog`, `npmenus`, `songinfo` | Screens and navigation (`controls` moved out above) | **Reference only** | Structure rejected (`controls` extracted to `Adapt`) (`codebase_details`) |
| `airplay_*`, `cast_*`, `remote_*`, `receiver*`, `lastfm*`, `weather`, `netart`, `books`, `bookmeta`, `custom_saver_*`, `saver`, `search`, `upnext`, `folderbrowser`, `albumwall`, `audio_*`, `opustitle*`, `device_names*`, `colorpick`, `quicksettings` | Features outside `1.0` | **Skip** | Out of scope |
| `theme_*`, bitmap `font_*`, `i18n*` | `diskOS` visual identity, `8` themes, translations | **Skip** (`[Corrected]`) | DialOS has `2` themes; CJK handled by `fontft` above (`codebase_details`) |
| `fwcaps`, `version` | Firmware version & quirk detection | **`[Corrected]` Reference only (checklist) + Adapt startup check** (was `Skip`) | Skip multi-firmware UI branching, but run a startup `V2.57` version/schema guard and honor `V2.57` quirks (PEQ write block, `MY_LOVE.ID`, `0649`, direct SD mount, radio boot arbitration) (`codebase_details`, `structural`, `zoom_out`) |

## Proposed Roadmap

See [ROADMAP.md](../../../ROADMAP.md) for the canonical milestone breakdown (`M0` -> `M8` for `1.0`, plus `1.1`–`1.6`).

## Key Decisions & Staging for Next Steps

All six architectural decisions are settled (M0 hardware spikes supply the final measurements for decisions 2, 3, and 5):

1. **Codebase origin (`[Decided: Clean-room DialOS repo; Mask-ROM blobs only, zero LVGL]`):** Clean DialOS repository with zero `diskOS` or `../dial-os` code copied into `src/` or `tools/`. Only the verified X2000 Mask-ROM hardware blobs (`disc_spl_lpddr3.bin`, `my_write6_dram.bin`) and `usbboot` USB transport (plus optionally an isolated out-of-process cover decoder helper) are reused as vendor assets. The macOS flasher/SD-update CLI and all runtime modules (`src/bsp/` watchdog/crash log/SD lease/power/IPC/playstate, direct 2D renderer + `/dev/fb0` 180° HAL, `.dialos/library.db` + scanner, atomic config, lyrics, gestures, screens, navigation) are built clean and unit-tested in DialOS using `diskOS` and `../dial-os/docs/hardware/` strictly as reference checklists for V2.57 invariants.
2. **Library ceiling & `song.db` ownership (`[Decided: Option A + M0 Plan B]`):** V2.57 binary RE confirmed no hardcoded local track cap in `mq_player`, but `/usr/data/fiio/db/song.db` + `LIST_SONG_0` hits the `83 MB` `/usr/data` ceiling. **Decision:** DialOS keeps an indexed SQLite catalog on the microSD card (`.dialos/library.db`, `journal_mode=TRUNCATE`, canonical path keys, guarded by SD-lease teardown) and feeds only the active playback slice to `mq_player` via reserved `CUSTOM_PLAYLIST` / `comm_folder_play_control`. M0 verifies rolling-slice handoff; **Plan B:** if rolling slices stutter or break gapless/shuffle at slice boundaries, pass the full active album/playlist/folder to `mq_player` and slice `All songs` only above a safe track threshold.
3. **Rendering baseline (`[Decided: Direct custom 2D renderer, no LVGL; M0 gate]`):** Direct custom `360x360` XRGB8888 2D software renderer (dropping LVGL and `fb_pan.c` to eliminate widget-tree overhead and `LV_DEF_REFR_PERIOD 33` cap), using fixed-point integer scanline rotation (or polar-LUT + hardware LCD overlay planes if needed). M0 measures active-playback `360x360` rotation fps and `FBIOPAN_DISPLAY` tearing on real X2000 hardware; fallback to static cover art + rotating progress ring if `<30 fps`. Freeze vinyl spin when blurred overlays (`Lyrics`, `Quick Actions`) open so blur is computed once per track (`90x90` downsample).
4. **Input & physical key contract (`[Decided via ../dial-os/docs/hardware/buttons.md]`):** V2.57 `x2000_key` codes on `/dev/input/event0` are already verified: `mq_player` handles short-press Play/Pause (`250`) and Volume Up/Down (`251`/`252`) directly; DialOS handles long-press/double-click (`268`/`269`) and Power/Lock (`265`/`259`/`264`). Touch uses disjoint polar zones `(r, theta)` (`DIAL_ZONE_VINYL_SEEK`, `start_x >= 60` swipe separation) with scrub timestamp rendered above the thumb, eliminating the `250–300 ms` touch double-tap delay.
5. **`V2.57` EQ scope (`[Decided: Hardware-verified presets + graphic EQ]`):** `1.0` ships only hardware-verified fixed EQ presets + graphic EQ (cutting custom 10-band PEQ unless M0 proves V2.57 accepts PEQ writes).
6. **Install/update flow & `1.x` deferrals (`[Decided: Clean-room macOS flasher + microSD hot-reload for 1.0; Wi-Fi/OTA in 1.x]`):** `1.0` uses a clean-room macOS USB Mask-ROM flasher (`flash` / `restore-stock`), microSD hot-reload (`sd-update`), and `use_stock` escape hatch. Queue/Folder Browser polish (`1.1`), Wi-Fi & easy provisioning (`1.2`), signed Wi-Fi OTA (`1.3`), music downloads (`1.4`), receiver modes (`1.5`), and games (`1.6`) are deferred to `1.x`.

## Reviewer Status

| Reviewer Lens | Role Title | Status | Report |
|---|---|---|---|
| `design_critic` | Hostile Design Critic Brainstormer | `completed` | `01_design.md` |
| `codebase_details` | Codebase Details Brainstormer | `completed` | `02_codebase.md` |
| `zoom_out` | Skeptical Architect Brainstormer | `completed` | `03_zoom_out.md` |
| `comprehension` | UX & Ergonomics Brainstormer | `completed` | `04_comprehension.md` |
| `implementation_readiness` | Implementation Readiness Brainstormer | `completed` | `05_readiness.md` |
| `structural` | Migration & Compatibility Brainstormer | `completed` | `06_structural.md` |
| `inventive_catalyst` | Inventive Catalyst Brainstormer | `completed` | `07_inventive.md` |
