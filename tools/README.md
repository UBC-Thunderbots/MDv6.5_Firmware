# MD6.5 CAN Tools

## Setup
```
pip install python-can
```
All tools default to slcan interface at 1 Mbps. Override with `-c <channel>` if your CANable port differs.

## Workflow

### 1. Flash firmware (board must be in bootloader mode)
The bootloader blinks LED 3 times on startup. If the board is running the main app, reboot it first:
```
python3 tools/can_flash.py reboot
```
Wait for the 3 blinks, then flash:
```
python3 tools/can_flash.py flash
```
This uses the default .bin at `Official_MD65_Proj/STM32CubeIDE/Debug/Official_MD65_Proj.bin`. Or specify a path:
```
python3 tools/can_flash.py flash path/to/firmware.bin
```

### 2. Boot into main app (from bootloader)
```
python3 tools/can_flash.py boot
```

### 3. Control the motor (main app must be running)
Interactive mode:
```
python3 tools/md_control.py
```
Then at the `md>` prompt:
```
speed 1000          # spin at 1000 RPM (motor enabled)
speed 500 off       # set speed but keep motor disabled
torque 200          # set torque (motor enabled)
stop                # stop motor
poll                # live-read responses at 10Hz (Ctrl+C to stop)
poll 50             # poll at 50Hz
rtype 2             # change response type (1-7, see below)
pid speed 1000 500  # set speed PID Kp=1000 Ki=500
pid torque 800 400  # set torque PID
pid flux 800 400    # set flux PID
noop                # ping, read current response
reboot              # jump back to bootloader
raw 01 01 03 E8     # send raw hex bytes
help                # show all commands
quit                # exit
```

One-shot commands (no interactive prompt):
```
python3 tools/md_control.py speed 1000
python3 tools/md_control.py stop
python3 tools/md_control.py poll --rate 20
python3 tools/md_control.py reboot
```

### 4. Debug / listen
Listen for all CAN traffic:
```
python3 tools/can_listen.py
```
Send a test frame every second:
```
python3 tools/can_led_test.py
```

## Response Types
Set with `rtype <n>` in md_control:

| # | Name | Value 1 | Value 2 |
|---|------|---------|---------|
| 1 | SPEED_AND_FAULTS | speed (RPM) | fault code |
| 2 | IQ_AND_ID | Iq | Id |
| 3 | VQ_AND_VD | Vq | Vd |
| 4 | PHASE_CURRENT_AND_VOLTAGE | phase current | phase voltage |
| 5 | IQ_AND_IQ_REF | Iq | Iq reference |
| 6 | ID_AND_ID_REF | Id | Id reference |
| 7 | SPEED_AND_SPEED_REF | speed (RPM) | speed reference |

## CAN ID Map
| ID | Direction | Purpose |
|----|-----------|---------|
| 0x101 | PC → MD | Motor commands (opcodes) |
| 0x201 | MD → PC | Motor responses |
| 0x7F0 | PC → MD | Reboot to bootloader |
| 0x700 | PC → MD | Bootloader: jump to app |
| 0x701 | PC → MD | Bootloader: start flash |
| 0x702 | PC → MD | Bootloader: data frame |
| 0x703 | PC → MD | Bootloader: end/verify |
