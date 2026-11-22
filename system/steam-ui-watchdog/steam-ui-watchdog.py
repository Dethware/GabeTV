#!/usr/bin/env python3
"""Ends Steam's interface process when it hangs, sooner than Steam does.

Steam's Gaming Mode interface is a web page in its own browser (steamwebhelper). Opening the
side menu sometimes sends the page's process into a spin: its main thread sits at 100 % of a
core and the interface stops responding, until Steam itself kills that process ("killing
unresponsive browser") and starts a new one. Steam takes one to five minutes for that.

This does the same after HANG_SECONDS. A hang is recognised by what it looks like: the main
thread of one of Steam's page processes at or above THRESHOLD of a core, without a break. In
healthy use those threads never came close (see docs/gabecube-findings.md).

Only the processes that show pages or draw them are watched: not Steam's main browser process
and not the "zygotes" the others are forked from. Every kill is written to the journal.
"""

import argparse
import os
import signal
import sys
import time

COMM = "steamwebhelper"
TICKS = os.sysconf("SC_CLK_TCK")


def read(path):
    with open(path) as file:
        return file.read()


def helpers():
    """pid -> (parent pid, CPU ticks of the main thread, start time in ticks), for each helper."""
    found = {}
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            if read(f"/proc/{entry}/comm").strip() != COMM:
                continue
            # After the name in parentheses: state, ppid, … (fields 3 onward of proc(5) stat).
            process = read(f"/proc/{entry}/stat").rsplit(")", 1)[1].split()
            thread = read(f"/proc/{entry}/task/{entry}/stat").rsplit(")", 1)[1].split()
        except (OSError, IndexError):
            continue  # gone in the meantime
        found[int(entry)] = (int(process[1]), int(thread[11]) + int(thread[12]), int(process[19]))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--hang-seconds", type=float, default=15, help="how long a process may spin (default 15)")
    parser.add_argument("--threshold", type=float, default=0.9, help="share of one core that counts as spinning (default 0.9)")
    parser.add_argument("--min-age", type=float, default=30, help="leave processes younger than this alone, in seconds (default 30)")
    parser.add_argument("--cooldown", type=float, default=20, help="pause after a kill, in seconds (default 20)")
    parser.add_argument("--interval", type=float, default=1, help="seconds between looks (default 1)")
    options = parser.parse_args()

    print(f"watching {COMM}: ending a page process after {options.hang_seconds:g} s at "
          f"{options.threshold:.0%} of a core or more", flush=True)
    ticks = {}        # pid -> main thread ticks at the last look
    spinning = {}     # pid -> when it started spinning (monotonic seconds)
    last_look = time.monotonic()
    last_kill = -options.cooldown
    while True:
        time.sleep(options.interval)
        now = time.monotonic()
        elapsed = now - last_look
        last_look = now
        found = helpers()
        parents = {parent for parent, _, _ in found.values()}
        uptime = float(read("/proc/uptime").split()[0])
        for pid, (parent, used, started) in found.items():
            before = ticks.get(pid)
            share = (used - before) / TICKS / elapsed if before is not None and elapsed > 0 else 0
            if share >= options.threshold:
                spinning.setdefault(pid, now - elapsed)
            else:
                spinning.pop(pid, None)
        ticks = {pid: used for pid, (_, used, _) in found.items()}
        for pid in list(spinning):
            if pid not in found:
                del spinning[pid]

        # Page processes only: a helper started by another helper, with none of its own.
        candidates = [pid for pid in spinning
                      if found[pid][0] in found and pid not in parents
                      and uptime - found[pid][2] / TICKS >= options.min_age
                      and now - spinning[pid] >= options.hang_seconds]
        if not candidates or now - last_kill < options.cooldown:
            continue
        pid = min(candidates, key=lambda candidate: spinning[candidate])
        print(f"Steam's interface process {pid} has been spinning for {now - spinning[pid]:.0f} s: "
              f"ending it (Steam starts a new one)", flush=True)
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError as error:
            print(f"could not end {pid}: {error}", file=sys.stderr, flush=True)
        spinning.pop(pid, None)
        last_kill = now


if __name__ == "__main__":
    main()
