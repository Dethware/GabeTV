# Build image for Jellyfin Desktop on gabecube: the SteamOS toolchain (toolchains/steamos-3.9)
# plus what the player needs on top. mpv comes from SteamOS's own repositories, in the version
# built against the libraries the Steam Machine has.
#
#   docker build -t tv/apps/jellyfin-desktop:dev apps/jellyfin-desktop
FROM tv/toolchains/steamos-3.9:dev

# No -y: stay on the package lists the toolchain image was built from.
RUN pacman -S --noconfirm --needed git mpv extra-cmake-modules patchelf \
 && pacman -Scc --noconfirm \
 && pacman -Q > /etc/toolchain-packages.txt
