"""Send a CAN frame to 0x101 every second to toggle the motor driver LED."""

import can
import time
import sys

CHANNEL = "/dev/cu.usbmodem3801"  # update this to match your CANable port
BITRATE = 1_000_000
CAN_ID = 0x101

def main():
    # update channel if provided as argument
    channel = sys.argv[1] if len(sys.argv) > 1 else CHANNEL

    bus = can.interface.Bus(
        interface="slcan",
        channel=channel,
        bitrate=BITRATE,
    )

    print(f"Connected to {channel} at {BITRATE} bps")
    print(f"Sending to 0x{CAN_ID:03X} every 1 second. Ctrl+C to stop.")

    try:
        count = 0
        while True:
            msg = can.Message(
                arbitration_id=CAN_ID,
                data=[0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
                is_extended_id=False,
            )
            bus.send(msg)
            count += 1
            print(f"Sent frame #{count}")
            time.sleep(1.0)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        bus.shutdown()

if __name__ == "__main__":
    main()
