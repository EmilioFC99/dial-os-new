# FiiO Snowsky Disc (`V2.57`) — Verified Hardware Reference

This file records only **directly verified** hardware and stock `V2.57` facts (from `vendor/stock_v257/` and real-device testing). New facts are added here as each phase inspects stock firmware.

## Compute & Memory
- **SoC:** Ingenic X2000 dual-core `1.2 GHz` MIPS32r2, no GPU
- **Target ABI:** `mipsel-linux-musleabihf` (`-mcpu=mips32r2 -mfp64 -mno-odd-spreg -mnan=2008`, `EF_MIPS_NAN2008` `0x400`)
- **RAM:** `117.5 MiB` total (`~64 MB` free while `mq_player` runs; OOM-killing `mq_player` reboots the MCU)
- **Writable Flash:** `83 MB` on `/usr/data`
- **Firmware Fingerprint:** `/etc/product_version/version.in` (`PRODUCT=SNOWSKY_DISC`, `MAIN_OS_VER=257`)

## Display & Input
- **Display:** `360x360` round `XRGB8888` on `/dev/fb0`, mounted `180°` (triple-buffer `FBIOPAN_DISPLAY` measured at `270 fps` in [docs/benchmarks/m0-v257-results.md](benchmarks/m0-v257-results.md))
- **Backlight:** `/sys/bus/platform/drivers/pwm-backlight/backlight/backlight/backlight/brightness`
- **Physical Keys:** `/dev/input/event0`
- **Touchscreen:** `/dev/input/event1` (`cst816t` single-finger `MT type-B`, `~38–40 Hz`, active bounds `x=33..342`, `y=4..356`)

## Stock OS & IPC (`V2.57`)
- **Init & Watchdog:** `/etc/init.d/S98FIIO` -> `/usr/project/fiio_init.sh` launches `mq_ui &`, sleeps `2 s`, launches `mq_player &`, sleeps `3 s`, then polls `pgrep -x "mq_ui"` and `pgrep -x "mq_player"` every `5 s` (restarting both and zeroing backlight if either exits).
- **Audio Engine IPC:** POSIX message queues `/player` (commands to `mq_player`) and `/ui` (events from `mq_player`). `mq_player` emits its `aa24` boot config snapshot ~`7.8 s` after `mq_ui` starts.
- **Rootfs Font:** `/usr/project/config/fonts/SourceHanSans-Regular.ttc` (CJK/Cyrillic coverage, `22.3 MB` read-only on rootfs).
