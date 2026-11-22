# FiiO Snowsky Disc (`V2.57`) — Verified Hardware Reference

This file records only **directly verified** hardware and stock `V2.57` facts (from `vendor/stock_v257/` and real-device testing). New facts are added here as each phase inspects stock firmware.

## Compute & Memory
- **SoC:** Ingenic X2000 dual-core `1.2 GHz` MIPS32r2, no GPU
- **Target ABI:** `mipsel-linux-musleabihf` (`-mcpu=mips32r2 -mfp64 -mno-odd-spreg -mnan=2008`, `EF_MIPS_NAN2008` `0x400`)
- **RAM:** `117.5 MiB` total (`~64 MB` free while `mq_player` runs; OOM-killing `mq_player` reboots the MCU)
- **Writable Flash:** `83 MB` on `/usr/data`
- **Firmware Fingerprint:** `/etc/product_version/version.in` (`PRODUCT=SNOWSKY_DISC`, `MAIN_OS_VER=257`)

## Display & Backlight
- **Display:** `360x360` round `XRGB8888` on `/dev/fb0`, mounted `180°` (triple-buffer `FBIOPAN_DISPLAY` measured at `270 fps` in [docs/benchmarks/m0-v257-results.md](benchmarks/m0-v257-results.md))
- **MIPS `FBIO_WAITFORVSYNC`:** `0x80044620` (`_IOW('F', 0x20, __u32)` on MIPS Linux, where `_IOC_WRITE` is `4U << 29`)
- **LCD Sleep / Wake (`/dev/lcd_st77916`):** Sleep `0x2000ef01` (`120 ms` touch->LCD sleep delay), Wake `0x2000ef02` (`50 ms` LCD->backlight wake delay)
- **Touchscreen Sleep / Wake (`/dev/cst816t`):** Sleep `0x2000ef03`, Wake `0x2000ef04`
- **Backlight Sysfs:** `/sys/bus/platform/drivers/pwm-backlight/backlight/backlight/backlight/brightness` and sibling `.../max_brightness` (`0..255` raw PWM scale). Verified in `mq_player` (`hw_echo_set_brightness()`, `echo_wake()` `0x4e6a74`, `aa24` serializer `0x4eb910`) that `aa24.brightness` equals `echo_sys_config[20]` on the same `0..255` raw PWM scale, and `mq_player` has **no** internal `display_time` backlight/LCD sleep timer (`echo_sleep_in()` `0x4e6d9c` is only invoked by `hw_echo_set_brightness(0)`).

## Input & Power
- **Physical Keys (`/dev/input/event0`):** `x2000_key` driver emits `16`-byte MIPS32 `struct input_event` records (`type = 1`, `value = 1` press edge) with keycodes `250`–`269`:
  - Volume Up: `251`, `263`, `266`
  - Volume Down: `252`, `262`, `267`
  - Power Short-Press: `259`, `265`
  - Power Long-Press: `264`
- **MIPS `EVIOCGRAB`:** `0x80044590` (`_IOW('E', 0x90, int)` on MIPS Linux). `mq_player`'s `echo_start_key_server()` (`0x0e29d4` file offset / `0x4e29d4` VA) opens `/dev/input/event0`; verified fallback if kernel `3.10.14` `EVIOCGRAB` misbehaves is an 8-byte MIPS `jr $ra; or $v0, $zero, $zero` patch at `0x0e29d4`.
- **Touchscreen (`/dev/input/event1`):** `cst816t` single-finger `MT type-B`, `~38–40 Hz`, active bounds `x=33..342`, `y=4..356`
- **Battery Sysfs:** `/sys/class/power_supply/cw221X-bat/capacity` (`0..100` percentage; `aa24.battery` is only a discrete `0..5` bar level)

## Stock OS & IPC (`V2.57`)
- **Init, Watchdog & Poweroff:** `/etc/init.d/S98FIIO` -> `/usr/project/fiio_init.sh` launches `mq_ui &`, sleeps `2 s`, launches `mq_player &`, sleeps `3 s`, then polls `pgrep -x "mq_ui"` and `pgrep -x "mq_player"` every `5 s` (restarting both and zeroing backlight if either exits, logging to `/usr/data/fiio/log/process_failed.txt`). Hardware watchdog CLI is `/usr/bin/cmd_watchdog stop`. `/etc/inittab` runs `/etc/init.d/rcK`, `/sbin/swapoff -a`, and `/bin/umount -a -r` on normal BusyBox `/sbin/poweroff` (without `-f`).
- **Audio Engine IPC:** POSIX message queues `/player` (commands to `mq_player`) and `/ui` (events from `mq_player`), 12-byte ASCII envelope `%s%04X%04X%s` (`opcode[0..3]` + total hex byte length `[4..7]` + 4-hex-digit parameter `[8..11]` + optional payload `[12..]`).
  - `mq_player` emits `aa24` (38-key boot config snapshot) ~`7.8 s` after `mq_ui` starts, and `a607` ~`12.68 s` after start.
  - `aa22` (`TFCARD_KEY_EVENT_REPORT`, parameter `0001` inserted / `0000` removed) fires on GPIO `pb06` pin transition before `mount_sdcard()` completes.
  - Outbound `0664000C0000` disables `mq_player`'s unsynced idle `poweroff -f`; `0715000C%04X` sets volume (`0000` mutes DAC).
- **Rootfs Font:** `/usr/project/config/fonts/SourceHanSans-Regular.ttc` (CJK/Cyrillic coverage, `22.3 MB` read-only on rootfs).
