#include "hdrcapabilities.h"

#include <wayland-client.h>

#include "color-management-v1-client-protocol.h"

#include <cstring>
#include <memory>
#include <vector>

namespace {

struct OutProbe {
    wl_output *out = nullptr;
    wp_color_management_output_v1 *cmOut = nullptr;
    wp_image_description_v1 *desc = nullptr;
    wp_image_description_info_v1 *info = nullptr;
    QString name;
    bool isPq = false;
    uint32_t maxLum = 0, refLum = 0;
    bool done = false; // info done / failed / brak danych

    // KWin raportuje tf wyjścia jako gamma22 nawet w trybie HDR (to preferowana
    // przestrzeń klienta, nie sygnał łącza) — tryb HDR zdradzają luminancje:
    // peak (max_lum) > bieli odniesienia SDR (reference_lum).
    bool isHdr() const { return isPq || (maxLum > refLum && refLum > 0); }
};

struct Probe {
    wl_display *dpy = nullptr;
    wp_color_manager_v1 *cm = nullptr;
    bool pq = false, bt2020 = false;
    std::vector<std::unique_ptr<OutProbe>> outs;
};

const wl_output_listener outL = {
    /*geometry*/ [](void *, wl_output *, int32_t, int32_t, int32_t, int32_t,
                    int32_t, const char *, const char *, int32_t) {},
    /*mode*/ [](void *, wl_output *, uint32_t, int32_t, int32_t, int32_t) {},
    /*done*/ [](void *, wl_output *) {},
    /*scale*/ [](void *, wl_output *, int32_t) {},
    /*name*/
    [](void *d, wl_output *, const char *name) {
        static_cast<OutProbe *>(d)->name = QString::fromUtf8(name);
    },
    /*description*/ [](void *, wl_output *, const char *) {},
};

const wp_color_manager_v1_listener cmL = {
    /*supported_intent*/ [](void *, wp_color_manager_v1 *, uint32_t) {},
    /*supported_feature*/ [](void *, wp_color_manager_v1 *, uint32_t) {},
    /*supported_tf_named*/
    [](void *d, wp_color_manager_v1 *, uint32_t tf) {
        if (tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ)
            static_cast<Probe *>(d)->pq = true;
    },
    /*supported_primaries_named*/
    [](void *d, wp_color_manager_v1 *, uint32_t p) {
        if (p == WP_COLOR_MANAGER_V1_PRIMARIES_BT2020)
            static_cast<Probe *>(d)->bt2020 = true;
    },
    /*done*/ [](void *, wp_color_manager_v1 *) {},
};

const wp_image_description_info_v1_listener infoL = {
    /*done*/
    [](void *d, wp_image_description_info_v1 *) {
        static_cast<OutProbe *>(d)->done = true;
    },
    /*icc_file*/ [](void *, wp_image_description_info_v1 *, int32_t, uint32_t) {},
    /*primaries*/
    [](void *, wp_image_description_info_v1 *, int32_t, int32_t, int32_t,
       int32_t, int32_t, int32_t, int32_t, int32_t) {},
    /*primaries_named*/ [](void *, wp_image_description_info_v1 *, uint32_t) {},
    /*tf_power*/ [](void *, wp_image_description_info_v1 *, uint32_t) {},
    /*tf_named*/
    [](void *d, wp_image_description_info_v1 *, uint32_t tf) {
        fprintf(stderr, "[hdrcaps] %s: tf_named=%u\n",
                qPrintable(static_cast<OutProbe *>(d)->name), tf);
        if (tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ)
            static_cast<OutProbe *>(d)->isPq = true;
    },
    /*luminances*/
    [](void *d, wp_image_description_info_v1 *, uint32_t minL, uint32_t maxL,
       uint32_t refL) {
        auto *op = static_cast<OutProbe *>(d);
        op->maxLum = maxL;
        op->refLum = refL;
        fprintf(stderr, "[hdrcaps] %s: luminances min=%u max=%u ref=%u\n",
                qPrintable(op->name), minL, maxL, refL);
    },
    /*target_primaries*/
    [](void *, wp_image_description_info_v1 *, int32_t, int32_t, int32_t,
       int32_t, int32_t, int32_t, int32_t, int32_t) {},
    /*target_luminance*/
    [](void *, wp_image_description_info_v1 *, uint32_t, uint32_t) {},
    /*target_max_cll*/ [](void *, wp_image_description_info_v1 *, uint32_t) {},
    /*target_max_fall*/ [](void *, wp_image_description_info_v1 *, uint32_t) {},
};

const wp_image_description_v1_listener descL = {
    /*failed*/
    [](void *d, wp_image_description_v1 *, uint32_t cause, const char *msg) {
        fprintf(stderr, "[hdrcaps] %s: desc FAILED cause=%u (%s)\n",
                qPrintable(static_cast<OutProbe *>(d)->name), cause, msg);
        static_cast<OutProbe *>(d)->done = true;
    },
    /*ready*/
    [](void *d, wp_image_description_v1 *, uint32_t) {
        auto *op = static_cast<OutProbe *>(d);
        op->info = wp_image_description_v1_get_information(op->desc);
        wp_image_description_info_v1_add_listener(op->info, &infoL, op);
    },
};

const wl_registry_listener regL = {
    /*global*/
    [](void *d, wl_registry *r, uint32_t name, const char *iface, uint32_t ver) {
        auto *p = static_cast<Probe *>(d);
        if (!strcmp(iface, wp_color_manager_v1_interface.name)) {
            p->cm = static_cast<wp_color_manager_v1 *>(
                wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1));
            wp_color_manager_v1_add_listener(p->cm, &cmL, p);
        } else if (!strcmp(iface, wl_output_interface.name) && ver >= 4) {
            auto op = std::make_unique<OutProbe>();
            op->out = static_cast<wl_output *>(
                wl_registry_bind(r, name, &wl_output_interface, 4));
            wl_output_add_listener(op->out, &outL, op.get());
            p->outs.push_back(std::move(op));
        }
    },
    /*global_remove*/ [](void *, wl_registry *, uint32_t) {},
};

} // namespace

HdrCapabilities::HdrCapabilities(QObject *parent)
    : QObject(parent)
{
    probe();
}

void HdrCapabilities::probe()
{
    const char *i18nDomain = "plasma_wallpaper_io.github.jocker3201.hdrvideo";
    Q_UNUSED(i18nDomain);

    Probe p;
    p.dpy = wl_display_connect(nullptr);
    if (!p.dpy) {
        m_reason = QStringLiteral(
            "HDR niedostępny: to nie jest sesja Wayland (tapeta HDR wymaga "
            "Plasmy 6 na Waylandzie).");
        return;
    }
    m_wayland = true;

    wl_registry *reg = wl_display_get_registry(p.dpy);
    wl_registry_add_listener(reg, &regL, &p);
    wl_display_roundtrip(p.dpy); // globale
    wl_display_roundtrip(p.dpy); // supported_*, nazwy wyjść

    if (!p.cm) {
        m_reason = QStringLiteral(
            "HDR niedostępny: kompozytor nie udostępnia protokołu "
            "wp-color-management-v1 (wymagany KWin z Plasmy ≥ 6.3).");
    } else if (!p.pq || !p.bt2020) {
        m_protocol = true;
        m_reason = QStringLiteral(
            "HDR niedostępny: kompozytor nie deklaruje obsługi PQ (ST.2084) "
            "i/lub prymarii BT.2020.");
    } else {
        m_protocol = true;
        m_pq = true;

        // tryb kolorymetrii każdego wyjścia
        for (auto &op : p.outs) {
            op->cmOut = wp_color_manager_v1_get_output(p.cm, op->out);
            op->desc =
                wp_color_management_output_v1_get_image_description(op->cmOut);
            wp_image_description_v1_add_listener(op->desc, &descL, op.get());
        }
        for (int tries = 0; tries < 10; ++tries) {
            bool pending = false;
            for (auto &op : p.outs)
                pending = pending || !op->done;
            if (!pending)
                break;
            if (wl_display_roundtrip(p.dpy) < 0)
                break;
        }

        QStringList hdr, sdr;
        for (auto &op : p.outs) {
            const QString n =
                op->name.isEmpty() ? QStringLiteral("?") : op->name;
            (op->isHdr() ? hdr : sdr) << n;
        }
        m_anyHdr = !hdr.isEmpty();
        if (!m_anyHdr) {
            m_reason = QStringLiteral(
                "HDR nieaktywny: żaden ekran nie pracuje w trybie HDR. Włącz: "
                "Ustawienia systemowe → Ekrany i monitory → HDR.");
        } else {
            m_details = QStringLiteral("Ekrany w trybie HDR: %1.")
                            .arg(hdr.join(QStringLiteral(", ")));
            if (!sdr.isEmpty())
                m_details += QStringLiteral(
                                 " Bez HDR: %1 — tam obraz zostanie "
                                 "zmapowany do SDR.")
                                 .arg(sdr.join(QStringLiteral(", ")));
        }
    }

    // sprzątanie
    for (auto &op : p.outs) {
        if (op->info)
            wp_image_description_info_v1_destroy(op->info);
        if (op->desc)
            wp_image_description_v1_destroy(op->desc);
        if (op->cmOut)
            wp_color_management_output_v1_destroy(op->cmOut);
        if (op->out)
            wl_output_destroy(op->out);
    }
    if (p.cm)
        wp_color_manager_v1_destroy(p.cm);
    wl_registry_destroy(reg);
    wl_display_disconnect(p.dpy);
}
