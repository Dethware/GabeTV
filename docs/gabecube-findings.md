# gabecube: findings

The project's log: what we've learned about the Steam Machine and the shell by probing and
using them, with dates. Entries are added, not rewritten; a later entry corrects an earlier
one. Everything here was observed on the machine unless marked *inferred*. How things work
now is in [shell/DEVELOPMENT.md](../shell/DEVELOPMENT.md); how to use them in
[shell/README.md](../shell/README.md).

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

### Steam button, Quick Access and Home while the shell has the screen (tested 2026-11-21)

- Steam keeps the Steam and … buttons (they never reach SDL). Its main menu and Quick
  Access are drawn **in Steam's own window**, so with the shell on top they open unseen
  (only Steam's sounds play).
- The shell's window does **not** lose X focus when they open (a Tk test window did earlier).
- Steam button: Steam writes root `GAMESCOPE_BLUR_MODE` (value 0) each time its main menu
  opens; nothing when it closes. The shell uses that to step aside (works).
- Quick Access (…): **no X property change at all**, neither on the root nor on Steam's
  window (`STEAM_OVERLAY`, `STEAM_INPUT_FOCUS`, `STEAM_NOTIFICATION` unchanged). Only Steam's
  logs show it: `logs/webhelper.txt` `QuickAccess_uid2-'QuickAcces': WasHidden 0` (open) /
  `WasHidden 1` (closed); `logs/controller_ui.txt` `Guide button sent to JS` for the Steam
  button.
- Remote Home: a passive key grab on the root window (`xcb_grab_key`, keycode 180 =
  `XF86HomePage`) delivers it to the shell even while Steam has focus (works).
- Controller: **while Steam's own UI has focus, Steam doesn't feed its virtual pad** (no
  events at all reach SDL), so no controller gesture can reach the shell then. Buttons are
  seen only while the shell (or a game) has focus.
- Steam's Big Picture shortcut **Ctrl+2** toggles Quick Access (sent with XTest /
  `xdotool key ctrl+2` while Steam is focused; logged as `WasHidden 0`/`1`). Ctrl+1 is
  presumably the main menu (not tried).
- Handing the screen to Steam right after Quick Access opened: once Steam closed it again
  ~1 s later (focus change), twice it stayed open.
- Steam's UI is Chromium (CEF); Decky Loader hooks it via CEF remote debugging
  (`.cef-enable-remote-debugging`, not enabled here). Option for deeper integration.

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
  The shell uses it now (2026-11-21), and for the remote's Power.
- The shell takes the sleep key from logind with an inhibitor lock
  (`Inhibit("handle-suspend-key", …, "block")` over D-Bus, no root); logind still logs
  "Suspend key pressed short" but doesn't suspend.
- Root `GAMESCOPE_INPUT_COUNTER` increments on keyboard-type input through gamescope (seen
  for the remote's keys, including Power); a key press handled by a root grab bumps it too.
- Steam's own idle settings live in `~/.local/share/Steam/config/config.vdf`:
  `IdleBacklightDimACSeconds`, `IdleScreensaverACSeconds`, `IdleSuspendACSeconds` (also
  `…BatterySeconds` variants) and `CurrentScreensaver`. A value only appears once changed
  from Steam's default. Defaults seen: dimming off, screensaver after 5 min, sleep after
  60 min. Since 2026-11-21 all three AC values are `0` (off); the shell's idle chain replaces
  them. Steam's screensaver seemed to close open Steam menus (with sounds) when it started.
- Untested: what the LG G6 does on signal loss (standby after a timeout?), and how it
  wakes when the signal returns. Nothing wakes the output automatically: our shell would
  set it back to `0` on input.

## Switching off the Steam Controller (tested 2026-11-21)

- The controller connects through its USB puck, `28de:1305` ("Valve Software Steam Controller
  Puck"; SDL calls it the Nereid dongle, `1304` = Proteus dongle). It exposes four HID
  interfaces (`hidraw2`–`5`), all with a uaccess ACL for `deck`; Steam has them open too.
- SDL's driver for the new controller (`SDL_hidapi_steam_triton.c`, "Triton") uses the same
  feature-report messages as the classic Steam Controller. `ID_TURN_OFF_CONTROLLER = 0x9F` is
  defined there but unused.
- Sending `01 9F 04 6F 66 66 21` (report ID 1, type 0x9F, length 4, "off!"), padded to 64 bytes,
  with `HIDIOCSFEATURE` as `deck`: accepted on `hidraw2`, `EPIPE` on the other three (no
  controller in those slots). Steam's `logs/controller.txt` then showed the wireless link
  ending ("Disconnect (Spontaneous)"). No root, Steam kept running.

## Google TV remote over Bluetooth (tested 2026-11-21)

Remote: Chromecast with Google TV (2020) voice remote. Pairing mode: hold **Back + Home**
until the light pulses.

- Paired from SSH with `bluetoothctl --agent NoInputNoOutput` (`pair`, `trust`, `connect`).
  The first `pair` timed out after 20 s, the second succeeded. No root needed.
- Re-pairing after using the remote with the Chromecast again (2026-11-21, after a reboot):
  bluetoothd logged `Failed to add device FC:A8:9B:28:99:BE (1): Failed (0x03)` four times,
  and in pairing mode the remote's light only came on for a few seconds and went out.
  Pairing with the Chromecast itself was just as flaky. **Pulling the batteries and putting
  them back fixed it**: the remote paired right away and the shell found its voice service.
  So the remote's own state is the likely culprit, not gabecube (inferred).
  `system/pair-remote.sh` forgets the old pairing, waits for the advert and retries
  pair/trust/connect; written for this but not needed in the end (untested).
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

- Raw codes, read with `sudo evtest` (pressed OK, YouTube, Netflix, Assistant, Input, Power):

  | Button | HID usage (`MSC_SCAN`) | evdev key (code) |
  |---|---|---|
  | OK | `c0041` | `KEY_SELECT` (353) |
  | YouTube | `c0077` | `KEY_CAMERA_ACCESS_DISABLE` (588) |
  | Netflix | `c0078` | `KEY_CAMERA_ACCESS_TOGGLE` (589) |
  | Input (*inferred*, HID "Media Select TV") | `c0089` | `KEY_TV` (377) |
  | Power | `c019e` | `KEY_SCREENLOCK` (152) |
  | Assistant | — | nothing over HID (*inferred:* voice uses Google's vendor GATT service) |

  The device also declares `KEY_PLAYPAUSE`, `KEY_SEARCH`, `KEY_KBDILLUMUP` (unused by this remote).
- **Confirmed:** codes above 247 can't become X11 keycodes (evdev + 8 ≤ 255), so gamescope
  can't deliver OK, YouTube, Netflix or Input to X11 clients (Steam's UI included).
- Power arriving over Bluetooth means the shell can react to it (e.g. screen off / sleep),
  besides whatever the remote's IR side does to the TV.
### Key remap (installed 2026-11-21)

Files in this repo under `system/etc/`, installed with root to the same paths:
- `udev/hwdb.d/90-chromecast-remote.hwdb`: OK → `enter`, YouTube → `prog1`, Netflix → `prog2`,
  Input → `prog3`, Power → `sleep`.
- `atomic-update.conf.d/tv-remote.conf`: keeps the `.hwdb` file across SteamOS updates.

Install (as root): copy both, then `systemd-hwdb update` (writes `/etc/udev/hwdb.bin`, which
shadows `/usr/lib/udev/hwdb.bin`) and `udevadm trigger --action=change --subsystem-match=input`.
The trigger put the `KEYBOARD_KEY_*` properties on the device, but the keys only changed
**after the remote reconnected** (here: a suspend/resume cycle).

After an OS update, `/etc/udev/hwdb.bin` is dropped (not on the keep-list) and the `.hwdb`
stays. `systemd-hwdb-update.service` (`ConditionNeedsUpdate=/etc`, runs when
`/etc/udev/hwdb.d/` isn't empty) should rebuild it at boot. **Unverified until the next update**;
if it doesn't, the remap is just missing until `systemd-hwdb update` runs again.

Result in our X11 window:

| Button | Now arrives as |
|---|---|
| OK | `Return` (36) |
| YouTube / Netflix / Input | `XF86Launch1` (156) / `XF86Launch2` (157) / `XF86Launch3` (210) |
| Power | logind: "Suspend key pressed" → **suspends the Steam Machine at once** (`HandleSuspendKey=suspend`, remote is tagged `power-switch`) |

- Input confirmed as `c0089` (it produced `XF86Launch3`). Assistant sends no key (see below).
- Games can use it too: Isles of Sea and Sky was playable with the remote (keyboard input).
- Suspend test: Power at 15:46:26 → s2idle. **Pressing the remote didn't wake it**; the
  Steam Controller's Steam button did (15:46:41). bluetoothd logged
  "Controller resume with wake event 0x0" (not a Bluetooth wake). The remote reconnected
  18 s after resume, on its next button press. Our window got one `XF86WakeUp` on resume.
- Wake path looks allowed: the BT adapter is Valve's USB module `28de:1401` (btusb) on the
  same xHCI (`0000:07:00.3`) as the Steam Controller puck, both with `power/wakeup=enabled`;
  bluez reports `WakeAllowed: yes` for the remote; Valve set `ScanIntervalSuspend=2240`,
  `ScanWindowSuspend=224` in `/etc/bluetooth/main.conf`. Why the remote doesn't wake it
  is unknown.
- `btmon` trace across a suspend (2026-11-21 15:56–15:57; adapter reports as Qualcomm):
  - Suspend: kernel disconnects the remote ("terminated by local host for suspend"), sets
    LE passive scan, filter policy "ignore not in accept list", 1M PHY 1400 ms interval /
    140 ms window (+ Coded PHY 4200/420 ms), scan enabled; MGMT "Controller Suspended:
    page scanning and/or passive scanning". So wake-on-LE *is* configured.
  - 10 ms later the remote advertises (`ADV_IND`, connectable, appearance Keyboard,
    LE Limited Discoverable) and the controller reports it (still during suspend entry).
  - Then **no events for 40 s** while OK was pressed repeatedly on the remote; resume came
    from the Steam Controller ("wake event 0x0"); the remote reconnected 10 s after resume.
  - Running `btmon` from an SSH session dies with the suspend; start it with `systemd-run`.
- Second trace (16:01–16:02) with `ScanWindowSuspend=2240` (= interval, continuous scan;
  temporary edit of Valve's `/etc/bluetooth/main.conf`, reverted afterwards):
  - Remote is on the controller's accept list (`LE Add Device To Accept List` at startup).
  - Suspend scan: window = interval 1400 ms, accept-list filter, **`Filter duplicates: Enabled`**.
  - The remote's `ADV_IND` is reported 3 ms after the suspend scan starts, then **nothing for
    50 s** of button presses; still no wake.
  - On resume (Steam Controller) the kernel re-enables scanning and the remote's advert is
    reported **at once**, connection 47 ms later: it had been advertising during suspend.
  - **Conclusion (*inferred*, fits both traces):** the controller's duplicate filter already
    saw the remote in that first report, so later adverts are dropped as duplicates and never
    reach the host to wake it. The kernel sets that filter; no bluez setting changes it.
    Not the scan duty cycle.
- Wake with the remote is therefore not possible on stock SteamOS as is. Options: USB IR
  receiver learning the remote's IR power code; CEC; a kernel fix (report upstream/to Valve).
- For the shell to decide what Power does (screen off vs. suspend), it can hold a logind
  `handle-suspend-key` inhibitor lock; logind then leaves the key to us.

Alternative not used: a udev rule adding `uaccess` so our shell reads the remote's evdev directly.

### GATT services and the Assistant button (tested 2026-11-21)

Services (via bluez D-Bus `GetManagedObjects`, readable as `deck`):
standard GAP/GATT/Device Info/Battery (`180f`)/Scan Parameters (`1813`)/HID (`1812`), plus
- `ab5e0001-5a21-4f05-bc7d-af01f617b664`: **Android TV Voice** ("ATVV"). Chars: `ab5e0002`
  TX (write), `ab5e0003` audio (notify), `ab5e0004` control (notify).
- `f000bbc0-0451-4000-b000-000000000000`: TI OAD (*inferred:* firmware update).
- `d343bfc0-5a21-4f05-bc7d-af01f617b664`, `6c6f6701-5a21-…`: Google vendor
  (*inferred:* settings, maybe the IR code setup).

Subscribed (`StartNotify` over D-Bus as `deck`, **no root**) to ATVV control + audio while
pressing Assistant 4×, without any voice handshake:

- Press → control `04 03 02 <n>` (`n` counts up: `23`, `24`, `25`, `26`), then audio packets
  (160 bytes each, ~40/s) for as long as the button is held.
- Release → control `00 02`, audio stops.
- *Inferred from the Android TV voice protocol:* `04` = audio start, `00` = audio stop; the
  audio is the remote's microphone (likely ADPCM).
- That behaviour (hold-to-talk) was the remote's state left over from the dongle. After the
  remote reconnected a few times it sent **nothing** for Assistant until the host sent a
  capabilities request; the host has to pick the interaction model.

#### Voice protocol details (2026-11-21, evening)

Reference: Google's Android TV reference remote firmware in AOSP,
`platform/hardware/telink/atv/refDesignRcu` @ `47d234ba…`, `application/audio/gl_audio.{c,h}`.
The Chromecast remote runs different firmware (its replies differ in layout) but the
commands below work with it.

- Host → remote on `ab5e0002` (write without response):
  - GET_CAPS `0A <ver hi> <ver lo> <codecs hi> <codecs lo> <model>`. Version `01 00` selects
    protocol 1.0. **Model is a plain value**: `00` on request, `01` press-to-talk, `03`
    hold-to-talk; any other value (e.g. `07`, `04`, `02`) makes the remote ignore the button.
  - MIC_OPEN `0C <mode>` (2 bytes; on-request model), MIC_CLOSE `0D <stream id>`,
    MIC_EXTEND `0E <stream id>`.
- Remote → host on `ab5e0004` (notify): CAPS_RESP `0B …`, AUDIO_START `04 <reason> <codec>
  <stream>`, AUDIO_STOP `00 <reason>`, START_SEARCH `08`, SYNC `0A …`, MIC_OPEN_ERROR `0C <code>`.
- The Chromecast remote's CAPS_RESP for `0a 01 00 00 03 03`: `0b 01 00 02 03 00 a0 01 00 1a`
  = version 1.0, codec `02` (ADPCM 16 kHz), model `03` (confirms the request; it stayed `00`
  for invalid values), frame size `00 a0` (160), then `01 00 1a` (unknown).
- Hold-to-talk: press → `04 03 02 <stream>` + audio, release → `00 02`. No host replies.
  On request: press → `08`; the host must send MIC_OPEN; the stream ends with MIC_CLOSE or a
  15 s timeout (`00 08`).
- Audio (reference firmware): IMA ADPCM, 240 samples per packet (15 ms at 16 kHz), sent as
  raw chunks of the frame size; predictor/step index travel only in SYNC messages on the
  control characteristic.
- Audio (Chromecast remote, captured and decoded 2026-11-21): **160-byte notifications of raw
  IMA ADPCM, no header**, ~50/s = 16 kHz mono; **high nibble first**. No SYNC messages were
  sent, so each press decodes as one stream from predictor 0 / step index 0. (Low nibble first
  drifts away; a smoothness metric favoured the wrong order, the waveform picture didn't.)
  Microphone level is low: speech peaks ~7% of full scale. Sounds clear when amplified.
- The shell (2026-11-21) sends GET_CAPS with hold-to-talk on every (re)subscribe, decodes the
  audio while Assistant is held (ring follows the decoded level) and plays it back on release
  through miniaudio (picked "HDMI / DisplayPort Digital Stereo" via its PulseAudio backend).
  Works.
- With two D-Bus clients subscribed (StartNotify) to the same characteristic, each client
  received every notification twice.

### IR programming service (2026-11-22: a test code loaded, the remote sends it)

The remote sends no infrared now (phone camera: the soundbar's remote shows up, this one
doesn't). *Inferred:* no IR codes are loaded. It exposes the service the reference firmware
uses to load them:

| Characteristic | Flags on the remote | Reference firmware name |
|---|---|---|
| `d343bfc1-5a21-4f05-bc7d-af01f617b664` | write | Program Control |
| `d343bfc2-…` | write | Key ID |
| `d343bfc3-…` | write | Code |
| `d343bfc4-…` | write | Suppress |
| `d343bfc5-…` | notify | Key Event |

(`6c6f6701-…` has one readable characteristic, `6c6f6705-…`; not looked into.)

From the reference firmware (`refDesignRcu` `main`, `vendor/827x_ble_remote/app_ir.c`,
`app_ir.h`, `app_att.c`). The UUID values are defined in a header not read; the order of the
five characteristics matches. The Chromecast remote (TI chip, other firmware) follows it
as far as tested, see below.

- Program Control, 1 byte: `01` starts programming (clears the table), `00` ends it and
  commits to flash. 30 s timeout between writes.
- Key ID, 2 bytes big-endian, Android key codes: `0018` volume up, `0019` volume down,
  `00A4` mute, `001A` power, `00B2` input. At most 5 keys.
- Code, at most 300 bytes, may arrive in several writes (the total length is computed from
  the header): byte 0 type (`0` off, `1` one sequence, `2` repeating sequence, `3` first
  plus repeating sequence, other: two sequences alternating per press), byte 1 duty cycle in
  percent, bytes 2 to 3 carrier in units of 100 Hz, bytes 4 to 5 number of mark/space pairs;
  type 2 then has a repeat delay (2 bytes), types 3 and up the second sequence's pair count
  (2 bytes); then the pairs, 4 bytes each: mark and space as big-endian counts of carrier
  cycles. The Key ID is consumed by each Code write.
- Suppress: a list of key IDs (2 bytes each) that go over Bluetooth instead of IR; an empty
  write puts all programmed keys back on IR.
- Key Event (when notifications are on): 3 bytes per press of a programmed key, `00` down
  or `01` up, then the key ID. So the host still hears about a key that went out as IR.
- Codes are kept in flash and reloaded at start.

Test at 18:00:38, over bluez's D-Bus API as `deck` (MTU 247, so a code fits in one write;
script kept nowhere, the sequence is all there is to it):

1. `StartNotify` on Key Event.
2. Program Control `01`.
3. Key ID `00 b2` (Input, which the shell doesn't use).
4. Code `01 21 01 7c 00 08` + 8 × `0e d8 07 6c`: one sequence, 33 % duty, 38.0 kHz, 8 pairs
   of 3800 cycles on (100 ms) and 1900 off (50 ms). Not a real protocol, just easy to see.
5. Program Control `00`.

All writes were acknowledged. Pressing Input then **flickers in a phone camera** (reported),
and the remote notified `00 00 b2` on press and `01 00 b2` on release. Not checked: whether
Input still also sends its Bluetooth key while it carries a code, whether a code survives a
battery change, the other code types, Suppress.

Clearing, 18:04:12: Program Control `01`, then `00`, with no key in between (the reference
firmware keeps an empty table then). Acknowledged; Input **no longer flickers** (reported)
and its presses are no longer notified on Key Event.

### Waking the machine with the remote (2026-11-22)

Tried again after a Bluetooth mouse turned out to wake the machine. Result: not possible
over Bluetooth with this remote. Connected at suspend, it can't wake the machine (above);
disconnected at suspend, it wakes the machine by itself.

- A Bluetooth mouse (JLab GO Mouse, also LE HID like the remote) connected at suspend and
  lying still wakes the machine when used (16:11). So wake-on-LE works on this hardware.
- `btmon` trace, 16:36 to 16:48, remote held off with `bluetoothctl block`, discovery on
  (`bluetoothctl scan on`; one report per discovery cycle, about every 6 s):
  - **Left untouched for 10 minutes the remote never stopped calling.** 100 `ADV_IND`
    reports plus scan responses, no gap over 16 s.
  - **Every call is identical**: `ADV_IND`, connectable and scannable, flags `0x05` (LE
    Limited Discoverable, no BR/EDR), appearance Keyboard, data
    `02 01 05 03 19 c1 03 05 02 12 18 0f 18 10 ff e0 00 ff…`.
  - **Three OK presses (after 16:46:40) changed nothing**: same type, same data. The rate
    of its calls can't be read from this trace.
- So the adapter, which lets devices wake the machine by address only, can't tell a button
  press from the remote's idle calls.
- 16:30:19, suspended with the remote paired, not blocked and disconnected: the machine
  woke by itself at 16:30:30 (no button pressed, reported). The Bluetooth adapter (`1-5`)
  counted one wake event and the remote connected 2 s after the resume.
- 16:21:14, suspended by the power button while the remote was blocked: woke after 3.7 s,
  also with a wake event on the Bluetooth adapter.
- Both fit: a disconnected remote is always calling, and a call that reaches the suspend
  scan wakes the machine. The duplicate filter (above) is what keeps a remote that was
  connected at suspend from doing the same: its first call is spent before the machine
  sleeps. *Inferred:* the few seconds before the self-wake are the slow call rate meeting
  the suspend scan's 10 % duty cycle.
- Consequence: keep the remote connected when the machine suspends (the normal case).
  Waking with the remote needs another path: its IR side with a USB IR receiver, or CEC.
- bluez behavior met on the way (5.87):
  - `bluetoothctl disconnect` doesn't keep the remote away; it was connected again 5 s
    later. `block` does.
  - After a block of some minutes, `unblock` didn't bring the remote back although it was
    calling (16:29 and 16:48); `bluetoothctl connect` did at once, and so did a resume.
    After a 3 s block, `unblock` alone was enough.
  - `bluetoothd` crashed with SIGSEGV at 16:24:08, one second after the remote was blocked
    while `bluetoothctl scan on` was starting; systemd restarted it. Seen once; blocking
    with the scan already running was fine.
  - `dbus-monitor` on the device's path shows nothing while the device is blocked.
  - The daemon logs a `wake-policy` plugin looking for `/etc/bluetooth/wake-policy.conf`
    (absent); not looked into.
- The trace file can contain the remote's link key (it connected once during the trace); it
  was deleted after reading.

## Immich (photos.peterix.cz, checked 2026-11-21)

- Server: Immich **3.0.3** (`/api/server/version`, public), with smart search, faces, map,
  reverse geocoding, OCR enabled. API description per release:
  `open-api/immich-openapi-specs.json` in the immich repository at tag `v3.0.3`.
- Auth: header `x-api-key`. Each operation names its permission (`x-immich-permission`):
  `GET /albums`, `/albums/{id}` → `album.read`; `GET /memories` → `memory.read`;
  `GET /timeline/buckets`, `/timeline/bucket`, `/assets/{id}` → `asset.read`;
  `GET /assets/{id}/thumbnail` → `asset.view`; `GET /people…` → `person.read`.
- `GET /assets/{id}/thumbnail?size=` `thumbnail` | `preview` | `fullsize` | `original`.
- `GET /albums` gives name, `assetCount`, `albumThumbnailAssetId`, but **no assets**; an
  album's photos come from `GET /timeline/buckets?albumId=` (month buckets with counts), then
  `GET /timeline/bucket?albumId=&timeBucket=` per bucket, which answers in **columns**
  (`id[]`, `ratio[]`, `isImage[]`, `thumbhash[]`, …).
- `GET /memories?for=YYYY-MM-DD` gives that day's memories with their assets (width, height,
  type); `data.year` for "on this day".
- Orientation: some photos (e.g. in "London 2025") showed sideways in the viewer until Qt's
  `Image.autoTransform` (EXIF orientation; off by default) was turned on, so at least some
  `fullsize`/`preview` images keep the EXIF orientation flag. (A sample of 300 photos from
  other albums had none.) The thumbnail endpoint also defaults to `edited=false`, i.e. without
  rotations/crops made in Immich; the shell asks for `edited=true`.

### Place names (checked 2026-11-21)

Survey of everything the TV account sees: 13 albums, 7439 assets, 3776 with GPS.

- **Cause confirmed.** 1566 assets are labelled "Rinkeby". None is within 3.8 km of Rinkeby
  (median 5.2 km); about 1240 lie within roughly a kilometre of Blackeberg centre, the rest
  towards Grimsta, Vällingby and Hässelby. Another 134 labelled "Sundbyberg" are really in Södra
  Ängby and Blackeberg.
- Immich's lookup (`server/src/repositories/map.repository.ts` at v3.0.3): the nearest row of
  `geodata_places` within 25 km, no boundaries involved. The table is GeoNames `cities500.txt`
  (population ≥ 500 or an administrative seat) **minus feature codes `PPLX` and `PPLH`**
  (`PPLX`, "section of populated place", is kept only for Australia).
- Blackeberg is **not in `cities500`**. Of the area's entries there, Bromma, Tensta and
  Hässelby Villastad are `PPLX` and dropped, so the nearest surviving points are Rinkeby,
  Sundbyberg, Kista, Solna and Stockholm. (With `PPLX` kept, the same photos would read
  Bromma / Hässelby Villastad / Tensta.)
- Recomputing "nearest `cities500` point without `PPLX`" from each asset's GPS reproduces
  Immich's label for 3355 of 3776 assets, among them all but one of the 2749 Swedish ones.
  The rest are mostly Czech places (Prague's districts, Vranov) where today's GeoNames dump
  differs from the snapshot inside the Immich image.
- Where Blackeberg does exist:
  - full GeoNames (`SE.zip`, 28 471 populated places): `PPLX`, population 0, like Råcksta,
    Södra/Norra Ängby and Vällingby;
  - OSM: a `place=suburb` **node**. No boundary polygon exists for it; the only polygon in the
    area is the borough, "Bromma stadsdelsområde" (`admin_level=9`).
- Nearest-point lookup against either finer set fixes the labels: the 1566 "Rinkeby" assets
  become Blackeberg ×1260, Grimsta ×118–139, Hässelby gård/strand ×110–130, Vällingby ×24–36,
  Råcksta ×16. For the Swedish assets the median distance to the chosen point is 0.3–0.4 km
  (Immich's labels, all countries: 2.8 km). Side
  effect: the inner city splits up too ("Stockholm" → Södermalm, Riddarholmen, Norrmalm… with
  GeoNames; Zinkensdamm, Skanstull, Klara… with OSM), so a label needs the city next to the
  district.
- `GET /timeline/bucket?…&withCoordinates=true` adds `latitude[]` / `longitude[]` columns, so
  a lookup in the shell costs no extra requests.
- Immich re-imports its place data on start when `geodata-date.txt` in `/build/geodata`
  (`IMMICH_BUILD_DATA`) differs from the date stored in the database; existing assets keep
  their names until metadata extraction runs again.
- Options weighed: a custom `cities500.txt` mounted into the Immich container (fixes every
  client, but splits Explore → Places into districts for everyone and is unofficial);
  Immich ReverseGeo (matches by boundary polygon, so it can't produce Blackeberg, and needs
  database write access); a lookup in the shell. **Chosen: the shell** (`src/PlaceLookup.*`,
  `make-places.py`): all GeoNames populated places of Sweden and the Czech Republic, each
  with its municipality, labelled "District, City".
- Result over the same albums (the shell's client code run headless on gabecube): no
  "Rinkeby" left; Blackeberg, Stockholm ×1293, Södermalm, Stockholm ×318, Grimsta,
  Stockholm ×117…; Prague and Brno show their districts; Austria, Poland and Denmark keep
  Immich's names. 45 091 places load in 18 ms; a lookup takes about 4 µs.
- GeoNames models the two countries differently: a Swedish municipality is `admin2` and
  its record is named "Stockholms Kommun"; a Czech one is `admin3` with a plain name
  ("Brno"), and Prague is a region of its own (`admin1` 52).

## Video playback in the shell (2026-11-21)

- SteamOS ships ffmpeg 8.1.2 and Qt Multimedia 6.11.1 with the ffmpeg backend; mpv and
  gst-libav are not installed. Mesa provides VA-API for the GPU.
- Immich `GET /assets/{id}/video/playback` answers Range requests (206); for a phone video it
  served a transcoded MP4 (104 MB vs. 81 MB original).
- Qt's media player can't send custom headers, so the shell proxies through 127.0.0.1
  (`ImmichMediaProxy`). Played fine, sound included: 1080p30 clips and a 3840x2160 h264
  at 77 Mb/s.
- Hardware decoding not confirmed: `libva` was loaded in the shell, but
  `radeonsi_drv_video.so` wasn't mapped when checked after playback. Check during playback if
  4K ever stutters. Harmless log noise: VDPAU probing for `libvdpau_nvidia.so`, and a
  `spaVisitChoice: parse error` from Qt's PipeWire audio about the HDMI sink's IEC958 formats.

## The shell in use

- 2026-11-21: enabled as the default interface (`./deploy.sh --enable`). Confirmed: launching
  a game from the shell, quitting it and landing back in the shell; suspend and resume; a full
  Gaming Mode restart (Steam restarting included). Steam recreates its virtual pad after a
  game and after resume; the shell picks it up again.
- 2026-11-21, 23:30: after waking from 1.5 h of sleep (no network yet), Steam's menu opened
  and its UI stopped responding: a frozen, partly blurred screen. Steam's watchdog killed the
  webhelper at 23:32:25 ("killing unresponsive browser", core dump) and reloaded its UI at
  23:32:33. The shell had stepped aside normally. *Inferred:* a Steam bug, possibly tied to
  waking without network.
- 2026-11-22: the screensaver sometimes started with a black screen, seen when it started
  over YouTube. Cause: it covered the screen with black at once, then fetched its order and
  the first full-size photo (never cached: each run starts on a new random photo). Fixed by
  preparing the next run when one ends and fading in once the first photo is up.
- 2026-11-22: Steam's UI hang is a pattern, not a one-off. Four times so far (11-21 17:17 and
  23:32, 11-22 10:32 and 10:45) Steam's webhelper logged "killing unresponsive browser" and
  dumped core, each 45 s to 2 min after Steam's main menu was shown (`MainMenu … WasHidden 0`)
  following a stretch with the shell on screen (11 min to hours). After the reload Steam works,
  and reopening it within seconds is fine (10:46:53). Not tied to resume: 10:45 came 55 min
  after waking. Steam never logs its Big Picture window as hidden (`SP BPM … WasHidden 1`) while
  the shell covers it, nor did it for a game launched from the shell. Cause unknown.
  `~/steam-hang-watch.sh` (transient user unit `steam-hang-watch`, gone after a reboot) records
  the busiest threads and the webhelper processes' wait states for 60 s whenever the menu is
  shown, into `~/steam-hang-watch.log`. Stacks need root (`ptrace_scope` 1; `gdb` and
  `eu-stack` are installed).
- 2026-11-22: with the screen off (`drm_sleep_external_screen 1`, HDMI still "connected") the
  HDMI audio sink is gone: PipeWire has only "Dummy Output" (`auto_null`). Sound can't play
  while the shell has the screen switched off this way.

## YouTube (2026-11-22)

- SteamOS ships Qt WebEngine 6.11.1 (`qt6-webengine`, `/usr/lib/qt6/QtWebEngineProcess`);
  added to the toolchain image.
- `youtube.com/tv` with a Tizen smart-TV user agent gives the TV interface in a
  `WebEngineView`; sign-in with a code at youtube.com/activate worked, and the "Who's
  watching?" account picker comes up at start.
- The account picker's address is the same as the home screen's (`https://www.youtube.com/tv#/`;
  only `/tv` and `/tv#/` were logged on the way there). Inspected with Chromium's DevTools
  (`QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9222` through an SSH tunnel): the picker is a
  visible `ytlr-account-selector` element (custom elements are `ytlr-*`/`yt-*`), absent
  once an account is chosen.
- The remote's YouTube key reaches X11 as `XF86Launch1`, keycode 156 (after the hwdb remap
  to `prog1`); a root grab on it works like Home.
- SponsorBlock: Qt WebEngine 6.11 can load Chrome extensions (`QWebEngineExtensionManager`,
  since 6.10), but the SponsorBlock extension targets the desktop site; the TV-interface
  projects (TizenTube, youtube-webos) carry their own SponsorBlock code. Decided: our own
  script. The API answers `GET /api/skipSegments/<4 hex of sha256(id)>?categories=[…]&actionTypes=["skip"]`
  with a list of `{videoID, segments: [{segment: [start, end], category, …}]}` for every video
  under that prefix (checked with curl). Injection and skipping not yet seen working on
  gabecube; a web view wouldn't load in the toolchain container to test it there.
- YouTube Music has no TV website of its own that we found; on TVs it is a Music tab inside
  the YouTube app (YouTube's blog), so presumably inside `youtube.com/tv` too (not checked).
- Volume: the remote's volume keys change the sound while the shell is on screen, without
  the shell doing anything (confirmed by use). *Inferred:* Steam handles them. Mute did
  nothing (reported), so the shell now grabs it and mutes the output itself.
- SponsorBlock seen working on gabecube (11:31): a video with 3 segments, two sponsor
  segments skipped, logged by the injected script. So the user script, the application-world
  hand-over and the `#/watch?v=` address all work as assumed.
- The shell's web engine has no separate GPU process (zygotes, one renderer, one utility
  process): Qt WebEngine runs the GPU thread inside the shell. YouTube's interface looks as
  if some effects were missing (reported); cause not looked into yet (the TV user agent we
  send, or how the web view renders).
- YouTube's interface looked as if effects were missing because of the user agent. Seen with
  Chromium's DevTools on the page: `ytcfg.data_.LIVING_ROOM_APP_QUALITY` was
  `TV_APP_QUALITY_LIMITED_ANIMATION` and `environment.feature_switches.enable_animation` false
  (brand Samsung, model SmartTV, from our Tizen 7.0 user agent). Drawing was fine: WebGL
  reports ANGLE on radeonsi (hardware), 2560x1440 at device pixel ratio 1, `backdrop-filter`
  supported. Loading `/tv` under other user agents (DevTools override), all got
  `TV_APP_QUALITY_FULL_ANIMATION`: PlayStation 5 (Sony), Xbox Series X (read as "Xbox 360"),
  LG webOS 23 (adds LG styles and a pointer remote), a Cobalt-format one naming Valve /
  SteamMachine (generic TV, plus Lottie animations), and a Chromecast one, which switched the
  client to `TVHTML5_CAST` and came up signed out with a Cast prompt. Chosen: the Cobalt-format
  Valve one. Sign-in survived the change.
- What the page knows about the video playing: `document.querySelector(".html5-video-player")`
  has `getPlayerResponse()`, whose `videoDetails.musicVideoType` was `MUSIC_VIDEO_TYPE_UGC` for
  a music video from a mix (`#/watch?v=…&list=RD…`), and `navigator.mediaSession.metadata`
  has title and artist. An ordinary video (a 17-minute science video) has no
  `musicVideoType` at all, and its `mediaSession` metadata has the title and the channel.
- Playback is capped at 720p under the Valve identity: `getAvailableQualityLevels()` ends at
  `hd720` and the debug text says `optimal_format: 720p`, although the stream list offers up
  to 2160p60 (VP9 and AV1), the player knows the viewport is 2560x1440,
  `MediaSource.isTypeSupported` accepts everything up to 2160p60 and
  `mediaCapabilities.decodingInfo` says smooth and power-efficient for all of it. No dropped
  frames at 720p. *Inferred:* YouTube caps by the device it thinks this is. Not known whether
  the Tizen identity had the same cap.
- 4K on the current monitor (2026-11-22): the EDID that reaches the Steam Machine (EDID 1.3,
  "A409U", made 2015) offers 3840x2160 only at 30 Hz (297 MHz) and 2560x1440 at 60 Hz
  (241.5 MHz); the CTA block stops at 1080p60 and has an HDMI 1.x vendor block only (no HDMI
  Forum block, no 4K60 modes). The kernel lists a 3840x2160 mode; gamescope's list tops out at
  2560x1440@60. So over this path 4K60 isn't on offer: either the monitor input (or its mode
  setting) or the HDMI switch is limited to HDMI 1.4.
  Later the same day, with the HDMI switch taken out (reported): the same EDID, so the limit
  is the monitor's input, not the switch. While re-plugging, the connector was detected and
  dropped twice within a second (12:12), then stayed "disconnected" until the cable or input
  was sorted out (12:20); gamescope picked 2560x1440@60 again by itself.
  13:18, monitor moved to DisplayPort (`DP-1`), soundbar plugged into the HDMI port: over
  DisplayPort the monitor offers 3840x2160 at 60 Hz (533 MHz), and gamescope's mode list now
  starts with `3840x2160@60`, but it selected 2560x1440@60 (as before). gamescope composes a
  full 2560x1440 frame (screenshot), while the monitor shows it as a smaller rectangle in the
  middle (reported): the monitor isn't scaling the 1440p signal up on this input. Sound goes
  to the monitor over DisplayPort (one sink, "HDMI / DisplayPort Digital Stereo (HDMI 4)").
  The soundbar on `HDMI-A-1` isn't detected at all ("disconnected", no EDID). *Inferred:* its
  HDMI port is an output for a TV's ARC, not an input.
- The 720p cap, found and fixed (2026-11-22, on the 4K screen). Not the identity: the old
  Tizen one and a PlayStation 5 one were capped the same. Wrapping the page's capability calls
  showed the player's questions (about 80 while starting a video): besides real ones it asks
  `MediaSource.isTypeSupported` things no device can do, `…; width=99999`, `…; height=99999`,
  `…; eotf=catavision`, `…; cryptoblockformat=invalidformat`, `…; decode-to-texture=nope`,
  and Chromium said true to all of them (it ignores parameters it doesn't know). With a
  wrapper that answers those parameters (false beyond the screen's pixels, 60 fps, bt709) the
  quality levels went from `hd720` to `hd2160`. Deployed as `qml/mediacaps.js`: a video
  reached 3840x2160 (format 401, AV1) within 45 s, 0 dropped of 1500 frames, the shell at
  8 % CPU and the page's process at 5 %, libva loaded in the shell. *Inferred:* decoding runs on
  the GPU. Widevine isn't available (`requestMediaKeySystemAccess` rejected), which only
  matters for bought or rented films. Shell memory while playing 4K: 574 MB plus 358 MB for
  the page's process.
- Background music (decided 2026-11-22). Music keeps playing when YouTube is left, anything
  else pauses; what is music is YouTube's `musicVideoType`, not the Music section (music
  uploaded as ordinary videos carries the mark too, as far as YouTube recognises it); if the
  mark can't be read the video pauses. The remote has no play/pause key, so the top bar gets a
  now-playing item (OK toggles pause) and the YouTube button returns to it. Screen off with
  music is a black picture, because switching the signal off drops the audio sink (see
  above). Starting a game from the shell asks (pause the music, keep it, cancel). Not built:
  telling apart "other sources" outside the shell (a game's own sound, Steam). Open: whether
  YouTube really keeps playing with the page item at opacity 0, and what the hidden 4K
  decoding costs under a game.
  13:56, deployed: it does keep playing at opacity 0 (music from a mix, the shell on the
  Photos place, title in the top bar), with the shell at 10 % CPU and the page's process at
  5 %. The idle log showed "music is playing" / "music stopped" flipping several times within
  two minutes: the player reports not playing for a moment between tracks. Hence a 20 s
  grace before the music counts as stopped (otherwise a black screen would lose its signal
  between two tracks).
- Shell at other resolutions (decided 2026-11-22): scale the whole UI with Qt's scale factor
  from the X screen height (design height 1440) rather than a scale transform in QML, which
  would blur the web view and text. Not yet seen at 4K: whether gamescope's X screen follows
  the output resolution when Steam switches it is the open point (at 1440p output the X screen
  is 2560x1440).
  13:29, output switched to 3840x2160@60 in Steam's settings (over DisplayPort): the X screen
  is 3840x2160, the shell started with scale 1.50, and a gamescope screenshot is 3840x2160
  with the UI in the same proportions as at 1440p. So the X screen does follow the output,
  and the picture fills the monitor (reported).
  The restart on a resolution change wasn't exercised (the switch happened before the deploy).
- Soundbar over Bluetooth (2026-11-22, until the TV arrives): it is the default sink
  (`bluez_output.…`), next to the DisplayPort audio sink. "Turn off controllers" leaves it
  alone: it only disconnects bluez devices whose icon is `input-gaming`.
- Steam's menu opened at 11:17 and 11:43 without a hang (the shell had been on screen for 7
  and 19 minutes); `~/steam-hang-watch.log` has those as a healthy baseline.

## Suspend ends after 5 seconds: the wireless mouse receiver (2026-11-22)

Seen while testing background music: the black screen with sound, the signal going off 20 s
after the track ended and the suspend 2 minutes later all happened as intended, then the
machine resumed by itself and the shell came back on screen.

| Suspend | Resume | Slept | Setup | Requested by |
|---|---|---|---|---|
| 01:57:06 | 02:14:13 | 17 min | monitor on HDMI | shell (idle) |
| 03:52:05 | 09:50:55 | 6 h | monitor on HDMI | not the shell |
| 13:02:49 | 13:02:54 | 5 s | monitor on DisplayPort | Steam (power button) |
| 15:38:08 | 15:38:14 | 5 s | DisplayPort, soundbar on Bluetooth | shell (idle) |
| 15:47:09 | 15:47:15 | 5 s | same | shell (idle) |
| 15:50:41 | 15:50:47 | 6 s | DisplayPort cable pulled | `systemctl suspend` over SSH |
| 15:51:39 | 15:51:45 | 5 s | cable pulled, soundbar disconnected | same |
| 15:53:42 | 15:53:48 | 6 s | cable pulled, soundbar back on Bluetooth; wake sources snapshotted | same |
| 15:55:50 | 15:56:17 | 26 s | receiver unplugged 3 s earlier; ended by the power button | shell (idle) |
| 15:56:38 | 16:01:59 | 5 min 21 s | as 15:53, **mouse receiver unplugged**; ended by the power button | same |
| 16:08:28 | 16:11:43 | 3 min 14 s | receiver unplugged, mouse on Bluetooth, switched on, lying still; ended by using the mouse | same |

- Suspend is `s2idle`. Of the 5 seconds about 3 are the kernel going down and coming up, so
  the machine really sleeps for about 2.
- **Not the monitor:** it also happens with no display connected. (The first guess was the
  monitor toggling hot-plug on DisplayPort; gamescope does see `DP-1` disconnect and
  reconnect after each resume, but that is an effect.)
- **Not the soundbar's Bluetooth link:** it also happens with the soundbar disconnected, and
  the Bluetooth adapter (`1-5`) counted no wake event in the 15:53 snapshot. (`bluetoothd`'s
  "Controller resume with wake event 0x0" proves nothing: it says the same after the
  Bluetooth mouse woke the machine at 16:11.) The soundbar
  reconnects by itself after the resume.
- The 13:02 one is a different case: the power button (`PNP0C0C:00`) has an event inside
  that suspend, so a second press ended it.
- `/sys/power/pm_wakeup_irq` after the short sleeps, in order: 1 (`i8042`), 7
  (`pinctrl_amd`), 9 (ACPI), 9, 9. It doesn't single out a device.
- Wake sources (`/sys/class/wakeup/*`, readable without root) that counted events during the
  15:53 suspend, in order of their last change: `serio0` (the `i8042` keyboard port, +1),
  the USB controller `0000:07:00.3` and its ACPI device (+2, +1), then one second later the
  bridge to the Wi-Fi card `0000:00:02.2` (+2) and **USB device `1-2`, the JLab GO Mouse
  receiver (+1)**. Interrupts: IRQ 1 +8, IRQ 7 +2, IRQ 9 +5, GPIO pin 58 +1.
- **The cause: the wireless mouse receiver** (JLab GO Mouse, USB). With it unplugged and
  nothing else changed, the machine stayed asleep until the power button was pressed, and
  the USB controller counted no wake event in that suspend. *Inferred:* it signals a
  wake-up right after the bus is suspended. It sits on that USB controller, has wake-up enabled, and counted one event
  in that suspend (5 before it, after 4 short sleeps). It was plugged in during the long sleeps too, so something about
  it changed around midday: the mouse was switched on then (reported). The receiver stays
  unplugged; mouse and keyboard use Bluetooth instead. With the mouse connected over
  Bluetooth and lying still the machine stays asleep, and using the mouse wakes it: the
  Bluetooth adapter (`1-5`) and the USB controller count a wake event. To keep the mouse
  plugged in, its `power/wakeup` would have to be set to `disabled` (root, e.g. a udev rule);
  not done.
- Also wake-enabled: the Steam Controller Puck (`1-3`), the Bluetooth adapter (`1-5`) and
  CEC (`cros-ec-cec`, with `wake_tv`/`suspend_tv`/`allow_standby` set in
  `~/.config/cecd/config.d/`).
- The timer of `systemd-run --on-active=3` fired 48 s late (timer accuracy); a plain
  `systemd-run --user sh -c "sleep 3; systemctl suspend"` is on time.

## Screen stayed dark after a wake-up (2026-11-22)

- 17:00:31 the shell suspended the machine from its screen-off state (signal off through
  `gamescopectl drm_sleep_external_screen 1`). 17:42:04 it was woken; the shell went to
  Active and sent `drm_sleep_external_screen 0` within 10 ms of the kernel's "suspend exit".
  **The screen stayed dark.**
- For the next five minutes `DP-1` showed `connected` but `enabled=disabled`, `dpms=Off`:
  gamescope picked the connector and a mode again and again (the monitor dropping off and
  coming back: 17:42:04, 17:42:48, 17:42:50 to 17:43:14, 17:46:59, 17:47:02) without ever
  driving it. A power-button sleep in between (17:44:21 to 17:44:27) changed nothing; the
  shell was Active by then and sent nothing on that resume.
- 17:47:15, `gamescopectl drm_sleep_external_screen 0` sent by hand: the connector was
  enabled as soon as the monitor came back (17:47:27) and the picture returned (reported).
- *Inferred:* the first "screen on" went unheeded because it arrived while gamescope was
  still coming out of suspend. The command's exit code was not checked, so whether it failed
  outright is unknown. The other wake-ups from screen-off today (all after sleeps of
  seconds to minutes, same monitor on DisplayPort) brought the picture back.
- Change: the shell sends "screen on" on waking and after every resume, repeats it three
  times two seconds apart, and logs a failing `gamescopectl`.

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
- Steam Controller held without pressing anything (2026-11-21): the shell's screensaver ended
  with no button event on Steam's virtual pad. Ignoring the left stick outside the awake state
  fixed it. *Inferred:* a resting thumb on a trackpad (or stick drift) moves the virtual pad's
  stick; don't treat stick input as a deliberate press.

## Open / untested

- Does Steam rewrite `GAMESCOPECTRL_BASELAYER_APPID` on notifications or downloads finishing?
- Remote: wake via a USB IR receiver or CEC; does the remap survive an OS update;
  does the dongle reclaim the remote if both are powered.
- How to give a Wayland window a real app ID with `--expose-wayland`.
- How gamescope's "derived from cgroup/process hierarchy" app-ID lookup is triggered. It
  worked for the Steam-launched shortcut, but not for our own X windows on `:0` or `:1` with
  `SteamLaunch AppId=<id>` in their own or a parent's command line, nor in an
  `app-steam-app<id>-*` scope, nor for a GTK X11 window with `_NET_WM_PID` set.
  *Inferred:* Steam assigns it for programs it launches. For programs we start ourselves,
  `STEAM_GAME` is the only method that works.
- With our shell as a non-Steam shortcut: Steam menu/QAM behavior, downloads, idle/suspend.
