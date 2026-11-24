# Wake log

Writes down, every time the Steam Machine wakes from sleep, what may have woken it: how long
it slept, the interrupt that ended the sleep, the kernel's own lines from the sleep, and what
came in the first seconds after (devices connecting, controller presses). For finding out
what wakes the machine when nobody is there.

It only reads and changes nothing. It has nothing to do with the TV shell and works without it.

## Install

    ./install.sh             # copy it to the Steam Machine, enable and start it (no root)
    ./install.sh --log       # the reports so far
    ./install.sh --remove    # stop and remove it

It runs as the user service `wake-log.service`. Another machine: `TARGET=user@host ./install.sh`.

For the kernel to name the interrupt that ended a sleep (`PM: Triggering wakeup from IRQ …`),
`/sys/power/pm_debug_messages` has to be 1. That needs root, so `./install.sh` prints the
commands to run once; they install `tv-wake-log.conf` in `/etc/tmpfiles.d/` (sets it at every
boot) and keep it through SteamOS updates.

## Reading a report

    woke after 5911 s asleep (asleep since 21:34:10)
    wake interrupt: 9 (9-fasteoi acpi)
    wake counters that moved: none
    kernel: PM: suspend entry (s2idle)
    kernel: PM: suspend exit
    after: … input: Chromecast Remote as …

Interrupt 9 is ACPI, which most wake-ups go through; the kernel's debug lines then say which
ACPI event it was. The remote connects right after every wake, whatever woke the machine, so
an `after:` line for it alone proves nothing. A Joy-Con press or a Bluetooth connection from
something else is a better clue.
