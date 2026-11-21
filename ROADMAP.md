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
- **First-Principles V2.57 Rule (see [AGENTS.md](AGENTS.md)):** Direct custom `360x360` XRGB8888 2D renderer (no LVGL). Inspect stock `V2.57` binaries directly — never blindly imitate stock `mq_ui`, `diskOS`, or `dial-os`. Only reuse the 5 verified USB Mask-ROM / SquashFS binaries in `tools/vendor/`.
- **SD-Card Non-Destruction:** Never ship unpatched `mq_player` (`rm -rf %s` -> `rmdir  %s`), never modify user music files, write DialOS state only inside `.dial-os/` atomically (`temp` + `fsync` + `rename`), and disable `mq_player`'s unsynced idle `poweroff -f` (`0664=0`).
- **No Theme Leakage:** Zero theme names in file names, identifiers, or strings; use semantic **Dark Mode** and **Light Mode** tokens only.
- **Hardware-Verified Only:** No phase is complete until verified on the real FiiO Snowsky Disc (`V2.57`).

---

## Phases

### Phase 0 — Hardware Access & Baseline Metrics *(Done)*
- Clean-room `./dial-os-mac` CLI (`build`, `flash`, `sd-update`, `use-stock`, `restore-stock`) with microSD hot-reload hook and `mq_player` SD safety patch.
- Real V2.57 hardware baseline recorded in [docs/benchmarks/m0-v257-results.md](docs/benchmarks/m0-v257-results.md) (`270 fps` triple-buffer rotation, `~38–40 Hz` `cst816t` touch, clean `0100` M3U handoff, `147`-frame golden IPC trace).

### Phase 1 — Boot Screen
- First-principles startup sequence, display initialization, boot screen animation/status, `mq_player` readiness handoff, watchdog survival, and crash logging.

### Phase 2 — Mass Storage Mode
- Safe USB file transfer mode (mounting/unmounting SD cleanly with zero corruption) and basic physical/touch input to enter and exit.

### Phase 3 — Theme & Visual System
- Semantic **Dark Mode** and **Light Mode** design tokens, typography (CJK/Cyrillic via rootfs font), shared 2D drawing primitives, and tactile round-screen gesture rules.

### Phase 4 — Library
- Browsing and indexing large microSD music collections (`Favorites`, `Playlists`, `Artists`, `Albums`, `Genres`, `All songs`, letter seek) with screen-bounded RAM and launching playback via `mq_player`.

### Phase 5 — Now Playing
- Reliable playback controls, classic playback modes (linear/sequential, shuffle, repeat all, repeat one), spinning album art, progress ring & scrub, volume HUD, Quick Actions, Track Details, and Lyrics.

### Phase 6 — Home Screen
- Curved mechanical app launcher (`Now Playing`, `Library`, `Modes`, `Settings`), pull-down Control Center, and screen transitions.

### Phase 7 — Settings
- Hardware-verified Audio (presets/EQ, DAC filter, gain, gapless, balance, volume limit), Display (brightness, Dark/Light mode, vinyl spin, screen timeout), Power & System (battery, idle power-off, rescan), and Bluetooth last.

---

## Future (`1.x`)

Planned only after `1.0` (Phases 1–7) is rock-solid on hardware:
- Queue & Folder Browser polish
- Wi-Fi & easy provisioning
- Signed Wi-Fi OTA updates
- Receiver modes (USB DAC, Bluetooth Receiver, AirPlay)
- Games & extras
