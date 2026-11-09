#include "drone_state.h"
#include "telemetry_link.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QVariant>

#include <cstdio>
#include <exception>

int main(int argc, char *argv[])
{
    // QGuiApplication = цикл подій (як QCoreApplication з дня 1) + вікна, мишка, малювання
    QGuiApplication app{argc, argv};

    try
    {
        TelemetryLink link{14550};
        DroneState drone;

        // Ті самі connect, що в дні 1, але отримувач — модель, а не лямбда
        QObject::connect(&link, &TelemetryLink::heartbeatReceived, &drone, &DroneState::onHeartbeat);
        QObject::connect(&link, &TelemetryLink::attitudeReceived, &drone, &DroneState::onAttitude);
        QObject::connect(&link, &TelemetryLink::positionReceived, &drone, &DroneState::onPosition);
        QObject::connect(&link, &TelemetryLink::linkLost, &drone, &DroneState::onLinkLost);

        // Рушій QML навмисно оголошено ПІСЛЯ drone (див. питання 2)
        QQmlApplicationEngine engine;
        // Main.qml оголошує `required property DroneState drone` — тут ми її заповнюємо
        engine.setInitialProperties({{"drone", QVariant::fromValue(&drone)}});
        // Якщо в Main.qml помилка — вийти з кодом 1, а не висіти без вікна
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
            Qt::QueuedConnection);
        engine.loadFromModule("Gcs", "Main");

        std::printf("[gcs] listening on UDP 14550\n");
        std::fflush(stdout);
        return app.exec();
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
