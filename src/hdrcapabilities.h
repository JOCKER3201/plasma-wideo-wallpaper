// HdrCapabilities — a lightweight HDR capability probe for the wallpaper's
// configuration UI. It opens its OWN short-lived Wayland connection (without
// touching the Qt pipeline) and checks: the wp-color-management-v1 protocol,
// PQ/BT.2020 support, and per screen whether the output actually runs in HDR
// (PQ transfer on the output's image description). config.qml greys out the
// HDR options and shows `reason`.
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class HdrCapabilities : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool waylandSession READ waylandSession CONSTANT)
    Q_PROPERTY(bool protocolPresent READ protocolPresent CONSTANT)
    Q_PROPERTY(bool pqSupported READ pqSupported CONSTANT)
    Q_PROPERTY(bool anyHdrOutput READ anyHdrOutput CONSTANT)
    Q_PROPERTY(bool usable READ usable CONSTANT)
    Q_PROPERTY(QString reason READ reason CONSTANT)
    Q_PROPERTY(QString details READ details CONSTANT)

public:
    explicit HdrCapabilities(QObject *parent = nullptr);

    bool waylandSession() const { return m_wayland; }
    bool protocolPresent() const { return m_protocol; }
    bool pqSupported() const { return m_pq; }
    bool anyHdrOutput() const { return m_anyHdr; }
    bool usable() const { return m_wayland && m_protocol && m_pq && m_anyHdr; }
    QString reason() const { return m_reason; }
    QString details() const { return m_details; }

private:
    void probe();

    bool m_wayland = false;
    bool m_protocol = false;
    bool m_pq = false;
    bool m_anyHdr = false;
    QString m_reason;
    QString m_details;
};
