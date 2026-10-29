#!/usr/bin/env python3
"""在模拟器的真实 MIPS Linux 串口执行命令。"""
import argparse
import json
import re
import secrets
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs="?", help="客体 shell 命令；省略时读取最近启动日志")
    parser.add_argument("--url", default="http://127.0.0.1:8775")
    parser.add_argument("--timeout", type=float, default=15)
    args = parser.parse_args()
    url = args.url.rstrip("/")
    def get(path):
        with urllib.request.urlopen(url + path, timeout=5) as response: return json.load(response)
    if args.command is None:
        print(get("/api/console")["text"]); return
    token = get("/api/state")["token"]
    marker = "C1SIM_" + secrets.token_hex(8)
    text = args.command + "; c1sim_status=$?; printf '\\n" + marker + " %s\\n' \"$c1sim_status\"\n"
    request = urllib.request.Request(url + "/api/console", json.dumps({"text": text}).encode(),
                                    {"Content-Type": "application/json", "X-C1Sim-Token": token})
    with urllib.request.urlopen(request, timeout=5) as response: json.load(response)
    deadline = time.monotonic() + args.timeout
    pattern = re.compile(r"(?:\r?\n)" + marker + r" (\d+)\r?\n")
    while time.monotonic() < deadline:
        output = get("/api/console")["text"]
        if match := pattern.search(output):
            start = output.rfind("# ", 0, output.find(marker))
            print(output[start + 2:match.start()].replace("\r", ""))
            raise SystemExit(int(match.group(1)))
        time.sleep(0.1)
    raise SystemExit("串口命令未在指定时间内结束；可用不带命令的 console.py 查看日志。")


if __name__ == "__main__": main()
