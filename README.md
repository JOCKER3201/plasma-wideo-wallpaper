# plasma-wideo-wallpaper — „Wideo Wallpaper" w wyborze tapet Plazmy

Plugin tapety dla Plasmy 6 odtwarzający wideo w **prawdziwym HDR** (PQ/BT.2020)
— rzecz niemożliwa dla zwykłych pluginów QML, bo scena Qt Quick zawsze
tonemapuje do 8-bitowego sRGB.

## Architektura

- `package/` — zwykły KPackage `Plasma/Wallpaper` (pojawia się w Ustawieniach →
  Tapeta jako „Wideo (HDR)"); jego `main.qml` nie rysuje NIC (przezroczystość),
  tylko osadza element `HdrVideoSurface`.
- `src/` — moduł QML z natywnym backendem: `HdrVideoSurface` (QQuickItem)
  tworzy **wl_subsurface na oknie pulpitu plasmashell** (`place_above` —
  DesktopView ma nieprzezroczysty bufor, więc wariant `place_below` jest
  niewidoczny; kliknięcia przechodzą do pulpitu dzięki pustemu input region),
  renderuje przez **libmpv** (OpenGL render API, EGL, `hwdec=auto-copy` →
  NVDEC/VAAPI) i deklaruje **BT.2020 + ST2084** na powierzchni przez
  `wp-color-management-v1`. Efekty KWin (Przegląd) animują okno razem
  z subsurface, a HDR przechodzi 1:1 do kompozytora. Ikony pulpitu (Widok
  katalogu) byłyby zakryte przez wideo — używaj układu „Pulpit".
- Tryby: auto (detekcja `video-params` po `VIDEO_RECONFIG`), wymuszony HDR, SDR;
  `Sdr2Hdr` = inverse tone mapping; `Peak` = target-peak mpv; `Contain` = pasy.

## Budowanie (Chimera Linux)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
ln -sfn $PWD/package ~/.local/share/plasma/wallpapers/io.github.jocker3201.hdrvideo
# moduł QML ładowany przez QML2_IMPORT_PATH → ~/.config/plasma-workspace/env/hdrvideo-import.sh
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
