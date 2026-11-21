"""Unit tests for V2.57 firmware decryption, mq_player safety patching, and NAND padding."""

from __future__ import annotations

import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.dial_os_mac.constants import FIIO_OTA_PASSPHRASE, FIIO_PBKDF2_ITERATIONS, NAND_IMAGE_SIZE
from tools.dial_os_mac.firmware import (
    decrypt_fiio_chunk,
    pad_squashfs_to_nand,
    patch_mq_player_rmdir_safety,
)


class TestFirmwareTools(unittest.TestCase):
    """Tests for firmware.py pure functions and OpenSSL decryption."""

    def test_patch_mq_player_replaces_rm_rf_with_rmdir(self) -> None:
        original_binary = b"\x7fELF" + b"\x00" * 16 + b"prefix\x00rm -rf %s\x00suffix"
        patched_binary = patch_mq_player_rmdir_safety(original_binary)

        self.assertEqual(len(patched_binary), len(original_binary))
        self.assertNotIn(b"rm -rf %s\x00", patched_binary)
        self.assertIn(b"rmdir  %s\x00", patched_binary)

    def test_patch_mq_player_accepts_already_patched_binary(self) -> None:
        already_patched = b"\x7fELF" + b"\x00" * 16 + b"prefix\x00rmdir  %s\x00suffix"
        result_binary = patch_mq_player_rmdir_safety(already_patched)
        self.assertEqual(result_binary, already_patched)

    def test_patch_mq_player_rejects_unrecognized_binary(self) -> None:
        unrecognized_binary = b"\x7fELF" + b"\x00" * 32 + b"no_mount_cleanup_string"
        with self.assertRaises(RuntimeError):
            patch_mq_player_rmdir_safety(unrecognized_binary)

    def test_decrypt_fiio_chunk_roundtrip_with_macos_openssl(self) -> None:
        plaintext = b"DialOS V2.57 SquashFS test payload " * 10
        salt = b"12345678"
        key_and_iv = hashlib.pbkdf2_hmac(
            "sha256",
            FIIO_OTA_PASSPHRASE,
            salt,
            FIIO_PBKDF2_ITERATIONS,
            48,
        )
        encrypt_result = subprocess.run(
            [
                "/usr/bin/openssl",
                "enc",
                "-e",
                "-aes-256-cbc",
                "-K",
                key_and_iv[:32].hex(),
                "-iv",
                key_and_iv[32:48].hex(),
            ],
            input=plaintext,
            capture_output=True,
            check=True,
        )
        encrypted_chunk = b"Salted__" + salt + encrypt_result.stdout

        decrypted = decrypt_fiio_chunk(encrypted_chunk, "chunk 0000")
        self.assertEqual(decrypted, plaintext)

    def test_decrypt_fiio_chunk_rejects_bad_header_and_unaligned_ciphertext(self) -> None:
        with self.assertRaises(RuntimeError):
            decrypt_fiio_chunk(b"BadHead!" + b"\x00" * 32, "bad-header")

        with self.assertRaises(RuntimeError):
            decrypt_fiio_chunk(b"Salted__" + b"12345678" + b"\x00" * 15, "unaligned")

    def test_pad_squashfs_to_nand_exact_size_and_overflow_rejection(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temp_root = Path(temporary_directory)
            small_squashfs = temp_root / "small.squashfs"
            small_squashfs.write_bytes(b"hsqs" + b"\xaa" * 124)
            padded_output = temp_root / "small_nand.bin"

            pad_squashfs_to_nand(small_squashfs, padded_output)
            self.assertEqual(padded_output.stat().st_size, NAND_IMAGE_SIZE)
            with padded_output.open("rb") as output_handle:
                self.assertEqual(output_handle.read(4), b"hsqs")
                output_handle.seek(NAND_IMAGE_SIZE - 16)
                self.assertEqual(output_handle.read(16), b"\x00" * 16)

            exact_squashfs = temp_root / "exact.squashfs"
            with exact_squashfs.open("wb") as exact_handle:
                exact_handle.truncate(NAND_IMAGE_SIZE)
            exact_output = temp_root / "exact_nand.bin"
            pad_squashfs_to_nand(exact_squashfs, exact_output)
            self.assertEqual(exact_output.stat().st_size, NAND_IMAGE_SIZE)

            oversized_squashfs = temp_root / "oversized.squashfs"
            with oversized_squashfs.open("wb") as oversized_handle:
                oversized_handle.truncate(NAND_IMAGE_SIZE + 1)
            with self.assertRaises(RuntimeError):
                pad_squashfs_to_nand(oversized_squashfs, temp_root / "oversized_nand.bin")


if __name__ == "__main__":
    unittest.main()
