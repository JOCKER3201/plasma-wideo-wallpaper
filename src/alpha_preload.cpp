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

__attribute__((constructor)) static void hdrvideoEnableAlpha()
{
    QQuickWindow::setDefaultAlphaBuffer(true);
}
