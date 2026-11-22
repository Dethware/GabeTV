# Steam interface watchdog

Steam's Gaming Mode interface sometimes hangs when its side menu opens: the screen freezes,
partly blurred, and takes no input until Steam notices and restarts its interface, one to
five minutes later. This small service notices after 15 seconds and ends the stuck process;
Steam starts a new one at once, as it does after its own kill.

It has nothing to do with the TV shell and works without it.

## Install

    ./install.sh             # copy it to the Steam Machine, enable and start it (no root)
    ./install.sh --log       # what it has done so far
    ./install.sh --remove    # stop and remove it

It runs as the user service `steam-ui-watchdog.service`, started with every Gaming Mode
session. Another machine: `TARGET=user@host ./install.sh`.

## What to expect

- When Steam's interface hangs, it comes back about 15 seconds later instead of minutes.
  The journal gets a line for each time (`./install.sh --log`).
- A false alarm would look the same: Steam's interface reloading for a few seconds while you
  were using it. If that happens without a hang, the log line says when; raise
  `--hang-seconds` in the service file's `ExecStart`.
- It ends the process outright, so Steam's own crash report for the hang is not produced.

How a hang is recognised, and the measurements behind the numbers, are in the script's
header and in `docs/gabecube-findings.md`.
