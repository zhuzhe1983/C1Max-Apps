#!/usr/bin/env python3
"""Create a private bridge token and optionally pair exactly one ADB device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile

from server import CONFIG, load_config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="This Mac's LAN IPv4 address")
    parser.add_argument("--serial", help="ADB serial; omit to create Mac config only")
    parser.add_argument("--config", type=Path, default=CONFIG)
    parser.add_argument("--port", type=int, default=28766)
    parser.add_argument("--http-port", type=int, default=28767)
    args = parser.parse_args()
    socket.inet_aton(args.host)
    if not 1024 <= args.port <= 65535 or not 1024 <= args.http_port <= 65535 or args.port == args.http_port:
        parser.error("Choose two different ports between 1024 and 65535")
    args.config.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    token = load_config(args.config)["token"] if args.config.exists() else secrets.token_hex(32)
    config = {"host": args.host, "port": args.port, "http_port": args.http_port, "token": token}
    fd, temporary = tempfile.mkstemp(prefix="pair-", suffix=".json", dir=args.config.parent)
    try:
        with os.fdopen(fd, "w") as out:
            json.dump(config, out)
            out.write("\n")
        os.replace(temporary, args.config)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    if args.serial:
        adb = ["adb", "-s", args.serial]
        subprocess.run(adb + ["shell", "mkdir -p /storage/apps/data/cast && chmod 700 /storage/apps/data/cast"], check=True)
        # Transport contains the token; argv and logs never do.
        subprocess.run(adb + ["push", str(args.config), "/storage/apps/data/cast/game-bridge.json.tmp"], check=True)
        subprocess.run(adb + ["shell", "chmod 600 /storage/apps/data/cast/game-bridge.json.tmp && mv /storage/apps/data/cast/game-bridge.json.tmp /storage/apps/data/cast/game-bridge.json"], check=True)
        # Older C1 Max adbd does not propagate a remote command's exit code.
        # Compare the actual contents without ever printing the token.
        digest = hashlib.sha256(args.config.read_bytes()).hexdigest()
        observed = subprocess.check_output(adb + ["shell", "sha256sum /storage/apps/data/cast/game-bridge.json"], text=True).split()
        mode = subprocess.check_output(adb + ["shell", "ls -l /storage/apps/data/cast/game-bridge.json"], text=True).split()
        if not observed or observed[0] != digest or not mode or mode[0] != "-rw-------":
            raise RuntimeError("Device pairing contents or private permissions could not be verified")
        print("Paired C1 Max with this Mac. Select the Mac receiver in Settings.")
    else:
        print(f"Private bridge configuration created: {args.config}")


if __name__ == "__main__":
    main()
