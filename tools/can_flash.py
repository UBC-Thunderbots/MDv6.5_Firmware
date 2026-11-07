#!/usr/bin/env python3
"""STM32 CAN Bootloader host tool.

Commands:
  flash <file.bin>  — Send new firmware over CAN, then boot it
  boot              — Tell the bootloader to jump to the existing app

Protocol (must match BootloaderFW/Core/Src/main.c):
  0x100 BOOT : no payload — jump to existing app
  0x101 START: bytes 0-3 = image size (uint32 LE, multiple of 8, <= 256 KiB)
  0x102 DATA : 8 raw firmware bytes, in address order
  0x103 END  : bytes 0-3 = CRC-32/MPEG-2 of the padded image (uint32 LE)
"""
import argparse
import struct
import sys
import time

try:
    import can
except ImportError:
    print("Error: python-can is required. Install it with:")
    print("  pip install python-can")
    sys.exit(1)

BOOT_ID  = 0x100
START_ID = 0x101
DATA_ID  = 0x102
END_ID   = 0x103
MAX_IMAGE = 0x40000  # 256 KiB, Bank2
PAD_BYTE = 0xFF      # erased-flash value


def _build_table():
    table = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) if (c & 0x80000000) else (c << 1)
            c &= 0xFFFFFFFF
        table.append(c)
    return table


_TABLE = _build_table()


def crc32_mpeg2(data: bytes) -> int:
    """Same result as the STM32 hardware CRC in its default config (no reflection, no final XOR)."""
    crc = 0xFFFFFFFF
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _TABLE[((crc >> 24) ^ b) & 0xFF]
    return crc


assert crc32_mpeg2(b"123456789") == 0x0376E6E7, "CRC self-test failed"


def progress_bar(current, total, bar_len=40, elapsed=0):
    fraction = current / total
    filled = int(bar_len * fraction)
    bar = '#' * filled + '-' * (bar_len - filled)
    speed = current * 8 / elapsed if elapsed > 0 else 0
    sys.stdout.write(
        f"\r  [{bar}] {fraction * 100:5.1f}%  "
        f"{current}/{total} chunks  "
        f"{speed:.0f} B/s"
    )
    sys.stdout.flush()


def open_bus(args):
    try:
        return can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)
    except Exception as e:
        sys.exit(f"Error opening CAN bus: {e}")


def cmd_boot(args):
    """Send BOOT command to jump to existing app."""
    print("Sending BOOT command (0x100)...")
    bus = open_bus(args)
    try:
        bus.send(can.Message(arbitration_id=BOOT_ID, data=[], is_extended_id=False))
        print("BOOT sent. Board should jump to application.")
    finally:
        bus.shutdown()


def cmd_flash(args):
    """Flash new firmware over CAN."""
    # --- Read and prepare firmware ---
    with open(args.firmware, "rb") as f:
        image = f.read()

    original_size = len(image)
    image += bytes([PAD_BYTE]) * ((-len(image)) % 8)
    total_size = len(image)
    num_chunks = total_size // 8

    if total_size == 0 or total_size > MAX_IMAGE:
        sys.exit(f"Error: image is {total_size} bytes, must be 1..{MAX_IMAGE}")

    crc = crc32_mpeg2(image)

    # --- Print firmware info ---
    print("=" * 55)
    print("  STM32 CAN Bootloader - Firmware Flasher")
    print("=" * 55)
    print(f"  File     : {args.firmware}")
    print(f"  Size     : {original_size} bytes ({original_size / 1024:.1f} KB)")
    if total_size != original_size:
        print(f"  Padded   : {total_size} bytes (+{total_size - original_size} bytes)")
    print(f"  Chunks   : {num_chunks} x 8 bytes")
    print(f"  CRC-32   : 0x{crc:08X}")
    print(f"  Interface: {args.interface}")
    print(f"  Channel  : {args.channel}")
    print(f"  Bitrate  : {args.bitrate}")
    print("=" * 55)

    if args.dry_run:
        print("\nDry run - nothing sent.")
        return

    bus = open_bus(args)

    try:
        # --- START ---
        print(f"\n[1/3] Sending START ({total_size} bytes)...")
        start_payload = struct.pack("<I", total_size) + bytes(4)
        bus.send(can.Message(arbitration_id=START_ID, data=start_payload, is_extended_id=False))

        print(f"      Waiting {args.erase_wait}s for flash erase...")
        time.sleep(args.erase_wait)

        # --- DATA ---
        print(f"[2/3] Sending {num_chunks} data chunks...")
        t_start = time.time()

        for n in range(num_chunks):
            chunk = image[8 * n : 8 * n + 8]
            bus.send(can.Message(arbitration_id=DATA_ID, data=chunk, is_extended_id=False))
            time.sleep(args.frame_delay)
            progress_bar(n + 1, num_chunks, elapsed=time.time() - t_start)

        t_elapsed = time.time() - t_start
        print()  # newline after progress bar

        # --- END ---
        print(f"[3/3] Sending END (CRC: 0x{crc:08X})...")
        end_payload = struct.pack("<I", crc) + bytes(4)
        bus.send(can.Message(arbitration_id=END_ID, data=end_payload, is_extended_id=False))
        time.sleep(0.5)  # let the frame go out before shutdown

        # --- Summary ---
        throughput = total_size / t_elapsed if t_elapsed > 0 else 0
        print()
        print("-" * 55)
        print(f"  Transfer complete!")
        print(f"  Time       : {t_elapsed:.2f}s")
        print(f"  Speed      : {throughput:.0f} bytes/s ({throughput / 1024:.1f} KB/s)")
        print(f"  Effective  : {throughput * 8 / 1000:.1f} kbit/s")
        print("-" * 55)
        print()
        print("Board should jump to new app. 10 fast blinks = CRC failure.")

    except KeyboardInterrupt:
        print("\n\nTransfer aborted by user.")
        sys.exit(1)
    finally:
        bus.shutdown()


def main():
    ap = argparse.ArgumentParser(
        description="STM32 CAN Bootloader host tool",
    )

    # Common CAN arguments
    ap.add_argument("-i", "--interface", default="slcan",
                    help="python-can interface (default: slcan). "
                         "Examples: slcan, socketcan, pcan, ixxat")
    ap.add_argument("-c", "--channel", default="COM5",
                    help="CAN channel/device (default: COM5). "
                         "Examples: COM5, /dev/tty.usbmodemXXXX, can0, PCAN_USBBUS1")
    ap.add_argument("-b", "--bitrate", type=int, default=500000,
                    help="CAN bitrate in bps (default: 500000)")

    sub = ap.add_subparsers(dest="command", required=True)

    # boot subcommand
    sub.add_parser("boot", help="Tell bootloader to jump to existing app")

    # flash subcommand
    flash_p = sub.add_parser("flash", help="Flash new firmware over CAN")
    flash_p.add_argument("firmware", help="Path to .bin firmware file")
    flash_p.add_argument("-d", "--frame-delay", type=float, default=0.005,
                         help="seconds between DATA frames (default: 0.005)")
    flash_p.add_argument("--erase-wait", type=float, default=5.0,
                         help="seconds to wait after START for flash erase (default: 5.0)")
    flash_p.add_argument("--dry-run", action="store_true",
                         help="check size and CRC without sending anything")

    args = ap.parse_args()

    if args.command == "boot":
        cmd_boot(args)
    elif args.command == "flash":
        cmd_flash(args)


if __name__ == "__main__":
    main()
