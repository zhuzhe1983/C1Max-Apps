# Calendar and Terminal device GUI smoke tests

`device_smoke.py` runs on the host. `prepare` and `self-test` never invoke ADB.
Only the three named device-test commands inject input. The script does not
start/stop system services, change the launcher manifest, replace executables,
or kill applications on failure. Run device tests serially, with one operator
owning the foreground GUI.

## Prepare and foreground ownership

```sh
python3 apps/tests/device_smoke.py self-test
python3 apps/tests/device_smoke.py prepare apps/.build/device-smoke-session
```

The new host directory contains `session.json`, `calendar.sh`, and `terminal.sh`.
The JSON names a unique device directory `/storage/apps/.smoke-<12 hex digits>`.
Deploy both wrappers into that exact directory, preserve executable permissions,
and make the key/touch helpers available:

| Local build file | Default test location on device |
| --- | --- |
| `apps/.build/mips/c1max-key-test` | `/tmp/c1max-key-test` |
| `apps/.build/mips/c1max-touch` | `/tmp/c1max-touch` |

They are CMake targets, but intentionally are not part of the normal app
payload. `--key-helper` and `--touch-helper` override their device paths.
Screenshots use the already packaged
`/storage/apps/current/shared/c1max-capture`.

Each wrapper exports `C1_APPS_ROOT=/storage/apps/current` and an isolated
`C1_APPS_DATA=<session>/data`, writes its PID to `<session>/<app>.pid`, then
executes the **normal installed application binary**. Launch one wrapper using
the operator's existing foreground supervision. Valid contexts are:

- The formal launcher has launched the wrapper as its child and is waiting for
  it. A temporary QA manifest can point to these wrappers; the launcher itself
  still uses its normal data path and binary.
- No other application or stock desktop is running, and the operator has
  separately taken responsibility for restoring the normal foreground session.

Do not merely SIGSTOP an independently running launcher. Its evdev queues retain
test taps/keys, which it would process on resume. The helper rejects an existing
launcher unless it is the app's direct parent. It also rejects a running stock
desktop, another known GUI app, an unexpected executable, an ordinary data
directory, or a changed PID/starttime. The normal `launcher/run.sh` unconditionally
exports its own `C1_APPS_DATA`, so setting this outside `run.sh` does not isolate
its children; put the override in the per-app wrappers.

No original calendar, imported ICS, subscription, terminal history, shell
profile or SSH key is read or written by these tests. The script refuses to run
its create/output stages against existing test records, so stale files cannot
make a repeated failed input sequence appear successful.

## Execute

After launching `calendar.sh`, read its PID from the session directory. Use an
explicit serial or set `ANDROID_SERIAL`:

```sh
python3 apps/tests/device_smoke.py calendar-create \
  --session apps/.build/device-smoke-session/session.json --pid CALENDAR_PID
```

This types a new title, changes both dates to **2031-04-09**, saves it, edits the
same ID, exercises the real Backspace key, saves again, captures the detail
screen, then presses POWER and verifies the application exited. It checks the
serialized event after exit and records the original process identity in the
host session JSON.

Launch `calendar.sh` again, obtaining the new PID, then run:

```sh
python3 apps/tests/device_smoke.py calendar-reload-delete \
  --session apps/.build/device-smoke-session/session.json --pid NEW_CALENDAR_PID
```

The second stage requires a genuinely new process. It opens the all-events list,
enters the persisted event, appends text through the editor, and verifies that
the original event ID was changed. This proves the UI loaded the stored event.
It then confirms deletion, checks the empty on-disk store, captures the list,
and returns with POWER. It never invokes ICS sync or changes the device clock.

Next launch `terminal.sh`, and run:

```sh
python3 apps/tests/device_smoke.py terminal \
  --session apps/.build/device-smoke-session/session.json --pid TERMINAL_PID
```

This physically types:

```sh
printf '%s\n' 'c1_smoke=ok' | tee shellproof
pwd > homeproof
exit 0
```

The first two files are created relative to the isolated terminal HOME. Their
contents prove that the normal key mapper, prefix state machine, PTY and shell
executed the commands. The test checks the shell child exited while the
terminal application remained alive, captures its final status, then presses
POWER and verifies application exit. It does not infer rendered text from the
file checks: inspect `terminal-output.png` for `c1_smoke=ok` and
`terminal-shell-exited.png` for `Shell exited (0)`.

Screenshots and raw frames are saved under the host session's `artifacts/`.
With host `ffmpeg`, raw portrait BGRA frames are rotated to 800×340 PNGs. Without
ffmpeg, the fresh raw frames remain available. Each ADB shell call uses a unique
completion marker because the old adbd may return host exit status zero after
a remote failure. Captures additionally require `CAPTURED bytes=1088000`; a
failed pull cannot reuse an older local frame.

On any failure the current screen, test files and logs remain for inspection.
The operator can leave the app with POWER and restore its usual launcher
session. Remove only the explicitly recorded session directory after review;
do not clear `/storage/apps/data` or remove other smoke sessions.

## Source-derived coordinates and key map

Coordinates are logical landscape pixels. Physical event2 coordinates are
`ABS_X=y`, `ABS_Y=799-x`. `Device.tap(x,y)` applies this rotation and validates
bounds. Existing `c1max-touch` arguments are `ax0 ay0 ax1 ay1 steps step_ms`;
the helper uses a 100 ms stationary tap.

| Calendar control | Logical center | Native touch |
| --- | --- | --- |
| Month: New | (658, 26) | (26, 141) |
| Month: View selected day | (662, 271) | (271, 137) |
| Editor: title | (400, 95) | (95, 399) |
| Editor: start date | (100, 163) | (163, 699) |
| Editor: end date | (422, 163) | (163, 377) |
| Editor: Save | (620, 26) | (26, 179) |
| Detail: Edit | (106, 281) | (281, 693) |
| Detail: Delete | (304, 281) | (281, 495) |
| Delete dialog: Confirm | (554, 241) | (241, 245) |

Calendar shortcuts: `N` opens a new event with title focused; Enter advances
through title → start date → start time → end date → end time → location → note.
On a detail page `E` edits and `D` asks for deletion. `L` from Month opens every
local event, independent of the currently displayed month. The first event is
initially focused, so Enter opens it.

| Physical event | Effect |
| --- | --- |
| event0 + letter Linux codes | QWERTY text |
| event0: Shift 42 held with QWERTYUIOP | `1234567890` |
| event0: Shift 42 held with ASDFGHJKLZXCVBNM | `~@#$%&*().-/?:;,` |
| event1: 111 | Actual top-right Backspace |
| event1: 14 | Middle Return: calendar back; terminal Escape |
| event1: 28 | Enter |
| event1: 116 | POWER: leave the app |
| event1: 410 (`KEY_SHUFFLE`) | Terminal Symbol prefix |

Terminal Symbol is one-shot: once + A–Z gives Ctrl; twice + WASD gives arrows;
three times + the following letters supplies the remaining punctuation:

```text
Q W E R T Y U I O P A S D F G H
= + _ | \ " ' < > ! [ ] { } ` ^
```

`text_keys(text, terminal=True)` returns the physical chord sequence;
`Device.actions()` sends it in bounded shell batches. No nonexistent PC quote,
semicolon or equals keys are assumed. Generated test text is lowercase and
starts from a freshly opened app, whose keymap resets Caps to off.

Host-only checks cover rotation, Shift and Symbol encoding, stale/missing/failing
ADB markers, store parsing and wrapper shell syntax. Device GUI results are
only established after the operator runs the three test stages and reviews the
captured output/status images.
