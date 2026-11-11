// LD_PRELOAD-shim: wymusza kanał alfa w oknach QQuickWindow plasmashell.
//
// Okno pulpitu (DesktopView) domyślnie nie ma bufora alfa, więc subsurface
// z wideem umieszczona pod nim (place_below) jest niewidoczna. Ta biblioteka,
// wstrzyknięta przez LD_PRELOAD przed startem plasmashell, ustawia statyczną
// flagę Qt ZANIM powstaną okna — wtedy okno pulpitu jest ARGB, tapeta może
// zrobić clear na przezroczysto, a ikony pulpitu zostają NAD wideem.
//
// Bezpieczne przed main(): setDefaultAlphaBuffer to zwykły statyczny bool.
#include <QQuickWindow>

#include <cstdlib>
#include <cstring>
#include <string>

__attribute__((constructor)) static void hdrvideoEnableAlpha()
{
    QQuickWindow::setDefaultAlphaBuffer(true);

    // KRYTYCZNE: wytnij SIEBIE z LD_PRELOAD. Zmienna ma działać wyłącznie na
    // plasmashell — odziedziczona przez jego dzieci (Kickoff/KRunner → każda
    // aplikacja sesji) ładowała Qt do dowolnego procesu, a programy glibc
    // (glibc-run, Flatpak, Steam) zabijała na starcie muslowym NEEDED
    // libQt6Quick ("error while loading shared libraries"). Biblioteka jest
    // już załadowana w tym procesie, więc usunięcie wpisu niczego nie cofa.
    const char *cur = getenv("LD_PRELOAD");
    if (!cur || !*cur)
        return;
    std::string out;
    const char *p = cur;
    while (*p) {
        size_t len = strcspn(p, ": "); // ld.so przyjmuje oba separatory
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
