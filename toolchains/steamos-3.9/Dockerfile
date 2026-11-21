# Toolchain for gabecube (SteamOS 3.9): same glibc, GCC, Qt and SDL3 as the Steam Machine.
# Packages come from Valve's own repositories, signature-checked against the holo keyring
# copied from the Steam Machine (keyrings/). Toolchain only: no build steps, no entrypoint.
#
#   docker build -t tv/toolchains/steamos-3.9:dev toolchains/steamos-3.9
FROM archlinux:base@sha256:d4594bb2300cba11f41c2f27923eb56f63d9988909c402f9c95517d1e362c899

COPY pacman.conf /etc/pacman.conf
COPY keyrings/ /usr/share/pacman/keyrings/

# Trust Valve's keys, move every installed package to SteamOS's versions (-uu allows
# downgrades from the Arch base image), then add the toolchain.
RUN pacman-key --init \
 && pacman-key --populate holo \
 && pacman -Syuu --noconfirm \
 && pacman -S --noconfirm --needed \
      base-devel cmake ninja \
      qt6-base qt6-declarative qt6-shadertools qt6-tools \
      sdl3 libxcb \
 && pacman -Scc --noconfirm \
 && pacman -Q > /etc/toolchain-packages.txt
