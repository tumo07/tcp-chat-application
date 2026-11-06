#!/usr/bin/env python3
"""
Automated Test Suite for Multi-Threaded TCP Chat Application (Max 10 Concurrent Clients)
"""

import socket
import subprocess
import time
import sys
import os

TEST_PORT = 9999
HOST = "127.0.0.1"

def print_banner(text):
    print("\n" + "=" * 65)
    print(f"  🧪 TEST: {text}")
    print("=" * 65)

def clean_output(b):
    # Remove ANSI escape sequences
    text = b.decode("utf-8", errors="ignore")
    import re
    return re.sub(r'\x1b\[[0-9;]*[mK]', '', text)

def start_server():
    print(f"[*] Starting ./server on port {TEST_PORT}...")
    proc = subprocess.Popen(
        ["./server", str(TEST_PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    time.sleep(0.5)
    if proc.poll() is not None:
        out, err = proc.communicate()
        print(f"[ERROR] Server failed to start: {err}")
        sys.exit(1)
    return proc

def connect_client(name):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2.0)
    s.connect((HOST, TEST_PORT))
    # Send username
    s.sendall((name + "\n").encode())
    # Read welcome message
    welcome = clean_output(s.recv(4096))
    return s, welcome

def main():
    server_proc = start_server()
    try:
        # TEST 1: Basic Connection & Welcome Handshake
        print_banner("1. Handshake & Welcome Announcement")
        s1, welcome1 = connect_client("Alice")
        assert "Welcome to TCP Chat Application, Alice!" in welcome1
        print("  [PASS] Alice connected and received welcome banner.")

        # TEST 2: Broadcast to second client
        print_banner("2. Broadcast on Client Join & Messaging")
        s2, welcome2 = connect_client("Bob")
        assert "Welcome to TCP Chat Application, Bob!" in welcome2
        print("  [PASS] Bob connected.")

        # Check that Alice received notice of Bob's join
        alice_notify = clean_output(s1.recv(4096))
        assert "Bob has joined the chat room" in alice_notify
        print("  [PASS] Alice received notification: 'Bob has joined the chat room'.")

        # Bob sends a message
        s2.sendall(b"Hello everyone from Bob!\n")
        time.sleep(0.2)
        alice_got_msg = clean_output(s1.recv(4096))
        assert "[Bob]: Hello everyone from Bob!" in alice_got_msg
        print(f"  [PASS] Alice received broadcast message from Bob: {alice_got_msg.strip()}")

        # TEST 3: User list command (/list)
        print_banner("3. Command: /list (Online Users with Worker TIDs)")
        s1.sendall(b"/list\n")
        time.sleep(0.2)
        list_output = clean_output(s1.recv(4096))
        assert "Alice (You)" in list_output
        assert "Bob" in list_output
        assert "Online Users (2/10)" in list_output
        print(f"  [PASS] /list output verified:\n{list_output.strip()}")

        # TEST 4: Linux Kernel Thread Diagnostics (/threads)
        print_banner("4. Command: /threads (Linux Kernel Thread Inspection)")
        s1.sendall(b"/threads\n")
        time.sleep(0.2)
        threads_output = clean_output(s1.recv(4096))
        assert "Linux Kernel Thread Info" in threads_output
        assert "Server Process PID (TGID)" in threads_output
        assert "Your Handler Thread (TID)" in threads_output
        print(f"  [PASS] /threads output verified:\n{threads_output.strip()}")

        # TEST 5: Private Direct Message (/msg)
        print_banner("5. Command: /msg (Private Direct Message)")
        s1.sendall(b"/msg Bob Hey Bob, this is a secret!\n")
        time.sleep(0.2)
        bob_priv = clean_output(s2.recv(4096))
        alice_confirm = clean_output(s1.recv(4096))
        assert "[Private from Alice]: Hey Bob, this is a secret!" in bob_priv
        assert "[Private to Bob]: Hey Bob, this is a secret!" in alice_confirm
        print("  [PASS] Private message delivered to recipient and confirmed to sender.")

        # Clean up s1 and s2 for next test
        s1.close()
        s2.close()
        time.sleep(0.5)

        # TEST 6: Maximum 10 Concurrent Clients Capacity Enforcement
        print_banner("6. Maximum 10 Concurrent Clients & Overflow Rejection")
        clients = []
        print("[*] Connecting 10 concurrent clients (User1 through User10)...")
        for i in range(1, 11):
            name = f"User{i}"
            s, welcome = connect_client(name)
            clients.append(s)
            time.sleep(0.05)
        print("  [PASS] Successfully connected 10 concurrent clients.")

        # Now connect client #11 (should be REJECTED)
        print("[*] Attempting to connect 11th client (overflow test)...")
        s11 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s11.settimeout(2.0)
        s11.connect((HOST, TEST_PORT))
        overflow_response = clean_output(s11.recv(4096))
        print(f"  [SERVER RESPONSE TO CLIENT 11]: {overflow_response.strip()}")
        assert "Room is full (Maximum 10 clients reached)" in overflow_response
        
        # Verify socket was closed by server
        data = s11.recv(1024)
        assert len(data) == 0  # EOF indicates socket closed
        s11.close()
        print("  [PASS] 11th client was immediately rejected and socket closed by server!")

        # TEST 7: Slot Release and Re-connection
        print_banner("7. Client Disconnect & Slot Re-use")
        print("[*] Disconnecting User1 to free up 1 slot...")
        clients[0].sendall(b"/quit\n")
        clients[0].close()
        clients.pop(0)
        time.sleep(0.3)

        print("[*] Attempting to connect new user 'User11_New' in freed slot...")
        s_new, welcome_new = connect_client("User11_New")
        assert "Welcome to TCP Chat Application, User11_New!" in welcome_new
        clients.append(s_new)
        print("  [PASS] New user successfully joined now that total count is 10/10.")

        # Clean up all clients
        for c in clients:
            try:
                c.sendall(b"/quit\n")
                c.close()
            except Exception:
                pass

        print_banner("ALL TESTS PASSED SUCCESSFULLY! 100% SPEC COMPLIANCE")

    finally:
        server_proc.terminate()
        server_proc.wait()

if __name__ == "__main__":
    main()
