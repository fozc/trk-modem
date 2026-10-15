#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Device web check: new fixed-map modbus config surface + legacy tolerance."""
import json
import socket
import sys

HOST = "188.59.76.59"
PORT = 80


def connect():
    s = socket.create_connection((HOST, PORT), timeout=15)
    s.settimeout(40)
    return s


def req(s, method, path, body=None, token=""):
    if token:
        path = path + ("&" if "?" in path else "?") + "t=" + token
    r = (f"{method} {path} HTTP/1.1\r\nHost: {HOST}\r\n"
         "Connection: keep-alive\r\n")
    if token:
        r += f"Authorization: Bearer {token}\r\n"
    if body is not None:
        r += (f"Content-Type: application/json\r\n"
              f"Content-Length: {len(body)}\r\n")
    r += "\r\n"
    s.sendall(r.encode() + (body.encode() if body else b""))
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = s.recv(4096)
        if not chunk:
            raise RuntimeError("server closed mid-header")
        data += chunk
    head, _, rest = data.partition(b"\r\n\r\n")
    length = 0
    for line in head.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1].strip())
    while len(rest) < length:
        chunk = s.recv(4096)
        if not chunk:
            break
        rest += chunk
    return int(head.split(b" ", 2)[1]), rest[:length].decode("utf-8", "replace")


def main():
    s = connect()
    try:
        st, body = req(s, "POST", "/auth/login",
                       '{"username":"admin","password":"admin60"}')
        print("login:", st)
        assert st == 200, body
        token = json.loads(body)["token"]

        st, body = req(s, "GET", "/config/modbus", token=token)
        print("GET modbusConfigs:", st, body)
        data = json.loads(body)
        keys = sorted(data.keys())
        print("anahtarlar:", keys)
        leftover = [k for k in keys if k == "Hat" or k.startswith("ADDR")]
        print("Hat/ADDR kalinti:", leftover)
        assert keys == ["BaudRate", "CihazID", "SonHataKodu",
                        "SonHataZamani"], keys
        assert not leftover

        # POST echoes exactly what was read: no unknown keys, no mutation.
        payload = json.dumps({"CihazID": data["CihazID"],
                              "BaudRate": data["BaudRate"]})
        st, body = req(s, "POST", "/config/modbus", payload, token=token)
        print("POST (okunan degerlerin echoes):", st, body[:120])
        assert st == 200, body

        st, body = req(s, "GET", "/config/modbus", token=token)
        print("GET tekrar:", st, body)
        after = json.loads(body)
        assert after["CihazID"] == data["CihazID"], "CihazID degisti!"
        assert after["BaudRate"] == data["BaudRate"], "BaudRate degisti!"
        print("SONUC: PASS - ayarlar korundu, modbus web yuzeyi sabit haritaya uygun")
    finally:
        s.close()


if __name__ == "__main__":
    sys.exit(main())
