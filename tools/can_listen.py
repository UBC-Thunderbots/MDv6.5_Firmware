"""Listen for CAN frames and print them."""

import can
import sys

CHANNEL = "/dev/cu.usbmodem387C386630361"
BITRATE = 1_000_000

def main():
    channel = sys.argv[1] if len(sys.argv) > 1 else CHANNEL
    bus = can.interface.Bus(interface="slcan", channel=channel, bitrate=BITRATE)
    print(f"Listening on {channel} at {BITRATE} bps... Ctrl+C to stop")
    try:
        while True:
            msg = bus.recv(timeout=1.0)
            if msg:
                print(msg)
            else:
                print("(no message)")
    except KeyboardInterrupt:
        print("\nStopped")
    finally:
        bus.shutdown()

if __name__ == "__main__":
    main()
