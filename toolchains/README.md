# Toolchains

Build environments for code that runs on the Steam Machine (`gabecube`), one directory per
image, named `tv/toolchains/<directory>:dev`. Same idea as the MultiMC toolchains: images
hold toolchains only (no build steps, no entrypoint); build steps are mounted in at run time:

    docker run --rm --network none -u $(id -u):$(id -g) \
        -v <source>:/src:ro -v <output>:/out <image> <build command>

| Directory | Target | Made from |
|-----------|--------|-----------|
| [`steamos-3.9/`](steamos-3.9/README.md) | SteamOS 3.9.5 (build 20261023.100) | Arch base pinned by digest + Valve's SteamOS 3.9 package repositories |

## `archive/`

`docker save` of the built images, zstd-compressed. Not in git (`SHA256SUMS` is). Valve's
repositories move on, so the archive is what preserves a toolchain exactly. Load with
`zstd -dc <name>.tar.zst | docker load`.
