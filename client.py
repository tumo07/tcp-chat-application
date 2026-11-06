#!/usr/bin/env python3
"""
Cross-Platform Python Chat Client for tcp-chat-application
Works natively on Windows, macOS, and Linux without compiling!
"""

import socket
import threading
import sys
import os

DEFAULT_IP = "127.0.0.1"
DEFAULT_PORT = 8888

def receive_thread(sock):
    while True:
        try:
            data = sock.recv(2048)
            if not data:
                print("\r\033[2K\n[NOTICE] Disconnected from server.")
                break
            # Print incoming message and redraw prompt
            msg = data.decode("utf-8", errors="replace")
            sys.stdout.write(f"\r\033[2K{msg}> ")
            sys.stdout.flush()
        except Exception:
            break
    os._exit(0)

def main():
    server_ip = sys.argv[1] if len(sys.argv) >= 2 else DEFAULT_IP
    port = int(sys.argv[2]) if len(sys.argv) >= 3 else DEFAULT_PORT

    print("========================================================")
    print("         💬 TCP CHAT CLIENT (PYTHON CROSS-PLATFORM)")
    print(f"  - Target Server : {server_ip}:{port}")
    print("========================================================")

    # Prompt for username
    while True:
        try:
            name = input("Enter your username (2-30 characters): ").strip()
        except (KeyboardInterrupt, EOFError):
            print("\nExiting.")
            sys.exit(0)

        if len(name) < 2:
            print("Username too short! Please enter at least 2 characters.")
            continue
        if len(name) > 30:
            print("Username too long! Maximum 30 characters.")
            continue
        break

    # Connect to server
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((server_ip, port))
    except Exception as e:
        print(f"[FATAL] Failed to connect to {server_ip}:{port} -> {e}")
        sys.exit(1)

    # Send username handshake
    sock.sendall((name + "\n").encode("utf-8"))

    # Start background receive thread
    t = threading.Thread(target=receive_thread, args=(sock,), daemon=True)
    t.start()

    # Main send loop
    while True:
        try:
            sys.stdout.write("> ")
            sys.stdout.flush()
            line = sys.stdin.readline()
            if not line:
                break
            clean_line = line.strip()
            if not clean_line:
                continue

            # Send message or command to server
            sock.sendall((clean_line + "\n").encode("utf-8"))

            if clean_line.lower() in ["/quit", "/exit"]:
                break
        except (KeyboardInterrupt, EOFError):
            try:
                sock.sendall(b"/quit\n")
            except Exception:
                pass
            break

    try:
        sock.close()
    except Exception:
        pass
    print(f"\nExited chat application. Goodbye, {name}!")

if __name__ == "__main__":
    main()
