# DialOS Engineering Rules (`AGENTS.md`)

These rules apply to every session, subagent, and swarm worker operating in this repository.

## 1. Source of Truth: Stock V2.57 Binaries & Real Hardware — Never `diskOS` or `../dial-os`
- **Do NOT use `diskOS` or `../dial-os` documentation or source code** to infer `mq_player`, `mq_ui`, IPC, input, display, audio, or power behavior.
- **Only exception:** The 5 proven USB Mask-ROM / SquashFS host binaries in `tools/vendor/` (`usbboot`, `disc_spl_lpddr3.bin`, `my_write6_dram.bin`, `mksquashfs`, `unsquashfs`) and the `my_write6_dram.bin` DGSB flashing protocol.
- **Before building anything new:** Inspect the actual stock `V2.57` binaries in `vendor/stock_v257/` (`mq_player`, `mq_ui`, `rootfs.squashfs`) and verify on real FiiO Snowsky Disc (`V2.57`) hardware.
- **Improve, don't blindly imitate:** Understanding `mq_player` and `mq_ui` tells us the exact hardware/IPC contract — we do **not** have to copy how stock `mq_ui`, `diskOS`, or `../dial-os` solved a problem. Whenever we can build a cleaner, faster, or more reliable design, we do it, always prioritizing **performance** and **reliability**.

## 2. Hardware & Development Workflow Guardrails
- **Host & Target:** Apple Silicon macOS only (`zig cc` targeting `mipsel-linux-musleabihf`, `-mcpu=mips32r2 -mfp64 -mno-odd-spreg -mnan=2008`, `EF_MIPS_NAN2008` `0x400`); FiiO Snowsky Disc stock firmware `V2.57` only.
- **No `diskOS`, no Wi-Fi/SSH dev loop:** Deploy and collect logs via microSD hot-reload (`./dial-os-mac sd-update` → `<SDCARD>/.dial-os/mq_ui`, instant escape via `./dial-os-mac use-stock`).
- **Internals & Wired Audio First:** Build and stabilize core internals over wired headphones first; Bluetooth is deferred to the end (`M6b`).
- **SD-Card Non-Destruction (Non-Negotiable):**
  - Never ship an unpatched `mq_player` containing `rm -rf %s` in `mount_sdcard()`; `dial-os-mac build` must patch and verify `rmdir  %s`.
  - Never delete, format, or modify user music files during deploy or on-device runs; write only inside `.dial-os/` atomically (`temp` + `fsync` + `rename`).
  - Disable `mq_player`'s unsynced idle `poweroff -f` (`0664=0`) and `fsync()` logs/state.
- **On-Device Transparency:** Any on-device test or long-running operation must show live animated progress (spinner/progress bar + current step) so the operator always knows the device has not frozen.
- **No Theme Leakage:** Zero theme names in file names, identifiers, or strings; use semantic **Dark Mode** and **Light Mode** tokens only.
- **Hardware-Verified Only:** No milestone is complete until verified on the real FiiO Snowsky Disc (`V2.57`).

## 3. Verified Hardware Runtime Invariants
- **Non-Blocking 60 FPS UI Thread (Dual-Core X2000):** Keep Core 0 strictly non-blocking (`avg_ms <= 14 ms`, `slow_frames == 0`). Offload heavy SD card I/O and artwork decoding to a `-static -pthread` worker on Core 1 with mutex-guarded handoff, or bound cooperative main-thread work to `<= 1 ms/frame`. Never run blocking `statvfs`/directory scans on the render path, and always clamp frame `delta_seconds <= 0.05 s` so OS hiccups cannot skip choreography acts.
- **Span-Vectorized Integer Rendering:** Hoist clipping, `180°` coordinate mapping, and alpha precomputation out of inner pixel loops. Use integer fixed-point alpha/blits and per-row span fills (never per-pixel float math or `sqrtf()`).
- **RAM-Bounded Asset Decoding & Caching (~64 MB Target):** Never decode full-resolution user artwork into RAM. Stream-downsample thumbnails (`< 250 KB` peak RAM) into pre-cached `120×120` RGB565 files under `.dial-os/` plus a bounded RAM LRU so scrolling never decodes synchronously.
- **Smooth Choreography & Zero Empty-State Flashes:** Defer indexing, warm-boot delta checks, and precaching to Boot Act 02 (after `aa24` signal lock) without jumping progress; never wipe a warm catalog while on Home.
- **Full-Bleed `360×360` Canvas:** Always fill the entire `360×360` framebuffer with the active canvas color and clip only to `[0..359]` bounds; never mask an un-antialiased black circle in software (the physical ST77916 bezel clips cleanly).
- **180° Inversion & Touch-First Input:** Both `/dev/fb0` and `/dev/input/event1` (`cst816t`, `~38–40 Hz`) apply `359 - coord`. The Disc has no rotary encoder (`event0` has discrete Vol/Power keys only); normalize velocity by touch sample timestamps (`dt_ms`).
- **IPC & Volume Contract:** Hardware DAC volume `0..120` maps `120 -> 100%` in DialOS (`round(raw * 100 / 120)`). Parse `a202` and `a103` with their real V2.57 8-byte compact headers and double-escaped `"song"` JSON.
- **Persistent Event & Performance Logging:** Record lifecycle events and 5-second `PERF` frame-budget metrics (`fps_x10`, `avg_ms`, `max_ms`, `slow_frames`) in RAM and flush to `.dial-os/dialos.log` without stalling active transitions.
