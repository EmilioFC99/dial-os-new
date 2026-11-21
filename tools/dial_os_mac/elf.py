"""MIPS32r2 ELF32 EF_MIPS_NAN2008 flag patcher and verifier."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

from tools.dial_os_mac.constants import (
    ELF_CLASS_32,
    ELF_DATA_LSB,
    ELF_FLAGS_OFFSET,
    ELF_HEADER_MIN_SIZE,
    ELF_MACHINE_MIPS,
    ELF_MACHINE_OFFSET,
    ELF_MAGIC,
    ELF_MIPS_NAN2008_FLAG,
)


def _read_validated_mips_elf_header(binary_path: Path) -> bytearray:
    """Read and validate that binary_path is a little-endian ELF32 MIPS executable."""
    if not binary_path.is_file():
        raise RuntimeError(f"ELF binary not found: {binary_path}")

    header_bytes = bytearray(binary_path.read_bytes()[:ELF_HEADER_MIN_SIZE])
    if len(header_bytes) < ELF_HEADER_MIN_SIZE:
        raise RuntimeError(
            f"{binary_path} is truncated ({len(header_bytes)} bytes; expected >= {ELF_HEADER_MIN_SIZE})"
        )
    if header_bytes[0:4] != ELF_MAGIC:
        raise RuntimeError(f"{binary_path} is not an ELF binary (magic={header_bytes[0:4]!r})")
    if header_bytes[4] != ELF_CLASS_32 or header_bytes[5] != ELF_DATA_LSB:
        raise RuntimeError(
            f"{binary_path} must be 32-bit little-endian ELF (class={header_bytes[4]}, data={header_bytes[5]})"
        )

    machine = struct.unpack_from("<H", header_bytes, ELF_MACHINE_OFFSET)[0]
    if machine != ELF_MACHINE_MIPS:
        raise RuntimeError(
            f"{binary_path} is not a MIPS ELF binary (e_machine={machine}, expected {ELF_MACHINE_MIPS})"
        )
    return header_bytes


def patch_mips_nan2008_flag(binary_path: Path) -> None:
    """Set EF_MIPS_NAN2008 (0x400) in the ELF32 MIPS e_flags header field."""
    header_bytes = _read_validated_mips_elf_header(binary_path)
    current_flags = struct.unpack_from("<I", header_bytes, ELF_FLAGS_OFFSET)[0]
    updated_flags = current_flags | ELF_MIPS_NAN2008_FLAG
    if updated_flags == current_flags:
        return

    with binary_path.open("r+b") as binary_handle:
        binary_handle.seek(ELF_FLAGS_OFFSET)
        binary_handle.write(struct.pack("<I", updated_flags))


def verify_mips_nan2008_binary(binary_path: Path) -> None:
    """Raise RuntimeError unless binary_path is a MIPS32 LE ELF with EF_MIPS_NAN2008 set."""
    header_bytes = _read_validated_mips_elf_header(binary_path)
    flags = struct.unpack_from("<I", header_bytes, ELF_FLAGS_OFFSET)[0]
    if (flags & ELF_MIPS_NAN2008_FLAG) == 0:
        raise RuntimeError(
            f"{binary_path} is missing EF_MIPS_NAN2008 (0x{ELF_MIPS_NAN2008_FLAG:x}) in e_flags (0x{flags:08x})"
        )


def main(arguments: list[str]) -> int:
    """CLI entrypoint for patching and verifying a compiled MIPS32r2 binary."""
    if len(arguments) != 2:
        print("Usage: python3 -m tools.dial_os_mac.elf <binary_path>", file=sys.stderr)
        return 2

    target_path = Path(arguments[1])
    patch_mips_nan2008_flag(target_path)
    verify_mips_nan2008_binary(target_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
