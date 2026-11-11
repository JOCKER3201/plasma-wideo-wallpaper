#!/bin/sh
# Odinstalowanie "Wideo Wallpaper" (user-level). Przed uruchomieniem zmień
# tapetę na inną (np. Obraz) na wszystkich ekranach.
set -u
rm -f ~/.local/share/plasma/wallpapers/io.github.jocker3201.hdrvideo
rm -f ~/.config/plasma-workspace/env/hdrvideo-import.sh
if [ -e ~/.local/bin/plasmashell ] && grep -q hdrvideo-alpha ~/.local/bin/plasmashell 2>/dev/null; then
    rm -f ~/.local/bin/plasmashell
    echo "Usunieto wrapper ikon."
fi
echo "Odinstalowane. Przeloguj sie, aby plasmashell wystartowal bez modulu."
