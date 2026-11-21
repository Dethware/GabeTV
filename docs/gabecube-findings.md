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
  - The pad is recreated when the session restarts (it got `event19` again by chance), and
    has no `/dev/input/by-id` link: find it by name.
  - So our shell can read the controller as a standard gamepad (evdev or SDL) from `event19`,
    with Steam Input still active. *Inferred:* the same pad feeds games, so the shell must
    ignore input whenever `GAMESCOPE_FOCUSED_APP` isn't its own app ID.
  - Not checked: whether the pad keeps sending while the Steam menu/QAM is open, and whether
    `event19` stays the same device across Steam restarts.
- `cecd` exposes an evdev device (`cecd cros-ec-cec`, event13): TV remote keys over CEC may
  arrive as plain key events.

### Steam restart (tested 2026-11-21)

Our window held the base layer; then `systemctl --user restart steam-launcher.service`.

- Steam was back in ~3 s and **reset the base layer to `413091, 769`**, taking the screen.
  Our window stayed focusable.
- `gamescope-session.target` has `Upholds=steam-launcher.service`: stopping Steam is
  immediately followed by a start. To change Steam's files while it's down, act within ~2 s
  of the stop, or stop the whole session.
- Every Steam start (also each Gaming Mode restart) plays Steam's **startup movie**, loud,
  before Big Picture appears.
- Steam's own settings for this live in its config: `"CurrentScreensaver" "steam-bouncinglogo"`
  and a startup-movie list (`GetStartupMovies`).
- Re-claim rule that covers launch, exit and restart: claim the base layer whenever its list
  holds only Steam's own entries (`413091`, `769`), never while a game's app ID is in it.
- `413091` isn't an installed app; it appears only in another app's depot list in Steam's
  cache. Probably a Steam-internal placeholder (*inferred*).

### Our program as a non-Steam shortcut ("B-lite", tested 2026-11-21)

Added with `steam steam://addnonsteamgame/<url-encoded path>` (after `touch /tmp/addnonsteamgamefile`;
this is what `steamos-add-to-steam` does). Steam wrote `userdata/<id>/config/shortcuts.vdf` at once;
shortcut app ID 3564817749, launched with `steam://rungameid/<(appid << 32) | 0x02000000>`.

| Time | `BASELAYER_APPID` | Focused | Event |
|---|---|---|---|
| 15:12:22 | | | `steam://rungameid/15310775648188891136` (our shortcut) |
| 15:12:23 | `413091, 3564817749, 769` | 769 | Steam inserts the shortcut's app ID |
| 15:12:32 | `413091, 3564817749, 769` | 3564817749 | our window on screen (~9 s after launch) |
| 15:12:34 | | | `steam://rungameid/1233070` (real game) sent while ours runs |
| 15:12:39 | `413091, 1233070, 769` | 1233070 | game on top, no prompt |
| 15:12:49 | | | game's scope stopped |
| 15:12:52 | `413091, 3564817749, 769` | 3564817749 | **Steam put our window back by itself** |

- Steam started it via `reaper SteamLaunch AppId=3564817749 -- <exe>` in scope
  `app-steam-app3564817749-<pid>.scope`, on Xwayland `:1`, with `SteamGameId`, `SteamAppId`,
  `SteamOverlayGameId` set. gamescope derived the app ID itself; no `STEAM_GAME` property needed.
- **A Steam dialog popped up on screen** during the launch: several controller images and
  text about an "external controller"; it had to be confirmed with the controller.
  *Inferred:* a per-game controller prompt Steam shows for a game it has no controller info
  for (a new non-Steam shortcut has none). It's not in Steam's
  logs. It likely explains part of the ~9 s start. Unknown whether it comes back on every launch.
- Steam Input applied the gamepad template `controller_neptune_gamepad_fps.vdf`: no
  keyboard/mouse events in our window; reading the virtual pad would still work.
- So in B-lite, Steam handles returning to our shell after games; no property juggling.
- Things to weigh (*inferred*, not tested): Steam considers a game running all the time
  (Steam's "downloads during gameplay" setting, idle/suspend behavior, the Steam menu showing
  our shell as the running game); slow (~9 s) start; our shell lives inside Steam's library.
- Removing the shortcut: stop Steam and delete `shortcuts.vdf` (it didn't exist before);
  Steam keeps a playtime entry for the app ID in `localconfig.vdf`.

### Native Wayland clients (tested 2026-11-21)

- gamescope's socket `gamescope-0` advertises `xdg_wm_base` v3 and `zwlr_layer_shell_v1` v4,
  although the session doesn't pass `--expose-wayland`. Full list: `wayland-info` with
  `WAYLAND_DISPLAY=gamescope-0`.
- A GTK3 window with `GDK_BACKEND=wayland` connects and maps without errors, but is
  **never focusable or shown**, even with `424242` first on the base layer. Tried giving it an
  app ID three ways, none worked:
  1. `SteamLaunch AppId=424242` in its own command line;
  2. the same on a parent process;
  3. running it in a systemd scope named `app-steam-app424242-tvprobe.scope`
     (gamescope logs "AppID %u derived from cgroup for pid %d", so it reads cgroups somewhere).
- An X11 window in a scope named the same way didn't get an app ID either; it has no
  `_NET_WM_PID`, which may matter. Setting `STEAM_GAME` is the only route that worked.
- **Consequence:** in the stock session our shell has to be an X11 client (e.g. Qt with
  `QT_QPA_PLATFORM=xcb`, Chromium/Electron on X11).

#### With `--expose-wayland` (tested 2026-11-21, then rolled back)

Added via a user drop-in, no root and nothing in `/usr` touched:
`~/.config/systemd/user/gamescope-session.service.d/*.conf` set `Environment=PATH=<dir>:...`,
where `<dir>/gamescope` is a wrapper running `/usr/bin/gamescope --expose-wayland "$@"`.
This works because `/usr/lib/steamos/gamescope-session` runs `gamescope` by name.
Restarting Gaming Mode = `systemctl --user stop gamescope-session.target` (run via
`systemd-run --user`); SDDM's `Relogin=true` starts it again in ~40 s, with Steam.

- Session came up normally with the flag: Steam UI, mangoapp, all units active.
- The GTK Wayland window is **still not** in `GAMESCOPE_FOCUSABLE_*` (those list X windows only).
- Wayland windows get **app ID 0**, also when run in an `app-steam-app424242-*` scope.
- `GAMESCOPECTRL_BASELAYER_APPID=0` → the Wayland window **is shown** (gamescope's focus
  window `0x2`, appID 0) and gets keyboard focus. `424242` doesn't select it.
- GTK draws client-side decorations (title bar) unless the window goes fullscreen.
- Caveats: app ID 0 isn't unique to us; any other app-ID-less Wayland window would compete.
  No known way yet to give a Wayland window a real app ID.
- gamescope also logs that it looks for scripts in `/etc/gamescope/scripts` and
  `~/.config/gamescope/scripts` (both absent); see "gamescope Lua scripts" below.

Useful commands (with `DISPLAY=:0`, `XDG_RUNTIME_DIR=/run/user/1000`, `GAMESCOPE_WAYLAND_DISPLAY=gamescope-0`):

```bash
xprop -root GAMESCOPE_FOCUSED_APP GAMESCOPECTRL_BASELAYER_APPID GAMESCOPE_FOCUSABLE_APPS
gamescopectl focus_info      # dumps focus state into the gamescope-session journal
gamescopectl screenshot /tmp/x.png
```

## gamescope Lua scripts and convars (checked 2026-11-21)

- gamescope runs `*.lua` from `/usr/share/gamescope/scripts`, `/etc/gamescope`, and
  `~/.config/gamescope/` (user dir, no root, persistent). README: `/usr/share/gamescope/scripts/README.md`;
  it calls the system experimental and liable to change between versions.
- What scripts can do:
  - Define **known displays** (`gamescope.config.known_displays.<name>`): HDR metadata,
    colorimetry, refresh-rate lists, mode generation, matched by EDID vendor/product.
    Valve ships these for handheld panels only; one for the LG G6 could go here if needed.
  - Set **convars** (`gamescope.convars.<name>.value`).
  - **One hook only:** `OnPostPaint` (every frame). No window, focus or input hooks, so
    scripts **can't** replace the base-layer approach.
- Convars can also be changed at runtime: `gamescopectl <convar> <value>`. Full list: `gamescopectl help`.

### Screen off without CEC (tested 2026-11-21)

`gamescopectl drm_sleep_external_screen 1` → `/sys/class/drm/card0-HDMI-A-1/dpms` went
`Off` and `enabled` → `disabled` (HDMI signal stops); `... 0` → back `On` within 3 s.
Steam kept focus throughout.

- This is a candidate for the idle policy (screensaver → dim → **signal off**), with no CEC.
- Untested: what the LG G6 does on signal loss (standby after a timeout?), and how it
  wakes when the signal returns. Nothing wakes the output automatically: our shell would
  set it back to `0` on input.

## Google TV remote over Bluetooth (tested 2026-11-21)

Remote: Chromecast with Google TV (2020) voice remote. Pairing mode: hold **Back + Home**
until the light pulses.

- Paired from SSH with `bluetoothctl --agent NoInputNoOutput` (`pair`, `trust`, `connect`).
  The first `pair` timed out after 20 s, the second succeeded. No root needed.
- Shows up as BLE HID "Chromecast Remote" (`18D1:9450`, hid-generic via uhid), one evdev
  device, plus Battery Service (battery level readable via `bluetoothctl info`).
- Stayed connected to gabecube for 5+ minutes after pairing; the dongle didn't take it back.
- udev doesn't classify it as a keyboard (`ID_INPUT_KEY` only, no `ID_INPUT_KEYBOARD`), so
  it gets **no `uaccess` tag**: `deck` can't read its evdev node; only root can.
- gamescope does pick it up (it opens devices via logind). Keys our X11 window received:

  | Button | X11 keysym (keycode) |
  |---|---|
  | Ring up/down/left/right | `Up` (111) `Down` (116) `Left` (113) `Right` (114) |
  | Back | `XF86Back` (166) |
  | Home | `XF86HomePage` (180) |
  | Mute | `XF86AudioMute` (121) |
  | Volume rocker (side) | `XF86AudioRaiseVolume` (123) / `XF86AudioLowerVolume` (122) |
  | Power | `XF86ScreenSaver` (160) |
  | **OK (centre)**, Assistant, YouTube, Netflix, Input (bottom right) | nothing |

- *Inferred:* OK, YouTube, Netflix and Assistant send evdev codes above 247 (e.g.
  `KEY_SELECT` = 353), which X11 keycodes (≤ 255 = evdev + 8) can't carry, so gamescope
  can't deliver them to X11 clients (Steam's UI included). Input is probably IR-only, or
  also a high code. Not verified: needs root to read the evdev codes.
- Power arriving over Bluetooth means the shell can react to it (e.g. screen off / sleep),
  besides whatever the remote's IR side does to the TV.
- Fix options (both need root and an `/etc/atomic-update.conf.d/` keep entry):
  1. udev **hwdb remap** (`/etc/udev/hwdb.d/`, match `evdev:input:b0005v18D1p9450*`):
     OK → `enter`, YouTube/Netflix/Assistant → spare keys. Works for every X11 client.
  2. udev rule adding `uaccess` (or a group) for the remote, so our shell reads evdev directly.

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
- Current monitor: the connector lists a 3840x2160 mode, but gamescope's usable list tops
  out at 2560x1440@60, and our full-screen windows were 2560x1440. *Inferred:* 4K is only
  available below 60 Hz through the old switch/monitor. Irrelevant once the G6 is in.
- Audio: one sink, HDMI to the monitor (`A409U`); the soundbar hangs off the monitor's analog out.
- Present tools: `xprop`, `xdotool`, `xwininfo`, `gamescopectl`, `cec-ctl`, `ir-keytable`,
  `evtest`, `python3` (with tkinter and GTK3), `podman`, `distrobox`.

## Open / untested

- Does Steam rewrite `GAMESCOPECTRL_BASELAYER_APPID` on notifications or downloads finishing?
- Remote: real evdev codes of OK/YouTube/Netflix/Assistant/Input (needs root); does the
  dongle reclaim the remote if both are powered.
- How to give a Wayland window a real app ID with `--expose-wayland`.
- How gamescope's "derived from cgroup/process hierarchy" app-ID lookup is triggered. It
  worked for the Steam-launched shortcut, but not for our own X windows on `:0` or `:1` with
  `SteamLaunch AppId=<id>` in their own or a parent's command line, nor in an
  `app-steam-app<id>-*` scope, nor for a GTK X11 window with `_NET_WM_PID` set.
  *Inferred:* Steam assigns it for programs it launches. For programs we start ourselves,
  `STEAM_GAME` is the only method that works.
- With our shell as a non-Steam shortcut: Steam menu/QAM behavior, downloads, idle/suspend.
