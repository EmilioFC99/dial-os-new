"""DGSB base-gate plan builder, USB Mask-ROM detection, and usbboot NAND flasher."""

from __future__ import annotations

import hashlib
import struct
import subprocess
import tempfile
import time
from pathlib import Path

from tools.dial_os_mac.constants import (
    BASE_GATE_PARTITIONS,
    BUILD_DIRECTORY,
    DEBUG_DRAM_ADDRESS,
    DGSB_MAGIC,
    DGSB_PLAN_ADDRESS,
    DGSB_PLAN_SIZE_BYTES,
    FIRMWARE_CATALOGUE,
    FLASH_WAIT_SECONDS,
    IMAGE_DRAM_ADDRESS,
    MASKROM_WAIT_TIMEOUT_SECONDS,
    NAND_IMAGE_SIZE,
    TARGET_FIRMWARE_VERSION,
    USBBOOT_DONE_EXPECTED,
    USBBOOT_GATE_PASS,
    USBBOOT_MAGIC_EXPECTED,
    USBBOOT_READBACK_SIZE,
    USBBOOT_RESULT_EXPECTED,
    USB_PRODUCT_ID_MASKROM,
    USB_VENDOR_ID_INGENIC,
    VENDOR_DIRECTORY,
    WRITER_DRAM_ADDRESS,
    WRITER_ENTRY_ADDRESS,
    WRITER_EXPECTED_SHA256,
)

SHA256_CHUNK_SIZE: int = 1 << 20


def compute_catalogue_fingerprint() -> bytes:
    """Compute the SHA-256 fingerprint over all 4 base-gate firmware versions."""
    digest = hashlib.sha256()
    for version in sorted(FIRMWARE_CATALOGUE):
        entry = FIRMWARE_CATALOGUE[version]
        digest.update(f"{version}|{entry['id']}|".encode())
        for partition_name in BASE_GATE_PARTITIONS:
            partition_size, partition_sha256 = entry[partition_name]  # type: ignore[misc]
            digest.update(f"{partition_size}:{partition_sha256}|".encode())
    return digest.digest()


def _hex_digest_to_big_endian_words(hexdigest: str) -> list[int]:
    digest_bytes = bytes.fromhex(hexdigest)
    return [
        int.from_bytes(digest_bytes[4 * word_index : 4 * word_index + 4], "big")
        for word_index in range(8)
    ]


def build_dgsb_plan(padded_image_path: Path) -> bytes:
    """Build the 272-byte DGSB base-gate plan for my_write6_dram.bin at 0xa0b00000."""
    base_entry = FIRMWARE_CATALOGUE[TARGET_FIRMWARE_VERSION]
    image_digest = hashlib.sha256()
    image_size = 0
    with padded_image_path.open("rb") as image_handle:
        while True:
            chunk = image_handle.read(SHA256_CHUNK_SIZE)
            if not chunk:
                break
            image_digest.update(chunk)
            image_size += len(chunk)

    version_id_bytes = str(base_entry["id"]).encode().ljust(16, b"\x00")[:16]
    words: list[int] = [DGSB_MAGIC, 1, DGSB_PLAN_SIZE_BYTES, 0]
    words.extend(struct.unpack("<4I", version_id_bytes))

    for partition_name in BASE_GATE_PARTITIONS:
        partition_size, partition_sha256 = base_entry[partition_name]  # type: ignore[misc]
        words.append(int(partition_size))
        words.extend(_hex_digest_to_big_endian_words(str(partition_sha256)))

    words.append(image_size)
    words.extend(_hex_digest_to_big_endian_words(image_digest.hexdigest()))
    words.extend(struct.unpack(">8I", compute_catalogue_fingerprint()))
    words.extend([0] * 8)

    plan_body = struct.pack("<60I", *words)
    words.extend(struct.unpack(">8I", hashlib.sha256(plan_body).digest()))
    return struct.pack("<68I", *words)


def verify_usbboot_readback(readback_bytes: bytes) -> None:
    """Validate the 1024-byte usbboot DRAM readback buffer and raise with hex diagnostics on failure."""
    if len(readback_bytes) < USBBOOT_READBACK_SIZE:
        raise RuntimeError(
            f"Truncated usbboot readback ({len(readback_bytes)} bytes; expected {USBBOOT_READBACK_SIZE})"
        )

    words = struct.unpack("<256I", readback_bytes[:USBBOOT_READBACK_SIZE])
    magic = words[0]
    done = words[9]
    result = words[16]
    gate = words[128]

    if (
        magic != USBBOOT_MAGIC_EXPECTED
        or done != USBBOOT_DONE_EXPECTED
        or result != USBBOOT_RESULT_EXPECTED
        or gate != USBBOOT_GATE_PASS
    ):
        raise RuntimeError(
            f"usbboot readback verification failed: magic=0x{magic:08X} "
            f"done=0x{done:08X} result=0x{result:08X} gate=0x{gate:08X}"
        )


def parse_ioreg_maskrom_count(ioreg_output: str) -> int:
    """Count Ingenic X2000 USB Mask-ROM devices (VID=41224, PID=60143) in ioreg output."""
    vendor_token = f'"idVendor" = {USB_VENDOR_ID_INGENIC}'
    product_token = f'"idProduct" = {USB_PRODUCT_ID_MASKROM}'
    device_count = 0
    for block in ioreg_output.split("+-o "):
        if vendor_token in block and product_token in block:
            device_count += 1
    return device_count


def detect_maskrom_device_count() -> int:
    """Query macOS ioreg for connected Ingenic X2000 USB Mask-ROM devices."""
    ioreg_process = subprocess.run(
        ["/usr/sbin/ioreg", "-p", "IOUSB", "-w0", "-l"],
        capture_output=True,
        text=True,
    )
    if ioreg_process.returncode != 0:
        return 0
    return parse_ioreg_maskrom_count(ioreg_process.stdout)


def _wait_for_maskrom_device(timeout_seconds: int = MASKROM_WAIT_TIMEOUT_SECONDS) -> int:
    device_count = detect_maskrom_device_count()
    if device_count > 0:
        return device_count

    print("  [WAITING] Snowsky Disc not yet detected in USB Mask-ROM mode (VID=0xa108, PID=0xeaef).")
    print("    1. Power the Snowsky Disc completely OFF.")
    print("    2. Hold down the Volume-Down button.")
    print("    3. While holding Volume-Down, plug the USB-C cable into the USB/DATA port.")
    print(f"  Waiting up to {timeout_seconds}s for USB connection...", flush=True)

    start_time = time.time()
    while (time.time() - start_time) < timeout_seconds:
        time.sleep(0.5)
        device_count = detect_maskrom_device_count()
        if device_count > 0:
            print("  [CONNECTED] Detected Snowsky Disc in USB Mask-ROM mode (a108:eaef).", flush=True)
            time.sleep(0.5)
            return device_count
    return 0


def flash_nand_image(image_path: Path, flash_label: str) -> None:
    """Verify NAND image and writer SHA-256, then flash over USB Mask-ROM with readback check."""
    print("==================================================================")
    print(f" DialOS macOS Flasher — {flash_label}")
    print("==================================================================")
    print("  [SAFETY REMINDER] Eject and physically remove the microSD card before")
    print("  flashing NAND over USB Mask-ROM.")

    if not image_path.is_file() or image_path.stat().st_size != NAND_IMAGE_SIZE:
        raise RuntimeError(f"Invalid NAND image {image_path} (must be {NAND_IMAGE_SIZE} bytes)")

    usbboot_binary = VENDOR_DIRECTORY / "usbboot"
    spl_binary = VENDOR_DIRECTORY / "disc_spl_lpddr3.bin"
    writer_binary = VENDOR_DIRECTORY / "my_write6_dram.bin"

    writer_sha256 = hashlib.sha256(writer_binary.read_bytes()).hexdigest()
    if writer_sha256 != WRITER_EXPECTED_SHA256:
        raise RuntimeError(
            f"my_write6_dram.bin SHA-256 mismatch ({writer_sha256} != {WRITER_EXPECTED_SHA256}); refusing to flash"
        )

    device_count = _wait_for_maskrom_device(MASKROM_WAIT_TIMEOUT_SECONDS)
    if device_count == 0:
        raise RuntimeError("Timed out waiting for Snowsky Disc in USB Mask-ROM mode (a108:eaef)")
    if device_count > 1:
        raise RuntimeError(f"Found {device_count} Mask-ROM devices; disconnect other Ingenic devices first")

    BUILD_DIRECTORY.mkdir(parents=True, exist_ok=True)
    log_path = BUILD_DIRECTORY / "macos_flash.log"

    with tempfile.TemporaryDirectory(prefix="dialos-usb-") as temporary_directory:
        temp_root = Path(temporary_directory)
        plan_path = temp_root / "plan.bin"
        poison_path = temp_root / "poison.bin"
        readback_path = temp_root / "readback.bin"

        plan_path.write_bytes(build_dgsb_plan(image_path))
        poison_path.write_bytes(b"\xee" * 128)

        usbboot_command = [
            "/usr/bin/caffeinate",
            "-dimsu",
            str(usbboot_binary),
            "-v",
            "--cpu",
            "x2000",
            "--stage1",
            str(spl_binary),
            "--wait",
            "2",
            "--addr",
            WRITER_DRAM_ADDRESS,
            "--download",
            str(writer_binary),
            "--addr",
            IMAGE_DRAM_ADDRESS,
            "--download",
            str(image_path.resolve()),
            "--addr",
            DGSB_PLAN_ADDRESS,
            "--download",
            str(plan_path),
            "--addr",
            DEBUG_DRAM_ADDRESS,
            "--download",
            str(poison_path),
            "--start1",
            WRITER_ENTRY_ADDRESS,
            "--wait",
            FLASH_WAIT_SECONDS,
            "--addr",
            DEBUG_DRAM_ADDRESS,
            "--length",
            "0x400",
            "--upload",
            str(readback_path),
        ]

        print("  Keeping Mac awake via `caffeinate -dimsu`...")
        print(f"  Live log: {log_path}", flush=True)

        with log_path.open("w", encoding="utf-8") as log_handle:
            process = subprocess.Popen(usbboot_command, stdout=log_handle, stderr=subprocess.STDOUT)
            start_time = time.time()
            last_report = 0
            while process.poll() is None:
                elapsed = int(time.time() - start_time)
                if elapsed - last_report >= 30:
                    last_report = elapsed
                    percent = min(99, int((elapsed / int(FLASH_WAIT_SECONDS)) * 100))
                    print(
                        f"  [{elapsed // 60:02d}:{elapsed % 60:02d} / 18:00] ({percent:2d}%) Programming NAND...",
                        flush=True,
                    )
                time.sleep(2)

            if process.returncode != 0:
                raise RuntimeError(f"usbboot exited with code {process.returncode} (see {log_path})")

        if not readback_path.is_file():
            raise RuntimeError(f"No usbboot readback file produced (see {log_path})")
        verify_usbboot_readback(readback_path.read_bytes())
        print("\n[SUCCESS] NAND flash & readback verification passed.")
