#!/usr/bin/env python3
"""把一行 JSON 写到 HoloCubic 的 USB 串口（不复位 ESP32）。
用法:
  holocube_serial_send.py '<json>'            发送一行，最多等 0.6s 读 AIS:OK（读不到也不报错）
  holocube_serial_send.py --check             发送 {"ping":1} 并打印设备回复（诊断用）
端口: 环境变量 CUBE_PORT > ~/.holocube_port 文件 > 自动探测 /dev/cu.usbserial-* 等
需要 pyserial（~/.holocube-pio/bin/python3 自带）。任何错误都只打印到 stderr，退出码 0。
"""
import sys, os, glob, time, fcntl

def find_port():
    p = os.environ.get("CUBE_PORT", "").strip()
    if p:
        return p
    try:
        p = open(os.path.expanduser("~/.holocube_port")).readline().strip()
        if p:
            return p
    except Exception:
        pass
    for pat in ("/dev/cu.usbserial-*", "/dev/cu.wchusbserial*", "/dev/cu.SLAB_USBtoUART*", "/dev/cu.usbmodem*",
                "/dev/ttyUSB*", "/dev/ttyACM*"):
        c = sorted(glob.glob(pat))
        if c:
            return c[0]
    return None

def main():
    check = len(sys.argv) > 1 and sys.argv[1] == "--check"
    line = '{"ping":1}' if check else (sys.argv[1] if len(sys.argv) > 1 else "")
    if not line:
        return
    try:
        import serial
    except ImportError:
        print("holocube: pyserial not found (use ~/.holocube-pio/bin/python3)", file=sys.stderr)
        return
    port = find_port()
    if not port:
        print("holocube: no serial port found (set CUBE_PORT)", file=sys.stderr)
        return
    # 多个 hook 同时触发时串行化，最多等 1.5s，否则放弃
    lock = open(os.path.join(os.environ.get("TMPDIR", "/tmp"), "holocube_serial.lock"), "w")
    t0 = time.time()
    while True:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except OSError:
            if time.time() - t0 > 1.5:
                print("holocube: busy, dropped", file=sys.stderr)
                return
            time.sleep(0.05)
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.1
    s.write_timeout = 1
    s.dsrdtr = False
    s.rtscts = False
    s.dtr = False   # 打开前设置：DTR/RTS 保持释放，不触发 ESP32 自动复位/下载电路
    s.rts = False
    try:
        s.exclusive = True  # 端口被串口监视器等占用时直接失败
    except Exception:
        pass
    try:
        s.open()
    except Exception as e:
        print("holocube: cannot open %s: %s" % (port, e), file=sys.stderr)
        return
    try:
        s.reset_input_buffer()
        s.write((line.strip() + "\n").encode("utf-8"))
        s.flush()
        # 读回复（日志与回复混在一起，只找 AIS: 开头的行）
        deadline = time.time() + (1.5 if check else 0.6)
        buf = b""
        while time.time() < deadline:
            buf += s.read(256)
            for l in buf.split(b"\n"):
                if l.startswith(b"AIS:"):
                    if check:
                        print(port, l.decode("utf-8", "ignore").strip())
                    return
        if check:
            print(port, "no reply (is the new firmware flashed?)")
    except Exception as e:
        print("holocube: write failed: %s" % e, file=sys.stderr)
    finally:
        try:
            s.close()
        except Exception:
            pass

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("holocube: %s" % e, file=sys.stderr)
    sys.exit(0)
