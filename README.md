# plasma-wideo-wallpaper — "Video Wallpaper" in Plasma's wallpaper chooser

A wallpaper plugin for Plasma 6 that plays video in **true HDR** (PQ/BT.2020)
— something plain QML plugins cannot do, because the Qt Quick scene always
tonemaps down to 8-bit sRGB.

## Architecture

- `package/` — a regular `Plasma/Wallpaper` KPackage (shows up in Settings →
  Wallpaper as "Video Wallpaper"); its `main.qml` draws NOTHING (transparency),
  it only embeds the `HdrVideoSurface` element.
- `src/` — a QML module with the native backend: `HdrVideoSurface` (QQuickItem)
  creates a **wl_subsurface on the plasmashell desktop window**, renders through
  **libmpv** (OpenGL render API, EGL, `hwdec=auto-copy` → NVDEC/VAAPI)
  and declares **BT.2020 + ST2084** on the surface via
  `wp-color-management-v1`. KWin effects (Overview) animate the window together
  with the subsurface, and HDR passes 1:1 through to the compositor. Clicks go
  through to the desktop (empty input region).
- **Desktop icons above the video**: the plugin detects on its own whether the
  desktop window has an alpha channel. If it does — the subsurface goes
  `place_below`, the window clears to transparent and the icons (Folder View)
  stay ABOVE the video. DesktopView has no alpha by default, so the bundle
  ships `libhdrvideo-alpha.so` — an LD_PRELOAD shim that sets
  `QQuickWindow::setDefaultAlphaBuffer(true)` before plasmashell starts
  (the `~/.local/bin/plasmashell` wrapper injects it automatically; without
  the shim the plugin falls back to `place_above` — everything still works,
  only the icons would be covered).
- Modes: auto (`video-params` detection on `VIDEO_RECONFIG`), forced HDR, SDR;
  `Sdr2Hdr` = inverse tone mapping; `Peak` = mpv target-peak; `Contain` =
  letterboxing.

## Requirements

- **Plasma 6 on Wayland** with a KWin that supports `wp-color-management-v1`
  (tested on 6.8; the protocol has been in KWin since roughly 6.3). It will not
  work on X11.
- **libmpv** (0.38+), EGL, libwayland.
- **HDR enabled for the screen** (System Settings → Displays) — without it KWin
  tonemaps the wallpaper to SDR (it will play, just without the highlights).
  The wallpaper configuration detects this by itself: when HDR cannot work
  (missing protocol, no PQ/BT.2020, X11 session, no screen in HDR mode), the
  HDR options are greyed out and a message at the bottom of the form states
  the exact reason (the `HdrCapabilities` probe; note: KWin reports the output
  tf as gamma22 even in HDR — HDR mode is recognized from the luminances:
  peak > reference white).
- fp16 in the pipeline depends on the driver (NVIDIA 5xx+, Mesa) — when it is
  missing, the plugin falls back to 10/8 bit + dithering on its own.
- To build: cmake, ninja, pkgconf + headers: qt6-qtbase-devel,
  qt6-qtdeclarative-devel, wayland-devel, wayland-protocols, mpv-devel,
  EGL headers (libglvnd-devel / mesa-devel — depending on the distribution).

## Installation

```sh
./install.sh               # the wallpaper
./install.sh --with-icons  # + desktop icons above the video (plasmashell wrapper)
```

After installing: log out and back in, then pick the "Video Wallpaper"
wallpaper. Uninstall: `./uninstall.sh` (switch to another wallpaper first).

## Manual build (Chimera Linux)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
ln -sfn $PWD/package ~/.local/share/plasma/wallpapers/io.github.jocker3201.hdrvideo
# QML module loaded via QML2_IMPORT_PATH → ~/.config/plasma-workspace/env/hdrvideo-import.sh
# icons above the video (optional): a ~/.local/bin/plasmashell wrapper with
#   LD_PRELOAD=$PWD/build/libhdrvideo-alpha.so exec /usr/bin/plasmashell "$@"
```

Required: qt6-qtbase-devel, qt6-qtdeclarative-devel, wayland-devel,
wayland-protocols, mpv-devel, libglvnd-devel, cmake, ninja, pkgconf.

## Pitfalls that cost blood

1. **libmpv strictly requires `LC_NUMERIC=C`** — without
   `setlocale(LC_NUMERIC, "C")`, `mpv_create()` returns NULL under a Polish
   plasmashell locale; the NULL dereference kept killing plasmashell in a
   KCrash loop.
2. `video-params` is empty at `MPV_EVENT_FILE_LOADED` — do the HDR detection
   on `MPV_EVENT_VIDEO_RECONFIG`.
3. The worker thread has its OWN `wl_event_queue` (a `wl_proxy_create_wrapper`
   wrapper) — never touch the Qt queue; subsurface state is applied on the
   PARENT's commit (after `place_below`/`set_position` →
   `QQuickWindow::update()`).
4. EGL config **without an alpha channel** + `set_opaque_region` — otherwise
   mpv leaves alpha=0 and the compositor turns the wallpaper into thin air.
5. Chimera: no `Qt6::GuiPrivate` CMake target (the private headers are on
   disk — include them manually); the `-include` hack for the generated QML
   registration applies to CXX only (`$<COMPILE_LANGUAGE:CXX>`), because the
   protocol files are C.
6. qmlcachegen/EGL on NVIDIA gives at most 8 bpc for the surface — the banding
   is tamed by mpv's `dither-depth=auto`. TODO: a Vulkan/libplacebo path or
   `EGL_EXT_gl_colorspace_bt2020_pq`.

Authorship: AI (Claude, Anthropic) + the repository owner. MIT license.
