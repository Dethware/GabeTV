# TV controller service

The part of the TV that owns the Nintendo Joy-Cons. It is meant to do what a Switch does
with them: pair them, let two form one controller, show the player number on their lights,
watch their batteries, and bring up the TV's home screen when Home is pressed. Games, Steam
and the emulator don't see the Joy-Cons themselves; they see controllers this service makes.

The Pro Controller is not handled here. It works as an ordinary controller through Steam.
Installing this service switches off the kernel's own driver for Nintendo controllers,
which the Pro Controller used as well; Steam has its own support for it.

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
- Games and Steam see each of these as an ordinary controller. Home is kept back for the
  TV.
- When a Joy-Con is switched off, it can be woken again after about five seconds. One that
  is carried out of reach keeps its connection for up to 20 seconds and carries on when it
  is back.

Not there yet: pairing from the TV's own screens, Home bringing up the TV's interface,
motion, rumble and the Ring-Con.

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

For now through Steam's Bluetooth settings or `bluetoothctl`: hold the small sync button on
the Joy-Con's rail until the lights sweep, then pair it. Once paired, pressing any button
on the Joy-Con connects it.

## If Joy-Cons show up in Steam again

A SteamOS update may have dropped the system files. Run the sudo command from
`./deploy.sh --system` once more; `./deploy.sh` also says when they differ.
