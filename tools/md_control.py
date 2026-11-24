#!/usr/bin/env python3
"""MD6.5 Motor Driver Control Tool.

Interactive tool for sending commands and reading responses from the
motor driver over CAN bus, matching the interface defined in interface.h.

CAN IDs (per motor driver):
  TX to MD:   0x100 + MD_ID  (e.g. MD1=0x101, MD2=0x102, MD3=0x103, MD4=0x104)
  RX from MD: 0x200 + MD_ID  (e.g. MD1=0x201, MD2=0x202, MD3=0x203, MD4=0x204)
  Reboot:     0x7F0 (broadcast, all MDs)

Message format (8 bytes):
  Byte 0: opcode
  Bytes 1-7: payload (depends on opcode)

Response format (8 bytes):
  Byte 0: response type
  Bytes 1-4: data (two int16 values, big-endian)
  Bytes 5-7: unused
"""
import argparse
import struct
import sys
import time
import signal

try:
    import can
except ImportError:
    print("Error: python-can is required. Install it with:")
    print("  pip install python-can")
    sys.exit(1)

# CAN IDs (computed from MD_ID, set via --md-id argument)
CAN_REBOOT_ID = 0x7F0

# Opcodes
NO_OP              = 0x00
SET_TARGET_SPEED   = 0x01
SET_TARGET_TORQUE  = 0x02
SET_RESPONSE_TYPE  = 0x03
SET_PID_TORQUE_KP_KI = 0x04
SET_PID_FLUX_KP_KI   = 0x05
SET_PID_SPEED_KP_KI  = 0x06

# Response types
RESPONSE_NAMES = {
    0x01: "SPEED_AND_FAULTS",
    0x02: "IQ_AND_ID",
    0x03: "VQ_AND_VD",
    0x04: "PHASE_CURRENT_AND_VOLTAGE",
    0x05: "IQ_AND_IQ_REF",
    0x06: "ID_AND_ID_REF",
    0x07: "SPEED_AND_SPEED_REF",
}

# Fault codes (bitmask)
FAULT_NAMES = {
    0x0001: "DURATION",
    0x0002: "OVER_VOLT",
    0x0004: "UNDER_VOLT",
    0x0008: "OVER_TEMP",
    0x0010: "START_UP",
    0x0020: "SPEED_FDBK",
    0x0040: "OVER_CURR",
    0x0080: "SW_ERROR",
    0x0100: "SAMPLE_FAULT",
    0x0200: "OVERCURR_SW",
    0x0400: "DP_FAULT",
}


def decode_faults(fault_code):
    if fault_code == 0:
        return "NONE"
    faults = []
    for bit, name in FAULT_NAMES.items():
        if fault_code & bit:
            faults.append(name)
    return " | ".join(faults)


def decode_response(data):
    rtype = data[0]
    val1 = struct.unpack(">h", bytes(data[1:3]))[0]
    val2 = struct.unpack(">h", bytes(data[3:5]))[0]
    name = RESPONSE_NAMES.get(rtype, f"UNKNOWN(0x{rtype:02X})")

    if rtype == 0x01:
        return f"[{name}] speed={val1} RPM, faults={decode_faults(val2)} (0x{val2:04X})"
    elif rtype == 0x02:
        return f"[{name}] Iq={val1}, Id={val2}"
    elif rtype == 0x03:
        return f"[{name}] Vq={val1}, Vd={val2}"
    elif rtype == 0x04:
        return f"[{name}] phase_current={val1}, phase_voltage={val2}"
    elif rtype == 0x05:
        return f"[{name}] Iq={val1}, Iq_ref={val2}"
    elif rtype == 0x06:
        return f"[{name}] Id={val1}, Id_ref={val2}"
    elif rtype == 0x07:
        return f"[{name}] speed={val1} RPM, speed_ref={val2} RPM"
    else:
        return f"[{name}] val1={val1}, val2={val2}"


# Globals set by main() based on --md-id
CAN_TX_ID = 0x101
CAN_RX_ID = 0x201


def send_and_recv(bus, data, timeout=0.5):
    msg = can.Message(arbitration_id=CAN_TX_ID, data=data, is_extended_id=False)
    bus.send(msg)
    # wait for response
    resp = bus.recv(timeout=timeout)
    if resp and resp.arbitration_id == CAN_RX_ID:
        return resp.data
    return None


def cmd_speed(bus, args):
    enable = 1 if args.enable else 0
    speed = int(args.speed)
    data = [SET_TARGET_SPEED, enable, (speed >> 8) & 0xFF, speed & 0xFF, 0, 0, 0, 0]
    print(f"Setting speed={speed}, enable={enable}")
    resp = send_and_recv(bus, data)
    if resp:
        print(f"Response: {decode_response(resp)}")
    else:
        print("No response")


def cmd_torque(bus, args):
    enable = 1 if args.enable else 0
    torque = int(args.torque)
    data = [SET_TARGET_TORQUE, enable, (torque >> 8) & 0xFF, torque & 0xFF, 0, 0, 0, 0]
    print(f"Setting torque={torque}, enable={enable}")
    resp = send_and_recv(bus, data)
    if resp:
        print(f"Response: {decode_response(resp)}")
    else:
        print("No response")


def cmd_stop(bus, args):
    data = [SET_TARGET_SPEED, 0, 0, 0, 0, 0, 0, 0]
    print("Stopping motor (speed=0, enable=0)")
    resp = send_and_recv(bus, data)
    if resp:
        print(f"Response: {decode_response(resp)}")
    else:
        print("No response")


def cmd_response_type(bus, args):
    rtype = int(args.type)
    data = [SET_RESPONSE_TYPE, rtype, 0, 0, 0, 0, 0, 0]
    name = RESPONSE_NAMES.get(rtype, "UNKNOWN")
    print(f"Setting response type to {rtype} ({name})")
    resp = send_and_recv(bus, data)
    if resp:
        print(f"Response: {decode_response(resp)}")
    else:
        print("No response")


def cmd_pid(bus, args):
    kp = int(args.kp)
    ki = int(args.ki)
    loop_map = {"torque": SET_PID_TORQUE_KP_KI, "flux": SET_PID_FLUX_KP_KI, "speed": SET_PID_SPEED_KP_KI}
    opcode = loop_map[args.loop]
    data = [opcode, (kp >> 8) & 0xFF, kp & 0xFF, (ki >> 8) & 0xFF, ki & 0xFF, 0, 0, 0]
    print(f"Setting {args.loop} PID: Kp={kp}, Ki={ki}")
    resp = send_and_recv(bus, data)
    if resp:
        print(f"Response: {decode_response(resp)}")
    else:
        print("No response")


def cmd_poll(bus, args):
    data = [NO_OP, 0, 0, 0, 0, 0, 0, 0]
    print(f"Polling at {args.rate}Hz. Ctrl+C to stop.\n")
    interval = 1.0 / args.rate
    try:
        while True:
            resp = send_and_recv(bus, data, timeout=0.2)
            if resp:
                print(f"\r{decode_response(resp)}    ", end="", flush=True)
            else:
                print(f"\r(no response)              ", end="", flush=True)
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nStopped.")


def cmd_reboot(bus, args):
    msg = can.Message(arbitration_id=CAN_REBOOT_ID, data=[], is_extended_id=False)
    bus.send(msg)
    print("Reboot command sent. App should jump to bootloader.")


def cmd_noop(bus, args):
    data = [NO_OP, 0, 0, 0, 0, 0, 0, 0]
    print("Sending NO_OP...")
    resp = send_and_recv(bus, data)
    if resp:
        print(f"Response: {decode_response(resp)}")
    else:
        print("No response")


def cmd_interactive(bus, args):
    md_id = args.md_id
    print("=" * 55)
    print(f"  MD6.5 Motor Driver - Interactive Control (MD{md_id})")
    print("=" * 55)
    print()
    print("Commands:")
    print("  speed <rpm> [on|off]   - Set target speed (default: on)")
    print("  torque <val> [on|off]  - Set target torque (default: on)")
    print("  stop                   - Stop motor")
    print("  poll [hz]              - Poll responses (default: 10Hz)")
    print("  rtype <1-7>            - Set response type")
    print("  pid <torque|flux|speed> <kp> <ki>")
    print("  noop                   - Send NO_OP, read response")
    print("  reboot                 - Jump to bootloader")
    print("  raw <b0> <b1> ... <b7> - Send raw bytes (hex)")
    print("  quit                   - Exit")
    print()

    while True:
        try:
            line = input("md> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            break

        if not line:
            continue

        parts = line.split()
        cmd = parts[0].lower()

        try:
            if cmd == "quit" or cmd == "q" or cmd == "exit":
                break

            elif cmd == "speed":
                if len(parts) < 2:
                    print("Usage: speed <rpm> [on|off]")
                    continue
                speed = int(parts[1])
                enable = 1
                if len(parts) >= 3 and parts[2].lower() == "off":
                    enable = 0
                data = [SET_TARGET_SPEED, enable, (speed >> 8) & 0xFF, speed & 0xFF, 0, 0, 0, 0]
                resp = send_and_recv(bus, data)
                print(f"  speed={speed}, enable={enable}")
                if resp:
                    print(f"  {decode_response(resp)}")

            elif cmd == "torque":
                if len(parts) < 2:
                    print("Usage: torque <val> [on|off]")
                    continue
                torque = int(parts[1])
                enable = 1
                if len(parts) >= 3 and parts[2].lower() == "off":
                    enable = 0
                data = [SET_TARGET_TORQUE, enable, (torque >> 8) & 0xFF, torque & 0xFF, 0, 0, 0, 0]
                resp = send_and_recv(bus, data)
                print(f"  torque={torque}, enable={enable}")
                if resp:
                    print(f"  {decode_response(resp)}")

            elif cmd == "stop":
                data = [SET_TARGET_SPEED, 0, 0, 0, 0, 0, 0, 0]
                resp = send_and_recv(bus, data)
                print("  Motor stopped")
                if resp:
                    print(f"  {decode_response(resp)}")

            elif cmd == "poll":
                rate = float(parts[1]) if len(parts) > 1 else 10
                interval = 1.0 / rate
                noop_data = [NO_OP, 0, 0, 0, 0, 0, 0, 0]
                print(f"  Polling at {rate}Hz. Ctrl+C to stop.")
                try:
                    while True:
                        resp = send_and_recv(bus, noop_data, timeout=0.2)
                        if resp:
                            print(f"\r  {decode_response(resp)}    ", end="", flush=True)
                        time.sleep(interval)
                except KeyboardInterrupt:
                    print("\n  Stopped.")

            elif cmd == "rtype":
                if len(parts) < 2:
                    print("Usage: rtype <1-7>")
                    for k, v in RESPONSE_NAMES.items():
                        print(f"  {k}: {v}")
                    continue
                rtype = int(parts[1])
                data = [SET_RESPONSE_TYPE, rtype, 0, 0, 0, 0, 0, 0]
                resp = send_and_recv(bus, data)
                name = RESPONSE_NAMES.get(rtype, "UNKNOWN")
                print(f"  Response type set to {rtype} ({name})")
                if resp:
                    print(f"  {decode_response(resp)}")

            elif cmd == "pid":
                if len(parts) < 4:
                    print("Usage: pid <torque|flux|speed> <kp> <ki>")
                    continue
                loop_map = {"torque": SET_PID_TORQUE_KP_KI, "flux": SET_PID_FLUX_KP_KI, "speed": SET_PID_SPEED_KP_KI}
                loop = parts[1].lower()
                if loop not in loop_map:
                    print("  Loop must be: torque, flux, or speed")
                    continue
                kp = int(parts[2])
                ki = int(parts[3])
                opcode = loop_map[loop]
                data = [opcode, (kp >> 8) & 0xFF, kp & 0xFF, (ki >> 8) & 0xFF, ki & 0xFF, 0, 0, 0]
                resp = send_and_recv(bus, data)
                print(f"  {loop} PID: Kp={kp}, Ki={ki}")
                if resp:
                    print(f"  {decode_response(resp)}")

            elif cmd == "noop":
                data = [NO_OP, 0, 0, 0, 0, 0, 0, 0]
                resp = send_and_recv(bus, data)
                if resp:
                    print(f"  {decode_response(resp)}")
                else:
                    print("  No response")

            elif cmd == "reboot":
                msg = can.Message(arbitration_id=CAN_REBOOT_ID, data=[0]*8, is_extended_id=False)
                bus.send(msg)
                time.sleep(1.0)  # let slcan transmit before shutdown
                print("  Reboot sent. App should jump to bootloader.")
                break

            elif cmd == "raw":
                if len(parts) < 2:
                    print("Usage: raw <b0> <b1> ... <b7> (hex values)")
                    continue
                raw = [int(x, 16) for x in parts[1:9]]
                raw += [0] * (8 - len(raw))
                print(f"  Sending: {' '.join(f'{b:02X}' for b in raw)}")
                resp = send_and_recv(bus, raw)
                if resp:
                    print(f"  Response: {' '.join(f'{b:02X}' for b in resp)}")
                    print(f"  Decoded: {decode_response(resp)}")
                else:
                    print("  No response")

            elif cmd == "help" or cmd == "?":
                print("  speed <rpm> [on|off]   - Set target speed")
                print("  torque <val> [on|off]  - Set target torque")
                print("  stop                   - Stop motor")
                print("  poll [hz]              - Poll responses")
                print("  rtype <1-7>            - Set response type")
                print("  pid <torque|flux|speed> <kp> <ki>")
                print("  noop                   - Send NO_OP")
                print("  reboot                 - Jump to bootloader")
                print("  raw <b0> ... <b7>      - Send raw hex bytes")
                print("  quit                   - Exit")

            else:
                print(f"  Unknown command: {cmd}. Type 'help' for commands.")

        except ValueError as e:
            print(f"  Error: {e}")
        except can.CanError as e:
            print(f"  CAN error: {e}")


def main():
    ap = argparse.ArgumentParser(description="MD6.5 Motor Driver Control Tool")

    ap.add_argument("-i", "--interface", default="slcan",
                    help="python-can interface (default: slcan)")
    ap.add_argument("-c", "--channel", default="/dev/cu.usbmodem387C386630361",
                    help="CAN channel/device")
    ap.add_argument("-b", "--bitrate", type=int, default=1000000,
                    help="CAN bitrate (default: 1000000)")
    ap.add_argument("-m", "--md-id", type=int, default=1, choices=[1, 2, 3, 4],
                    help="Motor driver ID (1-4, default: 1)")

    sub = ap.add_subparsers(dest="command")

    # Interactive mode (default)
    sub.add_parser("interactive", help="Interactive control shell (default)")

    # One-shot commands
    sp = sub.add_parser("speed", help="Set target speed")
    sp.add_argument("speed", type=int, help="Target speed in RPM")
    sp.add_argument("--enable", action="store_true", default=True)
    sp.add_argument("--disable", dest="enable", action="store_false")

    tp = sub.add_parser("torque", help="Set target torque")
    tp.add_argument("torque", type=int, help="Target torque value")
    tp.add_argument("--enable", action="store_true", default=True)
    tp.add_argument("--disable", dest="enable", action="store_false")

    sub.add_parser("stop", help="Stop the motor")

    rp = sub.add_parser("rtype", help="Set response type")
    rp.add_argument("type", help="Response type (1-7)")

    pp = sub.add_parser("pid", help="Set PID gains")
    pp.add_argument("loop", choices=["torque", "flux", "speed"])
    pp.add_argument("kp", type=int)
    pp.add_argument("ki", type=int)

    plp = sub.add_parser("poll", help="Poll responses continuously")
    plp.add_argument("--rate", type=float, default=10, help="Poll rate in Hz (default: 10)")

    sub.add_parser("reboot", help="Jump to bootloader")
    sub.add_parser("noop", help="Send NO_OP and read response")

    args = ap.parse_args()

    global CAN_TX_ID, CAN_RX_ID
    CAN_TX_ID = 0x100 + args.md_id
    CAN_RX_ID = 0x200 + args.md_id
    print(f"Targeting MD{args.md_id} (TX: 0x{CAN_TX_ID:03X}, RX: 0x{CAN_RX_ID:03X})")

    bus = can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)

    try:
        if args.command == "speed":
            cmd_speed(bus, args)
        elif args.command == "torque":
            cmd_torque(bus, args)
        elif args.command == "stop":
            cmd_stop(bus, args)
        elif args.command == "rtype":
            cmd_response_type(bus, args)
        elif args.command == "pid":
            cmd_pid(bus, args)
        elif args.command == "poll":
            cmd_poll(bus, args)
        elif args.command == "reboot":
            cmd_reboot(bus, args)
        elif args.command == "noop":
            cmd_noop(bus, args)
        else:
            cmd_interactive(bus, args)
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()
