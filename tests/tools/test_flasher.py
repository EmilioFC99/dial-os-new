"""Unit tests for DGSB base-gate plan builder, Mask-ROM detection, and usbboot readback."""

from __future__ import annotations

import hashlib
import struct
import tempfile
import unittest
from pathlib import Path

from tools.dial_os_mac.constants import (
    BASE_GATE_PARTITIONS,
    DGSB_MAGIC,
    DGSB_PLAN_SIZE_BYTES,
    FIRMWARE_CATALOGUE,
    USBBOOT_DONE_EXPECTED,
    USBBOOT_GATE_PASS,
    USBBOOT_MAGIC_EXPECTED,
    USBBOOT_READBACK_SIZE,
    USBBOOT_RESULT_EXPECTED,
)
from tools.dial_os_mac.flasher import (
    build_dgsb_plan,
    compute_catalogue_fingerprint,
    parse_ioreg_maskrom_count,
    verify_usbboot_readback,
)


class TestFlasherTools(unittest.TestCase):
    """Tests for flasher.py pure functions."""

    def test_dgsb_plan_is_272_bytes_with_valid_magic_and_self_checksum(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            image_path = Path(temporary_directory) / "padded_nand.bin"
            image_path.write_bytes(b"\x5a" * 4096)

            plan_bytes = build_dgsb_plan(image_path)
            self.assertEqual(len(plan_bytes), DGSB_PLAN_SIZE_BYTES)

            words = struct.unpack("<68I", plan_bytes)
            self.assertEqual(words[0], DGSB_MAGIC)
            self.assertEqual(words[1], 1)
            self.assertEqual(words[2], DGSB_PLAN_SIZE_BYTES)
            self.assertTrue(plan_bytes[16:32].startswith(b"v257-r18\x00"))

            expected_checksum = hashlib.sha256(plan_bytes[:240]).digest()
            actual_checksum = struct.pack(">8I", *words[60:68])
            self.assertEqual(actual_checksum, expected_checksum)

    def test_catalogue_fingerprint_matches_four_version_digest(self) -> None:
        fingerprint = compute_catalogue_fingerprint()
        self.assertEqual(len(fingerprint), 32)

        expected_digest = hashlib.sha256()
        for version in ("209", "228", "240", "257"):
            entry = FIRMWARE_CATALOGUE[version]
            expected_digest.update(f"{version}|{entry['id']}|".encode())
            for partition_name in BASE_GATE_PARTITIONS:
                partition_size, partition_sha256 = entry[partition_name]  # type: ignore[misc]
                expected_digest.update(f"{partition_size}:{partition_sha256}|".encode())

        self.assertEqual(fingerprint, expected_digest.digest())

    def test_verify_usbboot_readback_accepts_success_words(self) -> None:
        words = [0] * 256
        words[0] = USBBOOT_MAGIC_EXPECTED
        words[9] = USBBOOT_DONE_EXPECTED
        words[16] = USBBOOT_RESULT_EXPECTED
        words[128] = USBBOOT_GATE_PASS
        readback_bytes = struct.pack("<256I", *words)

        self.assertEqual(len(readback_bytes), USBBOOT_READBACK_SIZE)
        verify_usbboot_readback(readback_bytes)

    def test_verify_usbboot_readback_rejects_truncated_or_failed_gate(self) -> None:
        with self.assertRaises(RuntimeError):
            verify_usbboot_readback(b"\x00" * 512)

        words = [0] * 256
        words[0] = USBBOOT_MAGIC_EXPECTED
        words[9] = USBBOOT_DONE_EXPECTED
        words[16] = USBBOOT_RESULT_EXPECTED
        words[128] = 0
        failed_readback = struct.pack("<256I", *words)

        with self.assertRaises(RuntimeError) as error_context:
            verify_usbboot_readback(failed_readback)

        error_message = str(error_context.exception)
        self.assertIn("0x4006E006", error_message)
        self.assertIn("0x55555555", error_message)
        self.assertIn("0x600DF10C", error_message)
        self.assertIn("0x00000000", error_message)

    def test_parse_ioreg_maskrom_count_counts_only_matching_vid_pid(self) -> None:
        empty_output = '+-o Root  <class IORegistryEntry>\n+-o AppleUSBHostPort\n  "idVendor" = 1452\n'
        self.assertEqual(parse_ioreg_maskrom_count(empty_output), 0)

        single_device_output = (
            '+-o Root\n'
            '+-o X2000@01100000\n'
            '  "idVendor" = 41224\n'
            '  "idProduct" = 60143\n'
            '+-o OtherDevice@01200000\n'
            '  "idVendor" = 41224\n'
            '  "idProduct" = 12345\n'
        )
        self.assertEqual(parse_ioreg_maskrom_count(single_device_output), 1)

        two_device_output = single_device_output + (
            '+-o X2000@01300000\n'
            '  "idVendor" = 41224\n'
            '  "idProduct" = 60143\n'
        )
        self.assertEqual(parse_ioreg_maskrom_count(two_device_output), 2)


if __name__ == "__main__":
    unittest.main()
