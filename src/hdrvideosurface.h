// HdrVideoSurface — element QML renderujący wideo przez libmpv na wl_subsurface
// umieszczonej POD oknem pulpitu plasmashell, z deklaracją BT.2020+PQ przez
// wp-color-management-v1. Dzięki temu tapeta wideo jest prawdziwym HDR, ikony
// pulpitu zostają na wierzchu (rysuje je bufor rodzica), a efekty KWin (Przegląd)
// animują wszystko spójnie — subsurface jest częścią okna.
#pragma once

#include <QQuickItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

class HdrVideoSurface : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int hdrMode READ hdrMode WRITE setHdrMode NOTIFY hdrModeChanged)
    Q_PROPERTY(int peak READ peak WRITE setPeak NOTIFY peakChanged)
    Q_PROPERTY(bool sdr2hdr READ sdr2hdr WRITE setSdr2hdr NOTIFY sdr2hdrChanged)
    Q_PROPERTY(bool contain READ contain WRITE setContain NOTIFY containChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool activeHdr READ activeHdr NOTIFY activeHdrChanged)

public:
    explicit HdrVideoSurface(QQuickItem *parent = nullptr);
    ~HdrVideoSurface() override;

    QUrl source() const { return m_source; }
    void setSource(const QUrl &u);
    int hdrMode() const { return m_hdrMode; }
    void setHdrMode(int m);
    int peak() const { return m_peak; }
    void setPeak(int p);
    bool sdr2hdr() const { return m_sdr2hdr; }
    void setSdr2hdr(bool v);
    bool contain() const { return m_contain; }
    void setContain(bool v);
    bool playing() const { return m_playing; }
    void setPlaying(bool v);
    bool activeHdr() const { return m_activeHdr; }

signals:
    void sourceChanged();
    void hdrModeChanged();
    void peakChanged();
    void sdr2hdrChanged();
    void containChanged();
    void playingChanged();
    void activeHdrChanged();

protected:
    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    void geometryChange(const QRectF &newG, const QRectF &oldG) override;

private:
    void maybeStart();
    void stopWorker();
    void pushGeometry();
    void setActiveHdr(bool v); // wołane z workera przez invokeMethod

    struct Worker;
    std::unique_ptr<Worker> m_worker;

    QUrl m_source;
    int m_hdrMode = 0; // 0 auto, 1 force, 2 off
    int m_peak = 1000;
    bool m_sdr2hdr = false;
    bool m_contain = false;
    bool m_playing = true;
    bool m_activeHdr = false;
    bool m_complete = false;

    friend struct Worker;
};
