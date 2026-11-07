#!/usr/bin/env python3
"""Motor speed dial: drag the knob (or scroll) to set duty 0-100 %, sent over CAN to the G474 app.

Sends the current value on ID 0x200, byte 0 = percent, every HEARTBEAT_MS.
The app drops to 0 % if it hears nothing for 500 ms, so the heartbeat must keep running.
Needs the CANable on COM5 (slcan). Close Cangaroo and can_flash.py first.
"""
import math
import tkinter as tk
from tkinter import messagebox

import can

CMD_ID = 0x200
PORT = "COM5"
BITRATE = 500000
HEARTBEAT_MS = 100

# dial geometry: 270 degree sweep, 0 % at 225 deg (bottom-left), 100 % at -45 deg (bottom-right)
START_DEG, SWEEP_DEG = 225.0, 270.0


def value_to_deg(v):
    return START_DEG - SWEEP_DEG * v / 100.0


class Dial(tk.Canvas):
    SIZE = 260
    RADIUS = 100

    def __init__(self, master, on_change):
        super().__init__(master, width=self.SIZE, height=self.SIZE, bg="#202020", highlightthickness=0)
        self.on_change = on_change
        self.value = 0
        self.cx = self.cy = self.SIZE / 2
        self.create_arc(self.cx - self.RADIUS, self.cy - self.RADIUS, self.cx + self.RADIUS, self.cy + self.RADIUS,
                        start=-45, extent=SWEEP_DEG, style=tk.ARC, width=10, outline="#555")
        self.arc = self.create_arc(self.cx - self.RADIUS, self.cy - self.RADIUS,
                                   self.cx + self.RADIUS, self.cy + self.RADIUS,
                                   start=-45, extent=0, style=tk.ARC, width=10, outline="#2fa84f")
        self.pointer = self.create_line(self.cx, self.cy, self.cx, self.cy, width=4, fill="white")
        self.label = self.create_text(self.cx, self.cy + 40, text="0 %", fill="white", font=("Segoe UI", 22, "bold"))
        self.bind("<Button-1>", self._from_mouse)
        self.bind("<B1-Motion>", self._from_mouse)
        self.bind("<MouseWheel>", self._from_wheel)
        self._draw()

    def _from_mouse(self, event):
        # angle of the pointer around the dial centre, counter-clockwise from east
        ang = math.degrees(math.atan2(-(event.y - self.cy), event.x - self.cx)) % 360
        # clockwise travel from the 0 % position; the 90-degree gap at the bottom clamps to the nearest end
        travel = (START_DEG - ang) % 360
        if travel > SWEEP_DEG:
            travel = 0 if (travel - SWEEP_DEG) > (360 - travel) else SWEEP_DEG
        self.set_value(round(travel / SWEEP_DEG * 100))

    def _from_wheel(self, event):
        self.set_value(self.value + (1 if event.delta > 0 else -1))

    def set_value(self, v):
        v = max(0, min(100, int(v)))
        if v != self.value:
            self.value = v
            self._draw()
            self.on_change(v)

    def _draw(self):
        ang = value_to_deg(self.value)
        rad = math.radians(ang)
        x = self.cx + (self.RADIUS - 15) * math.cos(rad)
        y = self.cy - (self.RADIUS - 15) * math.sin(rad)
        self.coords(self.pointer, self.cx, self.cy, x, y)
        self.itemconfig(self.label, text=f"{self.value} %")
        self.itemconfig(self.arc, start=ang, extent=SWEEP_DEG * self.value / 100.0)


class MotorUI:
    def __init__(self, root):
        self.root = root
        root.title("Motor speed")
        root.configure(bg="#202020")
        self.bus = None
        try:
            self.bus = can.Bus(interface="slcan", channel=PORT, bitrate=BITRATE)
        except Exception as e:
            messagebox.showerror("CAN", f"Could not open {PORT}: {e}\nIs Cangaroo or can_flash.py still using it?")
            root.destroy()
            return

        self.dial = Dial(root, on_change=lambda v: None)
        self.dial.pack(padx=20, pady=(20, 10))
        tk.Button(root, text="STOP (0 %)", font=("Segoe UI", 12, "bold"), bg="#a83232", fg="white",
                  command=lambda: self.dial.set_value(0)).pack(pady=(0, 20))

        root.protocol("WM_DELETE_WINDOW", self.close)
        self.tick()

    def tick(self):
        self._send(self.dial.value)
        self.root.after(HEARTBEAT_MS, self.tick)

    def _send(self, percent):
        try:
            self.bus.send(can.Message(arbitration_id=CMD_ID, data=bytes([percent]), is_extended_id=False))
        except can.CanError as e:
            print("send failed:", e)

    def close(self):
        if self.bus is not None:
            self._send(0)  # leave the motor stopped
            self.bus.shutdown()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    MotorUI(root)
    root.mainloop()
