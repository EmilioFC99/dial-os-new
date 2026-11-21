"""Unit tests for atomic SD-card deployment and stock escape hatch."""

from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from tools.dial_os_mac.constants import ELF_FLAGS_OFFSET, ELF_MACHINE_MIPS, ELF_MIPS_NAN2008_FLAG
from tools.dial_os_mac.sd_deploy import (
    deploy_ui_binary_to_sdcard,
    resolve_sdcard_volume,
    set_stock_boot_mode,
)


def build_valid_mips_nan2008_elf() -> bytes:
    """Create a minimal valid 52-byte MIPS32 LE ELF with EF_MIPS_NAN2008 set."""
    header = bytearray(52)
    header[0:4] = b"\x7fELF"
    header[4] = 1
    header[5] = 1
    header[6] = 1
    struct.pack_into("<HHI", header, 16, 2, ELF_MACHINE_MIPS, 1)
    struct.pack_into("<I", header, ELF_FLAGS_OFFSET, 0x70001005 | ELF_MIPS_NAN2008_FLAG)
    return bytes(header)


class TestSdDeploy(unittest.TestCase):
    """Tests for sd_deploy.py functions."""

    def test_resolve_sdcard_volume_picks_single_external_volume(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            volumes_root = Path(temporary_directory) / "Volumes"
            volumes_root.mkdir()
            (volumes_root / "Macintosh HD").symlink_to("/")
            sd_volume = volumes_root / "SNOWSKY_SD"
            sd_volume.mkdir()

            resolved = resolve_sdcard_volume(None, volumes_root=volumes_root)
            self.assertEqual(resolved, sd_volume)

    def test_resolve_sdcard_volume_rejects_zero_or_ambiguous_volumes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            volumes_root = Path(temporary_directory) / "Volumes"
            volumes_root.mkdir()
            (volumes_root / "Macintosh HD").symlink_to("/")

            with self.assertRaises(RuntimeError):
                resolve_sdcard_volume(None, volumes_root=volumes_root)

            (volumes_root / "SD_A").mkdir()
            (volumes_root / "SD_B").mkdir()

            with self.assertRaises(RuntimeError) as error_context:
                resolve_sdcard_volume(None, volumes_root=volumes_root)

            error_message = str(error_context.exception)
            self.assertIn("SD_A", error_message)
            self.assertIn("SD_B", error_message)

    def test_deploy_writes_atomically_and_preserves_existing_music_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temp_root = Path(temporary_directory)
            sd_volume = temp_root / "SNOWSKY_SD"
            music_file = sd_volume / "Music" / "song.flac"
            music_file.parent.mkdir(parents=True)
            music_file.write_bytes(b"FLAC_TEST_AUDIO")

            source_binary = temp_root / "m0_mq_ui"
            binary_bytes = build_valid_mips_nan2008_elf()
            source_binary.write_bytes(binary_bytes)

            deployed_path, stock_flag_warning = deploy_ui_binary_to_sdcard(
                source_binary,
                sd_volume,
            )

            self.assertFalse(stock_flag_warning)
            self.assertEqual(deployed_path, sd_volume / ".dial-os" / "mq_ui")
            self.assertEqual(deployed_path.read_bytes(), binary_bytes)
            self.assertFalse((sd_volume / ".dial-os" / ".mq_ui.tmp").exists())
            self.assertEqual(music_file.read_bytes(), b"FLAC_TEST_AUDIO")

    def test_deploy_warns_when_use_stock_exists_and_clears_only_when_requested(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temp_root = Path(temporary_directory)
            sd_volume = temp_root / "SNOWSKY_SD"
            dialos_directory = sd_volume / ".dial-os"
            dialos_directory.mkdir(parents=True)
            use_stock_path = dialos_directory / "use_stock"
            use_stock_path.write_text("1\n", encoding="utf-8")

            source_binary = temp_root / "m0_mq_ui"
            source_binary.write_bytes(build_valid_mips_nan2008_elf())

            _, warning_without_clear = deploy_ui_binary_to_sdcard(
                source_binary,
                sd_volume,
                clear_stock_flag=False,
            )
            self.assertTrue(warning_without_clear)
            self.assertTrue(use_stock_path.exists())

            _, warning_with_clear = deploy_ui_binary_to_sdcard(
                source_binary,
                sd_volume,
                clear_stock_flag=True,
            )
            self.assertFalse(warning_with_clear)
            self.assertFalse(use_stock_path.exists())

    def test_deploy_rejects_non_mips_binary_without_creating_sd_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temp_root = Path(temporary_directory)
            sd_volume = temp_root / "SNOWSKY_SD"
            sd_volume.mkdir()

            invalid_binary = temp_root / "not_mips"
            invalid_binary.write_bytes(b"\xcf\xfa\xed\xfe" + b"\x00" * 60)

            with self.assertRaises(RuntimeError):
                deploy_ui_binary_to_sdcard(invalid_binary, sd_volume)

            self.assertFalse((sd_volume / ".dial-os" / "mq_ui").exists())

    def test_set_stock_boot_mode_creates_and_removes_use_stock_safely(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            sd_volume = Path(temporary_directory) / "SNOWSKY_SD"
            dialos_directory = sd_volume / ".dial-os"
            dialos_directory.mkdir(parents=True)
            sibling_file = dialos_directory / "mq_ui"
            sibling_file.write_bytes(b"KEEP_ME")

            flag_path = set_stock_boot_mode(sd_volume, use_stock=True)
            self.assertEqual(flag_path, dialos_directory / "use_stock")
            self.assertTrue(flag_path.is_file())
            self.assertEqual(sibling_file.read_bytes(), b"KEEP_ME")

            set_stock_boot_mode(sd_volume, use_stock=False)
            self.assertFalse(flag_path.exists())
            self.assertEqual(sibling_file.read_bytes(), b"KEEP_ME")


if __name__ == "__main__":
    unittest.main()
