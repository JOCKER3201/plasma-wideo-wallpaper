#!/bin/sh
# Instalacja "Wideo Wallpaper" dla bieżącego użytkownika (bez roota).
# Użycie: ./install.sh [--with-icons]
#   --with-icons  dodatkowo wrapper ~/.local/bin/plasmashell z shimem ARGB
#                 (ikony pulpitu nad wideem; patrz README)
set -eu
cd "$(dirname "$0")"

for tool in cmake ninja pkg-config; do
    command -v "$tool" >/dev/null || { echo "Brak narzędzia: $tool"; exit 1; }
done
pkg-config --exists wayland-client wayland-egl egl mpv || {
    echo "Brak nagłówków (pkg-config): wayland-client / wayland-egl / egl / mpv."
    echo "Doinstaluj pakiety -devel swojej dystrybucji (patrz README)."
    exit 1
}

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build

# KPackage tapety (katalog repo jest źródłem — aktualizacja = git pull + ./install.sh)
mkdir -p ~/.local/share/plasma/wallpapers
ln -sfn "$PWD/package" ~/.local/share/plasma/wallpapers/io.github.jocker3201.hdrvideo

# moduł QML dla plasmashell (wchodzi przy logowaniu do Plazmy)
mkdir -p ~/.config/plasma-workspace/env
cat > ~/.config/plasma-workspace/env/hdrvideo-import.sh <<EOF
export QML2_IMPORT_PATH="$PWD/build/qml\${QML2_IMPORT_PATH:+:\$QML2_IMPORT_PATH}"
EOF

if [ "${1:-}" = "--with-icons" ]; then
    if [ -e ~/.local/bin/plasmashell ] && ! grep -q hdrvideo-alpha ~/.local/bin/plasmashell 2>/dev/null; then
        echo "UWAGA: ~/.local/bin/plasmashell istnieje i nie jest naszym wrapperem — pomijam --with-icons."
    else
        mkdir -p ~/.local/bin
        cat > ~/.local/bin/plasmashell <<EOF
#!/bin/sh
# Wrapper plasmashell (Wideo Wallpaper): shim ARGB -> ikony pulpitu nad wideem.
# Shim sam usuwa sie z LD_PRELOAD, procesy potomne dziedzicza czyste srodowisko.
SHIM="$PWD/build/libhdrvideo-alpha.so"
[ -r "\$SHIM" ] && export LD_PRELOAD="\$SHIM\${LD_PRELOAD:+:\$LD_PRELOAD}"
exec /usr/bin/plasmashell "\$@"
EOF
        chmod +x ~/.local/bin/plasmashell
        echo "Zainstalowano wrapper ikon: ~/.local/bin/plasmashell"
    fi
fi

echo
echo "Gotowe. Wyloguj sie i zaloguj ponownie, potem:"
echo "  PPM na pulpicie -> Konfiguruj pulpit i tapete -> Tapeta: Wideo Wallpaper"
echo "HDR: wlacz HDR ekranu w Ustawieniach systemowych -> Ekrany (inaczej KWin zmapuje do SDR)."
