# M0 — V2.57 Hardware Access, Clean `dial-os-mac` Rewrite & Feasibility Spikes

**Date:** 2026-11-21
**Scope:** Milestone `M0` of [ROADMAP.md](../../../ROADMAP.md)
**Target Hardware:** FiiO Snowsky Disc (`V2.57`, Ingenic X2000 dual-core MIPS32r2, `360x360` XRGB8888 180°-rotated `/dev/fb0`, `cst816t` touch on `/dev/input/event1`, `x2000_key` buttons on `/dev/input/event0`, dual CS43131 wired DAC)
**Host Environment:** Apple Silicon macOS (`zig 0.17.1`, Python 3 standard library)

---

## 1. Intent & Why

Before writing any M1+ product code, we need real measurements from the FiiO Snowsky Disc (`V2.57`) so architectural decisions (spinning cover art vs. static art + rotating ring, rolling-slice playback vs. Plan B, graphic EQ vs. PEQ, and `mq_player` EOF/seek behavior) are based on hardware data instead of guesses.

At the same time, the legacy `../dial-os` macOS flasher (`dial-os-mac`) violates our coding standards (cryptic abbreviations, theme leakage, hardcoded `/Volumes/MUSIC`, zero unit tests). Rewriting `dial-os-mac` clean in M0 gives us a trustworthy, unit-tested, SD-card-safe deployment and recovery tool from day one.

---

## 2. Goals, Non-Goals, and Anti-Goals

### Goals
1. **Clean-room, TDD-tested `dial-os-mac` CLI** in `dial-os-new` conforming to all global naming/style conventions, reusing only the 5 proven vendor binaries (`usbboot`, `disc_spl_lpddr3.bin`, `my_write6_dram.bin`, `mksquashfs`, `unsquashfs`).
2. **Absolute SD-card non-destruction safety** across flashing, SD updates, and on-device spikes.
3. **`zig cc` MIPS32r2 cross-compilation** producing static `O32 FP64A` + `EF_MIPS_NAN2008` (`0x400`) binaries deployable in ~10 seconds via the Disc's existing microSD hot-reload hook (`/tmp/sdcard/.dial-os/mq_ui`).
4. **Interactive 5-stage M0 hardware spike binary** (`spike/m0_bench.c`) measuring all 5 M0 gates on real V2.57 hardware over **wired headphones** (`24/192` FLAC + EQ) and writing structured results (`m0_report.txt`) + golden IPC traces (`m0_ipc_trace.log`) to `/tmp/sdcard/.dial-os/`.
5. **Operator Transparency HTML Guide** (`docs/superpowers/specs/2026-11-21-m0-operator-guide.html`) showing exact step-by-step Mac/Disc actions, visual screen mockups for every stage, and safety checks.

### Non-Goals
- **No Bluetooth in M0:** Bluetooth is deferred to the end (`M6b`). M0 benchmarks run under wired headphone playback (`24/192` FLAC + EQ) so core internals are proven reliably first without BT quirks.
- **No `diskOS` tooling, Wi-Fi, or SSH:** Strictly SD-card hot-reload and SD-card log collection using our own tools.
- **No M1+ product architecture yet:** `spike/m0_bench.c` is explicitly throwaway code kept outside `src/`. It will not be carried into `src/` in M1.
- **No firmware versions other than `V2.57`** for building or flashing.

### Anti-Goals (Failure Modes to Prevent)
- **Erasing or corrupting the microSD card:** Never ship an unpatched `mq_player` containing `rm -rf %s` in `mount_sdcard()`; never format, delete, or modify user music files on the SD card during `sd-update` or on-device spikes; never let `mq_player`'s idle `poweroff -f` (`0664`) run without `sync`.
- **Silent flash/deploy errors:** Every SHA-256 check, `mq_player` safety patch check, ELF `EF_MIPS_NAN2008` patch, and SD write must verify and fail loudly on mismatch.
- **Simulator-only guesses:** M0 does not close until real benchmark numbers and fallback decisions from the Disc are recorded.

---

## 3. Hardware & Platform Constraints

- **Authorized protocol/hardware reference:** Per [ROADMAP.md](../../../ROADMAP.md#L10-L12), `../dial-os/docs/hardware/` (`platform.md`, `display-touch.md`, `buttons.md`, `ipc-protocol.md`, `quirks.md`) and `../dial-os/macos/dial_os_mac_installer.py` are authorized **strictly as reference specifications** for V2.57 hardware constants, IPC wire formats (`0602`, `0508`, `0201`, `0103`, `0642`, `0666`, `0657`, `0664`, `0715`, `a202`, `a103`), backlight sysfs paths, DGSB 272-byte struct layout, OpenSSL PBKDF2-HMAC-SHA256 chunk decryption, and `usbboot` DRAM addresses (`0xa0c00000`, `0xa1000000`, `0xa0b00000`, `0xa0a00000`). **Zero code is copied** into `dial-os-new`; all logic is written fresh and unit-tested.
- **Existing Disc state & hot-reload ownership:** The target Disc already has the microSD hot-reload hook flashed on NAND (`/usr/bin/mq_ui` stages `/tmp/sdcard/.dial-os/mq_ui` to `/usr/data/.dial-os/mq_ui` + `/tmp/mq_ui.sd` and `exec`s it; `/tmp/sdcard/.dial-os/use_stock` escapes to `/usr/bin/mq_ui.stock`). Routine M0 deploys use `./dial-os-mac sd-update` (~10 seconds) and **do not flash NAND**. Consequently, `dial-os-mac build` installs `--ui-binary` directly as `/usr/bin/mq_ui` (preserving stock as `/usr/bin/mq_ui.stock`); DialOS's own clean-room hot-reload boot shim is built into `mq_ui` in `src/bsp/` during M1 before any full NAND flash in M8.
- **Cross-compile ABI:** `zig cc -target mipsel-linux-musleabihf -mcpu=mips32r2 -mfp64 -mno-odd-spreg -mnan=2008 -static -ffp-contract=off -O2 -s` followed by setting bit `0x400` (`EF_MIPS_NAN2008`) in ELF header `e_flags` (byte offset `36`, little-endian `uint32`).
- **DGSB base-gate digest compatibility:** `my_write6_dram.bin` (expected SHA-256 `a3c3aa63e0c48f501181d542d9fa0526d36ded81e2e767f0c044af876674ea84`) validates a 272-byte DGSB plan containing a SHA-256 fingerprint over the 4-version base-gate catalogue (`209`, `228`, `240`, `257`). `dial-os-mac` restricts *building and flashing* to `V2.57` only, while preserving the exact 4-version catalogue digest bytes required by `my_write6_dram.bin` and zero-padding SquashFS images to `100,663,296` bytes (`768 × 128 KiB` blocks).
- **Watchdog & IPC invariants:**
  - `prctl(PR_SET_NAME, "mq_ui")` so `/usr/project/fiio_init.sh`'s `pgrep -x mq_ui` does not respawn stock UI.
  - Create `/ui` POSIX mqueue (`O_CREAT | O_RDONLY | O_NONBLOCK`, `mq_maxmsg = 10`, `mq_msgsize = 8192`) and pass a `>= 8192`-byte buffer (`16384` bytes) to `mq_receive()` (`Q01`).
  - Lazily open `/player` (`O_WRONLY | O_NONBLOCK`) after `mq_player` starts.
  - Assert wired CS43131 route on connect (`0642000C0000`, `0666000C0006`, `0657000C0008`, `0715000C<vol>`) and disable `mq_player` idle power-off (`0664000C0000`).
- **Display & input invariants:**
  - `/dev/fb0` is `360x360` visible (`360x1080` triple-buffered virtual height), 32-bpp XRGB8888, mounted 180° upside-down (`dst[(359 - y) * stride + (359 - x)] = src[y * 360 + x]`).
  - `/dev/input/event1` (`cst816t`) emits Linux MT Type-B (`ABS_MT_TRACKING_ID`, `ABS_MT_POSITION_X`, `ABS_MT_POSITION_Y`), inverted 180° (`x = 359 - raw_x`, `y = 359 - raw_y`).
  - `/dev/input/event0` (`x2000_key`): short Play/Pause (`250`) and Volume Up/Down (`251`/`252`) are handled natively by `mq_player`; Long-Press Play/Pause (`268`, `>= 1.0 s`) and Double-Click Play/Pause (`269`) are ignored by `mq_player` and safe for spike navigation.

---

## 4. Approaches Considered

### Approach 1: Single Interactive Multi-Stage Spike Binary + Raw SD Logs *(Selected)*
- **Design:** One throwaway C spike binary (`spike/m0_bench.c`) deployed once via `./dial-os-mac sd-update`. Controlled on-device via large touch buttons (`RUN` / `NEXT`) + physical Long-Press Play/Pause (`268`) backup, while native Play/Pause (`250`) and Vol Up/Down (`251`/`252`) let the operator verify wired `24/192` FLAC playback before each stage. Flushes (`fsync`) `m0_report.txt` and `m0_ipc_trace.log` to `/tmp/sdcard/.dial-os/` after every stage.
- **Tradeoffs:** Slightly larger throwaway file (~500–700 lines of C) than single-purpose programs, but requires only one SD card swap to collect all 5 benchmarks and gives exact human control over active playback and timed rim-touch input.

### Approach 2: Five Separate Auto-Run Micro-Spike Binaries *(Rejected)*
- **Design:** Five standalone ~120-line binaries (`m0_rot`, `m0_touch`, `m0_handoff`, `m0_eq`, `m0_trace`) that auto-run on boot for 10–15 seconds, flush to SD, and idle.
- **Tradeoffs:** Tiny individual binaries, but forces 5+ SD-card swap/reboot cycles and makes it awkward to coordinate active `24/192` FLAC playback and finger placement on the rim before an auto-run timer fires.

**Why Approach 1:** All 5 gates either depend on active wired audio playback or timed human touch interaction; flushing after each stage provides the same crash resilience without 5 reboot/SD-swap cycles.

---

## 5. Detailed Design

### 5.1 Repository & Build Layout for M0

- Initialize `git` repository in `dial-os-new` (so all M0 work follows branch/worktree/atomic-commit discipline from the start).
- Layout introduced in M0:
  - `dial-os-mac` — executable bash wrapper invoking `python3 tools/dial_os_mac/cli.py "$@"`.
  - `tools/dial_os_mac/` — clean-room Python package (stdlib only, zero third-party deps, zero prohibited abbreviations, zero theme names).
  - `tools/vendor/` — the 5 proven vendor binaries copied from `../dial-os/macos/vendor/`: `usbboot`, `disc_spl_lpddr3.bin`, `my_write6_dram.bin`, `mksquashfs`, `unsquashfs`.
  - `tests/tools/` — Python `unittest` suite testing all pure logic in `tools/dial_os_mac/` (TDD).
  - `spike/m0_bench.c` — throwaway M0 on-device benchmark binary (explicitly outside `src/`).
  - `Makefile` — targets: `test-tools`, `m0-spike` (cross-compile + `EF_MIPS_NAN2008` patch -> `build/m0_mq_ui`), `clean`.
  - `docs/superpowers/specs/2026-11-21-m0-operator-guide.html` — detailed visual operator guide.

### 5.2 Clean-Room `dial-os-mac` CLI Specification

Commands supported by `./dial-os-mac`:
- `doctor`: Verifies macOS host, `zig`, the 5 vendor binaries (including `my_write6_dram.bin` SHA-256), V2.57 stock rootfs availability (`vendor/stock_v257/rootfs.squashfs`), USB Mask-ROM device count (`ioreg -p IOUSB` matching `idVendor=41224`, `idProduct=60143`), mounted microSD volumes under `/Volumes/`, and saved stock backups in `~/Library/Application Support/DialOS/backups/`.
- `build [firmware_zip] [--ui-binary PATH]`:
  - Uses verified `vendor/stock_v257/rootfs.squashfs` directly if SHA-256 matches `V2.57` (`111e4dd7ee3d7ff91ba7e61181690be7ffd22bd1bbd13ad513f5f015ffb302ae`, `80,596,992` bytes); otherwise decrypts `V2.57` chunks from an official FiiO ZIP via PBKDF2-HMAC-SHA256 + macOS `/usr/bin/openssl` (rejecting non-V2.57 archives).
  - Mounts a temporary Case-Sensitive APFS sparse image via `hdiutil` (always detached in `finally`).
  - Unpacks SquashFS, preserves `/usr/bin/mq_ui` as `/usr/bin/mq_ui.stock`, installs the specified MIPS32r2 `mq_ui` binary (after verifying its ELF header has `EF_MIPS_NAN2008` set).
  - **Mandatory SD-card safety patch:** Scans `/usr/bin/mq_player` for `b"rm -rf %s\x00"`, replaces with `b"rmdir  %s\x00"`, and **asserts** `b"rm -rf %s\x00"` no longer exists and `b"rmdir  %s\x00"` is present (fails loudly otherwise).
  - Repacks with `mksquashfs -comp lzo -b 131072 -no-xattrs -all-root -noappend -processors 1`, pads to `100,663,296` bytes (`build/dialos_v257_nand.bin`), and saves a verified untouched stock backup (`stock_V257_nand.bin`).
- `flash`: Prints an explicit reminder to **eject/remove the microSD card before USB Mask-ROM flashing** as a physical safety belt, verifies `my_write6_dram.bin` SHA-256, generates the 272-byte DGSB plan (preserving the exact 4-version base-gate digest table), runs `usbboot` under `caffeinate -dimsu`, and validates readback (`magic=0x4006E006`, `done=0x55555555`, `result=0x600DF10C`, `gate=1`).
- `restore-stock`: Flashes the saved `stock_V257_nand.bin` backup with the same verification and SD-removal reminder.
- `sd-update [--binary PATH] [--volume PATH]`:
  - Auto-detects the mounted SD card volume (or uses `--volume` if multiple external volumes are mounted; refuses to guess if ambiguous).
  - Verifies the source binary is a valid MIPS32r2 ELF with `EF_MIPS_NAN2008` (`0x400`).
  - Creates `<VOLUME>/.dial-os/` if missing, writes to `<VOLUME>/.dial-os/.mq_ui.tmp`, `fsync`s, and atomically `os.replace()`s to `<VOLUME>/.dial-os/mq_ui`. **Never** deletes, formats, or touches any other file or folder on the SD card.
  - Removes `<VOLUME>/.dial-os/use_stock` only if `--clear-stock-flag` is passed (otherwise warns if `use_stock` is present so the user isn't surprised why stock UI booted).
- `use-stock [--volume PATH]` / `use-dialos [--volume PATH]`: Safely creates or removes `<VOLUME>/.dial-os/use_stock` to toggle the instant stock escape hatch without touching anything else.

### 5.3 M0 Interactive 5-Stage Spike Binary (`spike/m0_bench.c`)

#### Boot, Safety & Test Track Discovery
1. Calls `prctl(PR_SET_NAME, "mq_ui")`.
2. Creates `/ui` mqueue (`10` msgs x `8192` bytes, `16384`-byte receive buffer), lazily opens `/player`.
3. Initializes `/dev/fb0` (clears all 3 pages, pans to `yoffset = 0`, sets backlight `180`), opens `/dev/input/event0` and `/dev/input/event1` (`O_NONBLOCK`).
4. On `/player` connect: asserts wired CS43131 output (`0642000C0000`, `0666000C0006`, `0657000C0008`), disables `mq_player` idle power-off (`0664000C0000`), sets moderate initial volume (`0715000C0030` = 48/120).
5. Discovers test audio tracks: scans `/tmp/sdcard/.dial-os/m0_test/` first for `.flac`/`.wav`/`.mp3` files (expects at least 2 tracks — real `24/48` FLAC is sufficient for normal wired testing, and an optional synthetic `24/192` FLAC test tone can be generated on Mac if we also want to measure peak decode load); if empty, falls back to the first 2 audio files found in `/tmp/sdcard/`. **Read-only** access to all audio files.
6. Every stage appends results to `/tmp/sdcard/.dial-os/m0_report.txt` and raw timestamped IPC frames to `/tmp/sdcard/.dial-os/m0_ipc_trace.log`, calling `fflush()` + `fsync()` after each stage, and then displays an on-screen **Stage Result Summary** (`PASS` / `FAIL` / `LOGGED`, exact captured metrics, and `SAVED TO SD`) before you tap `NEXT`.

#### On-Device UI & Controls (Minimal 8x8 Bitmap Font on `360x360` Direct FB)
- Top header: Stage number (`M0 1/5` .. `M0 5/5`) + stage name + live playback status (`PLAY` / `PAUSE` / `STOP` + current `position_ms`).
- Center area: Live test readout while running, followed by the **Stage Result Summary** (status badge + exact measured numbers + `SAVED TO SD`) once the stage finishes.
- Bottom zone: Two large high-contrast touch targets inside the safe circular chord:
  - Left button: **`RUN`** (starts/re-runs current stage; in Stage 1 auto-starts track 1 via `0602` if playback is stopped).
  - Right button: **`NEXT`** (advances to next stage; after Stage 5 shows **`DONE`** and runs `sync()`).
  - **Stage 2 touch lockout:** During Stage 2's 10-second rim-trace countdown, both `RUN` and `NEXT` buttons are **hidden and completely ignored** so tracing the full 360° rim can never accidentally press a button; they reappear automatically when the countdown hits `0 s`.
- Physical keys:
  - Short Play/Pause (`250`) & Vol Up/Down (`251`/`252`): handled natively by `mq_player` so you can pause/resume playback or adjust wired headphone volume anytime.
  - Long-Press Play/Pause (`268`, hold `>= 1.0 s`): backup trigger for `RUN` / `NEXT` in case touch is busy.

#### The 5 Hardware Benchmark Stages & Exit Gates

| Stage | What It Measures (Wired `24/192` FLAC + EQ Active) | Pass Gate | Fallback / Decision Recorded |
|---|---|---|---|
| **1. Rotation & Tearing** | *(Design call)* Per-pixel `16.16` fixed-point affine nearest-neighbor rotation of a real `360x360` XRGB8888 procedural texture buffer (circular `~101,788`-pixel mask + `180°` flip baked into destination write) with a moving vertical tear bar: 5 s single-page direct blit + 5 s triple-buffer `FBIOPAN_DISPLAY`. Logs `avg fps`, `min fps`, `p99 frame ms`, plus `/proc/asound` xrun snapshot (if readable) alongside operator listening. | `>= 30 fps` average, zero audible underruns, tearing behavior observed visually + logged | If `< 30 fps` or audio stutters → fallback to static cover art + rotating progress ring; record whether `FBIOPAN_DISPLAY` eliminates tearing |
| **2. Rim Touch Rate** | 10 s capture window while operator traces the outer rim (`r ≈ 140..175 px`); logs every `SYN_REPORT` timestamp and inverted `(x, y)` | `>= 60 Hz` effective sample rate during continuous touch, zero unexplained `> 50 ms` dropouts | If `< 60 Hz` or rim dead-zone exists → record safe inner radius & smoothing parameters for M2 |
| **3. Rolling-Slice Handoff** | *(Design call — zero SQLite dependency, zero writes to `/usr/data/fiio/db/song.db`)* Tests alternating `0602` M3U files (`//tmp/dialos_play_A.m3u` / `B.m3u`): (a) multi-track M3U slice advancing naturally across track boundary, (b) slice swap `A -> B` triggered at track-end `a202` and mid-track, and (c) rapid `< 2 s` back-to-back `0602` switch to measure stock `Q17` rate-limiting. | Clean track handoff without stutter, `mq_player` freeze, or stock `song.db` fallback | If rolling M3U slices stutter or break gapless/shuffle at boundaries → trigger **Plan B** (full active album/playlist/folder to `mq_player`, slice `All songs` only above safe threshold) |
| **4. V2.57 EQ/PEQ Probe** | Sends fixed EQ preset (`0508`), graphic EQ (`filterType: 0`), and custom PEQ probe frames; captures exact `/ui` responses | Record exact accepted/rejected `filterType` and audible effect on wired output | Confirms `1.0` EQ scope: fixed presets + graphic EQ (or PEQ if accepted) |
| **5. Golden IPC Trace (`Q14`/`Q17`/`Q18`)** | Drives play, pause, resume, `95%` seek, `98%` seek, and natural track EOF; records every raw `/player` TX and `/ui` RX frame with `clock_gettime(CLOCK_MONOTONIC)` ms timestamps | Complete raw trace saved to `/tmp/sdcard/.dial-os/m0_ipc_trace.log` | Becomes the golden V2.57 replay fixture for M1 macOS simulator and M3a playback state machine |

---

## 6. What's Left to the Builder

- Internal module decomposition inside `tools/dial_os_mac/` (e.g. `cli.py`, `firmware.py`, `flasher.py`, `elf.py`, `sd_deploy.py`, `constants.py`).
- Exact 8x8 bitmap font table and fixed-point sin/cos LUT implementation inside `spike/m0_bench.c`.
- Exact synthetic ELF/SquashFS/ZIP test fixtures inside `tests/tools/`.
- Exact placement of the recorded M0 benchmark report in the repo (`docs/benchmarks/m0-v257-results.md`) once hardware runs complete.
