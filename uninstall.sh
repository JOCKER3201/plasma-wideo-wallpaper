#!/bin/sh
# Uninstall "Video Wallpaper" (user-level). Before running, switch the
# wallpaper to a different one (e.g. Image) on all screens.
set -u
rm -f ~/.local/share/plasma/wallpapers/io.github.jocker3201.hdrvideo
rm -f ~/.config/plasma-workspace/env/hdrvideo-import.sh
if [ -e ~/.local/bin/plasmashell ] && grep -q hdrvideo-alpha ~/.local/bin/plasmashell 2>/dev/null; then
    rm -f ~/.local/bin/plasmashell
    echo "Removed the icons wrapper."
fi
echo "Uninstalled. Log out and back in so plasmashell starts without the module."
