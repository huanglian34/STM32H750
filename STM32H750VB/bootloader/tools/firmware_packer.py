#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32H750 Bootloader Firmware Packaging Tool
-------------------------------------------
Pack raw application binary (.bin) with a secure 64-byte metadata header
including CRC32 checksum, semantic version, target slot, and build timestamp.
"""

import sys
import os
import struct
import time
import argparse
import zlib

FIRMWARE_MAGIC = 0x53544D32      # ASCII: 'STM2'
HEADER_VERSION = 0x00010000      # Header format v1.0
HEADER_SIZE = 64

# Target Slot Identifiers
SLOT_A = 0
SLOT_B = 1
SLOT_ANY = 0xFF

def calc_crc32(data: bytes) -> int:
    """Calculate standard IEEE 802.3 CRC32 (0xEDB88320 polynomial)"""
    return zlib.crc32(data) & 0xFFFFFFFF

def parse_version_string(ver_str: str) -> int:
    """Convert version 'x.y.z' to uint32 (e.g. '1.2.3' -> 0x01020300)"""
    parts = ver_str.strip().lstrip('vV').split('.')
    major = int(parts[0]) if len(parts) > 0 else 1
    minor = int(parts[1]) if len(parts) > 1 else 0
    patch = int(parts[2]) if len(parts) > 2 else 0
    return ((major & 0xFF) << 24) | ((minor & 0xFF) << 16) | ((patch & 0xFF) << 8)

def format_version(ver_int: int) -> str:
    major = (ver_int >> 24) & 0xFF
    minor = (ver_int >> 16) & 0xFF
    patch = (ver_int >> 8) & 0xFF
    return f"v{major}.{minor}.{patch}"

def pack_firmware(input_file: str, output_file: str, ver_str: str = "1.0.0", target_slot: int = SLOT_ANY, git_tag: str = ""):
    if not os.path.isfile(input_file):
        print(f"[ERROR] Input file not found: {input_file}")
        sys.exit(1)

    with open(input_file, 'rb') as f:
        payload = f.read()

    payload_size = len(payload)
    if payload_size == 0:
        print("[ERROR] Input binary is empty!")
        sys.exit(1)

    payload_crc = calc_crc32(payload)
    version_int = parse_version_string(ver_str)
    timestamp = int(time.time())

    # Format git hash/tag (8 bytes)
    git_bytes = git_tag.encode('utf-8')[:8].ljust(8, b'\x00')
    reserved = b'\x00' * 24

    # Build Header payload (first 60 bytes)
    # Format:
    # 0x00: uint32 magic
    # 0x04: uint32 header_ver
    # 0x08: uint32 fw_version
    # 0x0C: uint32 fw_size
    # 0x10: uint32 fw_crc32
    # 0x14: uint32 target_slot
    # 0x18: uint32 timestamp
    # 0x1C: 8 bytes git_bytes
    # 0x24: 24 bytes reserved
    header_partial = struct.pack(
        "<IIIIIII8s24s",
        FIRMWARE_MAGIC,
        HEADER_VERSION,
        version_int,
        payload_size,
        payload_crc,
        target_slot,
        timestamp,
        git_bytes,
        reserved
    )

    header_crc = calc_crc32(header_partial)
    full_header = header_partial + struct.pack("<I", header_crc)

    assert len(full_header) == HEADER_SIZE, f"Header size mismatch: {len(full_header)} vs {HEADER_SIZE}"

    with open(output_file, 'wb') as f:
        f.write(full_header)
        f.write(payload)

    print("============================================================")
    print("   STM32H750 Firmware Packaged Successfully!")
    print("============================================================")
    print(f" Input File   : {input_file} ({payload_size:,} bytes)")
    print(f" Output File  : {output_file} ({payload_size + HEADER_SIZE:,} bytes)")
    print(f" Magic        : 0x{FIRMWARE_MAGIC:08X} ('STM2')")
    print(f" Version      : {format_version(version_int)}")
    print(f" Payload Size : {payload_size} bytes")
    print(f" Payload CRC32: 0x{payload_crc:08X}")
    slot_names = {SLOT_A: "Slot A (0x90000000)", SLOT_B: "Slot B (0x903C0000)", SLOT_ANY: "Any Slot"}
    print(f" Target Slot  : {slot_names.get(target_slot, 'Unknown')}")
    print(f" Timestamp    : {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(timestamp))}")
    print(f" Header CRC32 : 0x{header_crc:08X}")
    print("============================================================")

def info_firmware(target_file: str):
    if not os.path.isfile(target_file):
        print(f"[ERROR] Target file not found: {target_file}")
        sys.exit(1)

    with open(target_file, 'rb') as f:
        header_data = f.read(HEADER_SIZE)
        payload = f.read()

    if len(header_data) < HEADER_SIZE:
        print("[ERROR] File is smaller than header size!")
        sys.exit(1)

    magic, header_ver, ver_int, fw_size, fw_crc, target_slot, ts, git_bytes, reserved, header_crc = struct.unpack(
        "<IIIIIII8s24sI", header_data
    )

    if magic != FIRMWARE_MAGIC:
        print(f"[WARNING] Magic 0x{magic:08X} does NOT match 'STM2' (0x{FIRMWARE_MAGIC:08X})!")
        print("This file appears to be a raw binary without a secure header.")
        return

    calc_hdr_crc = calc_crc32(header_data[:60])
    hdr_status = "[VALID]" if calc_hdr_crc == header_crc else "[INVALID! CORRUPTED]"

    calc_pl_crc = calc_crc32(payload[:fw_size])
    pl_status = "[VALID]" if calc_pl_crc == fw_crc else "[INVALID! MISMATCH]"

    print("============================================================")
    print("   STM32H750 Firmware Header Inspection")
    print("============================================================")
    print(f" Target File  : {target_file}")
    print(f" Magic        : 0x{magic:08X} (Valid 'STM2')")
    print(f" Version      : {format_version(ver_int)}")
    print(f" Payload Size : {fw_size:,} bytes (Actual payload: {len(payload):,} bytes)")
    print(f" Payload CRC32: 0x{fw_crc:08X} {pl_status}")
    slot_names = {SLOT_A: "Slot A (0x90000000)", SLOT_B: "Slot B (0x903C0000)", SLOT_ANY: "Any Slot"}
    print(f" Target Slot  : {slot_names.get(target_slot, 'Unknown')}")
    print(f" Timestamp    : {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(ts))}")
    print(f" Git Tag/Hash : {git_bytes.decode('utf-8', errors='ignore').strip()}")
    print(f" Header CRC32 : 0x{header_crc:08X} {hdr_status}")
    print("============================================================")

def main():
    parser = argparse.ArgumentParser(description="STM32H750 QSPI Bootloader Firmware Packager")
    subparsers = parser.add_subparsers(dest="command")

    # Pack command
    pack_parser = subparsers.add_parser("pack", help="Pack raw .bin into secure firmware with header")
    pack_parser.add_argument("input", help="Path to input raw binary file (.bin)")
    pack_parser.add_argument("output", help="Path to output packaged binary file (.bin)")
    pack_parser.add_argument("--version", "-v", default="1.0.0", help="Firmware version (e.g. 1.0.0)")
    pack_parser.add_argument("--slot", "-s", choices=["a", "b", "any"], default="any", help="Target installation slot")
    pack_parser.add_argument("--tag", "-t", default="", help="Optional git commit tag/hash")

    # Info command
    info_parser = subparsers.add_parser("info", help="Inspect and verify packaged firmware header")
    info_parser.add_argument("file", help="Path to packaged firmware file")

    args = parser.parse_args()

    if args.command == "pack":
        slot_map = {"a": SLOT_A, "b": SLOT_B, "any": SLOT_ANY}
        pack_firmware(args.input, args.output, args.version, slot_map[args.slot], args.tag)
    elif args.command == "info":
        info_firmware(args.file)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
