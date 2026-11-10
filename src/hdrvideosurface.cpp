#include "hdrvideosurface.h"

#include <QGuiApplication>
#include <QPointer>
#include <QQuickWindow>
#include <QScreen>
#include <QtGui/qguiapplication_platform.h> // QNativeInterface::QWaylandApplication
#include <qpa/qplatformnativeinterface.h>   // Qt6::GuiPrivate — wl_surface okna

#include <wayland-client.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x0040
#endif

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include "color-management-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include <clocale>
#include <poll.h>
#include <unistd.h>

namespace {
void wlog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[hdrvideo] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void *mpv_gpa(void *, const char *name)
{
    return reinterpret_cast<void *>(eglGetProcAddress(name));
}

void pipeWake(int fd)
{
    const char b = 1;
    (void)!write(fd, &b, 1);
}

void pipeWakeCb(void *ctx)
{
    pipeWake(static_cast<int>(reinterpret_cast<intptr_t>(ctx)));
}

void drain(int fd)
{
    char buf[64];
    (void)!read(fd, buf, sizeof(buf));
}
} // namespace

// ======================================================================
struct HdrVideoSurface::Worker
{
    // --- komunikacja (main thread -> worker) ---
    std::mutex mtx;
    std::condition_variable cvDone;
    bool finished = false;
    struct Shared {
        bool quit = false;
        bool dirtyGeo = true, dirtySource = false, dirtyOpts = false, dirtyPlay = false;
        int logW = 0, logH = 0, pxW = 0, pxH = 0, posX = 0, posY = 0;
        QString source;
        int hdrMode = 0;
        int peak = 1000;
        bool sdr2hdr = false, contain = false, playing = true;
    } sh;
    int cmdW = -1, cmdR = -1;

    QPointer<HdrVideoSurface> item;
    std::thread thr;

    // --- stan wątku workera ---
    wl_display *dpy = nullptr;
    wl_surface *parent = nullptr;
    wl_event_queue *queue = nullptr;
    wl_display *dpyW = nullptr; // wrapper na naszej kolejce
    wl_registry *reg = nullptr;
    wl_compositor *comp = nullptr;
    wl_subcompositor *subcomp = nullptr;
    wp_viewporter *vpr = nullptr;
    wp_color_manager_v1 *cm = nullptr;
    bool cmPq = false, cmBt2020 = false;

    wl_surface *surf = nullptr;
    wl_subsurface *subsurf = nullptr;
    wp_viewport *vp = nullptr;
    wp_color_management_surface_v1 *cmSurf = nullptr;
    bool descReady = false, descFailed = false;

    wl_egl_window *eglWin = nullptr;
    EGLDisplay edpy = EGL_NO_DISPLAY;
    EGLConfig ecfg = nullptr;
    EGLContext ectx = EGL_NO_CONTEXT;
    EGLSurface esurf = EGL_NO_SURFACE;

    mpv_handle *mpv = nullptr;
    mpv_render_context *rctx = nullptr;
    int mpvR = -1, mpvW = -1, updR = -1, updW = -1;

    bool hdrApplied = false;
    bool fileHdr = false;
    int curPxW = 0, curPxH = 0;

    // ------------------------------------------------ registry
    static void onGlobal(void *data, wl_registry *r, uint32_t name,
                         const char *iface, uint32_t ver)
    {
        auto *w = static_cast<Worker *>(data);
        if (!strcmp(iface, wl_compositor_interface.name))
            w->comp = static_cast<wl_compositor *>(
                wl_registry_bind(r, name, &wl_compositor_interface, std::min(ver, 4u)));
        else if (!strcmp(iface, wl_subcompositor_interface.name))
            w->subcomp = static_cast<wl_subcompositor *>(
                wl_registry_bind(r, name, &wl_subcompositor_interface, 1));
        else if (!strcmp(iface, wp_viewporter_interface.name))
            w->vpr = static_cast<wp_viewporter *>(
                wl_registry_bind(r, name, &wp_viewporter_interface, 1));
        else if (!strcmp(iface, wp_color_manager_v1_interface.name)) {
            w->cm = static_cast<wp_color_manager_v1 *>(
                wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1));
            static const wp_color_manager_v1_listener cmL = {
                /*supported_intent*/ [](void *, wp_color_manager_v1 *, uint32_t) {},
                /*supported_feature*/ [](void *, wp_color_manager_v1 *, uint32_t) {},
                /*supported_tf_named*/
                [](void *d, wp_color_manager_v1 *, uint32_t tf) {
                    if (tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ)
                        static_cast<Worker *>(d)->cmPq = true;
                },
                /*supported_primaries_named*/
                [](void *d, wp_color_manager_v1 *, uint32_t p) {
                    if (p == WP_COLOR_MANAGER_V1_PRIMARIES_BT2020)
                        static_cast<Worker *>(d)->cmBt2020 = true;
                },
                /*done*/ [](void *, wp_color_manager_v1 *) {},
            };
            wp_color_manager_v1_add_listener(w->cm, &cmL, w);
        }
    }
    static void onGlobalRemove(void *, wl_registry *, uint32_t) {}

    // ------------------------------------------------ pomocnicze (wątek workera)
    void requestParentRepaint()
    {
        // stan subsurface (pozycja, place_below) aplikuje się przy commicie RODZICA;
        // okno pulpitu jest statyczne, więc wymuszamy jedną klatkę sceny Qt
        QMetaObject::invokeMethod(
            item.data(), [it = item]() { if (it && it->window()) it->window()->update(); },
            Qt::QueuedConnection);
    }

    void notifyActiveHdr(bool on)
    {
        QMetaObject::invokeMethod(
            item.data(), [it = item, on]() { if (it) it->setActiveHdr(on); },
            Qt::QueuedConnection);
    }

    void mpvOpt(const char *k, const char *v)
    {
        if (!mpv)
            return;
        int r = mpv_set_option_string(mpv, k, v);
        if (r < 0)
            wlog("opcja mpv %s=%s: %s", k, v, mpv_error_string(r));
    }

    void applyHdr(bool on, int peak, bool sdr2hdr)
    {
        if (!cm || !cmPq || !cmBt2020)
            on = false;
        if (on) {
            if (!cmSurf)
                cmSurf = wp_color_manager_v1_get_surface(cm, surf);
            descReady = descFailed = false;
            auto *par = wp_color_manager_v1_create_parametric_creator(cm);
            wp_image_description_creator_params_v1_set_primaries_named(
                par, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);
            wp_image_description_creator_params_v1_set_tf_named(
                par, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
            wp_image_description_v1 *desc =
                wp_image_description_creator_params_v1_create(par);
            static const wp_image_description_v1_listener dl = {
                /*failed*/
                [](void *d, wp_image_description_v1 *, uint32_t, const char *msg) {
                    static_cast<Worker *>(d)->descFailed = true;
                    wlog("image description odrzucony: %s", msg);
                },
                /*ready*/
                [](void *d, wp_image_description_v1 *, uint32_t) {
                    static_cast<Worker *>(d)->descReady = true;
                },
            };
            wp_image_description_v1_add_listener(desc, &dl, this);
            while (!descReady && !descFailed)
                if (wl_display_roundtrip_queue(dpy, queue) < 0)
                    break;
            if (descReady)
                wp_color_management_surface_v1_set_image_description(
                    cmSurf, desc, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
            wp_image_description_v1_destroy(desc);
            if (!descReady)
                on = false;
        } else if (cmSurf) {
            wp_color_management_surface_v1_unset_image_description(cmSurf);
        }

        if (on) {
            char pk[32];
            snprintf(pk, sizeof(pk), "%d", peak);
            mpvOpt("target-prim", "bt.2020");
            mpvOpt("target-trc", "pq");
            mpvOpt("target-peak", pk);
            mpvOpt("tone-mapping", "clip");
            mpvOpt("hdr-compute-peak", "no");
            mpvOpt("inverse-tone-mapping", sdr2hdr ? "yes" : "no");
        } else {
            mpvOpt("target-prim", "auto");
            mpvOpt("target-trc", "auto");
            mpvOpt("target-peak", "auto");
            mpvOpt("tone-mapping", "spline");
            mpvOpt("hdr-compute-peak", "auto");
            mpvOpt("inverse-tone-mapping", "no");
        }
        if (hdrApplied != on) {
            hdrApplied = on;
            wlog("powierzchnia: %s", on ? "BT.2020 + PQ (HDR)" : "SDR");
            notifyActiveHdr(on);
        }
    }

    void decideHdr(int mode, int peak, bool sdr2hdr)
    {
        bool want = mode == 1 ? true : mode == 2 ? false : fileHdr;
        applyHdr(want, peak, sdr2hdr);
    }

    // ------------------------------------------------ główna pętla wątku
    void run()
    {
        // 1. registry + globale na własnej kolejce
        queue = wl_display_create_queue(dpy);
        dpyW = static_cast<wl_display *>(
            wl_proxy_create_wrapper(reinterpret_cast<void *>(dpy)));
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(dpyW), queue);
        reg = wl_display_get_registry(dpyW);
        static const wl_registry_listener regL = { onGlobal, onGlobalRemove };
        wl_registry_add_listener(reg, &regL, this);
        wl_display_roundtrip_queue(dpy, queue);
        wl_display_roundtrip_queue(dpy, queue);
        if (!comp || !subcomp) {
            wlog("brak wl_compositor/wl_subcompositor — koniec");
            return;
        }
        if (!cm)
            wlog("brak wp_color_manager_v1 — tylko SDR");

        // 2. powierzchnia + subsurface pod oknem pulpitu
        surf = wl_compositor_create_surface(comp);
        wl_region *empty = wl_compositor_create_region(comp);
        wl_surface_set_input_region(surf, empty);
        wl_region_destroy(empty);
        wl_region *full = wl_compositor_create_region(comp);
        wl_region_add(full, 0, 0, INT32_MAX, INT32_MAX);
        wl_surface_set_opaque_region(surf, full);
        wl_region_destroy(full);

        subsurf = wl_subcompositor_get_subsurface(subcomp, surf, parent);
        // place_above, nie place_below: okno pulpitu plasmashell (DesktopView) ma
        // nieprzezroczysty bufor (format bez użytecznej alphy) i zasłania wszystko
        // pod sobą — wideo musi być nad nim. Kliknięcia i tak przechodzą do pulpitu
        // (pusty input region), a efekty KWin animują subsurface razem z oknem.
        wl_subsurface_place_above(subsurf, parent);
        wl_subsurface_set_desync(subsurf);

        int logW, logH, pxW, pxH, posX, posY;
        {
            std::lock_guard<std::mutex> l(mtx);
            logW = sh.logW; logH = sh.logH; pxW = sh.pxW; pxH = sh.pxH;
            posX = sh.posX; posY = sh.posY;
            sh.dirtyGeo = false;
        }
        wl_subsurface_set_position(subsurf, posX, posY);
        if (vpr) {
            vp = wp_viewporter_get_viewport(vpr, surf);
            wp_viewport_set_destination(vp, logW, logH);
        }
        wlog("subsurface: pos %d,%d log %dx%d px %dx%d", posX, posY, logW, logH,
             pxW, pxH);
        requestParentRepaint();

        // 3. EGL (wspólny EGLDisplay z Qt — bez eglTerminate!)
        edpy = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, dpy, nullptr);
        EGLint maj, min;
        if (!eglInitialize(edpy, &maj, &min)) {
            wlog("eglInitialize nieudane");
            return;
        }
        eglBindAPI(EGL_OPENGL_ES_API);
        EGLint n = 0;
        const EGLint cfg10[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                 EGL_RED_SIZE, 10, EGL_GREEN_SIZE, 10,
                                 EGL_BLUE_SIZE, 10, EGL_ALPHA_SIZE, 0, EGL_NONE };
        const EGLint cfg8[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_NONE };
        if (!eglChooseConfig(edpy, cfg10, &ecfg, 1, &n) || n < 1) {
            eglChooseConfig(edpy, cfg8, &ecfg, 1, &n);
            wlog("EGL: 8 bitów na kanał");
        } else {
            wlog("EGL: 10 bitów na kanał");
        }
        if (n < 1) {
            wlog("brak configu EGL");
            return;
        }
        const EGLint ctx3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        const EGLint ctx2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        ectx = eglCreateContext(edpy, ecfg, EGL_NO_CONTEXT, ctx3);
        if (ectx == EGL_NO_CONTEXT)
            ectx = eglCreateContext(edpy, ecfg, EGL_NO_CONTEXT, ctx2);
        eglWin = wl_egl_window_create(surf, pxW, pxH);
        curPxW = pxW; curPxH = pxH;
        esurf = eglCreateWindowSurface(
            edpy, ecfg, reinterpret_cast<EGLNativeWindowType>(eglWin), nullptr);
        if (esurf == EGL_NO_SURFACE || !eglMakeCurrent(edpy, esurf, esurf, ectx)) {
            wlog("EGL surface/context nieudane");
            return;
        }
        eglSwapInterval(edpy, 1);

        // 4. mpv — libmpv twardo wymaga LC_NUMERIC=C (inaczej mpv_create -> NULL)
        setlocale(LC_NUMERIC, "C");
        mpv = mpv_create();
        if (!mpv) {
            wlog("mpv_create zwrócił NULL (locale?) — koniec workera");
            return;
        }
        mpvOpt("terminal", "no");
        mpvOpt("vo", "libmpv");
        mpvOpt("audio", "no");
        mpvOpt("loop-file", "inf");
        mpvOpt("hwdec", "auto-copy");
        {
            std::lock_guard<std::mutex> l(mtx);
            mpvOpt("panscan", sh.contain ? "0.0" : "1.0");
        }
        if (mpv_initialize(mpv) < 0) {
            wlog("mpv_initialize nieudane");
            return;
        }
        mpv_opengl_init_params glinit = { mpv_gpa, nullptr };
        int adv = 1;
        mpv_render_param rp[] = {
            { MPV_RENDER_PARAM_API_TYPE,
              const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL) },
            { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glinit },
            { MPV_RENDER_PARAM_ADVANCED_CONTROL, &adv },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
        if (mpv_render_context_create(&rctx, mpv, rp) < 0) {
            wlog("mpv_render_context_create nieudane");
            return;
        }
        int p1[2], p2[2];
        (void)!pipe(p1); (void)!pipe(p2);
        mpvR = p1[0]; mpvW = p1[1]; updR = p2[0]; updW = p2[1];
        mpv_set_wakeup_callback(mpv, pipeWakeCb,
                                reinterpret_cast<void *>(intptr_t(mpvW)));
        mpv_render_context_set_update_callback(
            rctx, pipeWakeCb, reinterpret_cast<void *>(intptr_t(updW)));

        // 5. pętla
        const int wfd = wl_display_get_fd(dpy);
        bool quit = false;
        while (!quit) {
            wl_display_flush(dpy);
            while (wl_display_prepare_read_queue(dpy, queue) != 0)
                wl_display_dispatch_queue_pending(dpy, queue);
            pollfd fds[4] = { { wfd, POLLIN, 0 },
                              { cmdR, POLLIN, 0 },
                              { mpvR, POLLIN, 0 },
                              { updR, POLLIN, 0 } };
            poll(fds, 4, 1000);
            if (fds[0].revents & POLLIN)
                wl_display_read_events(dpy);
            else
                wl_display_cancel_read(dpy);
            wl_display_dispatch_queue_pending(dpy, queue);

            if (fds[1].revents & POLLIN) { // komendy z itemu
                drain(cmdR);
                Shared s;
                {
                    std::lock_guard<std::mutex> l(mtx);
                    s = sh;
                    sh.dirtyGeo = sh.dirtySource = sh.dirtyOpts = sh.dirtyPlay = false;
                }
                if (s.quit)
                    quit = true;
                if (s.dirtyGeo) {
                    wl_subsurface_set_position(subsurf, s.posX, s.posY);
                    if (vp)
                        wp_viewport_set_destination(vp, s.logW, s.logH);
                    if (s.pxW != curPxW || s.pxH != curPxH) {
                        wl_egl_window_resize(eglWin, s.pxW, s.pxH, 0, 0);
                        curPxW = s.pxW; curPxH = s.pxH;
                    }
                    requestParentRepaint();
                }
                if (s.dirtySource && !s.source.isEmpty()) {
                    const QByteArray p = s.source.toUtf8();
                    const char *cmd[] = { "loadfile", p.constData(), nullptr };
                    int r = mpv_command(mpv, cmd);
                    if (r < 0)
                        wlog("loadfile: %s", mpv_error_string(r));
                    else
                        wlog("gram: %s", p.constData());
                }
                if (s.dirtyOpts) {
                    mpvOpt("panscan", s.contain ? "0.0" : "1.0");
                    decideHdr(s.hdrMode, s.peak, s.sdr2hdr);
                }
                if (s.dirtyPlay)
                    mpv_set_property_string(mpv, "pause", s.playing ? "no" : "yes");
            }

            if (fds[2].revents & POLLIN) { // zdarzenia mpv
                drain(mpvR);
                while (true) {
                    mpv_event *ev = mpv_wait_event(mpv, 0.0);
                    if (ev->event_id == MPV_EVENT_NONE)
                        break;
                    if (ev->event_id == MPV_EVENT_SHUTDOWN) {
                        quit = true;
                    } else if (ev->event_id == MPV_EVENT_FILE_LOADED
                               || ev->event_id == MPV_EVENT_VIDEO_RECONFIG) {
                        // video-params wypełnia się dopiero przy rekonfiguracji
                        // toru wideo — FILE_LOADED bywa za wcześnie
                        char *prim = mpv_get_property_string(
                            mpv, "video-params/primaries");
                        double sig = 0;
                        mpv_get_property(mpv, "video-params/sig-peak",
                                         MPV_FORMAT_DOUBLE, &sig);
                        const bool nowHdr =
                            (prim && !strcmp(prim, "bt.2020")) || sig > 1.01;
                        const bool changed = nowHdr != fileHdr
                            || ev->event_id == MPV_EVENT_FILE_LOADED;
                        fileHdr = nowHdr;
                        mpv_free(prim);
                        if (changed) {
                            Shared s;
                            {
                                std::lock_guard<std::mutex> l(mtx);
                                s = sh;
                            }
                            wlog("parametry wideo (hdr w pliku: %d)", int(fileHdr));
                            decideHdr(s.hdrMode, s.peak, s.sdr2hdr);
                        }
                    }
                }
            }

            if (fds[3].revents & POLLIN) { // nowa klatka
                drain(updR);
                uint64_t fl = mpv_render_context_update(rctx);
                if (fl & MPV_RENDER_UPDATE_FRAME) {
                    mpv_opengl_fbo fbo = { 0, curPxW, curPxH, 0 };
                    int flip = 1;
                    mpv_render_param rr[] = {
                        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
                        { MPV_RENDER_PARAM_FLIP_Y, &flip },
                        { MPV_RENDER_PARAM_INVALID, nullptr },
                    };
                    mpv_render_context_render(rctx, rr);
                    eglSwapBuffers(edpy, esurf);
                    mpv_render_context_report_swap(rctx);
                }
            }
        }

        // 6. sprzątanie (bez eglTerminate — display dzielony z Qt)
        mpv_render_context_free(rctx);
        mpv_terminate_destroy(mpv);
        eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (esurf != EGL_NO_SURFACE) eglDestroySurface(edpy, esurf);
        if (ectx != EGL_NO_CONTEXT) eglDestroyContext(edpy, ectx);
        if (eglWin) wl_egl_window_destroy(eglWin);
        if (vp) wp_viewport_destroy(vp);
        if (cmSurf) wp_color_management_surface_v1_destroy(cmSurf);
        if (subsurf) wl_subsurface_destroy(subsurf);
        if (surf) wl_surface_destroy(surf);
        if (cm) wp_color_manager_v1_destroy(cm);
        if (vpr) wp_viewporter_destroy(vpr);
        if (subcomp) wl_subcompositor_destroy(subcomp);
        if (comp) wl_compositor_destroy(comp);
        if (reg) wl_registry_destroy(reg);
        if (dpyW) wl_proxy_wrapper_destroy(dpyW);
        wl_display_flush(dpy);
        if (queue) wl_event_queue_destroy(queue);
        for (int fd : { mpvR, mpvW, updR, updW })
            if (fd >= 0) close(fd);
        {
            std::lock_guard<std::mutex> l(mtx);
            finished = true;
        }
        cvDone.notify_all();
        wlog("worker zakończony");
    }
};

// ======================================================================
HdrVideoSurface::HdrVideoSurface(QQuickItem *parent)
    : QQuickItem(parent)
{
}

HdrVideoSurface::~HdrVideoSurface()
{
    stopWorker();
}

void HdrVideoSurface::componentComplete()
{
    QQuickItem::componentComplete();
    m_complete = true;
    maybeStart();
}

void HdrVideoSurface::itemChange(ItemChange change, const ItemChangeData &data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange) {
        stopWorker();
        if (data.window)
            maybeStart();
    }
}

void HdrVideoSurface::geometryChange(const QRectF &newG, const QRectF &oldG)
{
    QQuickItem::geometryChange(newG, oldG);
    pushGeometry();
}

void HdrVideoSurface::maybeStart()
{
    if (m_worker || !m_complete || !window())
        return;
    if (!window()->handle()) {
        // okno platformowe jeszcze nie istnieje (np. samodzielny podgląd qml) —
        // spróbuj ponownie po inicjalizacji scene grapha
        connect(window(), &QQuickWindow::sceneGraphInitialized, this,
                &HdrVideoSurface::maybeStart, Qt::UniqueConnection);
        return;
    }

    auto *wapp = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    wl_display *dpy = wapp ? wapp->display() : nullptr;
    QPlatformNativeInterface *ni = QGuiApplication::platformNativeInterface();
    wl_surface *parentSurf = dpy && ni
        ? static_cast<wl_surface *>(
              ni->nativeResourceForWindow(QByteArrayLiteral("surface"), window()))
        : nullptr;
    if (!dpy || !parentSurf) {
        fprintf(stderr, "[hdrvideo] brak wl_display/wl_surface (nie Wayland?)\n");
        return;
    }

    m_worker = std::make_unique<Worker>();
    m_worker->item = this;
    m_worker->dpy = dpy;
    m_worker->parent = parentSurf;
    int p[2];
    if (pipe(p) != 0)
        return;
    m_worker->cmdR = p[0];
    m_worker->cmdW = p[1];

    {
        std::lock_guard<std::mutex> l(m_worker->mtx);
        auto &s = m_worker->sh;
        const qreal dpr = window()->devicePixelRatio();
        const QPointF scenePos = mapToScene(QPointF(0, 0));
        s.logW = std::max(1, int(std::round(width())));
        s.logH = std::max(1, int(std::round(height())));
        s.pxW = std::max(1, int(std::round(width() * dpr)));
        s.pxH = std::max(1, int(std::round(height() * dpr)));
        s.posX = int(std::round(scenePos.x()));
        s.posY = int(std::round(scenePos.y()));
        s.source = m_source.isLocalFile() ? m_source.toLocalFile()
                                          : m_source.toString();
        s.dirtySource = !s.source.isEmpty();
        s.hdrMode = m_hdrMode;
        s.peak = m_peak;
        s.sdr2hdr = m_sdr2hdr;
        s.contain = m_contain;
        s.playing = m_playing;
        s.dirtyOpts = s.dirtyPlay = true;
    }
    Worker *w = m_worker.get();
    m_worker->thr = std::thread([w] { w->run(); });
    pipeWake(m_worker->cmdW);
}

void HdrVideoSurface::stopWorker()
{
    if (!m_worker)
        return;
    {
        std::lock_guard<std::mutex> l(m_worker->mtx);
        m_worker->sh.quit = true;
    }
    pipeWake(m_worker->cmdW);
    // swap może wisieć na vsync zgaszonego ekranu — nie blokuj plasmashell w nieskończoność
    std::unique_lock<std::mutex> l(m_worker->mtx);
    bool done = m_worker->cvDone.wait_for(l, std::chrono::seconds(3),
                                          [&] { return m_worker->finished; });
    l.unlock();
    if (done) {
        m_worker->thr.join();
    } else {
        fprintf(stderr, "[hdrvideo] worker nie kończy (zgaszony ekran?) — detach\n");
        m_worker->thr.detach();
        m_worker.release(); // świadomy, jednorazowy przeciek zamiast deadlocka
        return;
    }
    close(m_worker->cmdR);
    close(m_worker->cmdW);
    m_worker.reset();
}

void HdrVideoSurface::pushGeometry()
{
    if (!m_worker || !window())
        return;
    const qreal dpr = window()->devicePixelRatio();
    const QPointF scenePos = mapToScene(QPointF(0, 0));
    std::lock_guard<std::mutex> l(m_worker->mtx);
    auto &s = m_worker->sh;
    s.logW = std::max(1, int(std::round(width())));
    s.logH = std::max(1, int(std::round(height())));
    s.pxW = std::max(1, int(std::round(width() * dpr)));
    s.pxH = std::max(1, int(std::round(height() * dpr)));
    s.posX = int(std::round(scenePos.x()));
    s.posY = int(std::round(scenePos.y()));
    s.dirtyGeo = true;
    pipeWake(m_worker->cmdW);
}

#define SETTER(fn, member, dirty, signal)                                      \
    {                                                                          \
        if (member == v)                                                       \
            return;                                                           \
        member = v;                                                           \
        if (m_worker) {                                                       \
            std::lock_guard<std::mutex> l(m_worker->mtx);                     \
            fn;                                                               \
            m_worker->sh.dirty = true;                                        \
        }                                                                     \
        if (m_worker)                                                         \
            pipeWake(m_worker->cmdW);                                         \
        emit signal();                                                        \
    }

void HdrVideoSurface::setSource(const QUrl &v)
{
    if (m_source == v)
        return;
    m_source = v;
    if (m_worker) {
        std::lock_guard<std::mutex> l(m_worker->mtx);
        m_worker->sh.source =
            v.isLocalFile() ? v.toLocalFile() : v.toString();
        m_worker->sh.dirtySource = true;
    }
    if (m_worker)
        pipeWake(m_worker->cmdW);
    emit sourceChanged();
}

void HdrVideoSurface::setHdrMode(int v)
    SETTER(m_worker->sh.hdrMode = v, m_hdrMode, dirtyOpts, hdrModeChanged)
void HdrVideoSurface::setPeak(int v)
    SETTER(m_worker->sh.peak = v, m_peak, dirtyOpts, peakChanged)
void HdrVideoSurface::setSdr2hdr(bool v)
    SETTER(m_worker->sh.sdr2hdr = v, m_sdr2hdr, dirtyOpts, sdr2hdrChanged)
void HdrVideoSurface::setContain(bool v)
    SETTER(m_worker->sh.contain = v, m_contain, dirtyOpts, containChanged)
void HdrVideoSurface::setPlaying(bool v)
    SETTER(m_worker->sh.playing = v, m_playing, dirtyPlay, playingChanged)

void HdrVideoSurface::setActiveHdr(bool v)
{
    if (m_activeHdr == v)
        return;
    m_activeHdr = v;
    emit activeHdrChanged();
}
