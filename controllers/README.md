# TV controller service

The part of the TV that owns Nintendo's Switch controllers: the Joy-Cons and the Pro
Controller. It is meant to do what a Switch does with them: pair them, let two Joy-Cons
form one controller, show the player number on their lights, watch their batteries, and
bring up the TV's home screen when Home is pressed. Games, Steam and the emulator don't
see these controllers themselves; they see controllers this service makes.

A Pro Controller on a USB cable is the exception: the service only handles Bluetooth, and
on a cable Steam handles it as an ordinary controller.

## What it does today

This is an early version:

- It finds every Joy-Con that connects and is the only program that can talk to it. Steam
  and other programs no longer see Joy-Cons at all.
- A connected Joy-Con flashes all four lights, the way it does on a Switch while it waits
  to be told how it is held.
- **Press SL and SR together** (the two small buttons on the rail) and that Joy-Con
  becomes a controller held sideways, rail up. Its lights show the player number.
- **Press L on a left Joy-Con and R on a right one together** and the two become one full
  controller.
- **A Pro Controller** is a controller as soon as it connects.
- In every grip, of the four buttons under your right thumb the one on the right is A
  (OK) and the lower one is B (Back), as on a Switch.
- Games and Steam see each of these as an ordinary controller. Home is kept back for the
  TV: it brings up the TV's home screen, like Home on the remote.
- When a Joy-Con is switched off, it can be woken again after about five seconds. One that
  is carried out of reach keeps its connection for up to 20 seconds and carries on when it
  is back.

- A button on a paired Joy-Con or Pro Controller wakes the machine from sleep.
- **Motion** (tilting and turning a controller) is offered to emulators the way they
  expect it from outside: as a cemuhook (DSU) server on this machine, port 26760. Pad 0
  is player 1's left Joy-Con or only controller, pad 1 player 1's right Joy-Con, pads 2
  and 3 the same for player 2.

- **Motion calibration**: the TV's motion test (Settings → Controllers → a controller →
  Motion test → Calibrate) measures a controller lying still on a table and corrects its
  motion from then on, for games too. It is remembered per controller, in
  `~/.config/tvcontrollers/motion.ini` on the Steam Machine.

- **The Ring-Con** is recognised on a right Joy-Con and its squeeze and pull are read.
  The TV's motion test shows them. With the Joy-Con in the ring, a pair steers the TV
  and ordinary programs with that Joy-Con alone, as you hold it: its stick moves as you
  see it, and of its four buttons the right one is A and the lower one B (printed X
  and A while it is in the ring).
- **Switch games in Eden** (`../apps/eden`) get the Joy-Cons, the Pro Controller and the
  Ring-Con straight from this service, each controller by itself, the way a Switch has
  them. Ring Fit Adventure runs with the ring and the leg strap. While a game is
  being played the controllers are the game's alone; Home still brings up the TV.

Not there yet: rumble, the player lights as a game sets them, and the IR camera.

## Install

On the computer you build on:

    ./build.sh
    ./deploy.sh

The first time, `deploy.sh` prints one command to run with your sudo password. It installs
the rule that hides the Joy-Cons from other programs, switches off the kernel's own Joy-Con
driver, and starts the service with the machine. After that, `./deploy.sh` updates and
restarts the service without a password.

    ./deploy.sh --log       # what the service is doing
    ./deploy.sh --system    # print the sudo command again

To take it out again, on the Steam Machine:

    sudo bash ~/.local/share/tvcontrollers/system/install.sh --remove

Joy-Cons are then visible to Steam again from their next connection.

## Pairing a Joy-Con

The service pairs the controllers itself; Steam's Bluetooth settings are not needed. Open
Settings → Controllers on the TV and go into the list: the search runs while you are in
it. Without the
TV's interface, start one on the Steam Machine:

    busctl --user call local.tvcontrollers /local/tvcontrollers local.tvcontrollers StartSearch

Then hold the small sync button until the lights sweep: on a Joy-Con it is on the rail,
on a Pro Controller on the top edge next to the USB socket. After a few
seconds all four lights flash: it is paired and connected. The search ends by itself after
two minutes. Once paired, pressing any button on a Joy-Con connects it.

## If Joy-Cons show up in Steam again

A SteamOS update may have dropped the system files. Run the sudo command from
`./deploy.sh --system` once more; `./deploy.sh` also says when they differ.
