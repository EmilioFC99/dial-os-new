"""Exact hardware, firmware, ELF, and USB Mask-ROM constants for FiiO Snowsky Disc (V2.57)."""

from __future__ import annotations

from pathlib import Path

REPOSITORY_ROOT: Path = Path(__file__).resolve().parents[2]
VENDOR_DIRECTORY: Path = REPOSITORY_ROOT / "tools" / "vendor"
BUILD_DIRECTORY: Path = REPOSITORY_ROOT / "build"
BACKUP_DIRECTORY: Path = Path.home() / "Library" / "Application Support" / "DialOS" / "backups"
STOCK_V257_ROOTFS_PATH: Path = REPOSITORY_ROOT / "vendor" / "stock_v257" / "rootfs.squashfs"

FIIO_OTA_PASSPHRASE: bytes = b"fo123"
FIIO_PBKDF2_ITERATIONS: int = 10000
SQUASHFS_MAGIC: bytes = b"hsqs"

NAND_IMAGE_SIZE: int = 100_663_296  # 768 x 128 KiB blocks
WRITER_EXPECTED_SHA256: str = "a3c3aa63e0c48f501181d542d9fa0526d36ded81e2e767f0c044af876674ea84"

DGSB_MAGIC: int = 0x42534744  # "DGSB" little-endian
DGSB_PLAN_SIZE_BYTES: int = 272
DGSB_PLAN_ADDRESS: str = "0xa0b00000"
WRITER_DRAM_ADDRESS: str = "0xa0c00000"
WRITER_ENTRY_ADDRESS: str = "0xa0c00030"
IMAGE_DRAM_ADDRESS: str = "0xa1000000"
DEBUG_DRAM_ADDRESS: str = "0xa0a00000"
FLASH_WAIT_SECONDS: str = "1080"
MASKROM_WAIT_TIMEOUT_SECONDS: int = 180

USB_VENDOR_ID_INGENIC: int = 41224   # 0xa108
USB_PRODUCT_ID_MASKROM: int = 60143  # 0xeaef

USBBOOT_READBACK_SIZE: int = 1024
USBBOOT_MAGIC_EXPECTED: int = 0x4006E006
USBBOOT_DONE_EXPECTED: int = 0x55555555
USBBOOT_RESULT_EXPECTED: int = 0x600DF10C
USBBOOT_GATE_PASS: int = 1

ELF_HEADER_MIN_SIZE: int = 52
ELF_MAGIC: bytes = b"\x7fELF"
ELF_CLASS_32: int = 1
ELF_DATA_LSB: int = 1
ELF_MACHINE_OFFSET: int = 18
ELF_MACHINE_MIPS: int = 8
ELF_FLAGS_OFFSET: int = 36
ELF_MIPS_NAN2008_FLAG: int = 0x400

TARGET_FIRMWARE_VERSION: str = "257"
BASE_GATE_PARTITIONS: tuple[str, ...] = ("main_kernel", "recovery_kernel", "recovery_rootfs")

# Exact 4-version base-gate digest catalogue required by my_write6_dram.bin
FIRMWARE_CATALOGUE: dict[str, dict[str, object]] = {
    "209": {
        "id": "v209-r17a",
        "name": "V2.09 (recovery 17)",
        "rootfs": (75_919_360, "f1e3c69fb0e88b923c135558e01f4387a661f68839c8118e8ad490bdc9fc74e6"),
        "main_kernel": (4_415_552, "f843cfcc28fe3c0d26bf27e35a2d8d595aa200ef82750200c888af5348f4f033"),
        "recovery_kernel": (4_042_816, "db8a2a7aaf286c381e8a760481fa13b3168a1716ef510834bdea03495a8876df"),
        "recovery_rootfs": (14_151_680, "367ea3159ad63c63ea8b2d752d3fa1f4d5fc292171db846c82fcfec360389331"),
    },
    "228": {
        "id": "v228-r17",
        "name": "V2.28 (recovery 17)",
        "rootfs": (72_957_952, "0ffd877bca2c69ddff9ca70f4494da0d9e580c18d0f587e2c6d9921f2db82bd2"),
        "main_kernel": (4_423_744, "f3c1820f5258a319eca820e0e6d64a4bac764ae36a9eac5de05f255d8910cfeb"),
        "recovery_kernel": (4_042_816, "5da45c40894c2101249c91063bc164a67f850e1f0b3e7bdf39856e940c26b314"),
        "recovery_rootfs": (14_151_680, "b093b73076bf4e6040b0d90f8e1a042818db3a8417ea6da520d3de4c10dbb4c5"),
    },
    "240": {
        "id": "v240-r17",
        "name": "V2.40 (recovery 17)",
        "rootfs": (88_420_352, "b479e159db5134325819b5f6e5a54388f3adefae373a4ee60680f02d5dcf0bb8"),
        "main_kernel": (4_423_744, "5ce48bb5b5e2149777ed367b4bc68146ff132ec45e3a461e62c916d0a3ab2a7a"),
        "recovery_kernel": (4_042_816, "5da45c40894c2101249c91063bc164a67f850e1f0b3e7bdf39856e940c26b314"),
        "recovery_rootfs": (14_151_680, "b093b73076bf4e6040b0d90f8e1a042818db3a8417ea6da520d3de4c10dbb4c5"),
    },
    "257": {
        "id": "v257-r18",
        "name": "V2.57 (recovery 18)",
        "rootfs": (80_596_992, "111e4dd7ee3d7ff91ba7e61181690be7ffd22bd1bbd13ad513f5f015ffb302ae"),
        "main_kernel": (4_423_744, "1a9484efcb64c391b23910306e867cb628af07e059d0d1ccc13e85269e030ed2"),
        "recovery_kernel": (4_051_008, "9561ccb7ad1bd752eb421509847760a5a88d4ccb49c8a75eb6710b1e9aaf9113"),
        "recovery_rootfs": (16_527_360, "73b0f8e834d36c0fae9b23f8832df59195bd0f4252a42a1afe4dd15ce0898780"),
    },
}
