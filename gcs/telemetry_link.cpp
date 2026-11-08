#include "telemetry_link.h"

#include <QNetworkDatagram>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace mav = pi_tracker::mavlink;

TelemetryLink::TelemetryLink(quint16 port, QObject *parent)
    : QObject{parent}, m_socket{new QUdpSocket{this}},
      m_linkTimer{new QTimer{this}} {
  if (!m_socket->bind(QHostAddress::AnyIPv4, port)) {
    throw std::runtime_error("bind UDP " + std::to_string(port) + ": " +
                             m_socket->errorString().toStdString());
  }

  QObject::connect(m_socket, &QUdpSocket::readyRead, this,
                   &TelemetryLink::readDatagrams);

  m_linkTimer->setSingleShot(true);
  m_linkTimer->setInterval(3000);


  QObject::connect(m_linkTimer, &QTimer::timeout, this,
                   &TelemetryLink::linkLost);

  m_linkTimer->start();
}

void TelemetryLink::readDatagrams() {
  while (m_socket->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = m_socket->receiveDatagram();
    const QByteArray data = datagram.data();
    // char → uint8_t: читати будь-які байти через unsigned char дозволено, тож
    // тут reinterpret_cast законний
    const std::span bytes{
        reinterpret_cast<const std::uint8_t *>(data.constData()),
        static_cast<std::size_t>(data.size())};

    for (const mav::Frame &frame : mav::parseDatagram(bytes, m_stats)) {
      if (const auto heartbeat = mav::decodeHeartbeat(frame)) {
        m_linkTimer->start(); // зв'язок живий: відлік 3 с починається знову
        emit heartbeatReceived(*heartbeat);
      } else if (const auto attitude = mav::decodeAttitude(frame)) {
        emit attitudeReceived(*attitude);
      } else if (const auto position = mav::decodeGlobalPosition(frame)) {
        emit positionReceived(*position);
      }
    }
  }
}
