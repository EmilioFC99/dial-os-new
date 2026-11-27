# DialOS Roadmap

> **How this roadmap works:** DialOS is built **one phase at a time**, from first principles against real **FiiO Snowsky Disc (V2.57)** stock binaries (`vendor/stock_v257/`) and real hardware.
>
> We do **not** build speculative "foundations" upfront. Shared layers (display, input, IPC, storage) emerge organically as each phase needs them. Product UX vision lives in [docs/north_star.md](docs/north_star.md).

## Per-Phase Workflow

Every phase follows the same five steps before moving to the next:

1. **Brainstorm** — Define ideas, user experience, and scope for this module.
2. **Inspect Stock V2.57** — Examine `vendor/stock_v257/` (`rootfs.squashfs`, `mq_ui`, `mq_player`) to understand what stock firmware does, what helps us, what to reuse, and what we can improve.
3. **Define Tests & Metrics Upfront** — Specify the unit tests, edge cases, and hardware pass/fail metrics before writing implementation code.
4. **Review** — Review the design and test plan.
5. **Develop & Hardware-Verify** — Implement with TDD, deploy via `./dial-os-mac sd-update`, and verify on the real Disc (`V2.57`).

## Guardrails

- **Host & Target:** Apple Silicon macOS (`zig cc` targeting `mipsel-linux-musleabihf`, `EF_MIPS_NAN2008`); FiiO Snowsky Disc stock firmware `V2.57` only; GPL-3.0-or-later.
- **First-Principles V2.57 Rule (see [AGENTS.md](AGENTS.md)):** Direct custom `360x360` XRGB8888 2D renderer (no LVGL). Inspect stock `V2.57` binaries directly — never blindly imitate stock `mq_ui`. Only reuse the 5 verified USB Mask-ROM / SquashFS binaries in `tools/vendor/`.
- **SD-Card Non-Destruction:** Never ship unpatched `mq_player` (`rm -rf %s` -> `rmdir  %s`), never modify user music files, write DialOS state only inside `.dial-os/` atomically (`temp` + `fsync` + `rename`), and disable `mq_player`'s unsynced idle `poweroff -f` (`0664=0`).
- **No Theme Leakage:** Zero theme names in file names, identifiers, or strings; use semantic **Dark Mode** and **Light Mode** tokens only.
- **Hardware-Verified Only:** No phase is complete until verified on the real FiiO Snowsky Disc (`V2.57`).

---

## Phases

### Phase 0 — Hardware Access & Baseline Metrics *(Done)*
- Clean-room `./dial-os-mac` CLI (`build`, `flash`, `sd-update`, `use-stock`, `restore-stock`) with microSD hot-reload hook and `mq_player` SD safety patch.
- Real V2.57 hardware baseline recorded in [docs/benchmarks/m0-v257-results.md](docs/benchmarks/m0-v257-results.md) (`270 fps` triple-buffer rotation, `~38–40 Hz` `cst816t` touch, clean `0100` M3U handoff, `147`-frame golden IPC trace).

### Phase 1 — Boot Loadscreen & Core Startup *(Done)*
- First-principles startup sequence, `360x360` `180°` triple-buffered display initialization, animated boot loadscreen, `mq_player` `aa24` readiness handoff, idle `poweroff -f` disable (`0664=0`), physical key handling (sleep/wake, poweroff, volume), watchdog survival, and atomic crash logging.

### Phase 2 — Theme & Themed Boot Screen *(Done)*
- Semantic **Dark Mode** and **Light Mode** design tokens (palette, surfaces, typography scale, spacing, motion curves), typography via rootfs `SourceHanSans-Regular.ttc` (Latin/CJK/Cyrillic), and shared 2D round-screen drawing primitives (anti-aliased arcs, circles, pills, icons, bezel clipping).
- Upgrade the **Boot Screen** (plus warning, ready, crash, and shutdown states) onto the theme and visual system, hardware-verifying the theme immediately on the real Disc (`V2.57`).

### Phase 3 — Home Menu *(Done)*
- `cst816t` round-screen touch and rim gesture layer (tuned for `~38–40 Hz` native sample rate, consistent left-edge back swipe).
- Curved mechanical app launcher (`Now Playing`, `Library`, `Modes`, `Settings`) with circular pebbles, bezel tick marks, detented carousel motion, screen transitions, and themed placeholders for unbuilt destinations.

### Phase 4 — Modes: Storage Mode *(Deferred)*
- `Modes` entry screen from the Home Menu (structured to host future `1.x` modes, starting with Storage Mode).
- Safe USB Mass Storage Mode (clean SD unmount/remount with zero corruption, live transfer status, and clean physical/touch exit back to Home).

### Phase 5 — Library *(In Progress — Hardware Verification)*
- Embedded SQLite 3 library index (`.dial-os/library.db`, no 16K track cap) with Core 1 `pthread` incremental stat-diff scanner, audio-payload move detection, and single-pass metadata/M3U reconciliation.
- 60 FPS sliding-window browsing (`96`-row viewport + Core 1 async prefetch) across `Favorites`, `Playlists`, `Artists`, `Albums` cover carousel, `Genres`, `All songs`, and 17-slab Quick-Seek, plus 256-bucket sharded incremental cover cache (`.dial-os/covers/<xx>/<album_uid>.rgb565`) and streaming `0100` M3U playback handoff.

### Phase 6 — Now Playing
- Full-bleed spinning album art (seamless expansion from Home/Albums), center spindle progress ring and scrub, reliable playback controls, classic playback modes (linear/sequential, shuffle, repeat all, repeat one), right-edge volume HUD, Quick Actions overlay, Track Details sheet, and synced/unsynced Lyrics.

### Phase 7 — Settings
- Hardware-verified Audio (presets/EQ, DAC filter, gain, gapless, balance, volume limit), Display (brightness slider, Dark/Light mode toggle, vinyl spin, screen timeout), Power & System (battery, power draw, idle power-off, live library rescan), and Bluetooth last.

---

## Future (`1.x`)

Planned only after `1.0` (Phases 1–7) is rock-solid on hardware:
- Pull-down Control Center
- Queue & Folder Browser polish
- Wi-Fi & easy provisioning
- Signed Wi-Fi OTA updates
- Receiver modes (USB DAC, Bluetooth Receiver, AirPlay)
- Games & extras
