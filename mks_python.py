import time
import can
import usb.core
import queue


VID = 0x1D50
PID = 0x606F

BITRATE = 500_000
CHANNEL = 1          # 0 or 1 (USB2CAN-X2 channel index)
MOTOR_ID = 0x01      # motor CAN ID (11-bit)

# --- open USB2CAN ---
devs = list(usb.core.find(find_all=True, idVendor=VID, idProduct=PID))
if not devs:
    raise SystemExit("No USB2CAN (VID:PID 1d50:606f) found")
if CHANNEL >= len(devs):
    raise SystemExit(f"CHANNEL={CHANNEL} but only {len(devs)} device(s) found")

d = devs[CHANNEL]

bus = can.Bus(
    interface="gs_usb",
    channel=d.product,
    bus=d.bus,
    address=d.address,
    bitrate=BITRATE,
    receive_own_messages=False,  # only this (no TX-echo cache)
)

rxq = queue.Queue(maxsize=2000)

# --- print RX frames from motor immediately ---
class PrintListener(can.Listener):
    def on_message_received(self, msg: can.Message):
        if msg.arbitration_id != MOTOR_ID:
            return
        data = " ".join(f"{b:02X}" for b in msg.data)
        print(f"{msg.timestamp:.6f}  ID={msg.arbitration_id:03X}  DLC={msg.dlc}  DATA={data}")

        # auch in Queue legen (für read_raw_encoder_counts usw.)
        try:
            rxq.put_nowait(msg)
        except queue.Full:
            pass


notifier = can.Notifier(bus, [PrintListener()], timeout=0.1)

# --- protocol helpers ---
def crc8(can_id: int, data_bytes: list[int]) -> int:
    return (can_id + sum(data_bytes)) & 0xFF

def send(data_bytes: list[int], delay_s: float = 0.05) -> None:
    """Send data bytes without CRC; CRC is appended automatically."""
    payload = list(data_bytes) + [crc8(MOTOR_ID, data_bytes)]
    bus.send(can.Message(arbitration_id=MOTOR_ID, is_extended_id=False, data=bytes(payload)))
    if delay_s:
        time.sleep(delay_s)

ENC48_ENDIAN = "big"  # wenn Werte „komisch“ sind: auf "little" ändern

def flush_rxq():
    try:
        while True:
            rxq.get_nowait()
    except queue.Empty:
        pass

def uplink_crc_ok(msg: can.Message) -> bool:
    # CRC = (CAN_ID + Summe aller Bytes außer CRC) & 0xFF
    if msg.arbitration_id != MOTOR_ID or len(msg.data) < 2:
        return False
    expected = (MOTOR_ID + sum(msg.data[:-1])) & 0xFF
    return expected == msg.data[-1]

def read_raw_encoder_counts(timeout_s: float = 0.5, retries: int = 3):
    """
    0x35: RAW accumulated multi-turn encoder (int48).
    Downlink:  [35, CRC]
    Uplink:    DLC=8, [35, b1..b6, CRC]  -> int48 in b1..b6
    """
    for _ in range(retries):
        flush_rxq()
        send([0x35], delay_s=0.0)

        deadline = time.monotonic() + timeout_s
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                msg = rxq.get(timeout=remaining)
            except queue.Empty:
                break

            # Reply muss DLC=8, Code=0x35 sein
            if msg.dlc != 8 or len(msg.data) != 8:
                continue
            if msg.data[0] != 0x35:
                continue
            if not uplink_crc_ok(msg):
                continue

            raw_bytes = msg.data[1:7]  # 6 bytes
            val = int.from_bytes(raw_bytes, byteorder=ENC48_ENDIAN, signed=True)
            return val

        time.sleep(0.01)

    return None

def counts_to_deg(counts: int) -> float:
    return (counts * 360.0) / COUNTS_PER_TURN

def counts_to_deg_singleturn(counts: int) -> float:
    return ((counts % COUNTS_PER_TURN) * 360.0) / COUNTS_PER_TURN

def rotate_counts_cmd(speed_rpm: int, acc: int, abs_counts: int):
    """
    F5 absolute coordinate, abs_counts in counts (0x4000 per turn).
    abs_counts ist signed int24: [-0x800000 .. 0x7FFFFF]
    """
    speed_rpm = max(0, min(3000, int(speed_rpm)))
    acc = max(0, min(255, int(acc)))
    abs_counts = int(abs_counts)

    # in signed int24 falten, falls außerhalb
    if abs_counts < -0x800000 or abs_counts > 0x7FFFFF:
        abs_counts = ((abs_counts + 0x800000) % 0x1000000) - 0x800000

    axis24 = abs_counts & 0xFFFFFF

    sp_hi = (speed_rpm >> 8) & 0xFF
    sp_lo = speed_rpm & 0xFF
    a2 = (axis24 >> 16) & 0xFF
    a1 = (axis24 >> 8) & 0xFF
    a0 = axis24 & 0xFF

    send([0xF5, sp_hi, sp_lo, acc, a2, a1, a0])


def clamp_int(prompt: str, lo: int, hi: int, default=None) -> int:
    while True:
        s = input(prompt).strip()
        if s == "" and default is not None:
            return default
        try:
            v = int(s)
        except ValueError:
            continue
        if lo <= v <= hi:
            return v

# --- commands ---
def set_sr_vfoc():
    send([0x82, 0x05])

def set_sr_close():
    send([0x82, 0x05])


def enable_motor():
    send([0xF3, 0x01])

def disable_motor():
    send([0xF3, 0x00])

def coord_zero():
    send([0x92])

def set_working_current_cmd(mA: int):
    hi = (mA >> 8) & 0xFF
    lo = mA & 0xFF
    send([0x83, hi, lo])  # big-endian current

def set_working_current():
    mA = clamp_int("Working current mA (0..3000): ", 0, 3000)
    set_working_current_cmd(mA)

def set_holding_current_cmd(perc: int):
    if not 0 <= perc <= 100:
        raise ValueError("Holding current percentage must be between 0 and 100")
    ratio = (perc // 10) - 1
    if ratio < 0:
        ratio = 0
    send([0x9B, ratio])  # ratio: 0=0%, 1=10%, ..., 9=90%

def set_holding_current():
    perc = clamp_int("Holding current percentage (0..100): ", 0, 100)
    set_holding_current_cmd(perc)

def run_motor_cmd(direction: int, speed: int, acc: int):
    b2 = (direction << 7) | ((speed >> 8) & 0x0F)
    b3 = speed & 0xFF
    send([0xF6, b2, b3, acc])

def run_motor():
    direction = clamp_int("Direction (0=CCW, 1=CW): ", 0, 1)
    speed = clamp_int("Speed RPM (0..3000): ", 0, 3000)
    acc = clamp_int("Acceleration (0..255): ", 0, 255)
    run_motor_cmd(direction, speed, acc)

def run_motor_time_cmd(direction: int, speed: int, acc: int, runtime_ms: int):
    units = min(0xFFFFFF, runtime_ms // 10)  # 10ms units
    b2 = (direction << 7) | ((speed >> 8) & 0x0F)
    b3 = speed & 0xFF
    t2 = (units >> 16) & 0xFF
    t1 = (units >> 8) & 0xFF
    t0 = units & 0xFF
    send([0xF6, b2, b3, acc, t2, t1, t0])  # 7 bytes + CRC = 8 bytes

def run_motor_time():
    direction = clamp_int("Direction (0=CCW, 1=CW): ", 0, 1)
    speed = clamp_int("Speed RPM (0..3000): ", 0, 3000)
    acc = clamp_int("Acceleration (0..255): ", 0, 255)
    runtime_ms = clamp_int("Runtime ms (0..16777215): ", 0, 0xFFFFFF)
    run_motor_time_cmd(direction, speed, acc, runtime_ms)

def stop_motor_cmd(acc: int):
    send([0xF6, 0x00, 0x00, acc])

def stop_motor():
    acc = clamp_int("Stop decel (0..255, default 2): ", 0, 255, default=2)
    stop_motor_cmd(acc)

COUNTS_PER_TURN = 16384
COUNTS_PER_DEG = COUNTS_PER_TURN / 360.0

def _deg_to_counts(deg: float) -> int:
    # Absolute coordinate counts (int24) from degrees; supports negative degrees.
    return int(round(deg * COUNTS_PER_DEG))

def home_speed_to_endstop_then_zero(
    direction: int,
    speed_rpm: int,
    acc: int,
    *,
    poll_s: float = 0.05,
    stall_delta_counts: int = 2,
    stall_samples: int = 3,
    timeout_s: float = 15.0,
    hold_speed_rpm: int = 200,
    hold_acc: int = 150,
    settle_s: float = 0.10,
):
    """
    Vorgehen:
    1) SR_vFOC + Enable
    2) temp. coord_zero() + raw0 merken
    3) Speed-Mode gegen Anschlag fahren
    4) Anschlag über Encoder-Stillstand erkennen (Delta ~ 0)
    5) RAW lesen -> coord_counts = raw_end - raw0
    6) F5 auf coord_counts schicken (stabiler Punkt)
    7) coord_zero() am Anschlag setzen (finaler Nullpunkt)
    8) optional F5 auf 0 schicken
    """

    direction = 1 if int(direction) else 0
    speed_rpm = max(1, min(3000, int(speed_rpm)))
    acc = max(0, min(255, int(acc)))

    # sicherer Zustand
    set_sr_vfoc()
    enable_motor()

    # temporär: Koordinatensystem definieren
    coord_zero()
    # time.sleep(0.05)
    # set_working_current_cmd(500)  # max. Strom für sicheres Anfahren des Anschlags
    time.sleep(0.05)

    raw0 = read_raw_encoder_counts()
    if raw0 is None:
        raise RuntimeError("0x35 liefert keinen Reply. Prüfe CRC/Endian/Listener/Queue.")

    # gegen Anschlag fahren (Speed Mode)
    run_motor_cmd(direction, speed_rpm, acc)

    last = raw0
    stable = 0
    t0 = time.monotonic()

    while True:
        if time.monotonic() - t0 > timeout_s:
            raise TimeoutError("Timeout: Anschlag nicht erkannt (Encoder ändert sich weiter).")

        time.sleep(poll_s)

        cur = read_raw_encoder_counts(timeout_s=0.25, retries=1)
        if cur is None:
            continue

        delta = cur - last
        last = cur

        if abs(delta) <= stall_delta_counts:
            stable += 1
        else:
            stable = 0

        if stable >= stall_samples:
            break

    # kurze Beruhigung, dann finalen Wert lesen (während weiter „gedrückt“ wird)
    time.sleep(settle_s)
    raw_end = read_raw_encoder_counts(timeout_s=0.25, retries=2)
    if raw_end is None:
        raw_end = last

    # Umrechnung: Koordinate relativ zum temp. Nullpunkt
    coord_counts = raw_end - raw0

    print(f"[HOME] raw0={raw0}  raw_end={raw_end}  coord_counts={coord_counts}")
    print(f"[HOME] coord_deg={counts_to_deg(coord_counts):.2f}°  singleturn(raw_end)={counts_to_deg_singleturn(raw_end):.2f}°")

    
    stop_motor_cmd(2)  # kurz stoppen, damit F5 besser greift
    time.sleep(1.0)
    # jetzt: auf exakt diesen aktuellen Winkel per absolut coordinate (F5)
    rotate_counts_cmd(hold_speed_rpm, hold_acc, coord_counts)
    time.sleep(0.15)

    # final: Anschlag als Null setzen
    coord_zero()
    time.sleep(0.05)

    # optional: Hold auf 0 im neuen Koordinatensystem
    rotate_counts_cmd(hold_speed_rpm, hold_acc, 0)
    time.sleep(0.05)

    print("[HOME] Fertig: Anschlag ist jetzt 0 (Coordinate Zero).")


def home_speed_to_endstop_then_zero_ui():
    direction = clamp_int("Homing Richtung (0=CCW, 1=CW): ", 0, 1)
    speed = clamp_int("Homing Speed RPM (1..3000): ", 1, 3000)
    acc = clamp_int("Homing Acc (0..255): ", 0, 255)

    stall_delta = clamp_int("Stillstand-Delta counts (default 2): ", 0, 200, default=2)
    stall_samp = clamp_int("Stillstand-Samples (default 3): ", 1, 50, default=3)
    timeout_s = clamp_int("Timeout s (default 15): ", 1, 120, default=15)

    home_speed_to_endstop_then_zero(
        direction, speed, acc,
        stall_delta_counts=stall_delta,
        stall_samples=stall_samp,
        timeout_s=float(timeout_s),
    )


def rotate_cmd(speed_rpm: int, acc: int, degrees: float):
    """
    Absolute coordinate execution (F5), but degrees-based input.

    degrees: absolute position in degrees relative to the motor's coordinate zero.
             (Use coord_zero() to define zero where you want.)
    """
    speed_rpm = max(0, min(3000, int(speed_rpm)))
    acc = max(0, min(255, int(acc)))

    abs_axis = _deg_to_counts(float(degrees))  # signed int (counts)

    # int24 signed range: -0x800000 .. +0x7FFFFF
    if abs_axis < -0x800000 or abs_axis > 0x7FFFFF:
        max_deg = 0x7FFFFF / COUNTS_PER_DEG
        min_deg = -0x800000 / COUNTS_PER_DEG
        raise ValueError(f"degrees out of range for int24: [{min_deg:.1f}, {max_deg:.1f}]")

    axis24 = abs_axis & 0xFFFFFF  # two's complement in 24-bit

    sp_hi = (speed_rpm >> 8) & 0xFF
    sp_lo = speed_rpm & 0xFF
    a2 = (axis24 >> 16) & 0xFF
    a1 = (axis24 >> 8) & 0xFF
    a0 = axis24 & 0xFF

    # F5 + speed(uint16 BE) + acc(uint8) + absAxis(int24 BE)
    send([0xF5, sp_hi, sp_lo, acc, a2, a1, a0])

def rotate():
    speed = clamp_int("Speed RPM (0..3000): ", 0, 3000)
    acc = clamp_int("Acceleration (0..255): ", 0, 255)

    while True:
        s = input("Absolute angle degrees (can be negative, e.g. 90, -45, 720): ").strip()
        try:
            deg = float(s)
            break
        except ValueError:
            continue

    rotate_cmd(speed, acc, deg)

def _int32_be(x: int) -> list[int]:
    x &= 0xFFFFFFFF
    return [(x >> 24) & 0xFF, (x >> 16) & 0xFF, (x >> 8) & 0xFF, x & 0xFF]

def home_torque_params_cmd(direction: int, speed_rpm: int, mode: int = 1):
    """
    0x90 Homing parameter setup.
    Matches the commonly used layout:
      [90, dir, 00, 00, spd_L, spd_H, mode]
    where speed is uint16 little-endian.
    mode=1 => torque/mechanical limit style return.
    """
    direction = 1 if int(direction) else 0
    speed_rpm = max(0, min(3000, int(speed_rpm)))
    mode = max(0, min(255, int(mode)))

    spd_l = speed_rpm & 0xFF
    spd_h = (speed_rpm >> 8) & 0xFF
    #.  cmd,homeTrig,homeDir, homeSpeed 2x,EndLimit, hm_mode
    send([0x90, 0x00, direction, spd_h, spd_l, 0x00, mode])

def home_torque_offset_current_cmd(offset_deg: float, current_mA: int):
    """
    0x94 Set origin offset + return current for torque homing.
    Payload:
      [94, ofs_b3, ofs_b2, ofs_b1, ofs_b0, cur_hi, cur_lo]
    offset is signed int32 counts (two's complement), big-endian.
    current is uint16 big-endian (mA).
    """
    # degrees -> counts (16384 counts / 360 deg)
    offset_counts = int(round(float(offset_deg) * COUNTS_PER_DEG))

    # clamp to signed int32 range
    if offset_counts < -0x80000000 or offset_counts > 0x7FFFFFFF:
        raise ValueError("offset_deg too large for int32")

    current_mA = max(0, min(3000, int(current_mA)))
    cur_hi = (current_mA >> 8) & 0xFF
    cur_lo = current_mA & 0xFF

    ofs = _int32_be(offset_counts)
    send([0x94, ofs[0], ofs[1], ofs[2], ofs[3], cur_hi, cur_lo])

def return_to_zero_by_torque_cmd(direction: int, speed_rpm: int, offset_deg: float, current_mA: int):
    """
    Full torque return-to-zero sequence:
      1) 0x90 params (dir, speed, mode=1)
      2) 0x94 offset + current
      3) 0x91 execute
    """
    home_torque_params_cmd(direction, speed_rpm, mode=1)
    home_torque_offset_current_cmd(offset_deg, current_mA)
    send([0x91])  # execute

def return_to_zero_by_torque():
    direction = clamp_int("Return dir (0=CCW, 1=CW): ", 0, 1)
    speed = clamp_int("Return speed RPM (0..3000): ", 0, 3000)
    current = clamp_int("Return torque/current mA (0..3000): ", 0, 3000)

    while True:
        s = input("Origin offset deg (e.g. 0, 180, -90): ").strip()
        try:
            offset_deg = float(s)
            break
        except ValueError:
            continue

    return_to_zero_by_torque_cmd(direction, speed, offset_deg, current)


# --- REPL ---
# Commands to type at "cmd>":
#   vfoc | en | dis | zero | ma | hold | run | runt | stop | rot | return | startup | quit


def startup():
    set_sr_vfoc()
    enable_motor()
    coord_zero()
    set_working_current_cmd(1000)
    rotate_cmd(100, 100, 215)
    time.sleep(1.5)
    rotate_cmd(0, 100, 0)
    time.sleep(0.5)
    coord_zero()
    time.sleep(1.5)
    rotate_cmd(500, 220, -110)
    time.sleep(1)
    

try:
    while True:
        cmd = input("cmd> ").strip().lower()
        if cmd in ("q", "quit", "exit"):
            break
        elif cmd == "vfoc":
            set_sr_vfoc()
        elif cmd == "sr_close":
            set_sr_close()
        elif cmd == "en":
            enable_motor()
        elif cmd == "dis":
            disable_motor()
        elif cmd == "zero":
            coord_zero()
        elif cmd == "ma":
            set_working_current()
        elif cmd == "hold":
            set_holding_current()
        elif cmd == "run":
            run_motor()
        elif cmd == "runt":
            run_motor_time()
        elif cmd == "stop":
            stop_motor()
        elif cmd == "startup":
            startup()
        elif cmd == "return":
            return_to_zero_by_torque()
        elif cmd == "":
            continue
        elif cmd == "rot":
            rotate()
        elif cmd == "cl":
            rotate_cmd(2000, 255, 0)
        elif cmd == "cl-":
            rotate_cmd(2000, 255, -1)
        elif cmd == "cl+":
            rotate_cmd(2000, 255, 5)
        elif cmd == "op":
            rotate_cmd(2000, 255, -120)
        elif cmd == "oph":
            rotate_cmd(2000, 255, -50)
        elif cmd == "enc":
            v = read_raw_encoder_counts()
            if v is None:
                print("kein 0x35 reply")
            else:
                print(f"RAW={v} counts  raw_deg={counts_to_deg(v):.2f}°  singleturn={counts_to_deg_singleturn(v):.2f}°")

        elif cmd == "home":
            home_speed_to_endstop_then_zero_ui()
        else:
            print("unknown command")
except KeyboardInterrupt:
    pass
finally:
    notifier.stop()
    bus.shutdown()
