#pragma once

#include "pi_tracker/mavlink.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Модель для екрана: останній відомий стан дрона. Це ViewModel, а Main.qml — її View.
// Q_PROPERTY ≈ val зі StateFlow у ViewModel: QML читає значення через READ
// і сам перечитує його, коли модель випускає сигнал із NOTIFY
class DroneState : public QObject
{
    Q_OBJECT
    QML_ELEMENT                           // тип стає видимим у QML як DroneState (import Gcs)
    QML_UNCREATABLE("Створюється в C++")  // QML отримує готовий об'єкт, а не створює свій

    // Q_PROPERTY(тип ім'я READ геттер NOTIFY сигнал). WRITE немає — з QML властивість лише читається
    Q_PROPERTY(bool linkOk READ linkOk NOTIFY heartbeatChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY heartbeatChanged)
    Q_PROPERTY(bool armed READ armed NOTIFY heartbeatChanged)
    Q_PROPERTY(double rollDeg READ rollDeg NOTIFY attitudeChanged)
    Q_PROPERTY(double pitchDeg READ pitchDeg NOTIFY attitudeChanged)
    Q_PROPERTY(double yawDeg READ yawDeg NOTIFY attitudeChanged)
    Q_PROPERTY(double altitudeM READ altitudeM NOTIFY positionChanged)

public:
    explicit DroneState(QObject *parent = nullptr);

    bool linkOk() const { return m_linkOk; }
    QString mode() const { return m_mode; }
    bool armed() const { return m_armed; }
    double rollDeg() const { return m_rollDeg; }
    double pitchDeg() const { return m_pitchDeg; }
    double yawDeg() const { return m_yawDeg; }
    double altitudeM() const { return m_altitudeM; }

public slots:
    // Підключаються до сигналів TelemetryLink у window_main.cpp
    void onHeartbeat(const pi_tracker::mavlink::Heartbeat &heartbeat);
    void onAttitude(const pi_tracker::mavlink::Attitude &attitude);
    void onPosition(const pi_tracker::mavlink::GlobalPosition &position);
    void onLinkLost();

signals:
    // Один сигнал на кожне повідомлення MAVLink: властивості з одного повідомлення оновлюються разом
    void heartbeatChanged();
    void attitudeChanged();
    void positionChanged();

private:
    bool m_linkOk{false};
    QString m_mode{"-"};
    bool m_armed{false};
    double m_rollDeg{};
    double m_pitchDeg{};
    double m_yawDeg{};
    double m_altitudeM{};
};
