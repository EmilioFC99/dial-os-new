# DialOS M0 — V2.57 Real Hardware Benchmark Results & Decisions

**Date:** 2026-11-21
**Device:** FiiO Snowsky Disc (`V2.57`, Ingenic X2000 dual-core MIPS32r2, wired headphones, no Bluetooth)
**Binary:** `build/m0_mq_ui` (`spike/m0_bench.c`, `zig cc` `mipsel-linux-musleabihf` + `EF_MIPS_NAN2008`, deployed via `./dial-os-mac sd-update`)
**Audio Load:** Real wired FLAC playback (`01 - Shakti Sound.flac`, `44.1 kHz / 16-bit`, `1411 kbps` + `03 - Boogie Man.flac`)

---

## Measured Results Summary

| Gate | Measured on Real V2.57 Hardware | Target / Gate | Decision for M1–M8 |
|---|---|---|---|
| **1. `360x360` Rotation & Tearing** | Single-page: `269.09 fps` avg (`83.33` min, `p99 = 5 ms`, `1346` frames)<br>Triple-buffer `FBIOPAN_DISPLAY`: `270.40 fps` avg (`200.00` min, `p99 = 5 ms`, `1352` frames)<br>`alsa_xrun_delta = 0` | `>= 60 fps`, zero underruns | **PASS (4.5× headroom).** Full-bleed `360x360` spinning cover art at `60 fps` with triple-buffer `FBIOPAN_DISPLAY` is confirmed for M3b — no static-cover fallback needed. |
| **2. `cst816t` Rim Touch Rate** | `345` samples, **`38.55 Hz` active** (`34.50 Hz` over 10 s window, `active_ms = 8949`), `max_gap = 104 ms`, `1` gap `> 50 ms`<br>Bounds: `x = 33..342`, `y = 4..356` | `>= 60 Hz` continuous | **Hardware limit recorded (`~38–40 Hz`).** The `cst816t` panel reports natively at `~38–40 Hz` (`~26 ms` nominal interval) with full rim reach (`x=33..342, y=4..356`). Design M2 polar gesture velocity & smoothing for `~38 Hz` input and tolerate `~100 ms` brief gaps. |
| **3. Rolling-Slice Handoff** | `0100` M3U slice A (`playing_num: "1/2"`): `109 ms`<br>Slice swap `A -> B`: `1328 ms`<br>Rapid `< 2 s` switch (`1.74 s` later): `309 ms`<br>`song.db` leak: `NONE` | Clean handoff, zero `song.db` leak | **PASS.** Rotating `/tmp/dialos_play_<seq>.m3u` via `0100<len>00000004//tmp/...` works cleanly with zero writes to `/usr/data/fiio/db/song.db` and zero stock-DB fallback. |
| **4. V2.57 EQ / PEQ Probe** | `0689` preset, `0689` JSON (`filterType: 0`), `0678` JSON (`filterType: 1`), `0689` off all returned `NO_REPLY` on `/ui`. Boot `aa24` config snapshot exposes `"eq_type": 3`. | Record V2.57 behavior | **Recorded.** `mq_player` does not emit `/ui` acks for those frames. Per `AGENTS.md`, inspect `vendor/stock_v257/mq_player` directly before implementing M6a EQ. |
| **5. Golden V2.57 IPC Trace** | `147` real hardware frames captured in `docs/benchmarks/m0-v257-ipc-trace.log` (`aa24` 35-key boot config, `0100` start with nested escaped JSON & `"state": 2` on new track, `a103` 1 Hz position ticks, `0201` pause/resume `state: 1 -> 0`, `0103` seek working in `165–248 ms`). | Capture golden replay fixture | **Captured.** Note: `a202` embeds `"song"` as an escaped JSON string (`\"song_duration_time\":154000`), which M1's clean-room IPC parser must unescape before extracting duration/metadata. |

---

## Key Hardware Findings Carried Into M1–M3

1. **X2000 software rendering is blazing fast:** `270 fps` (`p99 = 5 ms`) for full-screen `360x360` fixed-point rotation while decoding wired FLAC with `0` ALSA xruns.
2. **Touch rate is `~38–40 Hz`, not `60+ Hz`:** M2's polar gesture engine should tune smoothing/inertia for `~26 ms` sample intervals instead of assuming `60 Hz`.
3. **Real `a202` wire shape confirmed:** New-track `a202` embeds `"song"` as a double-encoded escaped JSON string (`\"song_duration_time\":154000`) and reports `"state": 2` while actively starting playback, followed ~1.9 s later by `a202 {"state": 0}` and `1 Hz` `a103` position ticks.
4. **Boot timing confirmed:** `mq_player` begins emitting `/ui` frames ~`7.8 s` after `mq_ui` starts (`aa24` full config snapshot at `7.78 s`, `a607` at `12.68 s`).
5. **SD-card non-destruction & hot-reload proven:** `./dial-os-mac sd-update` deployed cleanly to `/Volumes/MUSIC/.dial-os/mq_ui` (`130,792` bytes) with zero music files touched.
