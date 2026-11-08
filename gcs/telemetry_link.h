#pragma once

#include "pi_tracker/mavlink.h"

#include <QObject>
#include <QTimer>
#include <QUdpSocket>

// Приймає MAVLink по UDP і повідомляє про новини сигналами.
// Окремого потоку немає: сокет і таймер «будять» об'єкт через цикл подій Qt (як Handler на Looper головного потоку)
class TelemetryLink : public QObject
{
    Q_OBJECT // без цього макроса moc не згенерує код сигналів і слотів

public:
    explicit TelemetryLink(quint16 port, QObject *parent = nullptr);

    const pi_tracker::mavlink::ParseStats &stats() const { return m_stats; }

signals:
    // Сигнали лише оголошуються: тіло пише moc. emit heartbeatReceived(hb) викличе всі підключені слоти
    void heartbeatReceived(const pi_tracker::mavlink::Heartbeat &heartbeat);
    void attitudeReceived(const pi_tracker::mavlink::Attitude &attitude);
    void positionReceived(const pi_tracker::mavlink::GlobalPosition &position);
    void linkLost(); // 3 с без жодного HEARTBEAT

private slots:
    void readDatagrams(); // викликає цикл подій, коли сокет сигналізує readyRead

private:
    // Звичайні вказівники без delete: батьком цих об'єктів є this, і QObject видалить їх разом із собою
    QUdpSocket *m_socket;
    QTimer *m_linkTimer;
    pi_tracker::mavlink::ParseStats m_stats;
};
