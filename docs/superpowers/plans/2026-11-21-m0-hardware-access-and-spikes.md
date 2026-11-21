# M0 Hardware Access, Clean `dial-os-mac` Rewrite & Feasibility Spikes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a clean-room, TDD-tested `dial-os-mac` macOS CLI, `zig cc` MIPS32r2 cross-compilation target, and an interactive 5-stage on-device M0 hardware feasibility spike binary (`build/m0_mq_ui`) ready for microSD hot-reload verification on the FiiO Snowsky Disc (`V2.57`).

**Architecture:** Split M0 into two cleanly separated layers: (1) a stdlib-only Python package (`tools/dial_os_mac/`) backed by `unittest` and reusing only the 5 proven vendor binaries in `tools/vendor/`, and (2) a self-contained throwaway C spike binary (`spike/m0_bench.c`, outside `src/`) cross-compiled via `zig cc` (`mipsel-linux-musleabihf` + `EF_MIPS_NAN2008`) that runs all 5 hardware benchmarks over wired headphones and `fsync`s results + raw IPC traces to `/tmp/sdcard/.dial-os/`.

**Tech Stack:** Python 3 (stdlib only, `unittest`), `zig cc` (`0.17.1`, `mipsel-linux-musleabihf`), GNU `make`, POSIX mqueues, Linux `/dev/fb0` & `evdev` (`event0`/`event1`), macOS `ioreg`/`hdiutil`/`openssl`/`caffeinate`.

**Spec:** [docs/superpowers/specs/2026-11-21-m0-hardware-access-and-spikes-design.md](../specs/2026-11-21-m0-hardware-access-and-spikes-design.md)

## Global Constraints

- **Host & Target:** Apple Silicon macOS only; FiiO Snowsky Disc stock firmware `V2.57` only; wired headphones only in M0 (no Bluetooth, no Wi-Fi, no SSH, no `diskOS`).
- **Clean Naming & Zero Theme Leakage:** Follow global naming rules strictly — zero prohibited abbreviations (`cwd`, `dir`, `ctx`, `buf`, `src`, `dst`, `req`, `res`, `err`, `tmp`, single-letter loop variables), zero theme names in files/symbols/output.
- **Zero Copied Source Code:** Use `../dial-os/docs/hardware/` and `../dial-os/macos/dial_os_mac_installer.py` strictly as reference for V2.57 hardware/protocol constants; write all code fresh in `dial-os-new`. Copy only the 5 binary blobs into `tools/vendor/`.
- **Exact ABI & Flash Constants:**
  - `zig cc -target mipsel-linux-musleabihf -mcpu=mips32r2 -mfp64 -mno-odd-spreg -mnan=2008 -static -ffp-contract=off -O2 -s` + set `EF_MIPS_NAN2008` (`0x400`) at ELF offset `36` (`<I`).
  - `V2.57` stock rootfs: `80,596,992` bytes, SHA-256 `111e4dd7ee3d7ff91ba7e61181690be7ffd22bd1bbd13ad513f5f015ffb302ae`.
  - `NAND_IMAGE_SIZE = 100_663_296` bytes (`768 × 128 KiB`).
  - `my_write6_dram.bin` SHA-256: `a3c3aa63e0c48f501181d542d9fa0526d36ded81e2e767f0c044af876674ea84`.
  - Preserve exact 4-version base-gate digest table (`209`, `228`, `240`, `257`) inside the 272-byte DGSB plan.
- **SD-Card Non-Destruction:** `dial-os-mac build` must patch and verify `mq_player` `rm -rf %s\x00` → `rmdir  %s\x00`; `sd-update` writes atomically only to `<VOLUME>/.dial-os/mq_ui`; `spike/m0_bench.c` sends `0664000C0000`, reads audio strictly read-only, and `fsync`s after every stage.
- **TDD Iron Law:** Write failing unit tests first for every Python module in `tools/dial_os_mac/` (covering nominal, boundary, and negative/fallback cases).

## Review Focus

- **Unpatched or unrecognized `mq_player` binary during SquashFS build:** Must fail loudly rather than silently shipping an unpatched `rm -rf %s` binary (pinned in Task 3).
- **Ambiguous multiple external volumes under `/Volumes/` during `sd-update`:** Must refuse to guess and raise `RuntimeError` listing mounted volumes so we never write to the wrong drive (pinned in Task 5).
- **Stale `.dial-os/use_stock` file present during `sd-update`:** Must warn clearly (and only delete when `clear_stock_flag=True`) so the operator knows why stock UI would boot (pinned in Task 5).
- **Deploying a non-MIPS or unpatched ELF binary (missing `EF_MIPS_NAN2008`):** Must be rejected before writing to the SD card or repacking SquashFS (pinned in Task 2 and Task 5).
- **Truncated (`< 1024` bytes) or non-passing `usbboot` readback:** Must raise `RuntimeError` with hex diagnostics for `magic`, `done`, `result`, and `gate` (pinned in Task 4).

---

## File Map

- `Makefile` — Targets `test-tools`, `m0-spike`, `clean`.
- `dial-os-mac` — Executable bash wrapper invoking `python3 tools/dial_os_mac/cli.py "$@"`.
- `tools/vendor/` — Copied proven binaries: `usbboot`, `disc_spl_lpddr3.bin`, `my_write6_dram.bin`, `mksquashfs`, `unsquashfs`.
- `tools/dial_os_mac/__init__.py` — Package marker.
- `tools/dial_os_mac/constants.py` — V2.57 paths, SHA-256 digests, DGSB 4-version catalogue, ELF/USB/NAND constants.
- `tools/dial_os_mac/elf.py` — `patch_mips_nan2008_flag()`, `verify_mips_nan2008_binary()`.
- `tools/dial_os_mac/firmware.py` — `decrypt_fiio_chunk()`, `patch_mq_player_rmdir_safety()`, `pad_squashfs_to_nand()`, `build_v257_nand_image()`.
- `tools/dial_os_mac/flasher.py` — `compute_catalogue_fingerprint()`, `build_dgsb_plan()`, `verify_usbboot_readback()`, `detect_maskrom_device_count()`, `flash_nand_image()`.
- `tools/dial_os_mac/sd_deploy.py` — `resolve_sdcard_volume()`, `deploy_ui_binary_to_sdcard()`, `set_stock_boot_mode()`.
- `tools/dial_os_mac/cli.py` — CLI entry point (`doctor`, `build`, `flash`, `restore-stock`, `sd-update`, `use-stock`, `use-dialos`).
- `tests/tools/test_elf.py`, `test_firmware.py`, `test_flasher.py`, `test_sd_deploy.py` — Unit tests (`python3 -m unittest`).
- `spike/m0_bench.c` — Throwaway 5-stage interactive M0 hardware spike binary.

---

### Task 1: Repository Init, Vendor Binaries & Build Scaffold

**Files:**
- Modify: `.gitignore`
- Create: `Makefile`
- Create: `dial-os-mac`
- Create: `tools/dial_os_mac/__init__.py`
- Copy (binary blobs only): `tools/vendor/{usbboot,disc_spl_lpddr3.bin,my_write6_dram.bin,mksquashfs,unsquashfs}`

**Interfaces:**
- Produces: `make test-tools`, `make m0-spike`, `make clean`, executable `./dial-os-mac` wrapper, and verified `tools/vendor/` binaries.

- [ ] **Step 1: Initialize git repo and update `.gitignore`**

Run `git init -b main` if `.git` does not exist. Ensure `.gitignore` ignores `build/`, `**/__pycache__/`, `*.pyc`, `.DS_Store`, and `vendor/stock_v257/` (already local large blobs — check existing `.gitignore` first so we preserve what's there).

- [ ] **Step 2: Copy the 5 proven vendor binaries into `tools/vendor/` and verify `my_write6_dram.bin` SHA-256**

Copy `usbboot`, `disc_spl_lpddr3.bin`, `my_write6_dram.bin`, `mksquashfs`, `unsquashfs` from `../dial-os/macos/vendor/` into `tools/vendor/`. Verify:
```bash
shasum -a 256 tools/vendor/my_write6_dram.bin
```
Expected: `a3c3aa63e0c48f501181d542d9fa0526d36ded81e2e767f0c044af876674ea84`.

- [ ] **Step 3: Create `Makefile`, `dial-os-mac`, and `tools/dial_os_mac/__init__.py`**

- `Makefile` defines:
  - `test-tools`: `python3 -m unittest discover -s tests/tools -p "test_*.py" -v`
  - `m0-spike`: creates `build/`, runs `zig cc -target mipsel-linux-musleabihf -mcpu=mips32r2 -mfp64 -mno-odd-spreg -mnan=2008 -static -ffp-contract=off -O2 -s -std=c11 -Wall -Wextra -Werror spike/m0_bench.c -o build/m0_mq_ui -lm`, then patches & verifies `EF_MIPS_NAN2008` via `python3 -m tools.dial_os_mac.elf build/m0_mq_ui`.
  - `clean`: removes `build/`.
- `dial-os-mac` (`chmod +x`): bash wrapper invoking `exec /usr/bin/python3 -m tools.dial_os_mac.cli "$@"` with `PYTHONPATH` set to repo root.

- [ ] **Step 4: Commit initial repo baseline & scaffold**

```bash
git add .gitignore ROADMAP.md docs/ Makefile dial-os-mac tools/
git commit -m "chore: initialize repository with M0 specs, vendor binaries, and build scaffold"
```

---

### Task 2: Constants & MIPS32r2 ELF `EF_MIPS_NAN2008` Patcher/Verifier

**Files:**
- Create: `tools/dial_os_mac/constants.py`
- Create: `tools/dial_os_mac/elf.py`
- Test: `tests/tools/test_elf.py`

**Interfaces:**
- Produces:
  - `constants.py`: `REPOSITORY_ROOT`, `VENDOR_DIRECTORY`, `BUILD_DIRECTORY`, `BACKUP_DIRECTORY`, `STOCK_V257_ROOTFS_PATH`, `FIIO_OTA_PASSPHRASE`, `SQUASHFS_MAGIC`, `NAND_IMAGE_SIZE`, `WRITER_EXPECTED_SHA256`, `DGSB_PLAN_ADDRESS`, `FLASH_WAIT_SECONDS`, `USB_VENDOR_ID_INGENIC`, `USB_PRODUCT_ID_MASKROM`, `FIRMWARE_CATALOGUE`, `BASE_GATE_PARTITIONS`, `TARGET_FIRMWARE_VERSION = "257"`, `ELF_MIPS_NAN2008_FLAG = 0x400`, `ELF_FLAGS_OFFSET = 36`, `ELF_MACHINE_MIPS = 8`.
  - `elf.py`:
    - `patch_mips_nan2008_flag(binary_path: Path) -> None`
    - `verify_mips_nan2008_binary(binary_path: Path) -> None`
    - `python3 -m tools.dial_os_mac.elf <binary_path>` CLI entrypoint (patches then verifies).

- [ ] **Step 1: Write failing tests in `tests/tools/test_elf.py`**

Test cases (nominal, boundary, negative):
1. `test_patch_and_verify_sets_nan2008_bit_on_mips32_le_elf`: synthetic 52-byte ELF32 LE MIPS (`\x7fELF`, class=1, data=1, `e_machine=8`, `e_flags=0x70001005`) → after `patch_mips_nan2008_flag()`, `e_flags == 0x70001405` and `verify_mips_nan2008_binary()` passes.
2. `test_patch_is_idempotent_when_nan2008_already_set`: patching an ELF with `0x70001405` keeps `0x70001405`.
3. `test_verify_rejects_unpatched_mips_elf`: `e_flags=0x70001005` raises `RuntimeError` mentioning `EF_MIPS_NAN2008`.
4. `test_verify_rejects_non_elf_or_macho_or_truncated_file`: Mach-O magic (`\xcf\xfa\xed\xfe`) and 16-byte truncated file both raise `RuntimeError`.
5. `test_verify_rejects_non_mips_elf_architecture`: x86_64/ARM64 ELF (`e_machine != 8`) raises `RuntimeError`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test-tools`
Expected: FAIL (`ModuleNotFoundError: No module named 'tools.dial_os_mac.elf'`).

- [ ] **Step 3: Implement `tools/dial_os_mac/constants.py` and `tools/dial_os_mac/elf.py`**

- Populate `constants.py` with exact V2.57 and 4-version `FIRMWARE_CATALOGUE` constants (cross-checked against `../dial-os/macos/dial_os_mac_installer.py`).
- Implement `patch_mips_nan2008_flag()` and `verify_mips_nan2008_binary()` in `elf.py` with clean domain names (zero abbreviations) and `__main__` entrypoint for `Makefile`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test-tools`
Expected: PASS (all 5 tests in `test_elf.py`).

- [ ] **Step 5: Commit**

```bash
git add tools/dial_os_mac/constants.py tools/dial_os_mac/elf.py tests/tools/test_elf.py
git commit -m "feat(dial-os-mac): add V2.57 constants and MIPS NAN2008 ELF verifier"
```

---

### Task 3: Firmware Decryption, `mq_player` SD-Safety Patch & V2.57 NAND Builder

**Files:**
- Create: `tools/dial_os_mac/firmware.py`
- Test: `tests/tools/test_firmware.py`

**Interfaces:**
- Consumes: `constants.*`, `elf.verify_mips_nan2008_binary(binary_path: Path) -> None`.
- Produces:
  - `decrypt_fiio_chunk(encrypted_chunk: bytes, chunk_label: str) -> bytes`
  - `patch_mq_player_rmdir_safety(player_binary_bytes: bytes) -> bytes`
  - `pad_squashfs_to_nand(squashfs_path: Path, nand_image_path: Path) -> None`
  - `verify_stock_v257_squashfs(squashfs_path: Path) -> bool`
  - `build_v257_nand_image(ui_binary_path: Path, firmware_zip_path: Path | None = None) -> Path`

- [ ] **Step 1: Write failing tests in `tests/tools/test_firmware.py`**

Test cases (nominal, boundary, negative — including Review Focus #1):
1. `test_patch_mq_player_replaces_rm_rf_with_rmdir`: `b"...rm -rf %s\x00..."` → `b"...rmdir  %s\x00..."`, identical length, `b"rm -rf %s\x00"` absent.
2. `test_patch_mq_player_accepts_already_patched_binary`: `b"...rmdir  %s\x00..."` returns unchanged.
3. `test_patch_mq_player_rejects_unrecognized_binary`: bytes containing neither `b"rm -rf %s\x00"` nor `b"rmdir  %s\x00"` raises `RuntimeError` (Review Focus #1).
4. `test_decrypt_fiio_chunk_roundtrip_with_macos_openssl`: encrypts sample plaintext with `/usr/bin/openssl enc -aes-256-cbc` + PBKDF2-HMAC-SHA256 (`fo123`, 10000 iterations, 8-byte salt, `Salted__` prefix) and asserts `decrypt_fiio_chunk()` recovers exact plaintext.
5. `test_decrypt_fiio_chunk_rejects_bad_header_and_unaligned_ciphertext`: missing `Salted__` or non-16-byte-multiple ciphertext raises `RuntimeError`.
6. `test_pad_squashfs_to_nand_exact_size_and_overflow_rejection`: pads small file to `NAND_IMAGE_SIZE` (trailing zero bytes), accepts exact `NAND_IMAGE_SIZE`, and raises `RuntimeError` when input exceeds `NAND_IMAGE_SIZE` by 1 byte.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test-tools`
Expected: FAIL (`ModuleNotFoundError: No module named 'tools.dial_os_mac.firmware'`).

- [ ] **Step 3: Implement `tools/dial_os_mac/firmware.py`**

Implement all 5 functions:
- `build_v257_nand_image()` verifies `ui_binary_path` via `verify_mips_nan2008_binary()`, uses `STOCK_V257_ROOTFS_PATH` directly if `verify_stock_v257_squashfs()` passes (or decrypts `V2.57` ZIP chunks and rejects non-V2.57 ZIPs), backs up padded `stock_V257_nand.bin` to `BACKUP_DIRECTORY`, mounts a temporary Case-Sensitive APFS sparse image via `/usr/bin/hdiutil` (always detached in `finally`), unpacks with `tools/vendor/unsquashfs`, preserves `/usr/bin/mq_ui.stock`, installs `ui_binary_path` as `/usr/bin/mq_ui`, applies `patch_mq_player_rmdir_safety()` to `/usr/bin/mq_player`, repacks with `tools/vendor/mksquashfs -comp lzo -b 131072 -no-xattrs -all-root -noappend -processors 1`, and pads to `build/dialos_v257_nand.bin`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test-tools`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/dial_os_mac/firmware.py tests/tools/test_firmware.py
git commit -m "feat(dial-os-mac): add V2.57 firmware builder with mq_player rmdir safety patch"
```

---

### Task 4: DGSB Base-Gate Plan & USB Mask-ROM Flasher

**Files:**
- Create: `tools/dial_os_mac/flasher.py`
- Test: `tests/tools/test_flasher.py`

**Interfaces:**
- Consumes: `constants.*`.
- Produces:
  - `compute_catalogue_fingerprint() -> bytes`
  - `build_dgsb_plan(padded_image_path: Path) -> bytes`
  - `verify_usbboot_readback(readback_bytes: bytes) -> None`
  - `parse_ioreg_maskrom_count(ioreg_output: str) -> int`
  - `detect_maskrom_device_count() -> int`
  - `flash_nand_image(image_path: Path, flash_label: str) -> None`

- [ ] **Step 1: Write failing tests in `tests/tools/test_flasher.py`**

Test cases (nominal, boundary, negative — including Review Focus #5):
1. `test_dgsb_plan_is_272_bytes_with_valid_magic_and_self_checksum`: builds plan on a temp file, verifies `len(plan) == 272`, `w[0] == 0x42534744`, `w[1] == 1`, `w[2] == 272`, `id` field starts with `b"v257-r18\x00"`, and trailing 8 big-endian words equal `sha256(plan[:240]).digest()`.
2. `test_catalogue_fingerprint_matches_four_version_digest`: asserts `len(compute_catalogue_fingerprint()) == 32` and equals the exact SHA-256 over `209`, `228`, `240`, `257` base partitions.
3. `test_verify_usbboot_readback_accepts_success_words`: 1024-byte buffer with `w[0]=0x4006E006`, `w[9]=0x55555555`, `w[16]=0x600DF10C`, `w[128]=1` succeeds.
4. `test_verify_usbboot_readback_rejects_truncated_or_failed_gate`: `< 1024` bytes or `w[128]=0` raises `RuntimeError` containing hex diagnostics (Review Focus #5).
5. `test_parse_ioreg_maskrom_count_counts_only_matching_vid_pid`: sample `ioreg` text with 0, 1, and 2 blocks matching `"idVendor" = 41224` and `"idProduct" = 60143` returns exact counts.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test-tools`
Expected: FAIL (`ModuleNotFoundError: No module named 'tools.dial_os_mac.flasher'`).

- [ ] **Step 3: Implement `tools/dial_os_mac/flasher.py`**

Implement all 6 functions. In `flash_nand_image()`:
- Verify `image_path` exists and equals `NAND_IMAGE_SIZE`.
- Verify `tools/vendor/my_write6_dram.bin` SHA-256 equals `WRITER_EXPECTED_SHA256`.
- Print an explicit **SD-card safety reminder** telling the operator to eject/remove the microSD card before USB Mask-ROM flashing.
- Wait up to `180` seconds for exactly 1 Mask-ROM device, run `/usr/bin/caffeinate -dimsu tools/vendor/usbboot ...` with exact DRAM addresses (`0xa0c00000`, `0xa1000000`, `0xa0b00000`, `0xa0a00000`), and call `verify_usbboot_readback()`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test-tools`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/dial_os_mac/flasher.py tests/tools/test_flasher.py
git commit -m "feat(dial-os-mac): add DGSB plan builder and USB Mask-ROM flasher"
```

---

### Task 5: Non-Destructive SD Deploy, Escape Hatch & `dial-os-mac` CLI

**Files:**
- Create: `tools/dial_os_mac/sd_deploy.py`
- Create: `tools/dial_os_mac/cli.py`
- Test: `tests/tools/test_sd_deploy.py`

**Interfaces:**
- Consumes: `constants.*`, `elf.verify_mips_nan2008_binary`, `firmware.build_v257_nand_image`, `firmware.verify_stock_v257_squashfs`, `flasher.detect_maskrom_device_count`, `flasher.flash_nand_image`.
- Produces:
  - `resolve_sdcard_volume(explicit_volume: Path | None, volumes_root: Path = Path("/Volumes")) -> Path`
  - `deploy_ui_binary_to_sdcard(binary_path: Path, sdcard_volume: Path, clear_stock_flag: bool = False) -> tuple[Path, bool]` (returns `(deployed_binary_path, stock_flag_warning)`).
  - `set_stock_boot_mode(sdcard_volume: Path, use_stock: bool) -> Path`
  - `./dial-os-mac {doctor,build,flash,restore-stock,sd-update,use-stock,use-dialos}` CLI.

- [ ] **Step 1: Write failing tests in `tests/tools/test_sd_deploy.py`**

Test cases (nominal, boundary, negative — including Review Focus #2, #3, #4):
1. `test_resolve_sdcard_volume_picks_single_external_volume`: fake `/Volumes` containing `Macintosh HD` symlink + `SNOWSKY_SD` directory returns `SNOWSKY_SD`.
2. `test_resolve_sdcard_volume_rejects_zero_or_ambiguous_volumes`: 0 external dirs or 2 external dirs (`SD_A`, `SD_B`) without `explicit_volume` raises `RuntimeError` listing candidates (Review Focus #2).
3. `test_deploy_writes_atomically_and_preserves_existing_music_files`: deploys valid synthetic MIPS32r2 NAN2008 ELF into temp SD dir containing `Music/song.flac`; verifies `.dial-os/mq_ui` matches binary bytes, no `.mq_ui.tmp` leftover, and `Music/song.flac` is untouched.
4. `test_deploy_warns_when_use_stock_exists_and_clears_only_when_requested`: with `.dial-os/use_stock` present, default call leaves `use_stock` intact and returns `stock_flag_warning=True`; with `clear_stock_flag=True`, removes `use_stock` and returns `stock_flag_warning=False` (Review Focus #3).
5. `test_deploy_rejects_non_mips_binary_without_creating_sd_files`: passing a non-MIPS file raises `RuntimeError` and does not create `.dial-os/mq_ui` (Review Focus #4).
6. `test_set_stock_boot_mode_creates_and_removes_use_stock_safely`: toggles `.dial-os/use_stock` without touching sibling files.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test-tools`
Expected: FAIL (`ModuleNotFoundError: No module named 'tools.dial_os_mac.sd_deploy'`).

- [ ] **Step 3: Implement `tools/dial_os_mac/sd_deploy.py` and `tools/dial_os_mac/cli.py`**

- Implement `sd_deploy.py` with atomic temp-file + `os.fsync()` + `os.replace()` deployment.
- Implement `cli.py` with `argparse` subcommands:
  - `doctor` (checks macOS, `zig`, all 5 vendor binaries + `my_write6_dram.bin` SHA-256, `vendor/stock_v257/rootfs.squashfs`, Mask-ROM count, mounted SD volumes, saved stock backups).
  - `build [firmware_zip] [--ui-binary PATH]` (defaults `--ui-binary` to `build/m0_mq_ui`).
  - `flash` (flashes `build/dialos_v257_nand.bin`).
  - `restore-stock` (flashes saved `stock_V257_nand.bin`).
  - `sd-update [--binary PATH] [--volume PATH] [--clear-stock-flag]` (defaults `--binary` to `build/m0_mq_ui`).
  - `use-stock [--volume PATH]` and `use-dialos [--volume PATH]`.

- [ ] **Step 4: Run tests and `./dial-os-mac doctor`**

Run: `make test-tools && ./dial-os-mac doctor`
Expected: All Python unit tests PASS; `./dial-os-mac doctor` prints `[READY]` for `zig`, all 5 vendor binaries, and `vendor/stock_v257/rootfs.squashfs`.

- [ ] **Step 5: Commit**

```bash
git add tools/dial_os_mac/sd_deploy.py tools/dial_os_mac/cli.py tests/tools/test_sd_deploy.py
git commit -m "feat(dial-os-mac): add atomic SD deploy, stock escape hatch, and CLI"
```

---

### Task 6: M0 5-Stage Interactive Hardware Spike Binary (`spike/m0_bench.c`)

**Files:**
- Create: `spike/m0_bench.c`

**Interfaces:**
- Consumes: `Makefile` `m0-spike` target (`zig cc` + `tools/dial_os_mac/elf.py`).
- Produces: Cross-compiled static MIPS32r2 FP64A `EF_MIPS_NAN2008` binary at `build/m0_mq_ui`, deployable via `./dial-os-mac sd-update --binary build/m0_mq_ui`.

- [ ] **Step 1: Implement `spike/m0_bench.c` conforming to the M0 design spec**

Keep all identifiers clean (zero prohibited abbreviations, fixed-width types `uint8_t`/`uint32_t`/`int32_t`, named constants for hardware registers/opcodes/ioctls). Include:
1. **Boot & Watchdog Safety:** `prctl(PR_SET_NAME, "mq_ui")`; open `/ui` (`O_CREAT | O_RDONLY | O_NONBLOCK`, `mq_maxmsg=10`, `mq_msgsize=8192`, `16384`-byte receive buffer); lazily open `/player` (`O_WRONLY | O_NONBLOCK`); assert wired CS43131 route (`0642000C0000`, `0666000C0006`, `0657000C0008`, `0715000C0030`) and disable `mq_player` idle power-off (`0664000C0000`).
2. **Direct 180°-Rotated `/dev/fb0` Renderer + 8x8 Bitmap Font:** Clear all 3 pages (`360×1080`), pan to `yoffset=0`, write `"180\n"` to PWM backlight sysfs, draw `360×360` software canvas with 180° flip on blit, plus `RUN` (bottom-left) and `NEXT`/`DONE` (bottom-right) touch buttons.
3. **Input (`/dev/input/event0` & `/dev/input/event1`):** Read `cst816t` MT Type-B (`ABS_MT_TRACKING_ID`, `ABS_MT_POSITION_X`, `ABS_MT_POSITION_Y`, inverted `359 - raw`) and `x2000_key` Long-Press Play/Pause (`268`) as backup trigger. Short Play/Pause (`250`) and Vol Up/Down (`251`/`252`) remain native to `mq_player`.
4. **Read-Only Test Track Discovery:** Scan `/tmp/sdcard/.dial-os/m0_test/` first (fallback `/tmp/sdcard/`) for at least 2 `.flac`/`.wav`/`.mp3` files. Never delete or modify audio files.
5. **All 5 Stages + Per-Stage On-Screen Result Summary (`PASS`/`FAIL`/`LOGGED` + exact metrics + `SAVED TO SD`) + `fflush()`/`fsync()` to `/tmp/sdcard/.dial-os/m0_report.txt` & `m0_ipc_trace.log`:**
   - **Stage 1 (Rotation & Tearing):** Auto-starts track 1 via `0602` if stopped; benchmarks per-pixel `16.16` fixed-point affine nearest-neighbor rotation of a `360×360` XRGB8888 procedural texture (circular mask + moving red vertical tear bar) for 5 s single-page direct blit + 5 s triple-buffer `FBIOPAN_DISPLAY`; logs `avg fps`, `min fps`, `p99 frame ms`, and `/proc/asound` xrun snapshot (if readable).
   - **Stage 2 (Rim Touch Rate):** Hides and completely locks out `RUN`/`NEXT` buttons during the 10-second capture countdown; draws outer-rim guide + live touch dot; logs `samples`, `effective Hz`, `max gap ms`, `dropouts (>50ms)`, and `min/max (x, y)`.
   - **Stage 3 (Rolling-Slice Handoff):** Tests multi-track `0602` M3U natural advance, `A -> B` alternating M3U (`//tmp/dialos_play_A.m3u` / `B.m3u`) boundary swap, and rapid `< 2 s` switch (`Q17`); logs latency ms and `song.db` leak check.
   - **Stage 4 (V2.57 EQ/PEQ Probe):** Sends `0508` fixed preset, `filterType: 0` graphic EQ, and PEQ probe frames, then restores EQ off; logs exact `/ui` replies.
   - **Stage 5 (Golden IPC Trace `Q14`/`Q17`/`Q18`):** Drives play/pause/resume, `95%` seek, `98%` seek, and natural EOF while logging every timestamped TX/RX frame to `m0_ipc_trace.log`; shows `ALL 5 STAGES SYNCED` and calls `sync()` on `DONE`.

- [ ] **Step 2: Cross-compile and verify ELF ABI + tools**

Run:
```bash
make m0-spike && make test-tools && ./dial-os-mac doctor
```
Expected: `build/m0_mq_ui` builds cleanly with zero warnings (`-Wall -Wextra -Werror`), passes `verify_mips_nan2008_binary()`, all Python unit tests pass, and `doctor` reports ready.

- [ ] **Step 3: Commit**

```bash
git add spike/m0_bench.c
git commit -m "feat(m0): add interactive 5-stage V2.57 hardware feasibility spike"
```

---

## Hardware Verification Gate (Operator Runs on Snowsky Disc)

Once Task 6 passes on Mac:
1. Open [docs/superpowers/specs/2026-11-21-m0-operator-guide.html](../specs/2026-11-21-m0-operator-guide.html) and run `./dial-os-mac sd-update --binary build/m0_mq_ui`.
2. Run Stages 1–5 on the Snowsky Disc with wired headphones.
3. Read `/Volumes/<SDCARD>/.dial-os/m0_report.txt` and `m0_ipc_trace.log` back on Mac, record the measured numbers and fallback decisions in `docs/benchmarks/m0-v257-results.md`, and close M0.
