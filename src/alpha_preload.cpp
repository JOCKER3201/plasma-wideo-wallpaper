// LD_PRELOAD shim: forces an alpha channel in plasmashell's QQuickWindow windows.
//
// The desktop window (DesktopView) has no alpha buffer by default, so a video
// subsurface placed under it (place_below) is invisible. This library,
// injected via LD_PRELOAD before plasmashell starts, sets the static Qt flag
// BEFORE any windows are created — the desktop window is then ARGB, the
// wallpaper can clear to transparent, and the desktop icons stay ABOVE the
// video.
//
// Safe before main(): setDefaultAlphaBuffer is a plain static bool.
#include <QQuickWindow>

#include <cstdlib>
#include <cstring>
#include <string>

__attribute__((constructor)) static void hdrvideoEnableAlpha()
{
    QQuickWindow::setDefaultAlphaBuffer(true);

    // CRITICAL: cut OURSELVES out of LD_PRELOAD. The variable is meant to act
    // on plasmashell only — inherited by its children (Kickoff/KRunner → every
    // session application) it used to load Qt into arbitrary processes, and it
    // killed glibc programs (glibc-run, Flatpak, Steam) at startup with a musl
    // NEEDED libQt6Quick ("error while loading shared libraries"). The library
    // is already loaded in this process, so removing the entry undoes nothing.
    const char *cur = getenv("LD_PRELOAD");
    if (!cur || !*cur)
        return;
    std::string out;
    const char *p = cur;
    while (*p) {
        size_t len = strcspn(p, ": "); // ld.so accepts both separators
        if (len > 0) {
            std::string tok(p, len);
            if (tok.find("libhdrvideo-alpha.so") == std::string::npos) {
                if (!out.empty())
                    out += ':';
                out += tok;
            }
        }
        p += len;
        while (*p == ':' || *p == ' ')
            ++p;
    }
    if (out.empty())
        unsetenv("LD_PRELOAD");
    else
        setenv("LD_PRELOAD", out.c_str(), 1);
}
