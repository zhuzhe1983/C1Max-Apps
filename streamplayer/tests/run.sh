#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 - "$app_dir" <<'PY'
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

app = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix="c1max-timeline-test-") as directory:
    for name in ["timeline", "video", "hls", "subtitle", "subtitle_stream"]:
        binary = str(Path(directory) / (name+"-test"))
        command = shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O1", "-g",
            "-fsanitize=address,undefined,float-cast-overflow", "-fno-omit-frame-pointer",
            "-I", str(app / "src"), str(app / ("tests/"+name+"_test.cpp")), "-o", binary,
        ]
        if name == "subtitle": command.insert(-2, str(app / "src/subtitles.cpp"))
        if name == "subtitle_stream": command[-2:-2] = [str(app / "src/subtitles.cpp"), str(app / "src/subtitle_stream.cpp"), "-I", str(app.parent / "shared"), "-isystem", str(app.parent / ".build/include"), "-pthread"]
        subprocess.run(command, check=True)
        environment = dict(os.environ)
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([binary], env=environment, check=True)
PY
