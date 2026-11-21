# TV OS on the Steam Machine — project brief

A custom TV experience on a Valve Steam Machine, replacing a Google TV dongle, an HDMI switch and (eventually) a Nintendo Switch. This file carries over the context and decisions from a planning conversation.

## Working style

- The system's constraints are still being discovered. **Ask questions and discuss design decisions before writing larger amounts of code.** Don't fill gaps with assumptions.
- Prefer reasoning the design upfront, then a **scaffold** (a file layout resembling the end result) or one clear example to follow, then the implementation.

## Hardware and setup

### Current setup (what's connected today)
- **Display:** an older AMH 40" 4K monitor, 60 Hz only, plain HDMI with no smart features. The backlight is failing.
- **Routing:** a cheap HDMI switch between the monitor and three sources: the Steam Machine, a Google TV dongle and a Nintendo Switch dock.
- **Audio:** the Philips HTL3310 soundbar on an **analog** stereo cable. It's slow to wake on signal.
- No usable HDMI-CEC/eARC chain today. Anything display-related (refresh rates, VRR, HDR, CEC) can't be tested until the new TV is installed.

### Target setup (TV ordered, being installed)
Everything below describes the target unless noted.

- **Steam Machine**, hostname `gabecube`, stock SteamOS. Reachable with `ssh deck@gabecube`.
  - HDMI: HDMI 2.1 VRR works per Valve, output currently capped at 4K120. An update for 4K144 (FRL) / 4K240 (DSC) has been announced; check the SteamOS changelog.
  - HDMI-CEC on the Steam Machine is unreliable (spurious wake-from-sleep). Don't depend on it. The Steam Controller can wake the machine.
- **TV:** LG OLED G6 55" (`OLED55G64LW`), wall-mounted, **kept offline**. The Wi-Fi/Bluetooth module may be physically removed later, which means IR-only remote. Should boot to the last HDMI input.
- **Audio:** Philips HTL3310 soundbar on the TV's HDMI eARC port (Dolby Digital / LPCM 2ch).
- **Input:** Steam Controller, plus the **Google TV dongle's remote, paired to the Steam Machine over Bluetooth** (navigation keys). Its IR side keeps controlling the TV directly (power, volume). The TV stays a dumb display. Fallbacks: HDMI-CEC passthrough, or a USB IR receiver (FLIRC-style). Keyboard and mouse optional.
- The HDMI switch, Google TV dongle and Switch dock go away. The Steam Machine becomes the only source.
- The Steam Machine itself, its SteamOS install and SSH access are already in place, so software work can start now on the current monitor.

## Decisions so far

1. **Base: stock SteamOS for now**, because it gets new features first (HDMI 2.1, drivers). We may revisit a fully custom base in about a year.
   - Constraint: read-only root with A/B updates. Everything we build must live in `/home`, Flatpak, user systemd units, or (if verified to persist) `/etc`.
2. **Shell architecture: "Option B", our own gamescope session** with our home screen as the main client. Steam runs in the background for games, downloads and Steam Input. Games launch via `steam://rungameid/...`, and control returns to our shell afterwards.
   - Fallback ("B-lite"): Steam stays the session owner, and our shell auto-starts as the first thing shown.
3. **The UI concept is "places", not shelves.** It's a spatial library with stable positions that never auto-reorder, and it unifies every source (Steam, emulators, media apps, photos). It must be fully navigable with a d-pad or TV remote, with no mouse or keyboard needed.

## Apps and the plan for each

| Need | Current thinking | Notes |
|---|---|---|
| Jellyfin | Must be remote-friendly. Test Jellyfin Media Player TV mode and the web client's TV layout. The Android TV app works but needs configuration. | No desktop-isms allowed. |
| YouTube | Wants the TV-style UI with multiple accounts and remote navigation. **Test `youtube.com/tv` in a browser kiosk first** (the same leanback UI smart TVs use). | Android route possible but see Waydroid caveats. |
| Symfonium | Android-only → **Waydroid**. | Paid app, uses Google Play licensing, so it needs GApps and a registered device. Server type still to confirm. |
| Immich photos | **Build natively** in the shell: photo viewer and screensaver via the Immich API (albums, memories, random). | Good first module. On OLED: dim or switch off after idle; never a static image for hours. The current Android TV app's screensaver is broken. |
| Ring Fit Adventure | Ryujinx 1.3.3 runs the dump (`~/Games/switch/ringfit/`). | Ring-Con support is uncertain in Ryujinx; Eden (yuzu lineage) has it. |

### Waydroid caveats
- It needs binder in the kernel. Existing SteamOS install scripts often modify the read-only rootfs and break on updates. Check the kernel config first.
- Waydroid is regular Android, not Android TV, so the TV versions of apps may not appear or behave correctly.

## Joy-Con / Ring-Con (later)

- Plan: a small userspace program over `hidraw` using hidapi (cross-platform). Starting point: **SwitchKit** (https://github.com/Meorge/SwitchKit), which supports Joy-Con, Pro Controller and the Ring-Con. Also see dekuNukem's Nintendo_Switch_Reverse_Engineering.
- The kernel's `hid_nintendo` grabs the Joy-Cons. Either blacklist it or take over only while a Ring-Con game runs. Steam Input also grabs controllers, so disable it per game where needed.
- Mind licenses: yuzu/Suyu/Eden code is GPL-3.

## First task: reconnaissance on `gabecube`

Findings so far are in `docs/gabecube-findings.md`. Keep that file updated as tests run.

Map how the Gaming Mode session starts and what we can change without touching read-only parts:

```bash
cat /etc/os-release; uname -r
steamos-readonly status 2>&1
ls -la /usr/share/wayland-sessions/ /etc/sddm.conf.d/ 2>&1
cat /usr/share/wayland-sessions/*.desktop
cat /etc/sddm.conf.d/* 2>&1
which gamescope-session steamos-session-select 2>&1
zcat /proc/config.gz 2>/dev/null | grep -iE 'binder|ashmem'; lsmod | grep -iE 'binder|ashmem'
```

Questions to answer:
- Where are the session definition and autologin configured, and does `/etc` persist across SteamOS updates?
- Can a custom session be registered without modifying `/usr`?
- How does Steam behave when it isn't gamescope's main client? Launching games, focus, overlays, Steam Input.
- Is Waydroid viable without rootfs changes?

## Open questions

- Who uses the TV? Shared space vs. user profiles in the shell.
- Which server does Symfonium talk to (Navidrome, Jellyfin, other)?
- Idle/power policy: screensaver → dim → off. How do we turn the TV off without relying on the Steam Machine's CEC?

## Ideas drawer (not now)

- Spotify-Connect-like "move playback between devices" on an open stack, with desktop, Android and TV clients. Prior art to study: Music Assistant (Home Assistant ecosystem).