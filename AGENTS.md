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
