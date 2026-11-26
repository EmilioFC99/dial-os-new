#!/usr/bin/env python3
"""Unit and HTTP integration tests for the DialOS local simulator server."""

from __future__ import annotations

import http.client
import json
import pathlib
import struct
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

SIMULATOR_DIRECTORY = pathlib.Path(__file__).resolve().parent
if str(SIMULATOR_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SIMULATOR_DIRECTORY))

import server

FLAC_SIGNATURE = b"fLaC"
FLAC_LAST_BLOCK_MASK = 0b1000_0000
FLAC_BLOCK_STREAM_INFORMATION = 0
FLAC_BLOCK_VORBIS_COMMENT = 4
FLAC_BLOCK_PICTURE = 6
FLAC_PICTURE_TYPE_FRONT_COVER = 3
FLAC_MIN_BLOCK_SAMPLES = 4096
FLAC_MAX_BLOCK_SAMPLES = 4096
FLAC_SAMPLE_RATE_SHIFT_BITS = 44
FLAC_CHANNELS_SHIFT_BITS = 41
FLAC_BITS_PER_SAMPLE_SHIFT_BITS = 36
FLAC_SAMPLE_RATE_MASK = 0b1111_1111_1111_1111_1111
FLAC_CHANNEL_COUNT_MASK = 0b0111
FLAC_BITS_PER_SAMPLE_MASK = 0b0001_1111
FLAC_TOTAL_SAMPLES_MASK = 0b1111_1111_1111_1111_1111_1111_1111_1111_1111
FLAC_FRAME_BOUND_BYTES = 3
FLAC_MD5_PADDING_BYTES = 16
FLAC_PICTURE_DIMENSIONS_PX = 500
FLAC_PICTURE_COLOR_DEPTH_BITS = 24

ID3_VERSION_MAJOR = 3
ID3_VERSION_MINOR = 0
ID3_HEADER_FLAGS_NONE = 0
ID3_SYNCHSAFE_MASK = 0b0111_1111
ID3_SYNCHSAFE_SHIFT_HIGH = 21
ID3_SYNCHSAFE_SHIFT_MID_HIGH = 14
ID3_SYNCHSAFE_SHIFT_MID_LOW = 7
ID3_ENCODING_UTF8 = bytes([3])
ID3_ENCODING_LATIN1 = bytes([0])
ID3_PICTURE_TYPE_FRONT_COVER = bytes([3])

# MPEG-1 Layer III, no CRC, 320 kbps, 44100 Hz, no padding -> 1044 bytes per frame
MPEG_FRAME_HEADER_320K_44100 = bytes([0b1111_1111, 0b1111_1011, 0b1110_0000, 0b0000_0000])
MPEG_FRAME_BYTES_320K_44100 = 1044

STANDARD_SAMPLE_RATE_HZ = 44100
HIGH_RESOLUTION_SAMPLE_RATE_HZ = 96000
STANDARD_CHANNEL_COUNT = 2
STANDARD_BITS_PER_SAMPLE = 16
HIGH_RESOLUTION_BITS_PER_SAMPLE = 24
DEFAULT_FLAC_DURATION_SECONDS = 120
HIGH_RESOLUTION_FLAC_DURATION_SECONDS = 185
DEFAULT_MP3_FRAME_COUNT = 38
LONG_MP3_FRAME_COUNT = 100

HTTP_OK = 200
HTTP_ACCEPTED = 202
HTTP_PARTIAL_CONTENT = 206
HTTP_BAD_REQUEST = 400
HTTP_NOT_FOUND = 404
HTTP_RANGE_NOT_SATISFIABLE = 416

ASYNC_BOOT_POLL_INTERVAL_SECONDS = 0.01
ASYNC_BOOT_TIMEOUT_SECONDS = 5.0
AUDIO_PAYLOAD_BYTE_PATTERN_LENGTH = 256
RANGE_BOUNDED_END_BYTE = 15
RANGE_OPEN_TAIL_BYTES = 10
RANGE_SUFFIX_TAIL_BYTES = 8
RANGE_OUT_OF_BOUNDS_OFFSET = 50

SYNTHETIC_JPEG_BYTES = (
    b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xd9"
)
SYNTHETIC_PNG_BYTES = b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR"

BOOT_STATUS_REQUIRED_KEYS = (
    "scanId",
    "phase",
    "ready",
    "discoveredFiles",
    "scannedFiles",
    "progress",
    "musicRoot",
    "musicRootExists",
    "counts",
    "scanDurationSeconds",
    "error",
)


def build_synthetic_flac_bytes(
    *,
    sample_rate_hz: int = STANDARD_SAMPLE_RATE_HZ,
    channels: int = STANDARD_CHANNEL_COUNT,
    bits_per_sample: int = STANDARD_BITS_PER_SAMPLE,
    total_samples: int = STANDARD_SAMPLE_RATE_HZ * DEFAULT_FLAC_DURATION_SECONDS,
    vorbis_tags: dict[str, str] | None = None,
    embedded_picture_bytes: bytes | None = None,
    embedded_picture_mime: str = "image/jpeg",
    audio_payload: bytes = b"\xaa\xbb\xcc\xdd" * 16,
) -> bytes:
    """Construct a minimal valid FLAC byte stream for deterministic testing."""
    metadata_blocks: list[tuple[int, bytes]] = []

    packed_stream_fields = (
        ((sample_rate_hz & FLAC_SAMPLE_RATE_MASK) << FLAC_SAMPLE_RATE_SHIFT_BITS)
        | (((channels - 1) & FLAC_CHANNEL_COUNT_MASK) << FLAC_CHANNELS_SHIFT_BITS)
        | (((bits_per_sample - 1) & FLAC_BITS_PER_SAMPLE_MASK) << FLAC_BITS_PER_SAMPLE_SHIFT_BITS)
        | (total_samples & FLAC_TOTAL_SAMPLES_MASK)
    )
    stream_information_payload = (
        struct.pack(">HH", FLAC_MIN_BLOCK_SAMPLES, FLAC_MAX_BLOCK_SAMPLES)
        + (b"\x00" * FLAC_FRAME_BOUND_BYTES)
        + (b"\x00" * FLAC_FRAME_BOUND_BYTES)
        + packed_stream_fields.to_bytes(8, "big")
        + (b"\x00" * FLAC_MD5_PADDING_BYTES)
    )
    metadata_blocks.append((FLAC_BLOCK_STREAM_INFORMATION, stream_information_payload))

    if vorbis_tags:
        vendor_bytes = b"DialOS Simulator Test"
        comment_parts = [
            struct.pack("<I", len(vendor_bytes)),
            vendor_bytes,
            struct.pack("<I", len(vorbis_tags)),
        ]
        for tag_key, tag_value in vorbis_tags.items():
            encoded_entry = f"{tag_key}={tag_value}".encode("utf-8")
            comment_parts.append(struct.pack("<I", len(encoded_entry)))
            comment_parts.append(encoded_entry)
        metadata_blocks.append((FLAC_BLOCK_VORBIS_COMMENT, b"".join(comment_parts)))

    if embedded_picture_bytes is not None:
        mime_bytes = embedded_picture_mime.encode("ascii")
        picture_payload = (
            struct.pack(">II", FLAC_PICTURE_TYPE_FRONT_COVER, len(mime_bytes))
            + mime_bytes
            + struct.pack(
                ">IIIII",
                0,
                FLAC_PICTURE_DIMENSIONS_PX,
                FLAC_PICTURE_DIMENSIONS_PX,
                FLAC_PICTURE_COLOR_DEPTH_BITS,
                0,
            )
            + struct.pack(">I", len(embedded_picture_bytes))
            + embedded_picture_bytes
        )
        metadata_blocks.append((FLAC_BLOCK_PICTURE, picture_payload))

    output_parts = [FLAC_SIGNATURE]
    for block_index, (block_type, payload_bytes) in enumerate(metadata_blocks):
        is_last = block_index == len(metadata_blocks) - 1
        header_byte = block_type | (FLAC_LAST_BLOCK_MASK if is_last else 0)
        output_parts.append(bytes([header_byte]) + len(payload_bytes).to_bytes(3, "big"))
        output_parts.append(payload_bytes)
    output_parts.append(audio_payload)
    return b"".join(output_parts)


def encode_synchsafe_integer(integer_value: int) -> bytes:
    """Encode an integer as a 4-byte ID3v2 synchsafe size field."""
    return bytes(
        [
            (integer_value >> ID3_SYNCHSAFE_SHIFT_HIGH) & ID3_SYNCHSAFE_MASK,
            (integer_value >> ID3_SYNCHSAFE_SHIFT_MID_HIGH) & ID3_SYNCHSAFE_MASK,
            (integer_value >> ID3_SYNCHSAFE_SHIFT_MID_LOW) & ID3_SYNCHSAFE_MASK,
            integer_value & ID3_SYNCHSAFE_MASK,
        ]
    )


def build_synthetic_mp3_bytes(
    *,
    title: str = "Synth Pulse",
    artist: str = "Vector Unit",
    album: str = "Signal Tape",
    genre: str = "Electronic",
    year: str = "2025",
    track_number: str = "2/8",
    unsynced_lyrics: str | None = None,
    embedded_picture_bytes: bytes | None = None,
    mpeg_frame_count: int = DEFAULT_MP3_FRAME_COUNT,
) -> bytes:
    """Construct a minimal ID3v2.3 + MPEG-1 Layer III (320kbps, 44.1kHz) MP3 stream."""
    id3_frames: list[bytes] = []

    def append_text_frame(frame_id: bytes, text_value: str) -> None:
        payload_bytes = ID3_ENCODING_UTF8 + text_value.encode("utf-8")
        id3_frames.append(frame_id + struct.pack(">IH", len(payload_bytes), 0) + payload_bytes)

    append_text_frame(b"TIT2", title)
    append_text_frame(b"TPE1", artist)
    append_text_frame(b"TPE2", artist)
    append_text_frame(b"TALB", album)
    append_text_frame(b"TCON", genre)
    append_text_frame(b"TYER", year)
    append_text_frame(b"TRCK", track_number)

    if unsynced_lyrics:
        lyrics_payload = ID3_ENCODING_UTF8 + b"eng\x00" + unsynced_lyrics.encode("utf-8")
        id3_frames.append(b"USLT" + struct.pack(">IH", len(lyrics_payload), 0) + lyrics_payload)

    if embedded_picture_bytes is not None:
        picture_payload = (
            ID3_ENCODING_LATIN1
            + b"image/jpeg\x00"
            + ID3_PICTURE_TYPE_FRONT_COVER
            + b"\x00"
            + embedded_picture_bytes
        )
        id3_frames.append(b"APIC" + struct.pack(">IH", len(picture_payload), 0) + picture_payload)

    tag_body = b"".join(id3_frames)
    id3_header = (
        b"ID3"
        + bytes([ID3_VERSION_MAJOR, ID3_VERSION_MINOR, ID3_HEADER_FLAGS_NONE])
        + encode_synchsafe_integer(len(tag_body))
    )
    single_frame = MPEG_FRAME_HEADER_320K_44100 + (
        b"\x00" * (MPEG_FRAME_BYTES_320K_44100 - len(MPEG_FRAME_HEADER_320K_44100))
    )
    return id3_header + tag_body + (single_frame * mpeg_frame_count)


def create_sample_music_tree(music_directory: pathlib.Path) -> None:
    """Populate a deterministic multi-album music tree with valid and corrupt fixtures."""
    album_one_directory = music_directory / "Night Drift" / "SOLAR ECHO"
    album_one_directory.mkdir(parents=True)
    (album_one_directory / "cover.jpg").write_bytes(SYNTHETIC_JPEG_BYTES)
    (album_one_directory / "02 - MOVE.flac").write_bytes(
        build_synthetic_flac_bytes(
            vorbis_tags={
                "TITLE": "MOVE",
                "ARTIST": "Night Drift",
                "ALBUMARTIST": "Night Drift",
                "ALBUM": "SOLAR ECHO",
                "GENRE": "Electronic, House",
                "DATE": "2024",
                "TRACKNUMBER": "2",
                "DISCNUMBER": "1",
            },
            audio_payload=bytes(range(AUDIO_PAYLOAD_BYTE_PATTERN_LENGTH)),
        )
    )
    (album_one_directory / "01 - PRELUDE.flac").write_bytes(
        build_synthetic_flac_bytes(
            vorbis_tags={
                "TITLE": "PRELUDE",
                "ARTIST": "Night Drift",
                "ALBUMARTIST": "Night Drift",
                "ALBUM": "SOLAR ECHO",
                "GENRE": "Electronic",
                "DATE": "2024",
                "TRACKNUMBER": "1",
                "DISCNUMBER": "1",
            }
        )
    )
    (album_one_directory / "01 - PRELUDE.lrc").write_text(
        "[00:04.00] Lights up\n[00:10.50] Turn the dial\n",
        encoding="utf-8",
    )
    (album_one_directory / "disc2_01 - BONUS.flac").write_bytes(
        build_synthetic_flac_bytes(
            vorbis_tags={
                "TITLE": "BONUS",
                "ARTIST": "Night Drift",
                "ALBUMARTIST": "Night Drift",
                "ALBUM": "SOLAR ECHO",
                "GENRE": "Electronic",
                "DATE": "2024",
                "TRACKNUMBER": "1",
                "DISCNUMBER": "2",
            }
        )
    )

    album_two_directory = music_directory / "Vector Unit" / "Signal Tape"
    album_two_directory.mkdir(parents=True)
    (album_two_directory / "01 - Opening Signal.mp3").write_bytes(
        build_synthetic_mp3_bytes(
            title="Opening Signal",
            artist="Vector Unit",
            album="Signal Tape",
            genre="Synthwave",
            year="2025",
            track_number="1/1",
            unsynced_lyrics="First pulse\nSecond pulse",
            embedded_picture_bytes=SYNTHETIC_PNG_BYTES,
        )
    )
    (album_two_directory / "98 - Corrupt.mp3").write_bytes(b"NOT_VALID_MP3_BYTES")
    (album_two_directory / "99 - Corrupt.flac").write_bytes(b"NOT_VALID_FLAC_BYTES")


def create_sample_web_tree(sandbox_directory: pathlib.Path) -> pathlib.Path:
    """Create static web files inside web_directory and a sibling secret outside it."""
    web_directory = sandbox_directory / "web"
    shell_directory = web_directory / "shell"
    shell_directory.mkdir(parents=True)

    (web_directory / "index.html").write_text(
        "<!DOCTYPE html><html><body>DialOS Simulator</body></html>",
        encoding="utf-8",
    )
    (web_directory / "main.js").write_text(
        "export const simulatorReady = true;\n",
        encoding="utf-8",
    )
    (shell_directory / "shell.css").write_text(
        ":root { color-scheme: dark light; }\n",
        encoding="utf-8",
    )
    (sandbox_directory / "server.py").write_text(
        "# outside web root; traversal must never expose this file\n",
        encoding="utf-8",
    )
    return web_directory


class SimulatorMetadataParserTests(unittest.TestCase):
    """Tests for FLAC, MP3, and LRC metadata parsing."""

    def test_parse_flac_nominal_metadata_and_embedded_picture(self) -> None:
        with tempfile.TemporaryDirectory() as sandbox_directory:
            track_path = pathlib.Path(sandbox_directory) / "05 - ORBIT.flac"
            track_path.write_bytes(
                build_synthetic_flac_bytes(
                    sample_rate_hz=HIGH_RESOLUTION_SAMPLE_RATE_HZ,
                    bits_per_sample=HIGH_RESOLUTION_BITS_PER_SAMPLE,
                    total_samples=(
                        HIGH_RESOLUTION_SAMPLE_RATE_HZ * HIGH_RESOLUTION_FLAC_DURATION_SECONDS
                    ),
                    vorbis_tags={
                        "TITLE": "ORBIT",
                        "ARTIST": "Night Drift",
                        "ALBUMARTIST": "Night Drift",
                        "ALBUM": "SOLAR ECHO",
                        "GENRE": "Électronique, Dance",
                        "DATE": "2024",
                        "TRACKNUMBER": "5",
                        "DISCNUMBER": "1",
                    },
                    embedded_picture_bytes=SYNTHETIC_JPEG_BYTES,
                )
            )
            track_metadata = server.parse_audio_file_metadata(track_path)
            self.assertIsNotNone(track_metadata)
            assert track_metadata is not None
            self.assertEqual(track_metadata.title, "ORBIT")
            self.assertEqual(track_metadata.artist, "Night Drift")
            self.assertEqual(track_metadata.album_artist, "Night Drift")
            self.assertEqual(track_metadata.album_title, "SOLAR ECHO")
            self.assertEqual(track_metadata.year, "2024")
            self.assertEqual(track_metadata.track_number, 5)
            self.assertEqual(track_metadata.disc_number, 1)
            self.assertEqual(track_metadata.sample_rate_hz, HIGH_RESOLUTION_SAMPLE_RATE_HZ)
            self.assertEqual(track_metadata.bits_per_sample, HIGH_RESOLUTION_BITS_PER_SAMPLE)
            self.assertAlmostEqual(
                track_metadata.duration_seconds,
                float(HIGH_RESOLUTION_FLAC_DURATION_SECONDS),
                places=1,
            )
            self.assertEqual(track_metadata.format_label, "FLAC 24-BIT")
            self.assertEqual(track_metadata.genres, ["Électronique", "Dance"])
            self.assertIsNotNone(track_metadata.embedded_cover)
            assert track_metadata.embedded_cover is not None
            self.assertEqual(track_metadata.embedded_cover.mime_type, "image/jpeg")

    def test_parse_flac_sidecar_lrc_lyrics(self) -> None:
        with tempfile.TemporaryDirectory() as sandbox_directory:
            album_directory = pathlib.Path(sandbox_directory) / "Artist" / "Album"
            album_directory.mkdir(parents=True)
            track_path = album_directory / "01 - Dawn.flac"
            track_path.write_bytes(build_synthetic_flac_bytes(vorbis_tags={"TITLE": "Dawn"}))
            (album_directory / "01 - Dawn.lrc").write_text(
                "[00:08.50] Morning signal\n[00:02.00] First light\n",
                encoding="utf-8",
            )

            track_metadata = server.parse_audio_file_metadata(track_path)
            self.assertIsNotNone(track_metadata)
            assert track_metadata is not None
            self.assertEqual(
                track_metadata.lyrics_entry,
                {
                    "kind": "timed",
                    "lines": [
                        {"timeSeconds": 2.0, "text": "First light"},
                        {"timeSeconds": 8.5, "text": "Morning signal"},
                    ],
                },
            )

    def test_parse_mp3_nominal_id3_and_duration(self) -> None:
        with tempfile.TemporaryDirectory() as sandbox_directory:
            track_path = pathlib.Path(sandbox_directory) / "02 - Synth Pulse.mp3"
            track_path.write_bytes(
                build_synthetic_mp3_bytes(
                    title="Synth Pulse",
                    artist="Vector Unit",
                    album="Signal Tape",
                    genre="Synthwave",
                    year="2025",
                    track_number="2/8",
                    unsynced_lyrics="First line\nSecond line",
                    embedded_picture_bytes=SYNTHETIC_JPEG_BYTES,
                    mpeg_frame_count=LONG_MP3_FRAME_COUNT,
                )
            )
            track_metadata = server.parse_audio_file_metadata(track_path)
            self.assertIsNotNone(track_metadata)
            assert track_metadata is not None
            self.assertEqual(track_metadata.title, "Synth Pulse")
            self.assertEqual(track_metadata.artist, "Vector Unit")
            self.assertEqual(track_metadata.album_title, "Signal Tape")
            self.assertEqual(track_metadata.track_number, 2)
            self.assertEqual(track_metadata.sample_rate_hz, STANDARD_SAMPLE_RATE_HZ)
            self.assertEqual(track_metadata.format_label, "MP3 320K")
            self.assertGreater(track_metadata.duration_seconds, 2.0)
            self.assertEqual(
                track_metadata.lyrics_entry,
                {"kind": "untimed", "lines": ["First line", "Second line"]},
            )
            self.assertIsNotNone(track_metadata.embedded_cover)

    def test_parse_lrc_timed_lyrics_sorts_and_expands_multiple_timestamps(self) -> None:
        lrc_content = (
            "[ar:Test Artist]\n"
            "[00:12.50][01:02.00] Chorus refrains\n"
            "[00:05.25] Intro shimmer\n"
            "[invalid] Ignored header\n"
        )
        lyrics_entry = server.parse_lyrics_text(lrc_content)
        self.assertIsNotNone(lyrics_entry)
        assert lyrics_entry is not None
        self.assertEqual(lyrics_entry["kind"], "timed")
        self.assertEqual(
            lyrics_entry["lines"],
            [
                {"timeSeconds": 5.25, "text": "Intro shimmer"},
                {"timeSeconds": 12.5, "text": "Chorus refrains"},
                {"timeSeconds": 62.0, "text": "Chorus refrains"},
            ],
        )

    def test_parse_untimed_and_empty_lyrics(self) -> None:
        untimed_lyrics = server.parse_lyrics_text("Line one\n\nLine two\n")
        self.assertEqual(untimed_lyrics, {"kind": "untimed", "lines": ["Line one", "Line two"]})
        self.assertIsNone(server.parse_lyrics_text("   \n\n  "))
        self.assertIsNone(server.parse_lyrics_text("[ar:Metadata Only]\n[ti:Title]\n"))

    def test_corrupted_and_missing_audio_files_return_none(self) -> None:
        with tempfile.TemporaryDirectory() as sandbox_directory:
            album_directory = pathlib.Path(sandbox_directory) / "Artist" / "Album"
            album_directory.mkdir(parents=True)

            corrupt_flac = album_directory / "01 - Broken.flac"
            corrupt_flac.write_bytes(b"NOT_A_VALID_FLAC_FILE")
            self.assertIsNone(server.parse_audio_file_metadata(corrupt_flac))

            truncated_flac = album_directory / "02 - Truncated.flac"
            truncated_flac.write_bytes(FLAC_SIGNATURE + b"\x80\x00\x00\x22")
            self.assertIsNone(server.parse_audio_file_metadata(truncated_flac))

            corrupt_mp3 = album_directory / "03 - Broken.mp3"
            corrupt_mp3.write_bytes(b"NOT_A_VALID_MP3_STREAM")
            self.assertIsNone(server.parse_audio_file_metadata(corrupt_mp3))

            empty_flac = album_directory / "04 - Empty.flac"
            empty_flac.write_bytes(b"")
            self.assertIsNone(server.parse_audio_file_metadata(empty_flac))

            missing_file = album_directory / "05 - Missing.flac"
            self.assertIsNone(server.parse_audio_file_metadata(missing_file))


class SimulatorHttpTestMixin:
    """Shared HTTP client and boot polling helpers for simulator server tests."""

    base_url: str

    def start_http_server(
        self,
        *,
        web_directory: pathlib.Path,
        library_store: server.SimulatorLibraryStore,
    ) -> None:
        """Start an ephemeral Simulator HTTP server and register cleanup."""
        http_server = server.create_simulator_http_server(
            host="127.0.0.1",
            port=0,
            web_directory=web_directory,
            library_store=library_store,
        )
        server_port = http_server.server_address[1]
        self.base_url = f"http://127.0.0.1:{server_port}"
        self.server_port = server_port
        server_thread = threading.Thread(target=http_server.serve_forever, daemon=True)
        server_thread.start()
        self.addCleanup(http_server.server_close)
        self.addCleanup(http_server.shutdown)

    def fetch_json(
        self,
        route_path: str,
        method: str = "GET",
        payload: dict | None = None,
    ) -> tuple[int, dict]:
        """Send an HTTP JSON request and return (status_code, decoded_body)."""
        body_bytes = json.dumps(payload).encode("utf-8") if payload is not None else None
        headers = {"Content-Type": "application/json"} if body_bytes is not None else {}
        request = urllib.request.Request(
            self.base_url + route_path,
            body_bytes,
            headers,
            method=method,
        )
        with urllib.request.urlopen(request) as response:
            return response.status, json.loads(response.read().decode("utf-8"))

    def assert_boot_status_shape(self, boot_status: dict) -> None:
        """Verify all required fields and types on a boot status snapshot."""
        for required_key in BOOT_STATUS_REQUIRED_KEYS:
            self.assertIn(required_key, boot_status)
        self.assertIsInstance(boot_status["scanId"], int)
        self.assertNotIsInstance(boot_status["scanId"], bool)
        self.assertIsInstance(boot_status["phase"], str)
        self.assertIsInstance(boot_status["ready"], bool)
        self.assertIsInstance(boot_status["discoveredFiles"], int)
        self.assertIsInstance(boot_status["scannedFiles"], int)
        self.assertIsInstance(boot_status["progress"], (int, float))
        self.assertGreaterEqual(boot_status["progress"], 0.0)
        self.assertLessEqual(boot_status["progress"], 1.0)
        self.assertIsInstance(boot_status["musicRoot"], str)
        self.assertIsInstance(boot_status["musicRootExists"], bool)
        self.assertIsInstance(boot_status["counts"], dict)
        self.assertIsInstance(boot_status["scanDurationSeconds"], (int, float))
        self.assertGreaterEqual(boot_status["scanDurationSeconds"], 0.0)
        if boot_status["error"] is not None:
            self.assertIsInstance(boot_status["error"], str)

    def poll_boot_until_ready(self) -> dict:
        """Poll GET /api/boot/status until ready becomes True or timeout expires."""
        deadline = time.monotonic() + ASYNC_BOOT_TIMEOUT_SECONDS
        latest_status: dict = {}
        while time.monotonic() < deadline:
            status_code, latest_status = self.fetch_json("/api/boot/status")
            self.assertEqual(status_code, HTTP_OK)
            self.assert_boot_status_shape(latest_status)
            if latest_status["ready"] is True:
                return latest_status
            time.sleep(ASYNC_BOOT_POLL_INTERVAL_SECONDS)
        self.fail(f"Timed out waiting for async boot to finish; last status: {latest_status}")


class SimulatorBootAndScanTests(SimulatorHttpTestMixin, unittest.TestCase):
    """Tests for async boot scanning, empty directories, and missing directories."""

    def setUp(self) -> None:
        self.sandbox_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.sandbox_directory.cleanup)
        self.root = pathlib.Path(self.sandbox_directory.name)
        self.web_directory = create_sample_web_tree(self.root)

    def test_async_boot_api_polls_to_ready_with_exact_counts(self) -> None:
        music_directory = self.root / "Music"
        create_sample_music_tree(music_directory)
        library_store = server.SimulatorLibraryStore(
            music_directory=music_directory,
            state_file_path=self.root / "state.json",
        )
        self.start_http_server(web_directory=self.web_directory, library_store=library_store)

        boot_status_code, initial_snapshot = self.fetch_json(
            "/api/boot",
            method="POST",
            payload={"rescan": True},
        )
        self.assertIn(boot_status_code, (HTTP_OK, HTTP_ACCEPTED))
        self.assert_boot_status_shape(initial_snapshot)

        ready_snapshot = self.poll_boot_until_ready()
        self.assertEqual(ready_snapshot["phase"], "ready")
        self.assertIs(ready_snapshot["ready"], True)
        self.assertEqual(ready_snapshot["progress"], 1.0)
        self.assertIs(ready_snapshot["musicRootExists"], True)
        self.assertEqual(ready_snapshot["musicRoot"], str(music_directory.resolve()))
        self.assertIsNone(ready_snapshot["error"])
        self.assertGreaterEqual(ready_snapshot["discoveredFiles"], 4)
        self.assertEqual(ready_snapshot["scannedFiles"], ready_snapshot["discoveredFiles"])
        self.assertEqual(
            ready_snapshot["counts"],
            {
                "tracks": 4,
                "albums": 2,
                "artists": 2,
                "genres": 3,
                "favorites": 0,
                "favoriteTracks": 0,
                "favoriteAlbums": 0,
                "playlists": 0,
            },
        )

        health_status, health_payload = self.fetch_json("/api/health")
        self.assertEqual(health_status, HTTP_OK)
        self.assertIs(health_payload["ok"], True)
        self.assertEqual(health_payload["musicRoot"], str(music_directory.resolve()))
        self.assertIs(health_payload["musicRootExists"], True)
        self.assertEqual(health_payload["counts"], ready_snapshot["counts"])

        library_status, catalog = self.fetch_json("/api/library")
        self.assertEqual(library_status, HTTP_OK)
        self.assertEqual(catalog["counts"], ready_snapshot["counts"])

    def test_empty_music_directory_boots_ready_with_zero_tracks(self) -> None:
        empty_music_directory = self.root / "EmptyMusic"
        empty_music_directory.mkdir()
        empty_store = server.SimulatorLibraryStore(
            music_directory=empty_music_directory,
            state_file_path=self.root / "empty_state.json",
        )
        self.start_http_server(web_directory=self.web_directory, library_store=empty_store)

        boot_status_code, initial_snapshot = self.fetch_json(
            "/api/boot",
            method="POST",
            payload={"rescan": True},
        )
        self.assertIn(boot_status_code, (HTTP_OK, HTTP_ACCEPTED))
        self.assert_boot_status_shape(initial_snapshot)

        ready_snapshot = self.poll_boot_until_ready()
        self.assertEqual(ready_snapshot["phase"], "ready")
        self.assertIs(ready_snapshot["ready"], True)
        self.assertIs(ready_snapshot["musicRootExists"], True)
        self.assertEqual(ready_snapshot["progress"], 1.0)
        self.assertEqual(ready_snapshot["counts"]["tracks"], 0)
        self.assertIsNone(ready_snapshot["error"])

        catalog = empty_store.serialize_catalog()
        self.assertEqual(catalog["counts"]["tracks"], 0)
        self.assertEqual(catalog["albums"], [])
        self.assertEqual(catalog["artists"], [])

    def test_missing_music_directory_boots_ready_without_crash(self) -> None:
        missing_music_directory = self.root / "MissingMusicDirectory"
        missing_store = server.SimulatorLibraryStore(
            music_directory=missing_music_directory,
            state_file_path=self.root / "missing_state.json",
        )
        self.start_http_server(web_directory=self.web_directory, library_store=missing_store)

        initial_snapshot = missing_store.start_async_scan(force_rescan=True)
        self.assert_boot_status_shape(initial_snapshot)

        ready_snapshot = self.poll_boot_until_ready()
        self.assertEqual(ready_snapshot["phase"], "ready")
        self.assertIs(ready_snapshot["ready"], True)
        self.assertIs(ready_snapshot["musicRootExists"], False)
        self.assertEqual(ready_snapshot["counts"]["tracks"], 0)
        self.assertEqual(ready_snapshot["progress"], 1.0)

        health_status, health_payload = self.fetch_json("/api/health")
        self.assertEqual(health_status, HTTP_OK)
        self.assertIs(health_payload["ok"], True)
        self.assertIs(health_payload["musicRootExists"], False)
        self.assertEqual(health_payload["counts"]["tracks"], 0)

    def test_album_without_sidecar_or_embedded_cover_has_no_cover_url(self) -> None:
        bare_music_directory = self.root / "BareMusic"
        bare_album_directory = bare_music_directory / "Plain Artist" / "Bare Album"
        bare_album_directory.mkdir(parents=True)
        (bare_album_directory / "01 - Bare Track.flac").write_bytes(
            build_synthetic_flac_bytes(
                vorbis_tags={
                    "TITLE": "Bare Track",
                    "ARTIST": "Plain Artist",
                    "ALBUM": "Bare Album",
                }
            )
        )
        bare_store = server.SimulatorLibraryStore(
            music_directory=bare_music_directory,
            state_file_path=self.root / "bare_state.json",
        )
        catalog = bare_store.scan()
        self.assertEqual(len(catalog["albums"]), 1)
        self.assertIsNone(catalog["albums"][0]["coverUrl"])


class SimulatorHttpApiAndStaticTests(SimulatorHttpTestMixin, unittest.TestCase):
    """End-to-end tests for static serving, library catalog, Range streaming, and persistence."""

    def setUp(self) -> None:
        self.sandbox_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.sandbox_directory.cleanup)
        self.root = pathlib.Path(self.sandbox_directory.name)
        self.music_directory = self.root / "Music"
        self.state_file_path = self.root / "state.json"
        self.web_directory = create_sample_web_tree(self.root)
        create_sample_music_tree(self.music_directory)

        self.library_store = server.SimulatorLibraryStore(
            music_directory=self.music_directory,
            state_file_path=self.state_file_path,
        )
        self.library_store.scan()
        self.start_http_server(web_directory=self.web_directory, library_store=self.library_store)

    def fetch_raw_http(self, raw_request_path: str, method: str = "GET") -> tuple[int, dict[str, str], bytes]:
        """Issue an HTTP request without client-side path normalization."""
        connection = http.client.HTTPConnection("127.0.0.1", self.server_port, timeout=5)
        try:
            connection.request(method, raw_request_path)
            response = connection.getresponse()
            response_headers = {key.lower(): value for key, value in response.getheaders()}
            response_body = response.read()
            return response.status, response_headers, response_body
        finally:
            connection.close()

    def test_static_files_served_with_expected_mime_types(self) -> None:
        expected_routes = [
            ("/", "text/html", self.web_directory / "index.html"),
            ("/index.html", "text/html", self.web_directory / "index.html"),
            ("/main.js", "application/javascript", self.web_directory / "main.js"),
            ("/main.js?cache=1", "application/javascript", self.web_directory / "main.js"),
            ("/shell/shell.css", "text/css", self.web_directory / "shell" / "shell.css"),
        ]
        for route_path, expected_mime_type, source_file_path in expected_routes:
            with urllib.request.urlopen(self.base_url + route_path) as response:
                self.assertEqual(response.status, HTTP_OK)
                content_type_header = response.headers.get("Content-Type", "")
                media_type = content_type_header.split(";", 1)[0].strip().lower()
                self.assertEqual(media_type, expected_mime_type)
                self.assertEqual(response.read(), source_file_path.read_bytes())

    def test_path_traversal_and_unknown_ids_return_404(self) -> None:
        forbidden_or_missing_paths = [
            "/../server.py",
            "/%2e%2e/server.py",
            "/shell/../../server.py",
            "/nonexistent_asset.js",
            "/api/audio/nonexistent",
            "/api/cover/nonexistent",
            "/api/audio/..%2F..%2Fetc%2Fpasswd",
            "/api/cover/..%2F..%2Fetc%2Fpasswd",
        ]
        for target_path in forbidden_or_missing_paths:
            status_code, _headers, _body = self.fetch_raw_http(target_path)
            self.assertEqual(status_code, HTTP_NOT_FOUND, msg=f"Expected 404 for {target_path}")

        missing_favorite_request = urllib.request.Request(
            f"{self.base_url}/api/favorites/nonexistent",
            json.dumps({"isFavorite": True}).encode("utf-8"),
            {"Content-Type": "application/json"},
            method="POST",
        )
        with self.assertRaises(urllib.error.HTTPError) as http_error_context:
            urllib.request.urlopen(missing_favorite_request)
        self.assertEqual(http_error_context.exception.code, HTTP_NOT_FOUND)

    def test_scan_orders_multidisc_tracks_and_builds_facets(self) -> None:
        status_code, catalog = self.fetch_json("/api/library")
        self.assertEqual(status_code, HTTP_OK)
        self.assertEqual(catalog["counts"]["tracks"], 4)
        self.assertEqual(catalog["counts"]["albums"], 2)
        self.assertEqual(catalog["counts"]["artists"], 2)
        self.assertEqual(catalog["counts"]["genres"], 3)

        solar_echo_album = next(
            album_entry for album_entry in catalog["albums"] if album_entry["title"] == "SOLAR ECHO"
        )
        self.assertEqual(
            [track_entry["title"] for track_entry in solar_echo_album["tracks"]],
            ["PRELUDE", "MOVE", "BONUS"],
        )
        self.assertTrue(solar_echo_album["coverUrl"].startswith("/api/cover/"))

        genres_by_title = {
            track_entry["title"]: track_entry["genres"] for track_entry in catalog["allTracks"]
        }
        self.assertEqual(genres_by_title["MOVE"], ["Electronic", "House"])
        self.assertEqual(genres_by_title["PRELUDE"], ["Electronic"])
        self.assertEqual(genres_by_title["BONUS"], ["Electronic"])
        self.assertEqual(genres_by_title["Opening Signal"], ["Synthwave"])

        prelude_id = solar_echo_album["tracks"][0]["id"]
        self.assertEqual(catalog["lyricsCatalog"][prelude_id]["kind"], "timed")
        self.assertEqual(len(catalog["lyricsCatalog"][prelude_id]["lines"]), 2)

        signal_album = next(
            album_entry for album_entry in catalog["albums"] if album_entry["title"] == "Signal Tape"
        )
        opening_signal_id = signal_album["tracks"][0]["id"]
        self.assertEqual(
            catalog["lyricsCatalog"][opening_signal_id],
            {"kind": "untimed", "lines": ["First pulse", "Second pulse"]},
        )

    def test_cover_endpoint_serves_sidecar_and_embedded_art(self) -> None:
        _status_code, catalog = self.fetch_json("/api/library")
        solar_echo_album = next(
            album_entry for album_entry in catalog["albums"] if album_entry["title"] == "SOLAR ECHO"
        )
        signal_album = next(
            album_entry for album_entry in catalog["albums"] if album_entry["title"] == "Signal Tape"
        )

        with urllib.request.urlopen(self.base_url + solar_echo_album["coverUrl"]) as sidecar_response:
            self.assertEqual(sidecar_response.status, HTTP_OK)
            self.assertEqual(sidecar_response.headers.get("Content-Type"), "image/jpeg")
            self.assertEqual(sidecar_response.read(), SYNTHETIC_JPEG_BYTES)

        with urllib.request.urlopen(self.base_url + signal_album["coverUrl"]) as embedded_response:
            self.assertEqual(embedded_response.status, HTTP_OK)
            self.assertEqual(embedded_response.read(), SYNTHETIC_PNG_BYTES)

    def test_audio_streaming_supports_full_and_range_requests_and_rejects_invalid_ranges(self) -> None:
        _status_code, catalog = self.fetch_json("/api/library")
        track_id = catalog["allTracks"][0]["id"]
        audio_url = f"{self.base_url}/api/audio/{track_id}"

        with urllib.request.urlopen(audio_url) as full_response:
            self.assertEqual(full_response.status, HTTP_OK)
            self.assertEqual(full_response.headers.get("Accept-Ranges"), "bytes")
            full_bytes = full_response.read()
            self.assertGreater(len(full_bytes), 32)

        single_byte_request = urllib.request.Request(audio_url, headers={"Range": "bytes=0-0"})
        with urllib.request.urlopen(single_byte_request) as single_byte_response:
            self.assertEqual(single_byte_response.status, HTTP_PARTIAL_CONTENT)
            self.assertEqual(
                single_byte_response.headers.get("Content-Range"),
                f"bytes 0-0/{len(full_bytes)}",
            )
            self.assertEqual(single_byte_response.read(), full_bytes[:1])

        bounded_range_request = urllib.request.Request(
            audio_url,
            headers={"Range": f"bytes=0-{RANGE_BOUNDED_END_BYTE}"},
        )
        with urllib.request.urlopen(bounded_range_request) as partial_response:
            self.assertEqual(partial_response.status, HTTP_PARTIAL_CONTENT)
            self.assertEqual(
                partial_response.headers.get("Content-Range"),
                f"bytes 0-{RANGE_BOUNDED_END_BYTE}/{len(full_bytes)}",
            )
            self.assertEqual(partial_response.read(), full_bytes[: RANGE_BOUNDED_END_BYTE + 1])

        open_start = len(full_bytes) - RANGE_OPEN_TAIL_BYTES
        open_range_request = urllib.request.Request(
            audio_url,
            headers={"Range": f"bytes={open_start}-"},
        )
        with urllib.request.urlopen(open_range_request) as open_response:
            self.assertEqual(open_response.status, HTTP_PARTIAL_CONTENT)
            self.assertEqual(
                open_response.headers.get("Content-Range"),
                f"bytes {open_start}-{len(full_bytes) - 1}/{len(full_bytes)}",
            )
            self.assertEqual(open_response.read(), full_bytes[-RANGE_OPEN_TAIL_BYTES:])

        suffix_request = urllib.request.Request(
            audio_url,
            headers={"Range": f"bytes=-{RANGE_SUFFIX_TAIL_BYTES}"},
        )
        with urllib.request.urlopen(suffix_request) as suffix_response:
            self.assertEqual(suffix_response.status, HTTP_PARTIAL_CONTENT)
            self.assertEqual(suffix_response.read(), full_bytes[-RANGE_SUFFIX_TAIL_BYTES:])

        invalid_range_headers = [
            f"bytes={len(full_bytes) + RANGE_OUT_OF_BOUNDS_OFFSET}-",
            "bytes=20-10",
            "bytes=-0",
        ]
        for invalid_header in invalid_range_headers:
            invalid_range_request = urllib.request.Request(
                audio_url,
                headers={"Range": invalid_header},
            )
            with self.assertRaises(urllib.error.HTTPError) as http_error_context:
                urllib.request.urlopen(invalid_range_request)
            self.assertEqual(http_error_context.exception.code, HTTP_RANGE_NOT_SATISFIABLE)

    def test_favorites_and_playlists_persist_without_modifying_music_directory(self) -> None:
        music_snapshot_before = sorted(
            (
                str(file_path.relative_to(self.music_directory)),
                file_path.stat().st_size,
                file_path.stat().st_mtime_ns,
            )
            for file_path in self.music_directory.rglob("*")
            if file_path.is_file()
        )
        _status_code, catalog = self.fetch_json("/api/library")
        target_track_id = catalog["allTracks"][0]["id"]

        favorite_status, favorite_response = self.fetch_json(
            f"/api/favorites/{target_track_id}",
            method="POST",
            payload={"isFavorite": True},
        )
        self.assertEqual(favorite_status, HTTP_OK)
        self.assertIs(favorite_response["isFavorite"], True)

        expected_playlists = [
            {"id": "custom-mix", "name": "Custom Mix", "trackIds": [target_track_id]}
        ]
        playlist_status, playlist_response = self.fetch_json(
            "/api/playlists",
            method="POST",
            payload={"playlists": expected_playlists},
        )
        self.assertEqual(playlist_status, HTTP_OK)
        self.assertEqual(playlist_response["playlists"], expected_playlists)

        reloaded_store = server.SimulatorLibraryStore(
            music_directory=self.music_directory,
            state_file_path=self.state_file_path,
        )
        reloaded_store.scan()
        reloaded_catalog = reloaded_store.serialize_catalog()
        reloaded_track = next(
            track_entry
            for track_entry in reloaded_catalog["allTracks"]
            if track_entry["id"] == target_track_id
        )
        self.assertIs(reloaded_track["isFavorite"], True)
        self.assertEqual(reloaded_catalog["playlists"], expected_playlists)
        self.assertEqual(reloaded_catalog["counts"]["favorites"], 1)
        self.assertEqual(reloaded_catalog["counts"]["playlists"], 1)

        music_snapshot_after = sorted(
            (
                str(file_path.relative_to(self.music_directory)),
                file_path.stat().st_size,
                file_path.stat().st_mtime_ns,
            )
            for file_path in self.music_directory.rglob("*")
            if file_path.is_file()
        )
        self.assertEqual(music_snapshot_before, music_snapshot_after)

    def test_rescan_endpoint_returns_refreshed_catalog(self) -> None:
        rescan_status, refreshed_catalog = self.fetch_json("/api/rescan", method="POST", payload={})
        self.assertEqual(rescan_status, HTTP_OK)
        self.assertEqual(refreshed_catalog["counts"]["tracks"], 4)
        self.assertEqual(refreshed_catalog["counts"]["albums"], 2)

    def test_malformed_playlists_payloads_return_400(self) -> None:
        invalid_bodies = [
            b"{not valid json",
            b"[]",
            json.dumps({"playlists": "not-a-list"}).encode("utf-8"),
            json.dumps({}).encode("utf-8"),
        ]
        for body_bytes in invalid_bodies:
            bad_request = urllib.request.Request(
                f"{self.base_url}/api/playlists",
                body_bytes,
                {"Content-Type": "application/json"},
                method="POST",
            )
            with self.assertRaises(urllib.error.HTTPError) as http_error_context:
                urllib.request.urlopen(bad_request)
            self.assertEqual(http_error_context.exception.code, HTTP_BAD_REQUEST)

    def test_album_added_at_and_independent_album_favorites_persist(self) -> None:
        import os

        _status_code, catalog = self.fetch_json("/api/library")
        solar_album = next(
            album_entry for album_entry in catalog["albums"] if album_entry["title"] == "SOLAR ECHO"
        )
        signal_album = next(
            album_entry for album_entry in catalog["albums"] if album_entry["title"] == "Signal Tape"
        )
        self.assertIs(solar_album["isFavorite"], False)
        self.assertIs(signal_album["isFavorite"], False)
        self.assertIsInstance(solar_album["addedAt"], float)
        self.assertGreater(solar_album["addedAt"], 0)

        # Update one track mtime in Signal Tape to verify addedAt tracks newest audio file mtime.
        signal_track_path = self.music_directory / "Vector Unit" / "Signal Tape" / "01 - Opening Signal.mp3"
        self.assertTrue(signal_track_path.is_file())
        newer_timestamp = 1_900_000_000.0
        os.utime(signal_track_path, (newer_timestamp, newer_timestamp))
        _rescan_status, rescanned_catalog = self.fetch_json("/api/rescan", method="POST", payload={})
        rescanned_signal = next(
            album_entry
            for album_entry in rescanned_catalog["albums"]
            if album_entry["id"] == signal_album["id"]
        )
        self.assertAlmostEqual(rescanned_signal["addedAt"], newer_timestamp, places=2)

        # Like album explicitly, then toggle track inside that album -> completely independent.
        album_status, album_response = self.fetch_json(
            f"/api/favorite-albums/{solar_album['id']}",
            method="POST",
            payload={"isFavorite": True},
        )
        self.assertEqual(album_status, HTTP_OK)
        self.assertEqual(album_response, {"albumId": solar_album["id"], "isFavorite": True})

        _lib_status, after_album_like = self.fetch_json("/api/library")
        updated_solar = next(
            album_entry
            for album_entry in after_album_like["albums"]
            if album_entry["id"] == solar_album["id"]
        )
        self.assertIs(updated_solar["isFavorite"], True)
        self.assertTrue(all(not track_entry["isFavorite"] for track_entry in updated_solar["tracks"]))
        self.assertEqual(after_album_like["counts"]["favoriteAlbums"], 1)
        self.assertEqual(after_album_like["counts"]["favoriteTracks"], 0)
        self.assertEqual(after_album_like["counts"]["favorites"], 1)

        # Like a song inside the already-liked album -> both are independently favorited.
        solar_track_id = updated_solar["tracks"][0]["id"]
        track_status, _track_response = self.fetch_json(
            f"/api/favorites/{solar_track_id}",
            method="POST",
            payload={"isFavorite": True},
        )
        self.assertEqual(track_status, HTTP_OK)
        _lib_status2, after_both_liked = self.fetch_json("/api/library")
        self.assertEqual(after_both_liked["counts"]["favoriteAlbums"], 1)
        self.assertEqual(after_both_liked["counts"]["favoriteTracks"], 1)
        self.assertEqual(after_both_liked["counts"]["favorites"], 2)

        # Toggle album favorite off without body -> album unliked, track stays liked.
        toggle_status, toggle_response = self.fetch_json(
            f"/api/favorite-albums/{solar_album['id']}",
            method="POST",
            payload={},
        )
        self.assertEqual(toggle_status, HTTP_OK)
        self.assertIs(toggle_response["isFavorite"], False)
        _lib_status3, after_album_unliked = self.fetch_json("/api/library")
        unliked_solar = next(
            album_entry
            for album_entry in after_album_unliked["albums"]
            if album_entry["id"] == solar_album["id"]
        )
        self.assertIs(unliked_solar["isFavorite"], False)
        self.assertIs(unliked_solar["tracks"][0]["isFavorite"], True)
        self.assertEqual(after_album_unliked["counts"]["favoriteAlbums"], 0)
        self.assertEqual(after_album_unliked["counts"]["favoriteTracks"], 1)
        self.assertEqual(after_album_unliked["counts"]["favorites"], 1)

        # Re-like album and verify persistence across store reload & scan.
        self.fetch_json(
            f"/api/favorite-albums/{solar_album['id']}",
            method="POST",
            payload={"isFavorite": True},
        )
        reloaded_store = server.SimulatorLibraryStore(
            music_directory=self.music_directory,
            state_file_path=self.state_file_path,
        )
        reloaded_store.scan()
        reloaded_catalog = reloaded_store.serialize_catalog()
        reloaded_solar = next(
            album_entry
            for album_entry in reloaded_catalog["albums"]
            if album_entry["id"] == solar_album["id"]
        )
        self.assertIs(reloaded_solar["isFavorite"], True)
        self.assertEqual(reloaded_catalog["counts"]["favoriteAlbums"], 1)
        self.assertEqual(reloaded_catalog["counts"]["favoriteTracks"], 1)
        self.assertEqual(reloaded_catalog["counts"]["favorites"], 2)

    def test_favorite_albums_rejects_invalid_ids_and_payloads(self) -> None:
        _status_code, catalog = self.fetch_json("/api/library")
        album_id = catalog["albums"][0]["id"]

        for bad_path in ("/api/favorite-albums/nonexistent-id", "/api/favorite-albums/bad..id"):
            bad_request = urllib.request.Request(
                f"{self.base_url}{bad_path}",
                b"{}",
                {"Content-Type": "application/json"},
                method="POST",
            )
            with self.assertRaises(urllib.error.HTTPError) as http_error_context:
                urllib.request.urlopen(bad_request)
            self.assertEqual(http_error_context.exception.code, HTTP_NOT_FOUND)

        for bad_body in (b"{invalid", json.dumps({"isFavorite": "yes"}).encode("utf-8")):
            invalid_request = urllib.request.Request(
                f"{self.base_url}/api/favorite-albums/{album_id}",
                bad_body,
                {"Content-Type": "application/json"},
                method="POST",
            )
            with self.assertRaises(urllib.error.HTTPError) as http_error_context:
                urllib.request.urlopen(invalid_request)
            self.assertEqual(http_error_context.exception.code, HTTP_BAD_REQUEST)

    def test_artist_added_at_uses_newest_album_mtime_and_favorite_artists_persist_outside_favorites(self) -> None:
        import os

        second_night_album = self.music_directory / "Night Drift" / "LUNAR DRIFT"
        second_night_album.mkdir(parents=True)
        newer_track_path = second_night_album / "01 - Lunar.flac"
        newer_track_path.write_bytes(
            build_synthetic_flac_bytes(
                vorbis_tags={
                    "TITLE": "Lunar",
                    "ARTIST": "Night Drift",
                    "ALBUMARTIST": "Night Drift",
                    "ALBUM": "LUNAR DRIFT",
                    "GENRE": "Electronic",
                    "DATE": "2025",
                    "TRACKNUMBER": "1",
                }
            )
        )
        newer_timestamp = 1_950_000_000.0
        os.utime(newer_track_path, (newer_timestamp, newer_timestamp))

        rescan_status, catalog = self.fetch_json("/api/rescan", method="POST", payload={})
        self.assertEqual(rescan_status, HTTP_OK)

        night_drift_artist = next(
            artist_entry for artist_entry in catalog["artists"] if artist_entry["name"] == "Night Drift"
        )
        self.assertIs(night_drift_artist["isFavorite"], False)
        self.assertAlmostEqual(night_drift_artist["addedAt"], newer_timestamp, places=2)

        music_snapshot_before = sorted(
            (
                str(file_path.relative_to(self.music_directory)),
                file_path.stat().st_size,
                file_path.stat().st_mtime_ns,
            )
            for file_path in self.music_directory.rglob("*")
            if file_path.is_file()
        )

        like_status, like_response = self.fetch_json(
            f"/api/favorite-artists/{night_drift_artist['id']}",
            method="POST",
            payload={"isFavorite": True},
        )
        self.assertEqual(like_status, HTTP_OK)
        self.assertEqual(
            like_response,
            {"artistId": night_drift_artist["id"], "isFavorite": True},
        )

        _lib_status, after_like = self.fetch_json("/api/library")
        updated_artist = next(
            artist_entry
            for artist_entry in after_like["artists"]
            if artist_entry["id"] == night_drift_artist["id"]
        )
        self.assertIs(updated_artist["isFavorite"], True)
        self.assertEqual(after_like["counts"]["favorites"], 0)
        self.assertEqual(after_like["counts"]["favoriteTracks"], 0)
        self.assertEqual(after_like["counts"]["favoriteAlbums"], 0)
        self.assertTrue(all(not album_entry["isFavorite"] for album_entry in after_like["albums"]))
        self.assertTrue(all(not track_entry["isFavorite"] for track_entry in after_like["allTracks"]))

        # Toggle off without body
        toggle_status, toggle_response = self.fetch_json(
            f"/api/favorite-artists/{night_drift_artist['id']}",
            method="POST",
            payload={},
        )
        self.assertEqual(toggle_status, HTTP_OK)
        self.assertIs(toggle_response["isFavorite"], False)

        # Re-like and verify persistence across store reload & scan
        self.fetch_json(
            f"/api/favorite-artists/{night_drift_artist['id']}",
            method="POST",
            payload={"isFavorite": True},
        )
        reloaded_store = server.SimulatorLibraryStore(
            music_directory=self.music_directory,
            state_file_path=self.state_file_path,
        )
        reloaded_store.scan()
        reloaded_catalog = reloaded_store.serialize_catalog()
        reloaded_artist = next(
            artist_entry
            for artist_entry in reloaded_catalog["artists"]
            if artist_entry["id"] == night_drift_artist["id"]
        )
        self.assertIs(reloaded_artist["isFavorite"], True)
        self.assertEqual(reloaded_catalog["counts"]["favorites"], 0)

        music_snapshot_after = sorted(
            (
                str(file_path.relative_to(self.music_directory)),
                file_path.stat().st_size,
                file_path.stat().st_mtime_ns,
            )
            for file_path in self.music_directory.rglob("*")
            if file_path.is_file()
        )
        self.assertEqual(music_snapshot_before, music_snapshot_after)

    def test_favorite_artists_rejects_invalid_ids_and_payloads(self) -> None:
        _status_code, catalog = self.fetch_json("/api/library")
        artist_id = catalog["artists"][0]["id"]

        for bad_path in ("/api/favorite-artists/nonexistent-id", "/api/favorite-artists/bad..id"):
            bad_request = urllib.request.Request(
                f"{self.base_url}{bad_path}",
                b"{}",
                {"Content-Type": "application/json"},
                method="POST",
            )
            with self.assertRaises(urllib.error.HTTPError) as http_error_context:
                urllib.request.urlopen(bad_request)
            self.assertEqual(http_error_context.exception.code, HTTP_NOT_FOUND)

        for bad_body in (b"{invalid", json.dumps({"isFavorite": "yes"}).encode("utf-8")):
            invalid_request = urllib.request.Request(
                f"{self.base_url}/api/favorite-artists/{artist_id}",
                bad_body,
                {"Content-Type": "application/json"},
                method="POST",
            )
            with self.assertRaises(urllib.error.HTTPError) as http_error_context:
                urllib.request.urlopen(invalid_request)
            self.assertEqual(http_error_context.exception.code, HTTP_BAD_REQUEST)

    def test_http11_keepalive_post_routes_do_not_corrupt_subsequent_audio_stream(self) -> None:
        _status_code, catalog = self.fetch_json("/api/library")
        album_id = catalog["albums"][0]["id"]
        track_id = catalog["allTracks"][0]["id"]

        connection = http.client.HTTPConnection("127.0.0.1", self.server_port, timeout=3)
        self.addCleanup(connection.close)

        keepalive_post_cases = [
            ("/api/rescan", json.dumps({"trigger": True}).encode("utf-8"), HTTP_OK),
            (f"/api/favorite-albums/{album_id}", json.dumps({"isFavorite": True}).encode("utf-8"), HTTP_OK),
            ("/api/favorite-albums/bad..id", json.dumps({"isFavorite": True}).encode("utf-8"), HTTP_NOT_FOUND),
            ("/api/nonexistent-endpoint", json.dumps({"isFavorite": True}).encode("utf-8"), HTTP_NOT_FOUND),
        ]

        for route_path, body_bytes, expected_status in keepalive_post_cases:
            connection.request(
                "POST",
                route_path,
                body=body_bytes,
                headers={"Content-Type": "application/json"},
            )
            post_response = connection.getresponse()
            post_response.read()
            self.assertEqual(post_response.status, expected_status)

            connection.request("GET", f"/api/audio/{track_id}")
            audio_response = connection.getresponse()
            audio_payload = audio_response.read()
            self.assertEqual(audio_response.status, HTTP_OK)
            self.assertGreater(len(audio_payload), 0)

        oversized_connection = http.client.HTTPConnection("127.0.0.1", self.server_port, timeout=3)
        self.addCleanup(oversized_connection.close)
        oversized_connection.putrequest("POST", f"/api/favorite-albums/{album_id}")
        oversized_connection.putheader("Content-Type", "application/json")
        oversized_connection.putheader("Content-Length", str(server.HTTP_MAX_REQUEST_BODY_BYTES + 1))
        oversized_connection.endheaders()
        oversized_response = oversized_connection.getresponse()
        oversized_response.read()
        self.assertEqual(oversized_response.status, HTTP_BAD_REQUEST)
        self.assertEqual(oversized_response.getheader("Connection"), "close")


if __name__ == "__main__":
    unittest.main()
