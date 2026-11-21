"""CLI entrypoint for ./dial-os-mac."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
from pathlib import Path

from tools.dial_os_mac.constants import (
    BACKUP_DIRECTORY,
    BUILD_DIRECTORY,
    REPOSITORY_ROOT,
    STOCK_V257_ROOTFS_PATH,
    VENDOR_DIRECTORY,
    WRITER_EXPECTED_SHA256,
)
from tools.dial_os_mac.firmware import build_v257_nand_image, verify_stock_v257_squashfs
from tools.dial_os_mac.flasher import detect_maskrom_device_count, flash_nand_image
from tools.dial_os_mac.sd_deploy import (
    _list_external_volumes,
    deploy_ui_binary_to_sdcard,
    resolve_sdcard_volume,
    set_stock_boot_mode,
)


def _command_doctor(_arguments: argparse.Namespace) -> None:
    print("==================================================================")
    print(" DialOS macOS Tooling — System & Hardware Readiness (`doctor`)")
    print("==================================================================")
    if sys.platform != "darwin":
        raise RuntimeError("This tool is supported exclusively on macOS.")

    print(f"  macOS Host Architecture : {os.uname().machine} (Darwin)")

    required_ready = True
    zig_path = shutil.which("zig")
    if zig_path:
        print(f"  {'zig compiler':<24}: [READY] ({zig_path})")
    else:
        print(f"  {'zig compiler':<24}: [MISSING] (install zig)")
        required_ready = False

    vendor_binaries = (
        ("usbboot", VENDOR_DIRECTORY / "usbboot"),
        ("disc_spl_lpddr3.bin", VENDOR_DIRECTORY / "disc_spl_lpddr3.bin"),
        ("my_write6_dram.bin", VENDOR_DIRECTORY / "my_write6_dram.bin"),
        ("mksquashfs", VENDOR_DIRECTORY / "mksquashfs"),
        ("unsquashfs", VENDOR_DIRECTORY / "unsquashfs"),
    )
    for label, binary_path in vendor_binaries:
        if not binary_path.is_file():
            print(f"  {label:<24}: [MISSING] ({binary_path.relative_to(REPOSITORY_ROOT)})")
            required_ready = False
            continue

        if label == "my_write6_dram.bin":
            actual_sha256 = hashlib.sha256(binary_path.read_bytes()).hexdigest()
            if actual_sha256 != WRITER_EXPECTED_SHA256:
                print(f"  {label:<24}: [BAD SHA256] ({actual_sha256[:16]}...)")
                required_ready = False
                continue

        print(f"  {label:<24}: [READY] ({binary_path.relative_to(REPOSITORY_ROOT)})")

    stock_ready = verify_stock_v257_squashfs(STOCK_V257_ROOTFS_PATH)
    stock_status = "READY" if stock_ready else "NOT PRESENT"
    print(f"  {'V2.57 stock rootfs':<24}: [{stock_status}] ({STOCK_V257_ROOTFS_PATH.relative_to(REPOSITORY_ROOT)})")

    maskrom_count = detect_maskrom_device_count()
    if maskrom_count == 1:
        print("  Snowsky Disc USB Mode   : [CONNECTED] 1 device in Mask-ROM (a108:eaef)")
    elif maskrom_count == 0:
        print("  Snowsky Disc USB Mode   : [NOT CONNECTED] (Power OFF -> Hold Vol Down -> Plug USB)")
    else:
        print(f"  Snowsky Disc USB Mode   : [WARNING] {maskrom_count} Ingenic devices detected")

    external_volumes = _list_external_volumes(Path("/Volumes"))
    if external_volumes:
        volume_names = ", ".join(volume.name for volume in external_volumes)
        print(f"  Mounted /Volumes/       : {volume_names}")
    else:
        print("  Mounted /Volumes/       : [NONE]")

    if BACKUP_DIRECTORY.is_dir():
        backups = sorted(BACKUP_DIRECTORY.glob("stock_V*_nand.bin"))
    else:
        backups = []
    if backups:
        for backup_path in backups:
            print(f"  Saved Stock Backup      : [AVAILABLE] {backup_path}")
    else:
        print("  Saved Stock Backup      : [NONE YET] (Created on `./dial-os-mac build`)")

    print("==================================================================")
    if not required_ready:
        raise RuntimeError("One or more required host tools or vendor binaries are missing or invalid.")


def _command_build(arguments: argparse.Namespace) -> None:
    firmware_zip_path = Path(arguments.firmware_zip).expanduser() if arguments.firmware_zip else None
    ui_binary_path = Path(arguments.ui_binary).expanduser()
    nand_image_path = build_v257_nand_image(ui_binary_path, firmware_zip_path)
    print(f"[SUCCESS] Built V2.57 NAND image: {nand_image_path}")


def _command_flash(_arguments: argparse.Namespace) -> None:
    nand_image_path = BUILD_DIRECTORY / "dialos_v257_nand.bin"
    flash_nand_image(nand_image_path, "Installing DialOS (Base: V2.57)")


def _command_restore_stock(_arguments: argparse.Namespace) -> None:
    stock_backup_path = BACKUP_DIRECTORY / "stock_V257_nand.bin"
    if not stock_backup_path.is_file():
        raise RuntimeError(
            f"No saved V2.57 stock backup found at {stock_backup_path}. Run `./dial-os-mac build` first."
        )
    flash_nand_image(stock_backup_path, "Restoring Stock V2.57 Firmware")


def _command_sd_update(arguments: argparse.Namespace) -> None:
    explicit_volume = Path(arguments.volume).expanduser() if arguments.volume else None
    sdcard_volume = resolve_sdcard_volume(explicit_volume)
    binary_path = Path(arguments.binary).expanduser()
    deployed_path, stock_flag_warning = deploy_ui_binary_to_sdcard(
        binary_path,
        sdcard_volume,
        clear_stock_flag=arguments.clear_stock_flag,
    )
    print(f"[SUCCESS] Deployed {binary_path} -> {deployed_path}")
    if stock_flag_warning:
        print(
            f"[WARNING] {sdcard_volume / '.dial-os' / 'use_stock'} is present! "
            "The Disc will still boot stock UI until you run `./dial-os-mac use-dialos` "
            "or re-run with `--clear-stock-flag`."
        )


def _command_stock_toggle(arguments: argparse.Namespace, use_stock: bool) -> None:
    explicit_volume = Path(arguments.volume).expanduser() if arguments.volume else None
    sdcard_volume = resolve_sdcard_volume(explicit_volume)
    flag_path = set_stock_boot_mode(sdcard_volume, use_stock=use_stock)
    if use_stock:
        print(f"[SUCCESS] Enabled stock UI boot mode ({flag_path} created)")
    else:
        print(f"[SUCCESS] Enabled DialOS SD hot-reload mode ({flag_path} removed)")


def build_argument_parser() -> argparse.ArgumentParser:
    """Construct the ./dial-os-mac CLI argument parser."""
    parser = argparse.ArgumentParser(description="DialOS macOS V2.57 Tooling & Recovery CLI")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("doctor", help="Check macOS tools, vendor binaries, Mask-ROM, and SD volumes")

    build_parser = subparsers.add_parser("build", help="Build a V2.57 NAND image with mq_player safety patch")
    build_parser.add_argument("firmware_zip", nargs="?", default=None, help="Optional path to V2.57 firmware ZIP")
    build_parser.add_argument(
        "--ui-binary",
        default=str(BUILD_DIRECTORY / "mq_ui"),
        help="Path to compiled MIPS32r2 NAN2008 mq_ui binary (default: build/mq_ui)",
    )

    subparsers.add_parser("flash", help="Flash built V2.57 NAND image over USB Mask-ROM")
    subparsers.add_parser("restore-stock", help="Restore saved V2.57 stock NAND backup over USB Mask-ROM")

    sd_update_parser = subparsers.add_parser("sd-update", help="Atomically deploy mq_ui to <volume>/.dial-os/mq_ui")
    sd_update_parser.add_argument(
        "--binary",
        default=str(BUILD_DIRECTORY / "mq_ui"),
        help="Path to compiled MIPS32r2 NAN2008 binary (default: build/mq_ui)",
    )
    sd_update_parser.add_argument("--volume", default=None, help="Explicit /Volumes/<SD> path")
    sd_update_parser.add_argument(
        "--clear-stock-flag",
        action="store_true",
        help="Remove .dial-os/use_stock if present",
    )

    use_stock_parser = subparsers.add_parser("use-stock", help="Create .dial-os/use_stock to boot stock UI")
    use_stock_parser.add_argument("--volume", default=None, help="Explicit /Volumes/<SD> path")

    use_dialos_parser = subparsers.add_parser("use-dialos", help="Remove .dial-os/use_stock to boot DialOS UI")
    use_dialos_parser.add_argument("--volume", default=None, help="Explicit /Volumes/<SD> path")

    return parser


def main(arguments: list[str] | None = None) -> int:
    """CLI entrypoint."""
    parser = build_argument_parser()
    parsed = parser.parse_args(arguments)
    try:
        if parsed.command == "doctor":
            _command_doctor(parsed)
        elif parsed.command == "build":
            _command_build(parsed)
        elif parsed.command == "flash":
            _command_flash(parsed)
        elif parsed.command == "restore-stock":
            _command_restore_stock(parsed)
        elif parsed.command == "sd-update":
            _command_sd_update(parsed)
        elif parsed.command == "use-stock":
            _command_stock_toggle(parsed, use_stock=True)
        elif parsed.command == "use-dialos":
            _command_stock_toggle(parsed, use_stock=False)
    except RuntimeError as error:
        print(f"\n[ERROR] {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
