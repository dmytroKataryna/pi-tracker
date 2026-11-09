// QML — декларативний опис екрана, як Jetpack Compose або XML-розмітка.
// Головне тут — прив'язки (bindings): `text: drone.mode` означає «завжди дорівнює drone.mode».
// Коли DroneState випускає NOTIFY-сигнал, QML сам перераховує всі прив'язки, що від нього залежать
import QtQuick
import Gcs

Window {
    id: root

    // Об'єкт приходить із C++ (window_main.cpp, setInitialProperties)
    required property DroneState drone

    width: 520
    height: 380
    visible: true
    title: "pi-tracker GCS"
    color: "black"

    // Штучний горизонт. Квадрат більший за вікно, щоб при нахилі не було видно кутів
    Item {
        id: horizon
        anchors.centerIn: parent
        width: Math.hypot(root.width, root.height) * 2
        height: width
        rotation: -root.drone.rollDeg // крен вправо → горизонт нахиляється вліво

        Rectangle { // небо
            anchors.fill: parent
            color: "#3d7cc9"
        }
        Rectangle { // земля: її верхній край — лінія горизонту
            width: parent.width
            height: parent.height / 2
            y: parent.height / 2 + root.drone.pitchDeg * 5 // ніс угору → горизонт опускається
            color: "#7a5230"
        }
    }

    // «Літак» у центрі екрана — нерухомий
    Rectangle {
        anchors.centerIn: parent
        width: 120
        height: 4
        color: "yellow"
    }

    Column {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 4

        Text {
            text: root.drone.linkOk ? "LINK OK" : "NO LINK"
            color: root.drone.linkOk ? "lime" : "red"
            font.pixelSize: 22
            font.bold: true
        }
        Text {
            text: root.drone.mode + (root.drone.armed ? "  ARMED" : "  disarmed")
            color: "white"
            font.pixelSize: 18
        }
        Text {
            text: "alt " + root.drone.altitudeM.toFixed(1) + " m"
            color: "white"
            font.pixelSize: 18
        }
        Text {
            text: "roll " + root.drone.rollDeg.toFixed(1) + "  pitch " + root.drone.pitchDeg.toFixed(1)
                  + "  yaw " + root.drone.yawDeg.toFixed(0)
            color: "white"
            font.pixelSize: 14
        }
    }
}
