#!/usr/bin/env python3
"""Read-only local simulator HTTP server wiring a music folder into DialOS."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import http.server
import json
import mimetypes
import os
import pathlib
import re
import struct
import threading
import time
import urllib.parse
import webbrowser
from typing import Any

BITS_PER_BYTE = 8
KILOBITS_DIVISOR = 1000
BYTES_PER_MEGABYTE = 1024 * 1024
SECONDS_PER_MINUTE = 60
CENTISECONDS_DIVISOR = 100.0
MILLISECONDS_DIVISOR = 1000.0
MILLISECONDS_PER_SECOND_DIGITS = 2
SAMPLE_RATE_KHZ_DIVISOR = 1000.0
STABLE_ID_HEX_LENGTH = 12
STREAM_CHUNK_BYTES = 65536
MP3_SYNC_SCAN_LIMIT_BYTES = 65536
HTTP_MAX_REQUEST_BODY_BYTES = 2 * BYTES_PER_MEGABYTE

DEFAULT_HTTP_HOST = "127.0.0.1"
DEFAULT_HTTP_PORT = 8765
DEFAULT_SAMPLE_RATE_HZ = 44100
DEFAULT_BITS_PER_SAMPLE = 16
DEFAULT_MP3_BITRATE_KBPS = 320
DEFAULT_FALLBACK_DURATION_SECONDS = 180.0
UNORDERED_TRACK_SORT_FALLBACK = 9999
MIN_MB_PER_MINUTE = 1.0
MIN_DURATION_MINUTES_FOR_DENSITY = 0.25
MIN_FILE_SIZE_LABEL_MB = 0.1

HTTP_OK = 200
HTTP_ACCEPTED = 202
HTTP_PARTIAL_CONTENT = 206
HTTP_BAD_REQUEST = 400
HTTP_NOT_FOUND = 404
HTTP_RANGE_NOT_SATISFIABLE = 416

BOOT_INITIAL_DISCOVER_PROGRESS = 0.04
BOOT_SCAN_BASE_PROGRESS = 0.12
BOOT_SCAN_PROGRESS_SPAN = 0.72
BOOT_INDEXING_PROGRESS = 0.90
BOOT_READY_PROGRESS = 1.0
ACTIVE_SCAN_PHASES = ("discovering", "scanning", "indexing")

FLAC_SIGNATURE = b"fLaC"
FLAC_LAST_BLOCK_MASK = 0b1000_0000
FLAC_BLOCK_TYPE_MASK = 0b0111_1111
FLAC_STREAM_INFORMATION_BLOCK_TYPE = 0
FLAC_VORBIS_COMMENT_BLOCK_TYPE = 4
FLAC_PICTURE_BLOCK_TYPE = 6
FLAC_FRONT_COVER_PICTURE_TYPE = 3
FLAC_HEADER_SIZE_BYTES = 4
FLAC_STREAM_INFORMATION_MIN_BYTES = 18
FLAC_PICTURE_HEADER_MIN_BYTES = 32
FLAC_PICTURE_HEADER_PREVIEW_BYTES = 4096
FLAC_MAX_VORBIS_COMMENT_BYTES = 4 * BYTES_PER_MEGABYTE
FLAC_MAX_PICTURE_BLOCK_BYTES = 20 * BYTES_PER_MEGABYTE
FLAC_SAMPLE_RATE_HIGH_SHIFT_BITS = 12
FLAC_SAMPLE_RATE_MID_SHIFT_BITS = 4
FLAC_SAMPLE_RATE_LOW_SHIFT_BITS = 4
FLAC_BITS_PER_SAMPLE_HIGH_MASK = 0b0000_0001
FLAC_BITS_PER_SAMPLE_HIGH_SHIFT_BITS = 4
FLAC_BITS_PER_SAMPLE_LOW_SHIFT_BITS = 4
FLAC_TOTAL_SAMPLES_HIGH_MASK = 0b0000_1111
FLAC_TOTAL_SAMPLES_HIGH_SHIFT_BITS = 32

ID3_SIGNATURE = b"ID3"
ID3_HEADER_SIZE_BYTES = 10
ID3_MAX_TAG_BYTES = 16 * BYTES_PER_MEGABYTE
ID3_FLAG_EXTENDED_HEADER_MASK = 0b0100_0000
ID3_SYNCHSAFE_MASK = 0b0111_1111
ID3_SYNCHSAFE_SHIFT_HIGH = 21
ID3_SYNCHSAFE_SHIFT_MID_HIGH = 14
ID3_SYNCHSAFE_SHIFT_MID_LOW = 7
ID3_V2_MAJOR_VERSION = 2
ID3_V4_MAJOR_VERSION = 4
ID3_V2_FRAME_HEADER_BYTES = 6
ID3_V3_FRAME_HEADER_BYTES = 10
ID3_USLT_MIN_PAYLOAD_BYTES = 4
ID3_APIC_MIN_PAYLOAD_BYTES = 6
ID3_FRONT_COVER_PICTURE_TYPE = 3
ID3_ENCODING_ISO_8859_1 = 0
ID3_ENCODING_UTF16_BOM = 1
ID3_ENCODING_UTF16_BE = 2
ID3_ENCODING_UTF8 = 3

MPEG_FRAME_SYNC_FIRST_BYTE = 0b1111_1111
MPEG_SYNC_SECOND_BYTE_MASK = 0b1110_0000
MPEG_VERSION_MASK = 0b0001_1000
MPEG_VERSION_SHIFT_BITS = 3
MPEG_LAYER_MASK = 0b0000_0110
MPEG_LAYER_SHIFT_BITS = 1
MPEG_BITRATE_INDEX_MASK = 0b1111_0000
MPEG_BITRATE_SHIFT_BITS = 4
MPEG_SAMPLE_RATE_INDEX_MASK = 0b0000_1100
MPEG_SAMPLE_RATE_SHIFT_BITS = 2
MPEG_CHANNEL_MODE_MASK = 0b1100_0000
MPEG_CHANNEL_MODE_SHIFT_BITS = 6
MPEG_HEADER_BYTES = 4
MPEG_VERSION_MPEG1 = 3
MPEG_VERSION_RESERVED = 1
MPEG_LAYER_III = 1
MPEG_BITRATE_INVALID_INDEX = 15
MPEG_SAMPLE_RATE_INVALID_INDEX = 3
MPEG_SAMPLES_PER_FRAME_V1 = 1152
MPEG_SAMPLES_PER_FRAME_V2 = 576
MPEG_CHANNEL_MODE_MONO = 3
MPEG_SIDE_INFORMATION_MONO_V1_BYTES = 17
MPEG_SIDE_INFORMATION_STEREO_V1_BYTES = 32
MPEG_SIDE_INFORMATION_MONO_V2_BYTES = 9
MPEG_SIDE_INFORMATION_STEREO_V2_BYTES = 17
XING_FRAMES_FLAG_MASK = 0b0000_0001

MPEG1_LAYER3_BITRATES_KBPS = [
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
]
MPEG2_LAYER3_BITRATES_KBPS = [
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
]
MPEG_SAMPLE_RATES_BY_VERSION = {
    3: [44100, 48000, 32000, 0],
    2: [22050, 24000, 16000, 0],
    0: [11025, 12000, 8000, 0],
}

SUPPORTED_AUDIO_EXTENSIONS = {".flac", ".mp3"}
PREFERRED_COVER_FILENAMES = (
    "cover.jpg",
    "cover.jpeg",
    "cover.png",
    "cover.webp",
    "folder.jpg",
    "folder.jpeg",
    "folder.png",
    "front.jpg",
    "front.png",
    "album.jpg",
    "album.png",
)
IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".webp"}
COVER_MOTIF_CYCLE = ("prism", "grid", "monolith", "tape", "optical", "concrete")

TRACK_PREFIX_PATTERN = re.compile(r"^\s*(?:\d+\s*[-._]\s*)+")
LRC_TIMESTAMP_PATTERN = re.compile(r"\[(\d{1,2}):(\d{2})(?:[.:](\d{1,3}))?\]")
LRC_META_TAG_PATTERN = re.compile(r"^\[[a-zA-Z]{2,}:.*\]$")
YEAR_PATTERN = re.compile(r"\b(\d{4})\b")
GENRE_SPLIT_PATTERN = re.compile(r"\s*[,;/|]\s*")
ID3_NUMERIC_GENRE_PATTERN = re.compile(r"^\((\d+)\)(.*)$")
SAFE_IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z0-9_-]+$")

AUDIO_MIME_TYPES = {
    ".flac": "audio/flac",
    ".mp3": "audio/mpeg",
}

STATIC_MIME_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".mjs": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".svg": "image/svg+xml",
    ".json": "application/json; charset=utf-8",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
}

TEXT_FRAME_IDS = frozenset(
    {
        "TIT2",
        "TT2",
        "TPE1",
        "TP1",
        "TPE2",
        "TP2",
        "TALB",
        "TAL",
        "TCON",
        "TCO",
        "TDRC",
        "TYER",
        "TYE",
        "TRCK",
        "TRK",
        "TPOS",
        "TPA",
    }
)


@dataclasses.dataclass(frozen=True)
class EmbeddedCoverReference:
    """Byte range referencing embedded album artwork inside an audio file."""

    file_path: pathlib.Path
    byte_offset: int
    byte_length: int
    mime_type: str


@dataclasses.dataclass(frozen=True)
class SidecarCoverReference:
    """Path referencing a standalone cover image file inside an album folder."""

    file_path: pathlib.Path
    mime_type: str


@dataclasses.dataclass
class ParsedAudioMetadata:
    """Normalized metadata extracted from a single FLAC or MP3 audio file."""

    file_path: pathlib.Path
    file_size_bytes: int
    file_mtime_seconds: float
    title: str
    artist: str
    album_artist: str
    album_title: str
    genres: list[str]
    year: str
    track_number: int
    disc_number: int
    duration_seconds: float
    sample_rate_hz: int
    bits_per_sample: int
    bitrate_kbps: int
    format_label: str
    embedded_cover: EmbeddedCoverReference | None
    lyrics_entry: dict[str, Any] | None


def build_empty_counts() -> dict[str, int]:
    """Return a zeroed library counts dictionary matching the DialOS catalog schema."""
    return {
        "tracks": 0,
        "albums": 0,
        "artists": 0,
        "genres": 0,
        "favorites": 0,
        "favoriteTracks": 0,
        "favoriteAlbums": 0,
        "playlists": 0,
    }


def compute_stable_id(seed_text: str) -> str:
    """Generate a short deterministic URL-safe hexadecimal identifier."""
    return hashlib.sha1(seed_text.encode("utf-8")).hexdigest()[:STABLE_ID_HEX_LENGTH]


def format_clock_duration(duration_seconds: float) -> str:
    """Format seconds as MM:SS matching the DialOS display convention."""
    safe_seconds = max(0, int(round(duration_seconds)))
    minutes = safe_seconds // SECONDS_PER_MINUTE
    seconds = safe_seconds % SECONDS_PER_MINUTE
    return f"{minutes:02d}:{seconds:02d}"


def format_sample_rate_label(sample_rate_hz: int) -> str:
    """Format sample rate in Hz as e.g. '44.1 KHZ' or '96.0 KHZ'."""
    if sample_rate_hz <= 0:
        return "44.1 KHZ"
    return f"{sample_rate_hz / SAMPLE_RATE_KHZ_DIVISOR:.1f} KHZ"


def format_bitrate_label(bitrate_kbps: int) -> str:
    """Format bitrate in kbps with thousands separator."""
    safe_kbps = max(1, int(round(bitrate_kbps)))
    return f"{safe_kbps:,} KBPS"


def format_file_size_label(file_size_bytes: int) -> str:
    """Format byte count in megabytes with one decimal place."""
    megabytes = max(MIN_FILE_SIZE_LABEL_MB, file_size_bytes / BYTES_PER_MEGABYTE)
    return f"{megabytes:.1f} MB"


def clean_filename_title(file_path: pathlib.Path) -> str:
    """Derive a clean human-readable track title from an audio filename."""
    stripped_stem = TRACK_PREFIX_PATTERN.sub("", file_path.stem).strip()
    return stripped_stem or file_path.stem


def parse_leading_integer(text_value: str | None, fallback: int = 0) -> int:
    """Extract leading track or disc integer from strings like '05/12' or '2'."""
    if not text_value:
        return fallback
    match = re.search(r"\d+", text_value)
    return int(match.group(0)) if match else fallback


def parse_year_string(text_value: str | None) -> str:
    """Extract a 4-digit year string if present."""
    if not text_value:
        return ""
    match = YEAR_PATTERN.search(text_value)
    return match.group(1) if match else ""


def parse_genre_list(genre_tags: list[str]) -> list[str]:
    """Split and deduplicate multi-genre tags while preserving order."""
    seen_lowercase: set[str] = set()
    cleaned_genres: list[str] = []
    for genre_entry in genre_tags:
        cleaned_entry = genre_entry.strip()
        numeric_match = ID3_NUMERIC_GENRE_PATTERN.match(cleaned_entry)
        if numeric_match:
            remainder = numeric_match.group(2).strip()
            cleaned_entry = remainder if remainder else ""
        if not cleaned_entry:
            continue
        for token in GENRE_SPLIT_PATTERN.split(cleaned_entry):
            genre_name = token.strip()
            if not genre_name:
                continue
            lower_key = genre_name.casefold()
            if lower_key not in seen_lowercase:
                seen_lowercase.add(lower_key)
                cleaned_genres.append(genre_name)
    return cleaned_genres or ["Electronic"]


def parse_lyrics_text(lyrics_text: str) -> dict[str, Any] | None:
    """Parse timed LRC or plain untimed lyrics into the DialOS catalog format."""
    if not lyrics_text or not lyrics_text.strip():
        return None

    timed_cues: list[dict[str, Any]] = []
    plain_lines: list[str] = []

    for lyric_line in lyrics_text.splitlines():
        stripped_line = lyric_line.strip()
        if not stripped_line:
            continue
        matches = list(LRC_TIMESTAMP_PATTERN.finditer(stripped_line))
        if matches:
            lyric_body = LRC_TIMESTAMP_PATTERN.sub("", stripped_line).strip()
            if not lyric_body:
                continue
            for timestamp_match in matches:
                minutes = int(timestamp_match.group(1))
                seconds = int(timestamp_match.group(2))
                fraction_text = timestamp_match.group(3)
                fractional_seconds = 0.0
                if fraction_text:
                    divisor = (
                        CENTISECONDS_DIVISOR
                        if len(fraction_text) <= MILLISECONDS_PER_SECOND_DIGITS
                        else MILLISECONDS_DIVISOR
                    )
                    fractional_seconds = int(fraction_text) / divisor
                cue_time = round(minutes * SECONDS_PER_MINUTE + seconds + fractional_seconds, 3)
                timed_cues.append({"timeSeconds": cue_time, "text": lyric_body})
        elif not LRC_META_TAG_PATTERN.match(stripped_line) and not stripped_line.startswith("["):
            plain_lines.append(stripped_line)

    if timed_cues:
        timed_cues.sort(key=lambda cue_entry: cue_entry["timeSeconds"])
        return {"kind": "timed", "lines": timed_cues}

    if plain_lines:
        return {"kind": "untimed", "lines": plain_lines}

    return None


def read_sidecar_lyrics(audio_file_path: pathlib.Path) -> dict[str, Any] | None:
    """Load and parse a sibling .lrc file next to the audio file if one exists."""
    lrc_path = audio_file_path.with_suffix(".lrc")
    if not lrc_path.is_file():
        return None
    try:
        content = lrc_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    return parse_lyrics_text(content)


def infer_fallback_hierarchy(audio_file_path: pathlib.Path) -> tuple[str, str, str]:
    """Infer (artist, album, title) from Artist/Album/Track path layout."""
    album_folder = audio_file_path.parent.name or "Unknown Album"
    artist_folder = (
        audio_file_path.parent.parent.name
        if audio_file_path.parent.parent != audio_file_path.parent
        else "Unknown Artist"
    )
    return artist_folder, album_folder, clean_filename_title(audio_file_path)


def parse_flac_picture_block(
    audio_file_path: pathlib.Path,
    header_preview: bytes,
    block_length: int,
    block_payload_file_offset: int,
) -> tuple[int, EmbeddedCoverReference | None]:
    """Parse a FLAC METADATA_BLOCK_PICTURE header and locate the image bytes."""
    if block_length < FLAC_PICTURE_HEADER_MIN_BYTES or len(header_preview) < FLAC_PICTURE_HEADER_MIN_BYTES:
        return 0, None
    cursor = 0
    picture_type, mime_length = struct.unpack_from(">II", header_preview, cursor)
    cursor += 8
    if cursor + mime_length + 4 > len(header_preview):
        return picture_type, None
    mime_type = (
        header_preview[cursor : cursor + mime_length].decode("ascii", errors="replace").strip()
        or "image/jpeg"
    )
    cursor += mime_length
    description_length = struct.unpack_from(">I", header_preview, cursor)[0]
    cursor += 4 + description_length + 16
    if cursor + 4 > len(header_preview):
        return picture_type, None
    image_length = struct.unpack_from(">I", header_preview, cursor)[0]
    cursor += 4
    if image_length <= 0 or cursor + image_length > block_length:
        return picture_type, None
    return (
        picture_type,
        EmbeddedCoverReference(
            file_path=audio_file_path,
            byte_offset=block_payload_file_offset + cursor,
            byte_length=image_length,
            mime_type=mime_type,
        ),
    )


def parse_flac_vorbis_comment_block(payload_bytes: bytes, tags: dict[str, list[str]]) -> None:
    """Extract Vorbis comments from a FLAC VORBIS_COMMENT metadata block."""
    if len(payload_bytes) < 8:
        return
    offset = 0
    vendor_length = struct.unpack_from("<I", payload_bytes, offset)[0]
    offset += 4 + vendor_length
    if offset + 4 > len(payload_bytes):
        return
    comment_count = struct.unpack_from("<I", payload_bytes, offset)[0]
    offset += 4
    for _unused_comment in range(comment_count):
        if offset + 4 > len(payload_bytes):
            break
        comment_length = struct.unpack_from("<I", payload_bytes, offset)[0]
        offset += 4
        if offset + comment_length > len(payload_bytes):
            break
        comment_text = payload_bytes[offset : offset + comment_length].decode("utf-8", errors="replace")
        offset += comment_length
        if "=" in comment_text:
            field_name, field_value = comment_text.split("=", 1)
            tags.setdefault(field_name.strip().upper(), []).append(field_value.strip())


def parse_flac_metadata(
    audio_file_path: pathlib.Path,
    file_size_bytes: int,
    file_mtime_seconds: float = 0.0,
) -> ParsedAudioMetadata | None:
    """Read STREAMINFO, VORBIS_COMMENT, and PICTURE headers from a FLAC file."""
    try:
        with audio_file_path.open("rb") as audio_stream:
            if audio_stream.read(FLAC_HEADER_SIZE_BYTES) != FLAC_SIGNATURE:
                return None

            sample_rate_hz = DEFAULT_SAMPLE_RATE_HZ
            bits_per_sample = DEFAULT_BITS_PER_SAMPLE
            duration_seconds = 0.0
            tags: dict[str, list[str]] = {}
            chosen_cover: EmbeddedCoverReference | None = None
            chosen_cover_is_front = False
            saw_stream_information = False

            while True:
                block_header = audio_stream.read(FLAC_HEADER_SIZE_BYTES)
                if len(block_header) < FLAC_HEADER_SIZE_BYTES:
                    break
                is_last_block = (block_header[0] & FLAC_LAST_BLOCK_MASK) != 0
                block_type = block_header[0] & FLAC_BLOCK_TYPE_MASK
                block_length = int.from_bytes(block_header[1:FLAC_HEADER_SIZE_BYTES], "big")
                payload_offset = audio_stream.tell()

                if block_type == FLAC_STREAM_INFORMATION_BLOCK_TYPE and block_length >= FLAC_STREAM_INFORMATION_MIN_BYTES:
                    payload_bytes = audio_stream.read(block_length)
                    if len(payload_bytes) < FLAC_STREAM_INFORMATION_MIN_BYTES:
                        return None
                    sample_rate_hz = (
                        (payload_bytes[10] << FLAC_SAMPLE_RATE_HIGH_SHIFT_BITS)
                        | (payload_bytes[11] << FLAC_SAMPLE_RATE_MID_SHIFT_BITS)
                        | (payload_bytes[12] >> FLAC_SAMPLE_RATE_LOW_SHIFT_BITS)
                    )
                    bits_per_sample = (
                        ((payload_bytes[12] & FLAC_BITS_PER_SAMPLE_HIGH_MASK) << FLAC_BITS_PER_SAMPLE_HIGH_SHIFT_BITS)
                        | (payload_bytes[13] >> FLAC_BITS_PER_SAMPLE_LOW_SHIFT_BITS)
                    ) + 1
                    total_samples = (
                        (payload_bytes[13] & FLAC_TOTAL_SAMPLES_HIGH_MASK) << FLAC_TOTAL_SAMPLES_HIGH_SHIFT_BITS
                    ) | int.from_bytes(payload_bytes[14:18], "big")
                    if sample_rate_hz > 0 and total_samples > 0:
                        duration_seconds = total_samples / sample_rate_hz
                    saw_stream_information = True
                elif block_type == FLAC_VORBIS_COMMENT_BLOCK_TYPE and block_length <= FLAC_MAX_VORBIS_COMMENT_BYTES:
                    payload_bytes = audio_stream.read(block_length)
                    parse_flac_vorbis_comment_block(payload_bytes, tags)
                elif (
                    block_type == FLAC_PICTURE_BLOCK_TYPE
                    and not chosen_cover_is_front
                    and block_length <= FLAC_MAX_PICTURE_BLOCK_BYTES
                    and payload_offset + block_length <= file_size_bytes
                ):
                    header_preview_size = min(block_length, FLAC_PICTURE_HEADER_PREVIEW_BYTES)
                    header_preview = audio_stream.read(header_preview_size)
                    remaining_bytes = block_length - len(header_preview)
                    if remaining_bytes > 0:
                        audio_stream.seek(remaining_bytes, os.SEEK_CUR)
                    if len(header_preview) == header_preview_size:
                        picture_type, cover_reference = parse_flac_picture_block(
                            audio_file_path,
                            header_preview,
                            block_length,
                            payload_offset,
                        )
                        if cover_reference is not None and (
                            chosen_cover is None or picture_type == FLAC_FRONT_COVER_PICTURE_TYPE
                        ):
                            chosen_cover = cover_reference
                            chosen_cover_is_front = picture_type == FLAC_FRONT_COVER_PICTURE_TYPE
                else:
                    audio_stream.seek(block_length, os.SEEK_CUR)

                if is_last_block:
                    break

            if not saw_stream_information:
                return None
    except OSError:
        return None

    fallback_artist, fallback_album, fallback_title = infer_fallback_hierarchy(audio_file_path)
    title = (tags.get("TITLE") or [fallback_title])[0] or fallback_title
    artist = (tags.get("ARTIST") or tags.get("ALBUMARTIST") or [fallback_artist])[0] or fallback_artist
    album_artist = (tags.get("ALBUMARTIST") or tags.get("ALBUM ARTIST") or [artist])[0] or artist
    album_title = (tags.get("ALBUM") or [fallback_album])[0] or fallback_album
    genres = parse_genre_list(tags.get("GENRE") or [])
    year = parse_year_string((tags.get("DATE") or tags.get("YEAR") or [""])[0])
    track_number = parse_leading_integer((tags.get("TRACKNUMBER") or [""])[0], fallback=0)
    disc_number = parse_leading_integer((tags.get("DISCNUMBER") or ["1"])[0], fallback=1)

    safe_duration = duration_seconds if duration_seconds > 0 else DEFAULT_FALLBACK_DURATION_SECONDS
    bitrate_kbps = int(round((file_size_bytes * BITS_PER_BYTE) / (safe_duration * KILOBITS_DIVISOR)))
    lyrics_entry = read_sidecar_lyrics(audio_file_path)
    if lyrics_entry is None:
        embedded_lyrics_text = (tags.get("LYRICS") or tags.get("UNSYNCEDLYRICS") or [""])[0]
        lyrics_entry = parse_lyrics_text(embedded_lyrics_text)

    return ParsedAudioMetadata(
        file_path=audio_file_path,
        file_size_bytes=file_size_bytes,
        file_mtime_seconds=file_mtime_seconds,
        title=title,
        artist=artist,
        album_artist=album_artist,
        album_title=album_title,
        genres=genres,
        year=year,
        track_number=track_number,
        disc_number=disc_number,
        duration_seconds=safe_duration,
        sample_rate_hz=sample_rate_hz,
        bits_per_sample=bits_per_sample,
        bitrate_kbps=bitrate_kbps,
        format_label=f"FLAC {bits_per_sample}-BIT",
        embedded_cover=chosen_cover,
        lyrics_entry=lyrics_entry,
    )


def decode_synchsafe_integer(synchsafe_bytes: bytes) -> int:
    """Decode a 4-byte ID3v2 synchsafe integer."""
    return (
        ((synchsafe_bytes[0] & ID3_SYNCHSAFE_MASK) << ID3_SYNCHSAFE_SHIFT_HIGH)
        | ((synchsafe_bytes[1] & ID3_SYNCHSAFE_MASK) << ID3_SYNCHSAFE_SHIFT_MID_HIGH)
        | ((synchsafe_bytes[2] & ID3_SYNCHSAFE_MASK) << ID3_SYNCHSAFE_SHIFT_MID_LOW)
        | (synchsafe_bytes[3] & ID3_SYNCHSAFE_MASK)
    )


def decode_id3_text_bytes(encoding_byte: int, text_bytes: bytes) -> str:
    """Decode an ID3v2 encoded text payload into a clean Python string."""
    if encoding_byte == ID3_ENCODING_ISO_8859_1:
        return text_bytes.split(b"\x00", 1)[0].decode("latin-1", errors="replace").strip()
    if encoding_byte == ID3_ENCODING_UTF16_BOM:
        return text_bytes.decode("utf-16", errors="replace").split("\x00", 1)[0].strip()
    if encoding_byte == ID3_ENCODING_UTF16_BE:
        return text_bytes.decode("utf-16-be", errors="replace").split("\x00", 1)[0].strip()
    return text_bytes.split(b"\x00", 1)[0].decode("utf-8", errors="replace").strip()


def skip_null_terminated_id3_string(payload_bytes: bytes, start_offset: int, encoding_byte: int) -> int:
    """Advance past a null-terminated string in an ID3v2 frame."""
    if encoding_byte in (ID3_ENCODING_UTF16_BOM, ID3_ENCODING_UTF16_BE):
        cursor = start_offset
        while cursor + 1 < len(payload_bytes):
            if payload_bytes[cursor] == 0 and payload_bytes[cursor + 1] == 0:
                return cursor + 2
            cursor += 2
        return len(payload_bytes)
    null_index = payload_bytes.find(b"\x00", start_offset)
    return null_index + 1 if null_index != -1 else len(payload_bytes)


def parse_id3v2_tags(
    audio_file_path: pathlib.Path,
    id3_header: bytes,
    tag_bytes: bytes,
) -> tuple[dict[str, list[str]], EmbeddedCoverReference | None]:
    """Parse ID3v2 frames into tag lists and an optional embedded cover reference."""
    tags: dict[str, list[str]] = {}
    chosen_cover: EmbeddedCoverReference | None = None
    chosen_cover_is_front = False

    version_major = id3_header[3]
    flags = id3_header[5]
    cursor = 0

    if (flags & ID3_FLAG_EXTENDED_HEADER_MASK) != 0 and len(tag_bytes) >= 4:
        extended_header_size = (
            decode_synchsafe_integer(tag_bytes[:4])
            if version_major == ID3_V4_MAJOR_VERSION
            else struct.unpack_from(">I", tag_bytes, 0)[0]
        )
        cursor += extended_header_size if version_major == ID3_V4_MAJOR_VERSION else (extended_header_size + 4)

    frame_header_size = ID3_V2_FRAME_HEADER_BYTES if version_major == ID3_V2_MAJOR_VERSION else ID3_V3_FRAME_HEADER_BYTES
    while cursor + frame_header_size <= len(tag_bytes):
        if tag_bytes[cursor] == 0:
            break
        if version_major == ID3_V2_MAJOR_VERSION:
            frame_id = tag_bytes[cursor : cursor + 3].decode("latin-1", errors="replace")
            frame_size = int.from_bytes(tag_bytes[cursor + 3 : cursor + 6], "big")
        else:
            frame_id = tag_bytes[cursor : cursor + 4].decode("latin-1", errors="replace")
            size_bytes = tag_bytes[cursor + 4 : cursor + 8]
            frame_size = (
                decode_synchsafe_integer(size_bytes)
                if version_major == ID3_V4_MAJOR_VERSION
                else struct.unpack(">I", size_bytes)[0]
            )
        payload_start = cursor + frame_header_size
        payload_end = payload_start + frame_size
        if frame_size <= 0 or payload_end > len(tag_bytes):
            break
        payload_bytes = tag_bytes[payload_start:payload_end]

        if frame_id in TEXT_FRAME_IDS:
            if payload_bytes:
                decoded_value = decode_id3_text_bytes(payload_bytes[0], payload_bytes[1:])
                if decoded_value:
                    tags.setdefault(frame_id, []).append(decoded_value)
        elif frame_id == "USLT" and len(payload_bytes) > ID3_USLT_MIN_PAYLOAD_BYTES:
            encoding_byte = payload_bytes[0]
            body_offset = skip_null_terminated_id3_string(payload_bytes, ID3_USLT_MIN_PAYLOAD_BYTES, encoding_byte)
            lyrics_text = decode_id3_text_bytes(encoding_byte, payload_bytes[body_offset:])
            if lyrics_text:
                tags.setdefault("USLT", []).append(lyrics_text)
        elif frame_id == "APIC" and len(payload_bytes) > ID3_APIC_MIN_PAYLOAD_BYTES and not chosen_cover_is_front:
            encoding_byte = payload_bytes[0]
            mime_end = payload_bytes.find(b"\x00", 1)
            if mime_end != -1 and mime_end + 2 < len(payload_bytes):
                mime_type = payload_bytes[1:mime_end].decode("ascii", errors="replace").strip() or "image/jpeg"
                picture_type = payload_bytes[mime_end + 1]
                image_start = skip_null_terminated_id3_string(payload_bytes, mime_end + 2, encoding_byte)
                image_length = len(payload_bytes) - image_start
                if image_length > 0 and (chosen_cover is None or picture_type == ID3_FRONT_COVER_PICTURE_TYPE):
                    chosen_cover = EmbeddedCoverReference(
                        file_path=audio_file_path,
                        byte_offset=ID3_HEADER_SIZE_BYTES + payload_start + image_start,
                        byte_length=image_length,
                        mime_type=mime_type,
                    )
                    chosen_cover_is_front = picture_type == ID3_FRONT_COVER_PICTURE_TYPE

        cursor = payload_end

    return tags, chosen_cover


def parse_mp3_metadata(
    audio_file_path: pathlib.Path,
    file_size_bytes: int,
    file_mtime_seconds: float = 0.0,
) -> ParsedAudioMetadata | None:
    """Parse ID3v2 tags and MPEG Layer III frame headers from an MP3 file."""
    tags: dict[str, list[str]] = {}
    chosen_cover: EmbeddedCoverReference | None = None
    audio_start_offset = 0

    try:
        with audio_file_path.open("rb") as audio_stream:
            id3_header = audio_stream.read(ID3_HEADER_SIZE_BYTES)
            if len(id3_header) == ID3_HEADER_SIZE_BYTES and id3_header[:3] == ID3_SIGNATURE:
                tag_size = decode_synchsafe_integer(id3_header[6:10])
                audio_start_offset = ID3_HEADER_SIZE_BYTES + tag_size
                tag_bytes = audio_stream.read(min(tag_size, ID3_MAX_TAG_BYTES))
                tags, chosen_cover = parse_id3v2_tags(audio_file_path, id3_header, tag_bytes)

            audio_stream.seek(min(audio_start_offset, file_size_bytes))
            probe_buffer = audio_stream.read(MP3_SYNC_SCAN_LIMIT_BYTES)
    except OSError:
        return None

    sample_rate_hz = DEFAULT_SAMPLE_RATE_HZ
    bitrate_kbps = DEFAULT_MP3_BITRATE_KBPS
    duration_seconds = 0.0
    found_mpeg_frame = False

    for scan_index in range(max(0, len(probe_buffer) - MPEG_HEADER_BYTES)):
        if (
            probe_buffer[scan_index] != MPEG_FRAME_SYNC_FIRST_BYTE
            or (probe_buffer[scan_index + 1] & MPEG_SYNC_SECOND_BYTE_MASK) != MPEG_SYNC_SECOND_BYTE_MASK
        ):
            continue
        version_bits = (probe_buffer[scan_index + 1] & MPEG_VERSION_MASK) >> MPEG_VERSION_SHIFT_BITS
        layer_bits = (probe_buffer[scan_index + 1] & MPEG_LAYER_MASK) >> MPEG_LAYER_SHIFT_BITS
        bitrate_index = (probe_buffer[scan_index + 2] & MPEG_BITRATE_INDEX_MASK) >> MPEG_BITRATE_SHIFT_BITS
        sample_rate_index = (probe_buffer[scan_index + 2] & MPEG_SAMPLE_RATE_INDEX_MASK) >> MPEG_SAMPLE_RATE_SHIFT_BITS

        if (
            version_bits == MPEG_VERSION_RESERVED
            or layer_bits != MPEG_LAYER_III
            or bitrate_index in (0, MPEG_BITRATE_INVALID_INDEX)
            or sample_rate_index == MPEG_SAMPLE_RATE_INVALID_INDEX
        ):
            continue

        sample_rates = MPEG_SAMPLE_RATES_BY_VERSION.get(version_bits)
        if not sample_rates:
            continue
        sample_rate_hz = sample_rates[sample_rate_index]
        bitrate_table = MPEG1_LAYER3_BITRATES_KBPS if version_bits == MPEG_VERSION_MPEG1 else MPEG2_LAYER3_BITRATES_KBPS
        bitrate_kbps = bitrate_table[bitrate_index]
        samples_per_frame = (
            MPEG_SAMPLES_PER_FRAME_V1 if version_bits == MPEG_VERSION_MPEG1 else MPEG_SAMPLES_PER_FRAME_V2
        )
        channel_mode = (probe_buffer[scan_index + 3] & MPEG_CHANNEL_MODE_MASK) >> MPEG_CHANNEL_MODE_SHIFT_BITS

        if version_bits == MPEG_VERSION_MPEG1:
            side_information_bytes = (
                MPEG_SIDE_INFORMATION_MONO_V1_BYTES
                if channel_mode == MPEG_CHANNEL_MODE_MONO
                else MPEG_SIDE_INFORMATION_STEREO_V1_BYTES
            )
        else:
            side_information_bytes = (
                MPEG_SIDE_INFORMATION_MONO_V2_BYTES
                if channel_mode == MPEG_CHANNEL_MODE_MONO
                else MPEG_SIDE_INFORMATION_STEREO_V2_BYTES
            )

        xing_offset = scan_index + MPEG_HEADER_BYTES + side_information_bytes
        if xing_offset + 12 <= len(probe_buffer) and probe_buffer[xing_offset : xing_offset + 4] in (b"Xing", b"Info"):
            xing_flags = struct.unpack_from(">I", probe_buffer, xing_offset + 4)[0]
            if (xing_flags & XING_FRAMES_FLAG_MASK) != 0:
                total_frames = struct.unpack_from(">I", probe_buffer, xing_offset + 8)[0]
                if total_frames > 0 and sample_rate_hz > 0:
                    duration_seconds = (total_frames * samples_per_frame) / sample_rate_hz

        if duration_seconds <= 0 and bitrate_kbps > 0:
            audio_payload_bytes = max(1, file_size_bytes - (audio_start_offset + scan_index))
            duration_seconds = (audio_payload_bytes * BITS_PER_BYTE) / (bitrate_kbps * KILOBITS_DIVISOR)

        found_mpeg_frame = True
        break

    if not found_mpeg_frame:
        return None

    fallback_artist, fallback_album, fallback_title = infer_fallback_hierarchy(audio_file_path)
    title = (tags.get("TIT2") or tags.get("TT2") or [fallback_title])[0] or fallback_title
    artist = (tags.get("TPE1") or tags.get("TP1") or tags.get("TPE2") or tags.get("TP2") or [fallback_artist])[0] or fallback_artist
    album_artist = (tags.get("TPE2") or tags.get("TP2") or [artist])[0] or artist
    album_title = (tags.get("TALB") or tags.get("TAL") or [fallback_album])[0] or fallback_album
    genres = parse_genre_list(tags.get("TCON") or tags.get("TCO") or [])
    year = parse_year_string((tags.get("TDRC") or tags.get("TYER") or tags.get("TYE") or [""])[0])
    track_number = parse_leading_integer((tags.get("TRCK") or tags.get("TRK") or [""])[0], fallback=0)
    disc_number = parse_leading_integer((tags.get("TPOS") or tags.get("TPA") or ["1"])[0], fallback=1)

    safe_duration = duration_seconds if duration_seconds > 0 else DEFAULT_FALLBACK_DURATION_SECONDS
    effective_kbps = (
        int(round((file_size_bytes * BITS_PER_BYTE) / (safe_duration * KILOBITS_DIVISOR)))
        if bitrate_kbps <= 0
        else bitrate_kbps
    )
    lyrics_entry = read_sidecar_lyrics(audio_file_path)
    if lyrics_entry is None and tags.get("USLT"):
        lyrics_entry = parse_lyrics_text(tags["USLT"][0])

    return ParsedAudioMetadata(
        file_path=audio_file_path,
        file_size_bytes=file_size_bytes,
        file_mtime_seconds=file_mtime_seconds,
        title=title,
        artist=artist,
        album_artist=album_artist,
        album_title=album_title,
        genres=genres,
        year=year,
        track_number=track_number,
        disc_number=disc_number,
        duration_seconds=safe_duration,
        sample_rate_hz=sample_rate_hz,
        bits_per_sample=DEFAULT_BITS_PER_SAMPLE,
        bitrate_kbps=effective_kbps,
        format_label=f"MP3 {bitrate_kbps}K",
        embedded_cover=chosen_cover,
        lyrics_entry=lyrics_entry,
    )


def parse_audio_file_metadata(audio_file_path: pathlib.Path) -> ParsedAudioMetadata | None:
    """Dispatch audio metadata extraction based on file extension."""
    try:
        stat_result = audio_file_path.stat()
        file_size_bytes = stat_result.st_size
        file_mtime_seconds = float(stat_result.st_mtime)
    except OSError:
        return None
    if file_size_bytes <= 0:
        return None

    extension = audio_file_path.suffix.lower()
    if extension == ".flac":
        return parse_flac_metadata(audio_file_path, file_size_bytes, file_mtime_seconds)
    if extension == ".mp3":
        return parse_mp3_metadata(audio_file_path, file_size_bytes, file_mtime_seconds)
    return None


def find_sidecar_cover_in_directory(album_directory: pathlib.Path) -> SidecarCoverReference | None:
    """Locate a cover image file sitting inside an album folder."""
    try:
        directory_entries = list(album_directory.iterdir())
    except OSError:
        return None

    lowercase_name_map = {
        entry.name.lower(): entry
        for entry in directory_entries
        if entry.is_file() and not entry.name.startswith(".")
    }
    for preferred_name in PREFERRED_COVER_FILENAMES:
        matched_entry = lowercase_name_map.get(preferred_name)
        if matched_entry is not None:
            mime_type = mimetypes.guess_type(matched_entry.name)[0] or "image/jpeg"
            return SidecarCoverReference(file_path=matched_entry, mime_type=mime_type)

    for entry in sorted(directory_entries, key=lambda file_entry: file_entry.name.lower()):
        if entry.is_file() and not entry.name.startswith(".") and entry.suffix.lower() in IMAGE_EXTENSIONS:
            mime_type = mimetypes.guess_type(entry.name)[0] or "image/jpeg"
            return SidecarCoverReference(file_path=entry, mime_type=mime_type)
    return None


def parse_http_range_header(range_header: str, total_byte_length: int) -> tuple[int, int] | None:
    """Parse a single HTTP byte Range header into inclusive (start_byte, end_byte)."""
    if not range_header or not range_header.lower().startswith("bytes=") or total_byte_length <= 0:
        return None
    range_spec = range_header[len("bytes=") :].split(",", 1)[0].strip()
    if "-" not in range_spec:
        return None
    start_text, end_text = range_spec.split("-", 1)
    try:
        if start_text == "":
            suffix_length = int(end_text)
            if suffix_length <= 0:
                return None
            start_byte = max(0, total_byte_length - suffix_length)
            end_byte = total_byte_length - 1
            return start_byte, end_byte
        start_byte = int(start_text)
        end_byte = total_byte_length - 1 if end_text == "" else min(int(end_text), total_byte_length - 1)
    except ValueError:
        return None
    if start_byte < 0 or start_byte >= total_byte_length or end_byte < start_byte:
        return None
    return start_byte, end_byte


class SimulatorLibraryStore:
    """Read-only music scanner plus atomic user state store for DialOS Simulator."""

    def __init__(self, music_directory: pathlib.Path, state_file_path: pathlib.Path) -> None:
        self.music_directory = music_directory.expanduser().resolve()
        self.state_file_path = state_file_path.expanduser().resolve()
        self._lock = threading.RLock()
        self._scan_condition = threading.Condition(self._lock)
        self._scan_mutex = threading.Lock()
        self._track_paths_by_id: dict[str, pathlib.Path] = {}
        self._album_ids: set[str] = set()
        self._artist_ids: set[str] = set()
        self._covers_by_album_id: dict[str, SidecarCoverReference | EmbeddedCoverReference] = {}
        self._catalog: dict[str, Any] = {}
        self._has_catalog = False
        self._favorite_track_ids: set[str] = set()
        self._favorite_album_ids: set[str] = set()
        self._favorite_artist_ids: set[str] = set()
        self._playlists: list[dict[str, Any]] = []

        self._scan_id = 0
        self._boot_phase = "idle"
        self._boot_ready = False
        self._discovered_files = 0
        self._scanned_files = 0
        self._boot_progress = 0.0
        self._music_root_exists = self.music_directory.is_dir()
        self._boot_counts = build_empty_counts()
        self._scan_duration_seconds = 0.0
        self._boot_error: str | None = None

        self._load_user_state()

    def _load_user_state(self) -> None:
        if not self.state_file_path.is_file():
            return
        try:
            saved_state = json.loads(self.state_file_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return
        favorites = saved_state.get("favorites")
        if isinstance(favorites, list):
            self._favorite_track_ids = {str(track_id) for track_id in favorites}
        favorite_albums = saved_state.get("favoriteAlbums")
        if isinstance(favorite_albums, list):
            self._favorite_album_ids = {str(album_id) for album_id in favorite_albums}
        favorite_artists = saved_state.get("favoriteArtists")
        if isinstance(favorite_artists, list):
            self._favorite_artist_ids = {str(artist_id) for artist_id in favorite_artists}
        playlists = saved_state.get("playlists")
        if isinstance(playlists, list):
            sanitized_playlists: list[dict[str, Any]] = []
            for entry in playlists:
                if isinstance(entry, dict) and entry.get("id") and entry.get("name"):
                    track_ids = [str(track_id) for track_id in entry.get("trackIds", [])]
                    sanitized_playlists.append(
                        {
                            "id": str(entry["id"]),
                            "name": str(entry["name"]),
                            "trackIds": track_ids,
                        }
                    )
            self._playlists = sanitized_playlists

    def _persist_user_state_atomically(self) -> None:
        self.state_file_path.parent.mkdir(parents=True, exist_ok=True)
        payload_text = json.dumps(
            {
                "favorites": sorted(self._favorite_track_ids),
                "favoriteAlbums": sorted(self._favorite_album_ids),
                "favoriteArtists": sorted(self._favorite_artist_ids),
                "playlists": self._playlists,
            },
            indent=2,
            ensure_ascii=False,
        )
        staging_path = self.state_file_path.with_suffix(".tmp")
        with staging_path.open("w", encoding="utf-8") as staging_stream:
            staging_stream.write(payload_text)
            staging_stream.flush()
            os.fsync(staging_stream.fileno())
        os.replace(staging_path, self.state_file_path)

    def _snapshot_boot_status_locked(self) -> dict[str, Any]:
        return {
            "scanId": self._scan_id,
            "phase": self._boot_phase,
            "ready": self._boot_ready,
            "discoveredFiles": self._discovered_files,
            "scannedFiles": self._scanned_files,
            "progress": self._boot_progress,
            "musicRoot": str(self.music_directory),
            "musicRootExists": self._music_root_exists,
            "counts": dict(self._boot_counts),
            "scanDurationSeconds": self._scan_duration_seconds,
            "error": self._boot_error,
        }

    def get_boot_status(self) -> dict[str, Any]:
        """Return a thread-safe snapshot of current boot and scan telemetry."""
        with self._lock:
            return self._snapshot_boot_status_locked()

    def _build_catalog_snapshot(
        self,
        parsed_tracks: list[tuple[str, ParsedAudioMetadata]],
        favorite_track_ids: set[str],
        favorite_album_ids: set[str],
        favorite_artist_ids: set[str],
        stored_playlists: list[dict[str, Any]],
    ) -> tuple[
        dict[str, Any],
        dict[str, pathlib.Path],
        dict[str, SidecarCoverReference | EmbeddedCoverReference],
    ]:
        sidecar_cover_cache: dict[pathlib.Path, SidecarCoverReference | None] = {}
        album_buckets: dict[str, list[tuple[str, ParsedAudioMetadata]]] = {}
        album_metadata: dict[str, dict[str, Any]] = {}
        track_paths_by_id: dict[str, pathlib.Path] = {}
        covers_by_album_id: dict[str, SidecarCoverReference | EmbeddedCoverReference] = {}

        for track_id, metadata in parsed_tracks:
            track_paths_by_id[track_id] = metadata.file_path
            album_directory = metadata.file_path.parent
            relative_album_directory = album_directory.relative_to(self.music_directory).as_posix()
            album_key = f"{metadata.album_artist.casefold()}|{metadata.album_title.casefold()}|{relative_album_directory}"
            album_id = compute_stable_id(album_key)

            album_buckets.setdefault(album_id, []).append((track_id, metadata))
            if album_id not in album_metadata:
                album_metadata[album_id] = {
                    "id": album_id,
                    "title": metadata.album_title,
                    "artist": metadata.album_artist,
                    "genre": metadata.genres[0],
                    "year": metadata.year or "—",
                    "directory": album_directory,
                }
            if album_directory not in sidecar_cover_cache:
                sidecar_cover_cache[album_directory] = find_sidecar_cover_in_directory(album_directory)
            sidecar_cover = sidecar_cover_cache[album_directory]
            if sidecar_cover is not None:
                covers_by_album_id[album_id] = sidecar_cover
            elif album_id not in covers_by_album_id and metadata.embedded_cover is not None:
                covers_by_album_id[album_id] = metadata.embedded_cover

        with self._lock:
            self._boot_phase = "indexing"
            self._boot_progress = BOOT_INDEXING_PROGRESS
            self._boot_counts = {
                "tracks": len(parsed_tracks),
                "albums": len(album_buckets),
                "artists": 0,
                "genres": 0,
                "favorites": 0,
                "favoriteTracks": 0,
                "favoriteAlbums": 0,
                "playlists": len(stored_playlists),
            }

        albums_list: list[dict[str, Any]] = []
        all_tracks_list: list[dict[str, Any]] = []
        album_tech_specs: dict[str, dict[str, Any]] = {}
        lyrics_catalog: dict[str, dict[str, Any]] = {}
        artist_album_ids: dict[str, tuple[str, set[str]]] = {}
        album_added_at_by_id: dict[str, float] = {}
        genre_albums: dict[str, tuple[str, set[str], int]] = {}

        sorted_album_ids = sorted(
            album_buckets.keys(),
            key=lambda current_album_id: album_metadata[current_album_id]["title"].casefold(),
        )

        for album_index, album_id in enumerate(sorted_album_ids):
            current_album = album_metadata[album_id]
            album_track_entries = album_buckets[album_id]
            album_track_entries.sort(
                key=lambda track_pair: (
                    track_pair[1].disc_number,
                    track_pair[1].track_number if track_pair[1].track_number > 0 else UNORDERED_TRACK_SORT_FALLBACK,
                    track_pair[1].file_path.name.casefold(),
                )
            )
            has_cover = album_id in covers_by_album_id
            cover_url = f"/api/cover/{album_id}" if has_cover else None
            motif = COVER_MOTIF_CYCLE[album_index % len(COVER_MOTIF_CYCLE)]

            album_tracks_payload: list[dict[str, Any]] = []
            for track_position, (track_id, track_metadata) in enumerate(album_track_entries):
                duration_label = format_clock_duration(track_metadata.duration_seconds)
                is_favorite = track_id in favorite_track_ids
                format_spec = {
                    "format": track_metadata.format_label,
                    "sampleRate": format_sample_rate_label(track_metadata.sample_rate_hz),
                    "bitrate": format_bitrate_label(track_metadata.bitrate_kbps),
                    "fileSize": format_file_size_label(track_metadata.file_size_bytes),
                }
                album_tracks_payload.append(
                    {
                        "id": track_id,
                        "title": track_metadata.title,
                        "artist": track_metadata.artist,
                        "genres": list(track_metadata.genres),
                        "duration": duration_label,
                        "durationSeconds": round(track_metadata.duration_seconds, 2),
                        "isFavorite": is_favorite,
                        "audioUrl": f"/api/audio/{track_id}",
                        "format": format_spec["format"],
                        "sampleRate": format_spec["sampleRate"],
                        "bitrate": format_spec["bitrate"],
                        "fileSize": format_spec["fileSize"],
                    }
                )
                all_tracks_list.append(
                    {
                        "id": track_id,
                        "title": track_metadata.title,
                        "artist": track_metadata.artist,
                        "albumId": album_id,
                        "albumTitle": current_album["title"],
                        "genre": track_metadata.genres[0],
                        "genres": list(track_metadata.genres),
                        "duration": duration_label,
                        "durationSeconds": round(track_metadata.duration_seconds, 2),
                        "isFavorite": is_favorite,
                        "trackNumber": f"{track_position + 1:02d}",
                        "audioUrl": f"/api/audio/{track_id}",
                        "format": format_spec["format"],
                        "sampleRate": format_spec["sampleRate"],
                        "bitrate": format_spec["bitrate"],
                        "fileSize": format_spec["fileSize"],
                    }
                )
                if track_metadata.lyrics_entry is not None:
                    lyrics_catalog[track_id] = track_metadata.lyrics_entry

                for genre_name in track_metadata.genres:
                    genre_key = genre_name.casefold()
                    if genre_key not in genre_albums:
                        genre_albums[genre_key] = (genre_name, {album_id}, 1)
                    else:
                        canonical_name, album_set, track_count = genre_albums[genre_key]
                        album_set.add(album_id)
                        genre_albums[genre_key] = (canonical_name, album_set, track_count + 1)

            representative = album_track_entries[0][1]
            mb_per_minute = max(
                MIN_MB_PER_MINUTE,
                (representative.file_size_bytes / BYTES_PER_MEGABYTE)
                / max(MIN_DURATION_MINUTES_FOR_DENSITY, representative.duration_seconds / SECONDS_PER_MINUTE),
            )
            album_tech_specs[album_id] = {
                "format": representative.format_label,
                "sampleRate": format_sample_rate_label(representative.sample_rate_hz),
                "bitrate": format_bitrate_label(representative.bitrate_kbps),
                "mbPerMin": round(mb_per_minute, 1),
            }

            added_at = round(
                max(track_metadata.file_mtime_seconds for _track_id, track_metadata in album_track_entries),
                3,
            )
            album_added_at_by_id[album_id] = added_at
            albums_list.append(
                {
                    "id": album_id,
                    "title": current_album["title"],
                    "artist": current_album["artist"],
                    "genre": current_album["genre"],
                    "year": current_album["year"],
                    "motif": motif,
                    "coverUrl": cover_url,
                    "isFavorite": album_id in favorite_album_ids,
                    "addedAt": added_at,
                    "tracks": album_tracks_payload,
                }
            )

            artist_key = current_album["artist"].casefold()
            if artist_key not in artist_album_ids:
                artist_album_ids[artist_key] = (current_album["artist"], {album_id})
            else:
                artist_album_ids[artist_key][1].add(album_id)

        all_tracks_list.sort(key=lambda track_entry: track_entry["title"].casefold())

        artists_list = []
        for artist_key, (canonical_name, album_set) in sorted(
            artist_album_ids.items(),
            key=lambda artist_pair: artist_pair[1][0].casefold(),
        ):
            artist_id = compute_stable_id(f"artist:{artist_key}")
            artist_added_at = round(
                max((album_added_at_by_id.get(current_id, 0.0) for current_id in album_set), default=0.0),
                3,
            )
            artists_list.append(
                {
                    "id": artist_id,
                    "name": canonical_name,
                    "countLabel": str(len(album_set)),
                    "albumIds": sorted(
                        album_set,
                        key=lambda current_id: album_metadata[current_id]["title"].casefold(),
                    ),
                    "isFavorite": artist_id in favorite_artist_ids,
                    "addedAt": artist_added_at,
                }
            )

        genres_list = [
            {
                "id": compute_stable_id(f"genre:{genre_key}"),
                "name": canonical_name,
                "countLabel": f"{track_count:,}",
                "albumIds": sorted(album_set, key=lambda current_id: album_metadata[current_id]["title"].casefold()),
            }
            for genre_key, (canonical_name, album_set, track_count) in sorted(
                genre_albums.items(),
                key=lambda genre_pair: genre_pair[1][0].casefold(),
            )
        ]

        valid_track_ids = set(track_paths_by_id.keys())
        active_playlists = [
            {
                "id": playlist_entry["id"],
                "name": playlist_entry["name"],
                "trackIds": [track_id for track_id in playlist_entry["trackIds"] if track_id in valid_track_ids],
            }
            for playlist_entry in stored_playlists
        ]

        favorite_tracks_count = sum(1 for track_entry in all_tracks_list if track_entry["isFavorite"])
        favorite_albums_count = sum(1 for album_entry in albums_list if album_entry["isFavorite"])
        catalog_snapshot = {
            "source": "local",
            "musicRoot": str(self.music_directory),
            "counts": {
                "tracks": len(all_tracks_list),
                "albums": len(albums_list),
                "artists": len(artists_list),
                "genres": len(genres_list),
                "favorites": favorite_tracks_count + favorite_albums_count,
                "favoriteTracks": favorite_tracks_count,
                "favoriteAlbums": favorite_albums_count,
                "playlists": len(active_playlists),
            },
            "albums": albums_list,
            "artists": artists_list,
            "genres": genres_list,
            "allTracks": all_tracks_list,
            "playlists": active_playlists,
            "albumTechSpecs": album_tech_specs,
            "lyricsCatalog": lyrics_catalog,
        }
        return catalog_snapshot, track_paths_by_id, covers_by_album_id

    def _perform_scan(self) -> dict[str, Any]:
        """Run the two-pass read-only scan and update live boot state throughout."""
        scan_started_at = time.monotonic()
        music_root_exists = self.music_directory.is_dir()

        with self._lock:
            self._music_root_exists = music_root_exists
            self._boot_phase = "discovering"
            self._boot_ready = False
            self._boot_progress = BOOT_INITIAL_DISCOVER_PROGRESS
            self._discovered_files = 0
            self._scanned_files = 0
            self._boot_error = None
            favorite_snapshot = set(self._favorite_track_ids)
            favorite_album_snapshot = set(self._favorite_album_ids)
            favorite_artist_snapshot = set(self._favorite_artist_ids)
            playlists_snapshot = list(self._playlists)

        discovered_paths: list[pathlib.Path] = []
        if music_root_exists:
            for current_root, subdirectories, filenames in os.walk(self.music_directory):
                subdirectories[:] = sorted(name for name in subdirectories if not name.startswith("."))
                root_path = pathlib.Path(current_root)
                for filename in sorted(filenames):
                    if filename.startswith("."):
                        continue
                    candidate_path = root_path / filename
                    if candidate_path.suffix.lower() in SUPPORTED_AUDIO_EXTENSIONS:
                        discovered_paths.append(candidate_path)

        total_discovered = len(discovered_paths)
        with self._lock:
            self._discovered_files = total_discovered
            self._boot_phase = "scanning"
            self._boot_progress = BOOT_SCAN_BASE_PROGRESS
            self._scan_duration_seconds = round(max(0.0, time.monotonic() - scan_started_at), 4)

        parsed_tracks: list[tuple[str, ParsedAudioMetadata]] = []
        for file_index, candidate_path in enumerate(discovered_paths, start=1):
            metadata = parse_audio_file_metadata(candidate_path)
            if metadata is not None:
                relative_posix = candidate_path.relative_to(self.music_directory).as_posix()
                track_id = compute_stable_id(relative_posix)
                parsed_tracks.append((track_id, metadata))
            with self._lock:
                self._scanned_files = file_index
                self._boot_progress = round(
                    BOOT_SCAN_BASE_PROGRESS + BOOT_SCAN_PROGRESS_SPAN * (file_index / total_discovered),
                    4,
                )
                self._scan_duration_seconds = round(max(0.0, time.monotonic() - scan_started_at), 4)

        catalog_snapshot, track_paths_by_id, covers_by_album_id = self._build_catalog_snapshot(
            parsed_tracks,
            favorite_snapshot,
            favorite_album_snapshot,
            favorite_artist_snapshot,
            playlists_snapshot,
        )

        with self._scan_condition:
            self._track_paths_by_id = track_paths_by_id
            self._album_ids = {album_entry["id"] for album_entry in catalog_snapshot["albums"]}
            self._artist_ids = {artist_entry["id"] for artist_entry in catalog_snapshot["artists"]}
            self._covers_by_album_id = covers_by_album_id
            self._catalog = catalog_snapshot
            self._has_catalog = True
            self._boot_phase = "ready"
            self._boot_ready = True
            self._boot_progress = BOOT_READY_PROGRESS
            self._music_root_exists = music_root_exists
            self._boot_counts = dict(catalog_snapshot["counts"])
            self._scan_duration_seconds = round(max(0.0, time.monotonic() - scan_started_at), 4)
            self._boot_error = None
            self._scan_condition.notify_all()

        return catalog_snapshot

    def _run_scan_worker(self) -> None:
        try:
            with self._scan_mutex:
                self._perform_scan()
        except Exception as scan_exception:
            with self._scan_condition:
                self._boot_error = str(scan_exception)
                self._boot_phase = "ready"
                self._boot_ready = True
                self._boot_progress = BOOT_READY_PROGRESS
                self._scan_condition.notify_all()

    def scan(self) -> dict[str, Any]:
        """Synchronously walk music_directory read-only and rebuild the DialOS catalog."""
        with self._scan_mutex:
            with self._lock:
                self._scan_id += 1
                self._boot_phase = "discovering"
                self._boot_ready = False
                self._boot_progress = BOOT_INITIAL_DISCOVER_PROGRESS
                self._discovered_files = 0
                self._scanned_files = 0
                self._boot_error = None
                self._music_root_exists = self.music_directory.is_dir()
                self._scan_duration_seconds = 0.0
                self._boot_counts = build_empty_counts()
            return self._perform_scan()

    def start_async_scan(self, force_rescan: bool = True) -> dict[str, Any]:
        """Start an asynchronous scan unless one is already running or cached."""
        with self._lock:
            if self._boot_phase in ACTIVE_SCAN_PHASES:
                return self._snapshot_boot_status_locked()
            if not force_rescan and self._boot_phase == "ready" and self._has_catalog:
                return self._snapshot_boot_status_locked()

            self._scan_id += 1
            self._boot_phase = "discovering"
            self._boot_ready = False
            self._boot_progress = BOOT_INITIAL_DISCOVER_PROGRESS
            self._discovered_files = 0
            self._scanned_files = 0
            self._boot_error = None
            self._music_root_exists = self.music_directory.is_dir()
            self._scan_duration_seconds = 0.0
            self._boot_counts = build_empty_counts()
            snapshot = self._snapshot_boot_status_locked()

        scan_thread = threading.Thread(target=self._run_scan_worker, daemon=True)
        scan_thread.start()
        return snapshot

    def ensure_catalog(self) -> dict[str, Any]:
        """Return a valid catalog, waiting on an in-flight scan or running one if needed."""
        with self._scan_condition:
            while self._boot_phase in ACTIVE_SCAN_PHASES:
                self._scan_condition.wait()
            if self._has_catalog:
                return self._catalog

        with self._scan_mutex:
            with self._lock:
                if self._has_catalog:
                    return self._catalog
                self._scan_id += 1
            return self._perform_scan()

    def serialize_catalog(self) -> dict[str, Any]:
        """Return the latest scanned catalog snapshot."""
        with self._lock:
            return self._catalog

    def get_track_path(self, track_id: str) -> pathlib.Path | None:
        """Resolve an audio track ID to its validated on-disk path."""
        with self._lock:
            return self._track_paths_by_id.get(track_id)

    def get_album_cover(self, album_id: str) -> SidecarCoverReference | EmbeddedCoverReference | None:
        """Resolve an album ID to its sidecar or embedded cover artwork reference."""
        with self._lock:
            return self._covers_by_album_id.get(album_id)

    def set_favorite(self, track_id: str, is_favorite: bool | None) -> bool | None:
        """Toggle or explicitly set a track's favorite flag and persist atomically."""
        with self._lock:
            if track_id not in self._track_paths_by_id:
                return None
            next_state = (track_id not in self._favorite_track_ids) if is_favorite is None else bool(is_favorite)
            if next_state:
                self._favorite_track_ids.add(track_id)
            else:
                self._favorite_track_ids.discard(track_id)
            self._persist_user_state_atomically()

            favorite_tracks_count = 0
            for track_entry in self._catalog.get("allTracks", []):
                if track_entry["id"] == track_id:
                    track_entry["isFavorite"] = next_state
                if track_entry["isFavorite"]:
                    favorite_tracks_count += 1
            favorite_albums_count = 0
            for album_entry in self._catalog.get("albums", []):
                if album_entry.get("isFavorite"):
                    favorite_albums_count += 1
                for album_track in album_entry["tracks"]:
                    if album_track["id"] == track_id:
                        album_track["isFavorite"] = next_state
            if "counts" in self._catalog:
                self._catalog["counts"]["favoriteTracks"] = favorite_tracks_count
                self._catalog["counts"]["favoriteAlbums"] = favorite_albums_count
                self._catalog["counts"]["favorites"] = favorite_tracks_count + favorite_albums_count
                self._boot_counts = dict(self._catalog["counts"])
            return next_state

    def set_album_favorite(self, album_id: str, is_favorite: bool | None) -> bool | None:
        """Toggle or explicitly set an album's favorite flag independently of track favorites."""
        with self._lock:
            if album_id not in self._album_ids:
                return None
            next_state = (album_id not in self._favorite_album_ids) if is_favorite is None else bool(is_favorite)
            if next_state:
                self._favorite_album_ids.add(album_id)
            else:
                self._favorite_album_ids.discard(album_id)
            self._persist_user_state_atomically()

            favorite_albums_count = 0
            for album_entry in self._catalog.get("albums", []):
                if album_entry["id"] == album_id:
                    album_entry["isFavorite"] = next_state
                if album_entry.get("isFavorite"):
                    favorite_albums_count += 1
            favorite_tracks_count = sum(
                1 for track_entry in self._catalog.get("allTracks", []) if track_entry.get("isFavorite")
            )
            if "counts" in self._catalog:
                self._catalog["counts"]["favoriteTracks"] = favorite_tracks_count
                self._catalog["counts"]["favoriteAlbums"] = favorite_albums_count
                self._catalog["counts"]["favorites"] = favorite_tracks_count + favorite_albums_count
                self._boot_counts = dict(self._catalog["counts"])
            return next_state

    def set_artist_favorite(self, artist_id: str, is_favorite: bool | None) -> bool | None:
        """Toggle or explicitly set an artist's favorite flag without affecting Library/Favorites."""
        with self._lock:
            if artist_id not in self._artist_ids:
                return None
            next_state = (artist_id not in self._favorite_artist_ids) if is_favorite is None else bool(is_favorite)
            if next_state:
                self._favorite_artist_ids.add(artist_id)
            else:
                self._favorite_artist_ids.discard(artist_id)
            self._persist_user_state_atomically()

            for artist_entry in self._catalog.get("artists", []):
                if artist_entry["id"] == artist_id:
                    artist_entry["isFavorite"] = next_state
            return next_state

    def replace_playlists(self, playlists_payload: list[dict[str, Any]]) -> list[dict[str, Any]]:
        """Validate and atomically persist the user's playlists."""
        with self._lock:
            valid_track_ids = set(self._track_paths_by_id.keys())
            sanitized: list[dict[str, Any]] = []
            for entry in playlists_payload:
                if not isinstance(entry, dict):
                    continue
                playlist_id = str(entry.get("id", "")).strip()
                playlist_name = str(entry.get("name", "")).strip()
                if not playlist_id or not playlist_name:
                    continue
                track_ids = [
                    str(track_id)
                    for track_id in entry.get("trackIds", [])
                    if str(track_id) in valid_track_ids
                ]
                sanitized.append({"id": playlist_id, "name": playlist_name, "trackIds": track_ids})
            self._playlists = sanitized
            self._persist_user_state_atomically()
            if self._catalog:
                self._catalog["playlists"] = sanitized
                self._catalog["counts"]["playlists"] = len(sanitized)
                self._boot_counts = dict(self._catalog["counts"])
            return sanitized


class ReusableThreadingHTTPServer(http.server.ThreadingHTTPServer):
    """ThreadingHTTPServer configured with reusable socket addresses and daemon threads."""

    allow_reuse_address = True
    daemon_threads = True


def create_simulator_http_server(
    *,
    host: str,
    port: int,
    web_directory: pathlib.Path,
    library_store: SimulatorLibraryStore,
) -> http.server.ThreadingHTTPServer:
    """Create a multi-threaded local HTTP server serving the DialOS Simulator UI and API."""
    resolved_web_directory = web_directory.expanduser().resolve()

    class SimulatorRequestHandler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, format_string: str, *arguments: Any) -> None:
            return

        def ensure_request_body_settled(self) -> None:
            if self.command != "POST" or getattr(self, "_request_body_consumed", False):
                return
            if self.headers.get("Transfer-Encoding"):
                self.close_connection = True
                return
            content_length_text = self.headers.get("Content-Length", "0").strip()
            try:
                content_length = int(content_length_text or "0")
            except ValueError:
                self.close_connection = True
                return
            if content_length < 0 or content_length > HTTP_MAX_REQUEST_BODY_BYTES:
                self.close_connection = True
                return
            if content_length > 0:
                try:
                    remaining_bytes = self.rfile.read(content_length)
                except OSError:
                    self.close_connection = True
                    return
                if len(remaining_bytes) < content_length:
                    self.close_connection = True
                    return
            self._request_body_consumed = True

        def send_json_response(
            self,
            status_code: int,
            body_object: dict[str, Any],
            *,
            head_only: bool = False,
        ) -> None:
            self.ensure_request_body_settled()
            encoded_body = json.dumps(body_object, ensure_ascii=False).encode("utf-8")
            try:
                self.send_response(status_code)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(encoded_body)))
                self.send_header("Cache-Control", "no-store")
                if self.close_connection:
                    self.send_header("Connection", "close")
                self.end_headers()
                if not head_only:
                    self.wfile.write(encoded_body)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                return

        def send_bytes_response(
            self,
            status_code: int,
            content_type: str,
            payload_bytes: bytes,
            *,
            cache_control: str = "no-store",
            head_only: bool = False,
        ) -> None:
            self.ensure_request_body_settled()
            try:
                self.send_response(status_code)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(payload_bytes)))
                self.send_header("Cache-Control", cache_control)
                if self.close_connection:
                    self.send_header("Connection", "close")
                self.end_headers()
                if not head_only:
                    self.wfile.write(payload_bytes)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                return

        def stream_audio_file(self, audio_file_path: pathlib.Path, *, head_only: bool = False) -> None:
            try:
                total_size = audio_file_path.stat().st_size
            except OSError:
                self.send_json_response(HTTP_NOT_FOUND, {"error": "audio_not_found"}, head_only=head_only)
                return

            content_type = AUDIO_MIME_TYPES.get(audio_file_path.suffix.lower(), "application/octet-stream")
            range_header = self.headers.get("Range")

            start_byte = 0
            end_byte = max(0, total_size - 1)
            status_code = HTTP_OK

            if range_header:
                parsed_range = parse_http_range_header(range_header, total_size)
                if parsed_range is None:
                    try:
                        self.send_response(HTTP_RANGE_NOT_SATISFIABLE)
                        self.send_header("Content-Range", f"bytes */{total_size}")
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                    except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                        return
                    return
                start_byte, end_byte = parsed_range
                status_code = HTTP_PARTIAL_CONTENT

            bytes_to_send = (end_byte - start_byte + 1) if total_size > 0 else 0
            try:
                self.send_response(status_code)
                self.send_header("Content-Type", content_type)
                self.send_header("Accept-Ranges", "bytes")
                self.send_header("Content-Length", str(bytes_to_send))
                if status_code == HTTP_PARTIAL_CONTENT:
                    self.send_header("Content-Range", f"bytes {start_byte}-{end_byte}/{total_size}")
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()

                if head_only or bytes_to_send == 0:
                    return

                with audio_file_path.open("rb") as audio_stream:
                    audio_stream.seek(start_byte)
                    remaining = bytes_to_send
                    while remaining > 0:
                        chunk = audio_stream.read(min(STREAM_CHUNK_BYTES, remaining))
                        if not chunk:
                            break
                        self.wfile.write(chunk)
                        remaining -= len(chunk)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                return

        def serve_cover_image(self, album_id: str, *, head_only: bool = False) -> None:
            cover_reference = library_store.get_album_cover(album_id)
            if cover_reference is None:
                self.send_json_response(HTTP_NOT_FOUND, {"error": "cover_not_found"}, head_only=head_only)
                return
            try:
                if isinstance(cover_reference, SidecarCoverReference):
                    image_bytes = cover_reference.file_path.read_bytes()
                else:
                    with cover_reference.file_path.open("rb") as audio_stream:
                        audio_stream.seek(cover_reference.byte_offset)
                        image_bytes = audio_stream.read(cover_reference.byte_length)
            except OSError:
                self.send_json_response(HTTP_NOT_FOUND, {"error": "cover_unreadable"}, head_only=head_only)
                return
            self.send_bytes_response(
                HTTP_OK,
                cover_reference.mime_type,
                image_bytes,
                cache_control="public, max-age=86400",
                head_only=head_only,
            )

        def resolve_static_file_path(self, route_path: str) -> pathlib.Path | None:
            if route_path in ("/", "/index.html"):
                index_file = resolved_web_directory / "index.html"
                return index_file if index_file.is_file() else None

            encoded_relative = route_path.lstrip("/")
            unquoted_relative = urllib.parse.unquote(encoded_relative)
            for path_variant in (encoded_relative, unquoted_relative):
                if not path_variant or "\x00" in path_variant or "\\" in path_variant:
                    return None
                if ".." in path_variant.split("/"):
                    return None

            candidate_path = (resolved_web_directory / unquoted_relative).resolve()
            try:
                candidate_path.relative_to(resolved_web_directory)
            except ValueError:
                return None
            return candidate_path if candidate_path.is_file() else None

        def extract_safe_identifier(self, route_path: str, prefix: str) -> str | None:
            encoded_identifier = route_path[len(prefix) :]
            if not encoded_identifier or "\\" in encoded_identifier or ".." in encoded_identifier:
                return None
            decoded_identifier = urllib.parse.unquote(encoded_identifier)
            if not SAFE_IDENTIFIER_PATTERN.fullmatch(decoded_identifier):
                return None
            return decoded_identifier

        def handle_get_or_head(self, *, head_only: bool) -> None:
            parsed_url = urllib.parse.urlsplit(self.path)
            route_path = parsed_url.path

            if route_path == "/api/boot/status":
                self.send_json_response(HTTP_OK, library_store.get_boot_status(), head_only=head_only)
                return

            if route_path == "/api/health":
                boot_status = library_store.get_boot_status()
                self.send_json_response(
                    HTTP_OK,
                    {
                        "ok": True,
                        "musicRoot": boot_status["musicRoot"],
                        "musicRootExists": boot_status["musicRootExists"],
                        "phase": boot_status["phase"],
                        "counts": boot_status["counts"],
                    },
                    head_only=head_only,
                )
                return

            if route_path == "/api/library":
                catalog = library_store.ensure_catalog()
                self.send_json_response(HTTP_OK, catalog, head_only=head_only)
                return

            if route_path.startswith("/api/cover/"):
                album_id = self.extract_safe_identifier(route_path, "/api/cover/")
                if album_id is None:
                    self.send_json_response(HTTP_NOT_FOUND, {"error": "cover_not_found"}, head_only=head_only)
                    return
                self.serve_cover_image(album_id, head_only=head_only)
                return

            if route_path.startswith("/api/audio/"):
                track_id = self.extract_safe_identifier(route_path, "/api/audio/")
                if track_id is None:
                    self.send_json_response(HTTP_NOT_FOUND, {"error": "audio_not_found"}, head_only=head_only)
                    return
                audio_path = library_store.get_track_path(track_id)
                if audio_path is None:
                    self.send_json_response(HTTP_NOT_FOUND, {"error": "audio_not_found"}, head_only=head_only)
                    return
                self.stream_audio_file(audio_path, head_only=head_only)
                return

            static_file_path = self.resolve_static_file_path(route_path)
            if static_file_path is not None:
                extension = static_file_path.suffix.lower()
                mime_type = (
                    STATIC_MIME_TYPES.get(extension)
                    or mimetypes.guess_type(static_file_path.name)[0]
                    or "application/octet-stream"
                )
                try:
                    file_bytes = static_file_path.read_bytes()
                except OSError:
                    self.send_json_response(HTTP_NOT_FOUND, {"error": "not_found"}, head_only=head_only)
                    return
                self.send_bytes_response(
                    HTTP_OK,
                    mime_type,
                    file_bytes,
                    cache_control="no-store",
                    head_only=head_only,
                )
                return

            self.send_json_response(HTTP_NOT_FOUND, {"error": "not_found"}, head_only=head_only)

        def do_GET(self) -> None:
            self.handle_get_or_head(head_only=False)

        def do_HEAD(self) -> None:
            self.handle_get_or_head(head_only=True)

        def read_json_body(self) -> dict[str, Any] | None:
            if self.headers.get("Transfer-Encoding"):
                self.close_connection = True
                return None
            content_length_text = self.headers.get("Content-Length", "0").strip()
            try:
                content_length = int(content_length_text or "0")
            except ValueError:
                self.close_connection = True
                return None
            if content_length < 0 or content_length > HTTP_MAX_REQUEST_BODY_BYTES:
                self.close_connection = True
                return None
            if content_length == 0:
                self._request_body_consumed = True
                return {}
            try:
                body_bytes = self.rfile.read(content_length)
            except OSError:
                self.close_connection = True
                return None
            if len(body_bytes) < content_length:
                self.close_connection = True
                return None
            self._request_body_consumed = True
            try:
                decoded = json.loads(body_bytes.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                return None
            return decoded if isinstance(decoded, dict) else None

        def handle_favorite_entity_post(
            self,
            route_path: str,
            prefix: str,
            not_found_error: str,
            id_field_name: str,
            setter: Any,
        ) -> None:
            entity_id = self.extract_safe_identifier(route_path, prefix)
            if entity_id is None:
                self.send_json_response(HTTP_NOT_FOUND, {"error": not_found_error})
                return
            request_body = self.read_json_body()
            if request_body is None:
                self.send_json_response(HTTP_BAD_REQUEST, {"error": "invalid_json"})
                return
            requested_state = request_body.get("isFavorite")
            if requested_state is not None and not isinstance(requested_state, bool):
                self.send_json_response(HTTP_BAD_REQUEST, {"error": "invalid_favorite_payload"})
                return
            updated_state = setter(entity_id, requested_state)
            if updated_state is None:
                self.send_json_response(HTTP_NOT_FOUND, {"error": not_found_error})
                return
            self.send_json_response(HTTP_OK, {id_field_name: entity_id, "isFavorite": updated_state})

        def do_POST(self) -> None:
            self._request_body_consumed = False
            parsed_url = urllib.parse.urlsplit(self.path)
            route_path = parsed_url.path

            if route_path == "/api/boot":
                request_body = self.read_json_body()
                if request_body is None:
                    self.send_json_response(HTTP_BAD_REQUEST, {"error": "invalid_json"})
                    return
                rescan_value = request_body.get("rescan", True)
                if rescan_value is not None and not isinstance(rescan_value, bool):
                    self.send_json_response(HTTP_BAD_REQUEST, {"error": "invalid_boot_payload"})
                    return
                force_rescan = True if rescan_value is None else rescan_value
                boot_snapshot = library_store.start_async_scan(force_rescan=force_rescan)
                status_code = HTTP_OK if boot_snapshot["ready"] else HTTP_ACCEPTED
                self.send_json_response(status_code, boot_snapshot)
                return

            if route_path == "/api/rescan":
                catalog = library_store.scan()
                self.send_json_response(HTTP_OK, catalog)
                return

            if route_path.startswith("/api/favorites/"):
                self.handle_favorite_entity_post(
                    route_path,
                    "/api/favorites/",
                    "track_not_found",
                    "trackId",
                    library_store.set_favorite,
                )
                return

            if route_path.startswith("/api/favorite-albums/"):
                self.handle_favorite_entity_post(
                    route_path,
                    "/api/favorite-albums/",
                    "album_not_found",
                    "albumId",
                    library_store.set_album_favorite,
                )
                return

            if route_path.startswith("/api/favorite-artists/"):
                self.handle_favorite_entity_post(
                    route_path,
                    "/api/favorite-artists/",
                    "artist_not_found",
                    "artistId",
                    library_store.set_artist_favorite,
                )
                return

            if route_path == "/api/playlists":
                request_body = self.read_json_body()
                if request_body is None or not isinstance(request_body.get("playlists"), list):
                    self.send_json_response(HTTP_BAD_REQUEST, {"error": "invalid_playlists_payload"})
                    return
                saved_playlists = library_store.replace_playlists(request_body["playlists"])
                self.send_json_response(HTTP_OK, {"playlists": saved_playlists})
                return

            self.send_json_response(HTTP_NOT_FOUND, {"error": "not_found"})

    return ReusableThreadingHTTPServer((host, port), SimulatorRequestHandler)


def build_argument_parser() -> argparse.ArgumentParser:
    """Create CLI argument parser for the DialOS local simulator server."""
    default_state_path = pathlib.Path.home() / ".cache" / "dialos-simulator" / "state.json"

    parser = argparse.ArgumentParser(
        description="Serve DialOS Simulator UI wired to a local music library (strictly read-only)."
    )
    parser.add_argument(
        "--music",
        type=pathlib.Path,
        default=pathlib.Path.home() / "Music",
        help="Path to the local music directory (default: ~/Music)",
    )
    parser.add_argument(
        "--host",
        default=DEFAULT_HTTP_HOST,
        help=f"Bind host address (default: {DEFAULT_HTTP_HOST})",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_HTTP_PORT,
        help=f"Bind TCP port (default: {DEFAULT_HTTP_PORT})",
    )
    parser.add_argument(
        "--state-file",
        type=pathlib.Path,
        default=default_state_path,
        help="Path to local JSON state file for favorites/playlists (outside ~/Music)",
    )
    parser.add_argument(
        "--open",
        action="store_true",
        default=False,
        dest="open_browser",
        help="Open the DialOS Simulator URL in the default browser after startup",
    )
    parser.add_argument(
        "--no-open",
        action="store_false",
        dest="open_browser",
        help="Do not automatically open a browser tab after startup",
    )
    return parser


def main() -> None:
    """Entry point for starting the DialOS local simulator server."""
    arguments = build_argument_parser().parse_args()
    web_directory = pathlib.Path(__file__).resolve().parent / "web"
    library_store = SimulatorLibraryStore(
        music_directory=arguments.music,
        state_file_path=arguments.state_file,
    )
    http_server = create_simulator_http_server(
        host=arguments.host,
        port=arguments.port,
        web_directory=web_directory,
        library_store=library_store,
    )
    bound_host, bound_port = http_server.server_address
    server_url = f"http://{bound_host}:{bound_port}/"
    print(f"[DialOS Simulator] Music library: {library_store.music_directory} (read-only)", flush=True)
    print(f"[DialOS Simulator] Listening on {server_url}  (Ctrl+C to stop)", flush=True)

    if arguments.open_browser:
        webbrowser.open(server_url)

    try:
        http_server.serve_forever()
    except KeyboardInterrupt:
        print("\n[DialOS Simulator] Shutting down.")
    finally:
        http_server.server_close()


if __name__ == "__main__":
    main()
