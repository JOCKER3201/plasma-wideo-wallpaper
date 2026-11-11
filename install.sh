#!/bin/sh
# Install "Video Wallpaper" for the current user (no root needed).
# Usage: ./install.sh [--with-icons]
#   --with-icons  additionally install a ~/.local/bin/plasmashell wrapper with
#                 the ARGB shim (desktop icons above the video; see README)
set -eu
cd "$(dirname "$0")"

for tool in cmake ninja pkg-config; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool"; exit 1; }
done
pkg-config --exists wayland-client wayland-egl egl mpv || {
    echo "Missing headers (pkg-config): wayland-client / wayland-egl / egl / mpv."
    echo "Install your distribution's -devel packages (see README)."
    exit 1
}

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build

# Wallpaper KPackage (the repo checkout is the source — update = git pull + ./install.sh)
mkdir -p ~/.local/share/plasma/wallpapers
ln -sfn "$PWD/package" ~/.local/share/plasma/wallpapers/io.github.jocker3201.hdrvideo

# QML module for plasmashell (picked up when logging into Plasma)
mkdir -p ~/.config/plasma-workspace/env
cat > ~/.config/plasma-workspace/env/hdrvideo-import.sh <<EOF
export QML2_IMPORT_PATH="$PWD/build/qml\${QML2_IMPORT_PATH:+:\$QML2_IMPORT_PATH}"
EOF

if [ "${1:-}" = "--with-icons" ]; then
    if [ -e ~/.local/bin/plasmashell ] && ! grep -q hdrvideo-alpha ~/.local/bin/plasmashell 2>/dev/null; then
        echo "WARNING: ~/.local/bin/plasmashell exists and is not our wrapper — skipping --with-icons."
    else
        mkdir -p ~/.local/bin
        cat > ~/.local/bin/plasmashell <<EOF
#!/bin/sh
# plasmashell wrapper (Video Wallpaper): ARGB shim -> desktop icons above the video.
# The shim removes itself from LD_PRELOAD; child processes inherit a clean environment.
SHIM="$PWD/build/libhdrvideo-alpha.so"
[ -r "\$SHIM" ] && export LD_PRELOAD="\$SHIM\${LD_PRELOAD:+:\$LD_PRELOAD}"
exec /usr/bin/plasmashell "\$@"
EOF
        chmod +x ~/.local/bin/plasmashell
        echo "Installed the icons wrapper: ~/.local/bin/plasmashell"
    fi
fi

echo
echo "Done. Log out and back in, then:"
echo "  Right-click the desktop -> Configure Desktop and Wallpaper -> Wallpaper: Video Wallpaper"
echo "HDR: enable HDR for the screen in System Settings -> Displays (otherwise KWin will map it to SDR)."
