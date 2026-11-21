"""V2.57 firmware decryption, mq_player SD-safety patching, and NAND image builder."""

from __future__ import annotations

import contextlib
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import time
import zipfile
from pathlib import Path
from typing import Iterator

from tools.dial_os_mac.constants import (
    BACKUP_DIRECTORY,
    BUILD_DIRECTORY,
    FIIO_OTA_PASSPHRASE,
    FIIO_PBKDF2_ITERATIONS,
    FIRMWARE_CATALOGUE,
    NAND_IMAGE_SIZE,
    STOCK_V257_ROOTFS_PATH,
    TARGET_FIRMWARE_VERSION,
    VENDOR_DIRECTORY,
)
from tools.dial_os_mac.elf import verify_mips_nan2008_binary

UNSAFE_MOUNT_CLEANUP_STRING: bytes = b"rm -rf %s\x00"
SAFE_MOUNT_CLEANUP_STRING: bytes = b"rmdir  %s\x00"
OPENSSL_SALTED_PREFIX: bytes = b"Salted__"
OPENSSL_HEADER_SIZE: int = 16
AES_BLOCK_SIZE: int = 16
SHA256_CHUNK_SIZE: int = 1 << 20


def decrypt_fiio_chunk(encrypted_chunk: bytes, chunk_label: str) -> bytes:
    """Decrypt a FiiO OpenSSL 'Salted__' PBKDF2-HMAC-SHA256 AES-256-CBC chunk."""
    if len(encrypted_chunk) < OPENSSL_HEADER_SIZE + AES_BLOCK_SIZE or encrypted_chunk[:8] != OPENSSL_SALTED_PREFIX:
        raise RuntimeError(f"{chunk_label}: missing OpenSSL 'Salted__' header")

    salt = encrypted_chunk[8:16]
    ciphertext = encrypted_chunk[16:]
    if len(ciphertext) == 0 or (len(ciphertext) % AES_BLOCK_SIZE) != 0:
        raise RuntimeError(
            f"{chunk_label}: ciphertext length ({len(ciphertext)}) is not a multiple of {AES_BLOCK_SIZE} bytes"
        )

    key_and_iv = hashlib.pbkdf2_hmac(
        "sha256",
        FIIO_OTA_PASSPHRASE,
        salt,
        FIIO_PBKDF2_ITERATIONS,
        48,
    )
    openssl_process = subprocess.run(
        [
            "/usr/bin/openssl",
            "enc",
            "-d",
            "-aes-256-cbc",
            "-K",
            key_and_iv[:32].hex(),
            "-iv",
            key_and_iv[32:48].hex(),
        ],
        input=ciphertext,
        capture_output=True,
    )
    if openssl_process.returncode != 0:
        error_text = openssl_process.stderr.decode("utf-8", "ignore").strip()
        raise RuntimeError(f"{chunk_label}: AES-256-CBC decrypt failed: {error_text}")
    return openssl_process.stdout


def patch_mq_player_rmdir_safety(player_binary_bytes: bytes) -> bytes:
    """Neutralize stock mq_player mount_sdcard() 'rm -rf %s' into 'rmdir  %s'."""
    if UNSAFE_MOUNT_CLEANUP_STRING in player_binary_bytes:
        patched_bytes = player_binary_bytes.replace(
            UNSAFE_MOUNT_CLEANUP_STRING,
            SAFE_MOUNT_CLEANUP_STRING,
        )
        if UNSAFE_MOUNT_CLEANUP_STRING in patched_bytes or SAFE_MOUNT_CLEANUP_STRING not in patched_bytes:
            raise RuntimeError("Failed to verify mq_player rmdir safety patch")
        return patched_bytes

    if SAFE_MOUNT_CLEANUP_STRING in player_binary_bytes:
        return player_binary_bytes

    raise RuntimeError(
        "Unrecognized mq_player binary: neither 'rm -rf %s' nor 'rmdir  %s' was found"
    )


def pad_squashfs_to_nand(squashfs_path: Path, nand_image_path: Path) -> None:
    """Copy SquashFS image and zero-pad it to the exact 96 MiB NAND partition size."""
    squashfs_size = squashfs_path.stat().st_size
    if squashfs_size > NAND_IMAGE_SIZE:
        raise RuntimeError(
            f"SquashFS ({squashfs_size} bytes) exceeds NAND partition ({NAND_IMAGE_SIZE} bytes)"
        )

    nand_image_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(squashfs_path, nand_image_path)
    with nand_image_path.open("ab") as output_handle:
        output_handle.truncate(NAND_IMAGE_SIZE)


def _compute_file_sha256(file_path: Path) -> str:
    digest = hashlib.sha256()
    with file_path.open("rb") as file_handle:
        while True:
            chunk = file_handle.read(SHA256_CHUNK_SIZE)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def verify_stock_v257_squashfs(squashfs_path: Path) -> bool:
    """Return True if squashfs_path exists and matches the exact V2.57 size and SHA-256."""
    if not squashfs_path.is_file():
        return False
    expected_size, expected_sha256 = FIRMWARE_CATALOGUE[TARGET_FIRMWARE_VERSION]["rootfs"]  # type: ignore[misc]
    if squashfs_path.stat().st_size != expected_size:
        return False
    return _compute_file_sha256(squashfs_path) == expected_sha256


@contextlib.contextmanager
def _mount_case_sensitive_apfs(working_directory: Path) -> Iterator[Path]:
    """Mount a temporary macOS Case-sensitive APFS sparse image and always detach it."""
    sparse_base = working_directory / "dialos_case_sensitive"
    sparse_image = working_directory / "dialos_case_sensitive.sparseimage"
    mount_point = working_directory / "mount_case_sensitive"
    mount_point.mkdir(parents=True, exist_ok=True)

    try:
        subprocess.run(
            [
                "/usr/bin/hdiutil",
                "create",
                "-size",
                "2g",
                "-type",
                "SPARSE",
                "-fs",
                "Case-sensitive APFS",
                "-volname",
                "DialOSBuild",
                str(sparse_base),
            ],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [
                "/usr/bin/hdiutil",
                "attach",
                str(sparse_image),
                "-mountpoint",
                str(mount_point),
                "-nobrowse",
                "-owners",
                "off",
            ],
            check=True,
            capture_output=True,
        )
        yield mount_point
    finally:
        for attempt in range(5):
            if not os.path.ismount(mount_point):
                break
            detach_command = ["/usr/bin/hdiutil", "detach", str(mount_point)]
            if attempt >= 2:
                detach_command.append("-force")
            subprocess.run(detach_command, capture_output=True)
            time.sleep(0.3)
        if sparse_image.exists():
            sparse_image.unlink()


def _extract_v257_squashfs_from_zip(firmware_zip_path: Path, destination_squashfs: Path) -> Path:
    """Validate and decrypt V2.57 rootfs SquashFS chunks from an official FiiO ZIP."""
    if not firmware_zip_path.is_file():
        raise RuntimeError(f"Firmware ZIP not found: {firmware_zip_path}")

    with zipfile.ZipFile(firmware_zip_path, "r") as archive:
        corrupt_entry = archive.testzip()
        if corrupt_entry is not None:
            raise RuntimeError(f"Corrupt ZIP entry: {corrupt_entry}")

    with tempfile.TemporaryDirectory(prefix="dialos-unzip-") as temporary_directory:
        unzip_root = Path(temporary_directory)
        with zipfile.ZipFile(firmware_zip_path, "r") as archive:
            archive.extractall(unzip_root)

        manifests = sorted(unzip_root.glob("**/main_os/ota_v*/ota_update.in.enc"))
        if not manifests:
            raise RuntimeError("No main_os/ota_v*/ota_update.in.enc found in firmware ZIP")

        ota_directory = manifests[0].parent
        version_match = re.search(r"ota_v(\d+)", ota_directory.name)
        if not version_match or version_match.group(1) != TARGET_FIRMWARE_VERSION:
            found_version = version_match.group(1) if version_match else "unknown"
            raise RuntimeError(
                f"Unsupported firmware version V{found_version}; only V2.57 is supported"
            )

        chunk_paths: dict[int, Path] = {}
        for chunk_file in ota_directory.glob("rootfs.squashfs.*.enc"):
            suffix = chunk_file.name[len("rootfs.squashfs.") :]
            chunk_index_text = suffix.split(".", 1)[0]
            if chunk_index_text.isdigit():
                chunk_paths[int(chunk_index_text)] = chunk_file

        if not chunk_paths:
            raise RuntimeError("No rootfs.squashfs.*.enc chunks found in V2.57 OTA directory")

        destination_squashfs.parent.mkdir(parents=True, exist_ok=True)
        with destination_squashfs.open("wb") as output_handle:
            for chunk_index in sorted(chunk_paths):
                encrypted_bytes = chunk_paths[chunk_index].read_bytes()
                decrypted_bytes = decrypt_fiio_chunk(encrypted_bytes, f"chunk {chunk_index:04d}")
                output_handle.write(decrypted_bytes)

    if not verify_stock_v257_squashfs(destination_squashfs):
        raise RuntimeError("Decrypted V2.57 SquashFS failed size or SHA-256 verification")
    return destination_squashfs


def _resolve_v257_stock_squashfs(firmware_zip_path: Path | None) -> Path:
    """Locate verified V2.57 stock SquashFS or decrypt it from firmware_zip_path."""
    cached_squashfs = BUILD_DIRECTORY / "stock_V257.squashfs"
    if firmware_zip_path is None:
        if verify_stock_v257_squashfs(STOCK_V257_ROOTFS_PATH):
            return STOCK_V257_ROOTFS_PATH
        if verify_stock_v257_squashfs(cached_squashfs):
            return cached_squashfs

        download_candidates = sorted(
            list(Path.home().glob("Downloads/*SNOWSKY*DISC*257*.zip"))
            + list(Path.home().glob("Downloads/*DISC*update*257*.zip"))
        )
        if not download_candidates:
            raise RuntimeError(
                "No verified V2.57 rootfs found and no V2.57 firmware ZIP specified or found in ~/Downloads/"
            )
        firmware_zip_path = download_candidates[-1]

    return _extract_v257_squashfs_from_zip(firmware_zip_path, cached_squashfs)


def build_v257_nand_image(ui_binary_path: Path, firmware_zip_path: Path | None = None) -> Path:
    """Build a padded V2.57 DialOS NAND image with patched mq_player and verified mq_ui."""
    verify_mips_nan2008_binary(ui_binary_path)

    stock_squashfs_path = _resolve_v257_stock_squashfs(firmware_zip_path)
    BUILD_DIRECTORY.mkdir(parents=True, exist_ok=True)
    BACKUP_DIRECTORY.mkdir(parents=True, exist_ok=True)

    stock_backup_nand = BACKUP_DIRECTORY / "stock_V257_nand.bin"
    if not stock_backup_nand.is_file() or stock_backup_nand.stat().st_size != NAND_IMAGE_SIZE:
        pad_squashfs_to_nand(stock_squashfs_path, stock_backup_nand)

    unsquashfs_binary = VENDOR_DIRECTORY / "unsquashfs"
    mksquashfs_binary = VENDOR_DIRECTORY / "mksquashfs"
    dialos_squashfs_path = BUILD_DIRECTORY / "dialos_v257.squashfs"
    output_nand_path = BUILD_DIRECTORY / "dialos_v257_nand.bin"

    with tempfile.TemporaryDirectory(prefix="dialos-build-") as temporary_directory:
        with _mount_case_sensitive_apfs(Path(temporary_directory)) as mount_point:
            rootfs_directory = mount_point / "rootfs"
            subprocess.run(
                [str(unsquashfs_binary), "-processors", "1", "-d", str(rootfs_directory), str(stock_squashfs_path)],
                check=True,
            )

            target_ui_path = rootfs_directory / "usr" / "bin" / "mq_ui"
            stock_ui_backup_path = rootfs_directory / "usr" / "bin" / "mq_ui.stock"
            if target_ui_path.is_file():
                shutil.copy2(target_ui_path, stock_ui_backup_path)
            shutil.copyfile(ui_binary_path, target_ui_path)
            target_ui_path.chmod(0o755)

            player_binary_path = rootfs_directory / "usr" / "bin" / "mq_player"
            if not player_binary_path.is_file():
                raise RuntimeError("Missing /usr/bin/mq_player in extracted V2.57 SquashFS")
            patched_player_bytes = patch_mq_player_rmdir_safety(player_binary_path.read_bytes())
            player_binary_path.write_bytes(patched_player_bytes)
            player_binary_path.chmod(0o755)

            if dialos_squashfs_path.exists():
                dialos_squashfs_path.unlink()
            subprocess.run(
                [
                    str(mksquashfs_binary),
                    str(rootfs_directory),
                    str(dialos_squashfs_path),
                    "-comp",
                    "lzo",
                    "-b",
                    "131072",
                    "-no-xattrs",
                    "-all-root",
                    "-noappend",
                    "-processors",
                    "1",
                ],
                check=True,
            )

    pad_squashfs_to_nand(dialos_squashfs_path, output_nand_path)
    return output_nand_path
