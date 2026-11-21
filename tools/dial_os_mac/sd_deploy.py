"""Non-destructive atomic microSD deployment and stock boot escape-hatch helpers."""

from __future__ import annotations

import os
from pathlib import Path

from tools.dial_os_mac.elf import verify_mips_nan2008_binary


def _list_external_volumes(volumes_root: Path) -> list[Path]:
    if not volumes_root.is_dir():
        return []
    candidates: list[Path] = []
    for entry in sorted(volumes_root.iterdir(), key=lambda item: item.name):
        if entry.name.startswith("."):
            continue
        if entry.is_symlink() or not entry.is_dir():
            continue
        candidates.append(entry)
    return candidates


def resolve_sdcard_volume(
    explicit_volume: Path | None,
    volumes_root: Path = Path("/Volumes"),
) -> Path:
    """Resolve the target microSD volume, refusing to guess when ambiguous."""
    if explicit_volume is not None:
        if not explicit_volume.is_dir():
            raise RuntimeError(f"Specified SD-card volume does not exist: {explicit_volume}")
        return explicit_volume

    candidates = _list_external_volumes(volumes_root)
    if not candidates:
        raise RuntimeError(
            f"No external microSD volume found under {volumes_root}. Insert the card or pass --volume."
        )
    if len(candidates) > 1:
        names = ", ".join(candidate.name for candidate in candidates)
        raise RuntimeError(
            f"Multiple external volumes mounted under {volumes_root}: {names}. "
            "Refusing to guess; pass --volume <PATH>."
        )
    return candidates[0]


def deploy_ui_binary_to_sdcard(
    binary_path: Path,
    sdcard_volume: Path,
    clear_stock_flag: bool = False,
) -> tuple[Path, bool]:
    """Atomically deploy a verified MIPS32r2 NAN2008 binary to <volume>/.dial-os/mq_ui."""
    verify_mips_nan2008_binary(binary_path)

    if not sdcard_volume.is_dir():
        raise RuntimeError(f"SD-card volume is not a directory: {sdcard_volume}")

    dialos_directory = sdcard_volume / ".dial-os"
    dialos_directory.mkdir(parents=True, exist_ok=True)

    staging_path = dialos_directory / ".mq_ui.tmp"
    target_path = dialos_directory / "mq_ui"
    binary_bytes = binary_path.read_bytes()

    with staging_path.open("wb") as staging_handle:
        staging_handle.write(binary_bytes)
        staging_handle.flush()
        os.fsync(staging_handle.fileno())

    staging_path.chmod(0o755)
    os.replace(staging_path, target_path)

    use_stock_path = dialos_directory / "use_stock"
    stock_flag_warning = False
    if use_stock_path.exists():
        if clear_stock_flag:
            use_stock_path.unlink()
        else:
            stock_flag_warning = True

    return target_path, stock_flag_warning


def set_stock_boot_mode(sdcard_volume: Path, use_stock: bool) -> Path:
    """Create or remove <volume>/.dial-os/use_stock without touching any other files."""
    if not sdcard_volume.is_dir():
        raise RuntimeError(f"SD-card volume is not a directory: {sdcard_volume}")

    dialos_directory = sdcard_volume / ".dial-os"
    flag_path = dialos_directory / "use_stock"
    if use_stock:
        dialos_directory.mkdir(parents=True, exist_ok=True)
        flag_path.write_text("1\n", encoding="utf-8")
    elif flag_path.exists():
        flag_path.unlink()

    return flag_path
