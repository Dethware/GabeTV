# Build image for the Eden emulator on gabecube: the SteamOS toolchain (toolchains/steamos-3.9)
# plus the libraries Eden builds against that SteamOS's repositories have. What they don't
# have, Eden's own build fetches as source (./build.sh).
#
#   docker build -t tv/apps/eden:dev apps/eden
FROM tv/toolchains/steamos-3.9:dev

# No -y: stay on the package lists the toolchain image was built from.
RUN pacman -S --noconfirm --needed git patchelf zip unzip boost enet libzip fmt nlohmann-json \
        vulkan-headers vulkan-utility-libraries spirv-headers qt6-charts \
 && pacman -Scc --noconfirm \
 && pacman -Q > /etc/toolchain-packages.txt
