/*
    SPDX-FileCopyrightText: 2026 JOCKER3201
    SPDX-License-Identifier: MIT
*/

import QtQuick
import org.kde.plasma.plasmoid
import io.github.jocker3201.hdrvideo 1.0

WallpaperItem {
    // Id deliberately named "wallpaper": the document-scope id resolves
    // immediately at creation time (unlike the "wallpaper" context property
    // injected by WallpaperItem::loadWallpaper) and refers to the very same
    // root item, so wallpaper.configuration.* bindings are safe from the
    // first evaluation on.
    id: wallpaper

    // Intentionally NO background item (no Rectangle, no color fill).
    // The video is rendered by mpv on a Wayland subsurface placed BELOW the
    // plasmashell window, so everything this wallpaper draws must stay fully
    // transparent for the video to shine through. Do not add anything here.

    HdrVideoSurface {
        anchors.fill: parent

        // VideoFile is stored as a String and may be either a plain absolute
        // path ("/home/...") or already a file:// URL — normalize both.
        source: {
            const path = wallpaper.configuration.VideoFile;
            if (!path) {
                return "";
            }
            return path.startsWith("/") ? Qt.resolvedUrl("file://" + path)
                                        : Qt.resolvedUrl(path);
        }

        hdrMode: wallpaper.configuration.HdrMode
        peak: wallpaper.configuration.Peak
        sdr2hdr: wallpaper.configuration.Sdr2Hdr
        contain: wallpaper.configuration.Contain
        playing: true
    }
}
