"""Unit tests for MIPS32r2 ELF EF_MIPS_NAN2008 patching and verification."""

from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from tools.dial_os_mac.constants import ELF_FLAGS_OFFSET, ELF_MACHINE_MIPS, ELF_MIPS_NAN2008_FLAG
from tools.dial_os_mac.elf import patch_mips_nan2008_flag, verify_mips_nan2008_binary


def build_synthetic_elf_header(
    *,
    elf_class: int = 1,
    elf_data: int = 1,
    machine: int = ELF_MACHINE_MIPS,
    flags: int = 0x70001005,
) -> bytes:
    """Build a minimal 52-byte ELF32 header with the specified machine and flags."""
    header = bytearray(52)
    header[0:4] = b"\x7fELF"
    header[4] = elf_class
    header[5] = elf_data
    header[6] = 1  # EV_CURRENT
    struct.pack_into("<HHI", header, 16, 2, machine, 1)  # ET_EXEC, e_machine, e_version
    struct.pack_into("<I", header, ELF_FLAGS_OFFSET, flags)
    return bytes(header)


class TestMipsNan2008Elf(unittest.TestCase):
    """Tests for patch_mips_nan2008_flag() and verify_mips_nan2008_binary()."""

    def test_patch_and_verify_sets_nan2008_bit_on_mips32_le_elf(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            binary_path = Path(temporary_directory) / "mq_ui"
            binary_path.write_bytes(build_synthetic_elf_header(flags=0x70001005))

            patch_mips_nan2008_flag(binary_path)
            updated_bytes = binary_path.read_bytes()
            updated_flags = struct.unpack_from("<I", updated_bytes, ELF_FLAGS_OFFSET)[0]

            self.assertEqual(updated_flags, 0x70001405)
            self.assertTrue(updated_flags & ELF_MIPS_NAN2008_FLAG)
            verify_mips_nan2008_binary(binary_path)

    def test_patch_is_idempotent_when_nan2008_already_set(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            binary_path = Path(temporary_directory) / "mq_ui"
            binary_path.write_bytes(build_synthetic_elf_header(flags=0x70001405))

            patch_mips_nan2008_flag(binary_path)
            updated_flags = struct.unpack_from("<I", binary_path.read_bytes(), ELF_FLAGS_OFFSET)[0]

            self.assertEqual(updated_flags, 0x70001405)
            verify_mips_nan2008_binary(binary_path)

    def test_verify_rejects_unpatched_mips_elf(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            binary_path = Path(temporary_directory) / "mq_ui"
            binary_path.write_bytes(build_synthetic_elf_header(flags=0x70001005))

            with self.assertRaises(RuntimeError) as error_context:
                verify_mips_nan2008_binary(binary_path)

            self.assertIn("EF_MIPS_NAN2008", str(error_context.exception))

    def test_verify_rejects_non_elf_or_macho_or_truncated_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            macho_path = Path(temporary_directory) / "macho_bin"
            macho_path.write_bytes(b"\xcf\xfa\xed\xfe" + b"\x00" * 60)

            truncated_path = Path(temporary_directory) / "truncated_bin"
            truncated_path.write_bytes(b"\x7fELF" + b"\x00" * 12)

            with self.assertRaises(RuntimeError):
                verify_mips_nan2008_binary(macho_path)

            with self.assertRaises(RuntimeError):
                verify_mips_nan2008_binary(truncated_path)

    def test_verify_rejects_non_mips_elf_architecture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            x86_path = Path(temporary_directory) / "x86_64_bin"
            x86_path.write_bytes(
                build_synthetic_elf_header(elf_class=2, machine=62, flags=0x70001405)
            )

            with self.assertRaises(RuntimeError):
                verify_mips_nan2008_binary(x86_path)


if __name__ == "__main__":
    unittest.main()
