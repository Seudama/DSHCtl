# -*- coding: utf-8 -*-
"""DSHCtl 端到端测试 (沙箱测试模式)"""
import ctypes
import os
import socket
import subprocess
import sys
import time

EXE = "DSHCtl.exe"
PY = r"D:\Softwares\anaconda3\python.exe"

passed, failed = 0, 0


def run(args, env_extra=None, timeout=90):
    env = dict(os.environ)
    env["DSH_CTL_SANDBOX_TEST"] = "1"
    if env_extra:
        env.update(env_extra)
    p = subprocess.run([EXE] + args, capture_output=True, env=env, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print(f"[PASS] {name}")
    else:
        failed += 1
        print(f"[FAIL] {name} {detail}")


def port_ok(port):
    s = socket.socket()
    s.settimeout(0.4)
    r = s.connect_ex(("127.0.0.1", port)) == 0
    s.close()
    return r


def pid_alive(pid):
    k = ctypes.windll.kernel32
    h = k.OpenProcess(0x1000, False, pid)
    if h:
        k.CloseHandle(h)
        return True
    return False


print("=== T1 status (expect unmanaged 3080) ===")
rc, so, se = run(["status"])
print(so.strip())
check("T1 status sees unmanaged 3080", rc == 1 and "3080" in so, f"rc={rc} out={so}")

print("=== T2 start dummy http.server:39999 ===")
rc, so, se = run(["start", "--cmd", "dummy_http.bat", "--port", "39999", "--wait", "15"])
print(so.strip())
check("T2 start ready", rc == 0 and "就绪" in so, f"rc={rc} out={so}")
check("T2 port 39999 listening", port_ok(39999))

print("=== T3 status ===")
rc, so, se = run(["status"])
print(so.strip())
check("T3 status running", rc == 0 and "运行中" in so and "39999" in so, f"rc={rc} out={so}")

print("=== T4 start again (skip) ===")
rc, so, se = run(["start", "--cmd", "dummy_http.bat", "--port", "39999", "--wait", "15"])
print(so.strip())
check("T4 skip", rc == 0 and "跳过" in so, f"rc={rc} out={so}")

print("=== T5 restart ===")
rc, so, se = run(["restart", "--cmd", "dummy_http.bat", "--port", "39999", "--wait", "15"])
print(so.strip())
check("T5 restart ready", rc == 0 and "就绪" in so and "已停止" in so, f"rc={rc} out={so}")

print("=== T6 stop ===")
rc, so, se = run(["stop"])
print(so.strip())
check("T6 stop", rc == 0 and "已停止" in so, f"rc={rc} out={so}")
check("T6 port 39999 freed", not port_ok(39999))

print("=== T7 stop again ===")
rc, so, se = run(["stop"])
print(so.strip())
check("T7 stop again clean", rc == 0 and "未发现" in so, f"rc={rc} out={so}")

print("=== T8 logs ===")
rc, so, se = run(["logs", "--lines", "5"])
print(so.strip())
check("T8 logs", rc == 0 and "dsh.log" in so, f"rc={rc} out={so}")

print("=== T9 scan-kill via stub ===")
rc, so, se = run(["start", "--cmd", "dummy_sleep.bat", "--port", "39999", "--wait", "0"])
pid = 0
try:
    with open("dsh.pid", encoding="utf-8") as f:
        pid = int(f.read().split("|")[0])
except Exception:
    pass
print(f"dummy root pid={pid}")
check("T9 dummy started", rc == 0 and pid > 0 and pid_alive(pid), f"rc={rc} pid={pid}")
os.remove("dsh.pid")  # 模拟旧实例: 不受管理
rc, so, se = run(["stop", "--scan", "--match", "dummy_sleep", "--yes"],
                 env_extra={"DSH_CTL_SCAN_STUB": str(pid)})
print(so.strip())
check("T9 scan cleaned", rc == 0 and "已清理" in so, f"rc={rc} out={so}")
check("T9 dummy dead", pid > 0 and not pid_alive(pid))
rc, so, se = run(["stop", "--scan", "--match", "dummy_sleep", "--yes"],
                 env_extra={"DSH_CTL_SCAN_STUB": "0"})
check("T9 second scan none", rc == 0 and "未找到" in so, f"rc={rc} out={so}")

print("=== T10 real DSH on 3080 untouched ===")
check("T10 3080 alive", port_ok(3080))

print("=== T11 GUI selftest ===")
rc, so, se = run(["--gui-selftest"], timeout=30)
print(so.strip())
check("T11 GUI selftest OK", rc == 0 and "GUI_SELFTEST_OK" in so, f"rc={rc} out={so}")

print(f"\n===== {passed} passed, {failed} failed =====")
sys.exit(1 if failed else 0)
