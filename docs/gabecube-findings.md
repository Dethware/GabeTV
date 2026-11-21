# gabecube: findings

What we've learned about the Steam Machine by probing it. Updated as tests run.
Everything here was observed on the machine unless marked *inferred*.

## System (2026-11-21)

- SteamOS 3.9.5, build 20261023.100, `steamdeck_stable` Steam client branch.
- Kernel `7.2.7-valve2-2-neptune-72`. Board `Fremont` (Valve), 16 GB RAM.
- gamescope 3.16.30.1, Mesa 26.1.2, steamos-manager 26.4.1, SDDM 0.21.
- Output: `HDMI-A-1` to the old AMH monitor (EDID "A409U"), 60 Hz only, no HDR, no VRR.
- `deck` is in `wheel`, but sudo needs a password (none usable over SSH yet).

## How Gaming Mode starts

1. SDDM autologin: `/usr/lib/sddm/sddm.conf.d/holo.conf` sets `User=deck`,
   `Session=gamescope-wayland.desktop`, `Relogin=true`.
2. `gamescope-wayland.desktop` runs `start-gamescope-session`, which only does
   `systemctl --user --wait start gamescope-session.target`.
3. `gamescope-session.target` (in `/usr/lib/systemd/user/`):
   - `Requires/BindsTo=gamescope-session.service` → `/usr/lib/steamos/gamescope-session`:
     exports ~50 `STEAM_*`/`GAMESCOPE_*` feature flags, then
     `exec gamescope --xwayland-count 2 -e -R <socket> -T <stats> ...`.
     It writes the session environment to `$XDG_RUNTIME_DIR/gamescope-environment`.
   - `Upholds=steam-launcher.service` → `steam -steamos3 -steampal -steamdeck -gamepadui`
     (or `~/devkit-game/devkit-steam` if present).
   - `Wants=` mangoapp, ibus, steam-notif-daemon, galileo-mura-setup.
4. `steamos-manager` switches Game/Desktop mode by writing
   `zz-steamos-autologin.conf` / `zzt-steamos-temp-login.conf` into `/etc/sddm.conf.d/`
   (*inferred* from strings in the binary; the directory is empty now).
   `steamosctl get-default-login-mode` → `game`.

## What persists

- `/` is read-only btrfs (`steamos-readonly status` → enabled). `/usr/local` is read-only too.
- `/home`, `/var`, `/opt`, `/srv`, `/root`, `/nix`, `/var/lib/flatpak` are writable and persistent.
- `/etc` is an overlay (upper in `/var/lib/overlays/etc/upper`). An atomic update **drops all
  `/etc` changes** except the keep-list `/usr/lib/rauc/atomic-update-keep.conf`. Relevant kept paths:
  - `/etc/sddm.conf.d/*`
  - `/etc/systemd/system/*.service`, `*.service.d/**`, `*.wants/**`
  - `/etc/environment`
  - `/etc/atomic-update.conf.d/*.conf` (add our own keep entries here)
  - **Not kept:** `/etc/systemd/user/`.
- User unit search path puts `~/.config/systemd/user` before `/usr/lib/systemd/user`, so we can
  override or extend every Gaming Mode unit without root.
- logind has `KillUserProcesses=True`: processes started from SSH die when the SSH session ends.
  Start long-lived test processes with `systemd-run --user`.

## Custom session: options

- **User-unit drop-ins (no root):** extend or replace pieces of `gamescope-session.target`
  from `~/.config/systemd/user`. Autologin and the Desktop/Game switch keep working.
  Risk: a broken override makes the session restart-loop (SSH still works).
- **Own SDDM session (root):** both session dirs are read-only; a file in `/etc/sddm.conf.d/`
  could add a `/home` session dir, but would compete with steamos-manager's autologin files.

## gamescope focus control (tested 2026-11-21)

**Summary:** inside the stock session, with no changes to it, a program of ours can:
show itself by tagging its window `STEAM_GAME=<id>` and putting `<id>` first in
`GAMESCOPECTRL_BASELAYER_APPID`; launch games via `steam://rungameid/`; get the screen back
by re-claiming the base layer after Steam rewrites it on game launch/exit; and read the
controller from Steam's virtual Xbox pad. The Steam menu and QAM still work on top of it.

gamescope runs with `-e` (Steam integration) and reports
`Virtual connector strategy: SteamControlled`. Focus is decided by X properties on display `:0`.

| Step | Action | Result |
|---|---|---|
| 1 | Plain X11 window on `:0` | Mapped and resized to full screen, but **not focusable, not shown** |
| 2 | Set window property `STEAM_GAME=424242` (CARDINAL) | Listed in `GAMESCOPE_FOCUSABLE_APPS`, still not shown |
| 3 | Set root property `GAMESCOPECTRL_BASELAYER_APPID=424242,769` | **Shown immediately**; focus, input and keyboard focus on it |

- The first app ID in `GAMESCOPECTRL_BASELAYER_APPID` wins. 769 is Steam's own UI.
- Steam idle (screensaver): it did not rewrite the property during the 17 s we held it.
- Restoring the property to its previous value put Steam back on screen.
- Steam's normal value was `413091, 769`; 413091 is not identified yet.

### Game launch from our window (tested 2026-11-21)

Our window held the base layer (`424242, 769`), then we ran
`steam steam://rungameid/1233070` (Isles of Sea and Sky) from outside Steam, with the
environment from `gamescope-environment`. Focus log (0.5 s polling):

| Time | `BASELAYER_APPID` | Focused | Event |
|---|---|---|---|
| 14:47:19 | `424242, 769` | 424242 | our window on screen |
| 14:47:21 | | | `steam://rungameid/1233070` sent |
| 14:47:22 | `413091, 1233070, 769` | 769 | Steam **overwrote our value** at once |
| 14:47:28 | `413091, 1233070, 769` | 1233070 | game window up and on screen |
| 14:48:18 | | | game's scope stopped (`systemctl --user stop app-steam-app1233070-*.scope`) |
| 14:48:20 | `413091, 769` | 769 | Steam shows the game's library page; our window stays hidden |
| 14:48:55 | `424242, 769` | 424242 | we set it again: back on screen, held ≥ 15 s with Steam idle |

- The `steam://` URL works from outside Steam; the running client picks it up.
- Steam rewrites the base layer on launch and on exit, dropping our app ID both times.
  Our window stays focusable (`GAMESCOPE_FOCUSABLE_APPS` keeps 424242).
- So returning to our shell after a game means **watching the root properties and re-claiming**
  when the game's app ID disappears. Steam doesn't fight a re-claim while idle.
- The game ran on the second Xwayland (`:1`, `STEAM_MULTIPLE_XWAYLANDS=1`) in its own
  systemd scope `app-steam-app<id>-<pid>.scope`; stopping that scope is a clean way to kill a game.

### Steam button, Quick Access Menu, controller input (tested 2026-11-21)

Our window on the base layer; someone at the TV pressed controller buttons, the Steam button and the
**…** button (14:49–14:51).

- `GAMESCOPECTRL_BASELAYER_APPID` **did not change** during any of it: the Steam menu and
  the QAM are overlays above the base layer, not focus switches.
- Our window saw only X focus-out/focus-in pairs (8 of them) while the overlays were open.
  It got **no key or mouse events**: Steam Input doesn't map the controller to keyboard/mouse
  for an app ID it doesn't know.
- The Steam Controller's raw HID devices (`hidraw2-5`) are held by Steam. Its evdev nodes
  (`Steam Controller Puck`, event6/7/12/17) and Steam's virtual **"Microsoft X-Box 360 pad 0"**
  (`event19`, via uinput) are readable by `deck` through ACLs.
- Second round (14:51–14:52), evdev capture while our window had focus:
  - **Steam's virtual pad (`event19`) delivered everything**: buttons as `BTN_SOUTH/EAST/
    NORTH/WEST` (304/305/307/308), bumpers (310/311), Start (315); sticks on `ABS_X/Y/RX/RY`,
    triggers on `ABS_Z/RZ`, d-pad on `ABS_HAT0X/Y`. It also streams stick jitter at rest.
  - The controller's own evdev nodes (`Steam Controller Puck`) sent **nothing**: Steam has
    the hardware in its own mode.
  - So our shell can read the controller as a standard gamepad (evdev or SDL) from `event19`,
    with Steam Input still active. *Inferred:* the same pad feeds games, so the shell must
    ignore input whenever `GAMESCOPE_FOCUSED_APP` isn't its own app ID.
  - Not checked: whether the pad keeps sending while the Steam menu/QAM is open, and whether
    `event19` stays the same device across Steam restarts.
- `cecd` exposes an evdev device (`cecd cros-ec-cec`, event13): TV remote keys over CEC may
  arrive as plain key events.

Useful commands (with `DISPLAY=:0`, `XDG_RUNTIME_DIR=/run/user/1000`, `GAMESCOPE_WAYLAND_DISPLAY=gamescope-0`):

```bash
xprop -root GAMESCOPE_FOCUSED_APP GAMESCOPECTRL_BASELAYER_APPID GAMESCOPE_FOCUSABLE_APPS
gamescopectl focus_info      # dumps focus state into the gamescope-session journal
gamescopectl screenshot /tmp/x.png
```

## Waydroid prerequisites

- Kernel has the Rust binder driver built in (`CONFIG_ANDROID_BINDER_IPC_RUST=y`,
  `CONFIG_ANDROID_BINDER_DEVICES=""`); `binder` is a registered filesystem. No `/dev/binder*`
  exists until binderfs is mounted (needs root). Untested whether Waydroid works with the Rust driver.
- `waydroid` and `lxc` are not installed; installing via pacman means unlocking the rootfs
  and redoing it after each update. `podman` and `distrobox` are present.

## Other notes

- HDMI-CEC: `cecd` runs as a user service, `/dev/cec0` exists, state `extended`,
  remote passthrough enabled. `steamosctl` has wake/suspend TV and `hdmi-cec-make-active`.
- No Flatpaks and no browser installed.
- Present tools: `xprop`, `xdotool`, `xwininfo`, `gamescopectl`, `cec-ctl`, `ir-keytable`,
  `evtest`, `python3` (with tkinter and GTK3), `podman`, `distrobox`.

## Open / untested

- Does Steam rewrite `GAMESCOPECTRL_BASELAYER_APPID` on notifications, downloads finishing,
  or Steam restarting?
- What app ID 413091 is (Steam's default first base-layer entry).
- Input from the TV remote: CEC passthrough (`cecd` evdev) vs. a USB IR receiver.
- Whether a Wayland client (on `gamescope-0`) can be put on the base layer the same way
  as an X11 window.
