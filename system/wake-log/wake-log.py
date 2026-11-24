#!/usr/bin/env python3
"""Writes down, at every resume, what may have woken the machine.

The kernel doesn't say on its own what ended a sleep (s2idle on this machine): no log line,
and its per-device wake counters don't move for these wake-ups. So at each resume this
collects what there is and writes one report to the journal:

- how long the machine slept;
- the kernel's own lines from the sleep (with /sys/power/pm_debug_messages set to 1 they
  include "Triggering wakeup from IRQ …" and the ACPI wake events; see the README);
- the interrupt that ended it (/sys/power/pm_wakeup_irq) and what it belongs to;
- the wake counters that changed while it slept;
- what happened in the first seconds after: devices that came (input devices, Bluetooth
  connections) and controller presses the controller service saw. The first of those is
  often the culprit, or the person who woke it.

It follows logind's PrepareForSleep signal (through gdbus), needs no root, and only reads.
"""

import glob
import os
import re
import subprocess
import sys
import time

# How long after a resume to keep watching what comes and goes.
AFTER_SECONDS = 6
# Kernel lines worth having from the sleep.
KERNEL = re.compile(r"(?i:wakeup|wake up|waking|amd_pmc|s2idle)|GPE|IRQ \d|PM: suspend (entry|exit)|PM: Triggering")
# What came in the first seconds after a resume.
AFTER = re.compile(r"input: |connected|Connected|pressed|Home on|resumed", re.I)


def read(path, default=""):
    try:
        with open(path) as file:
            return file.read().strip()
    except OSError:
        return default


def counters():
    """Wake source name -> (events, wakeups), as the kernel counts them."""
    found = {}
    for source in glob.glob("/sys/class/wakeup/wakeup*"):
        found[read(source + "/name")] = (read(source + "/event_count", "0"), read(source + "/wakeup_count", "0"))
    return found


def irq_name(number):
    for line in read("/proc/interrupts").splitlines():
        if line.strip().startswith(number + ":"):
            return " ".join(line.split()[-2:])
    return "?"


def journal(since, until, *args):
    command = ["journalctl", "--no-pager", "-o", "short-precise", "--since", since, "--until", until, *args]
    return subprocess.run(command, capture_output=True, text=True).stdout.splitlines()


def stamp(seconds):
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(seconds))


def report(slept_at, before):
    woke_at = time.time()
    time.sleep(AFTER_SECONDS)
    lines = ["woke after %.0f s asleep (asleep since %s)" % (woke_at - slept_at, stamp(slept_at)[11:])]

    irq = read("/sys/power/pm_wakeup_irq")
    if irq:
        lines.append("wake interrupt: %s (%s)" % (irq, irq_name(irq)))

    after = counters()
    changed = ["%s %s→%s" % (name, before.get(name, ("0", "0"))[0], value[0])
               for name, value in sorted(after.items()) if value != before.get(name)]
    lines.append("wake counters that moved: " + (", ".join(changed) if changed else "none"))

    for line in journal(stamp(slept_at), stamp(woke_at + 1), "-k"):
        if KERNEL.search(line):
            lines.append("kernel: " + line.split(": ", 1)[-1])

    seen = 0
    for line in journal(stamp(woke_at), stamp(woke_at + AFTER_SECONDS), "--system") + \
            journal(stamp(woke_at), stamp(woke_at + AFTER_SECONDS), "--user"):
        if AFTER.search(line) and seen < 12:
            lines.append("after: " + line[16:])
            seen += 1

    for line in lines:
        print(line, flush=True)


def main():
    monitor = subprocess.Popen(
        ["gdbus", "monitor", "--system", "--dest", "org.freedesktop.login1", "--object-path", "/org/freedesktop/login1"],
        stdout=subprocess.PIPE, text=True)
    print("watching sleep and resume; pm_debug_messages is %s" % read("/sys/power/pm_debug_messages", "?"), flush=True)
    slept_at = None
    before = {}
    for line in monitor.stdout:
        if "PrepareForSleep (true" in line:
            slept_at = time.time()
            before = counters()
        elif "PrepareForSleep (false" in line and slept_at is not None:
            report(slept_at, before)
            slept_at = None
    return monitor.wait()


if __name__ == "__main__":
    sys.exit(main())
