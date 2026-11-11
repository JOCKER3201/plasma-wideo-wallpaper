# plasma-wideo-wallpaper — „Wideo Wallpaper" w wyborze tapet Plazmy

Plugin tapety dla Plasmy 6 odtwarzający wideo w **prawdziwym HDR** (PQ/BT.2020)
— rzecz niemożliwa dla zwykłych pluginów QML, bo scena Qt Quick zawsze
tonemapuje do 8-bitowego sRGB.

## Architektura

- `package/` — zwykły KPackage `Plasma/Wallpaper` (pojawia się w Ustawieniach →
  Tapeta jako „Wideo Wallpaper"); jego `main.qml` nie rysuje NIC (przezroczystość),
  tylko osadza element `HdrVideoSurface`.
- `src/` — moduł QML z natywnym backendem: `HdrVideoSurface` (QQuickItem)
  tworzy **wl_subsurface na oknie pulpitu plasmashell**, renderuje przez
  **libmpv** (OpenGL render API, EGL, `hwdec=auto-copy` → NVDEC/VAAPI)
  i deklaruje **BT.2020 + ST2084** na powierzchni przez
  `wp-color-management-v1`. Efekty KWin (Przegląd) animują okno razem
  z subsurface, a HDR przechodzi 1:1 do kompozytora. Kliknięcia przechodzą
  do pulpitu (pusty input region).
- **Ikony pulpitu nad wideem**: plugin sam wykrywa, czy okno pulpitu ma kanał
  alfa. Jeśli tak — subsurface idzie `place_below`, okno robi clear na
  przezroczysto i ikony (Widok katalogu) zostają NAD wideem. DesktopView
  domyślnie alfy nie ma, więc w zestawie jest `libhdrvideo-alpha.so` — shim
  LD_PRELOAD ustawiający `QQuickWindow::setDefaultAlphaBuffer(true)` przed
  startem plasmashell (wrapper `~/.local/bin/plasmashell` wstrzykuje go
  automatycznie; bez shima plugin spada na `place_above` — wszystko działa,
  tylko ikony byłyby zakryte).
- Tryby: auto (detekcja `video-params` po `VIDEO_RECONFIG`), wymuszony HDR, SDR;
  `Sdr2Hdr` = inverse tone mapping; `Peak` = target-peak mpv; `Contain` = pasy.

## Wymagania

- **Plasma 6 na Waylandzie** z KWin obsługującym `wp-color-management-v1`
  (testowane na 6.8; protokół jest w KWin od ok. 6.3). Na X11 nie zadziała.
- **libmpv** (0.38+), EGL, libwayland.
- **HDR włączony dla ekranu** (Ustawienia systemowe → Ekrany) — bez tego KWin
  tonemapuje tapetę do SDR (zagra, ale bez blasku). Konfiguracja tapety sama
  to wykrywa: gdy HDR nie może działać (brak protokołu, brak PQ/BT.2020,
  sesja X11, żaden ekran w trybie HDR), opcje HDR są wyszarzone, a na dole
  formularza komunikat podaje dokładny powód (sonda `HdrCapabilities`;
  uwaga: KWin raportuje tf wyjścia jako gamma22 nawet w HDR — tryb HDR
  rozpoznajemy po luminancjach: peak > bieli odniesienia).
- fp16 w torze zależy od sterownika (NVIDIA 5xx+, Mesa) — przy braku plugin
  sam spada na 10/8 bit + dithering.
- Do budowania: cmake, ninja, pkgconf + nagłówki: qt6-qtbase-devel,
  qt6-qtdeclarative-devel, wayland-devel, wayland-protocols, mpv-devel,
  nagłówki EGL (libglvnd-devel / mesa-devel — zależnie od dystrybucji).

## Instalacja

```sh
./install.sh               # tapeta
./install.sh --with-icons  # + ikony pulpitu nad wideem (wrapper plasmashell)
```

Po instalacji: wyloguj się i zaloguj, potem wybierz tapetę „Wideo Wallpaper".
Odinstalowanie: `./uninstall.sh` (najpierw przełącz tapetę na inną).

## Budowanie ręczne (Chimera Linux)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
ln -sfn $PWD/package ~/.local/share/plasma/wallpapers/io.github.jocker3201.hdrvideo
# moduł QML ładowany przez QML2_IMPORT_PATH → ~/.config/plasma-workspace/env/hdrvideo-import.sh
# ikony nad wideem (opcjonalnie): wrapper ~/.local/bin/plasmashell z
#   LD_PRELOAD=$PWD/build/libhdrvideo-alpha.so exec /usr/bin/plasmashell "$@"
```

Wymagane: qt6-qtbase-devel, qt6-qtdeclarative-devel, wayland-devel,
wayland-protocols, mpv-devel, libglvnd-devel, cmake, ninja, pkgconf.

## Pułapki, które kosztowały krew

1. **libmpv wymaga `LC_NUMERIC=C`** — bez `setlocale(LC_NUMERIC, "C")`
   `mpv_create()` zwraca NULL w polskim locale plasmashell; dereferencja NULL
   ubijała plasmashell w pętli KCrash.
2. `video-params` są puste przy `MPV_EVENT_FILE_LOADED` — detekcję HDR robić
   przy `MPV_EVENT_VIDEO_RECONFIG`.
3. Wątek workera ma WŁASNĄ `wl_event_queue` (wrapper `wl_proxy_create_wrapper`)
   — nie wolno dotykać kolejki Qt; stan subsurface aplikuje się przy commicie
   RODZICA (po `place_below`/`set_position` → `QQuickWindow::update()`).
4. EGL config **bez kanału alpha** + `set_opaque_region` — inaczej mpv zostawia
   alpha=0 i kompozytor robi z tapety powietrze.
5. Chimera: brak targetu CMake `Qt6::GuiPrivate` (nagłówki private są — include
   ręczny); `-include` hack dla wygenerowanej rejestracji QML tylko dla CXX
   (`$<COMPILE_LANGUAGE:CXX>`), bo pliki protokołów są w C.
6. qmlcachegen/EGL na NVIDII daje max 8 bpc dla powierzchni — banding tłumi
   `dither-depth=auto` mpv. TODO: ścieżka Vulkan/libplacebo albo
   `EGL_EXT_gl_colorspace_bt2020_pq`.

Autorstwo: AI (Claude, Anthropic) + właściciel repozytorium. Licencja MIT.
