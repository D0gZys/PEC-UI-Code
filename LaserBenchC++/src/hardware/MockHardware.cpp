#include "hardware/MockHardware.hpp"

#include <QDateTime>
#include <QPainter>

namespace laserbench::hardware {

QString MockStageController::displayName() const
{
    return "Platine XY";
}

core::DeviceState MockStageController::state() const
{
    return core::DeviceState::Simulated;
}

QString MockStageController::backendSummary() const
{
    return "Backend simule - future integration Newport/CONEX";
}

QPointF MockStageController::positionMm() const
{
    return {2.135, 4.820};
}

QString MockStageController::travelLimitsSummary() const
{
    return "Course attendue: 0.0 mm -> 25.0 mm";
}

QString MockCameraController::displayName() const
{
    return "Camera microscope";
}

core::DeviceState MockCameraController::state() const
{
    if (!connected_) {
        return core::DeviceState::Simulated;
    }
    return core::DeviceState::Connected;
}

QString MockCameraController::backendSummary() const
{
    return "Backend simule - future integration Thorlabs SDK natif";
}

QString MockCameraController::cameraIdentifier() const
{
    return connected_ ? QString("Thorlabs (%1)").arg(selectedCameraId_) : QString("Thorlabs (simulation)");
}

QString MockCameraController::previewSummary() const
{
    return "Preview live simule avec zoom souris/trackpad";
}

QStringList MockCameraController::discoverAvailableCameras()
{
    return QStringList {selectedCameraId_};
}

QString MockCameraController::selectedCamera() const
{
    return selectedCameraId_;
}

bool MockCameraController::isConnected() const
{
    return connected_;
}

bool MockCameraController::isLive() const
{
    return live_;
}

void MockCameraController::connectCamera(const QString& cameraId)
{
    selectedCameraId_ = cameraId.isEmpty() ? QString("SIM_CAM_01") : cameraId;
    connected_ = true;
}

void MockCameraController::disconnectCamera()
{
    live_ = false;
    connected_ = false;
}

void MockCameraController::startLive()
{
    connected_ = true;
    live_ = true;
}

void MockCameraController::stopLive()
{
    live_ = false;
}

bool MockCameraController::refreshFrame()
{
    if (!live_) {
        return false;
    }

    QImage frame(960, 720, QImage::Format_RGB32);
    frame.fill(QColor("#0f1722"));

    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qint64 tick = QDateTime::currentMSecsSinceEpoch() / 40;
    const int cx = 480 + static_cast<int>((tick % 160) - 80);
    const int cy = 360 + static_cast<int>(((tick / 2) % 120) - 60);

    painter.fillRect(frame.rect(), QColor("#111827"));

    painter.setPen(QColor(255, 255, 255, 28));
    for (int x = 0; x <= frame.width(); x += 80) {
        painter.drawLine(x, 0, x, frame.height());
    }
    for (int y = 0; y <= frame.height(); y += 80) {
        painter.drawLine(0, y, frame.width(), y);
    }

    painter.setPen(QPen(QColor("#f97316"), 2));
    painter.drawLine(cx - 60, cy, cx + 60, cy);
    painter.drawLine(cx, cy - 60, cx, cy + 60);

    painter.setBrush(QColor(56, 189, 248, 42));
    painter.setPen(QPen(QColor("#38bdf8"), 2));
    painter.drawEllipse(QPoint(cx, cy), 120, 84);

    painter.setBrush(QColor(255, 255, 255, 24));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(250, 180), 70, 70);
    painter.drawEllipse(QPoint(715, 505), 46, 46);

    painter.setPen(QColor("#dbe4ee"));
    painter.setFont(QFont("Segoe UI", 12, QFont::DemiBold));
    painter.drawText(QRect(24, 20, 360, 36), Qt::AlignLeft | Qt::AlignVCenter, live_ ? "Camera Thorlabs - live simule" : "Camera Thorlabs - apercu simule");

    painter.setFont(QFont("Segoe UI", 10));
    painter.drawText(
        QRect(24, frame.height() - 52, frame.width() - 48, 24),
        Qt::AlignLeft | Qt::AlignVCenter,
        QString("Exposure: %1 us    Gain: %2")
            .arg(exposureUs_, 0, 'f', 0)
            .arg(gain_, 0, 'f', 1)
    );

    lastFrame_ = frame;
    return true;
}

double MockCameraController::exposureTimeUs() const
{
    return exposureUs_;
}

double MockCameraController::gain() const
{
    return gain_;
}

void MockCameraController::setExposureTimeUs(double exposureUs)
{
    exposureUs_ = exposureUs;
}

void MockCameraController::setGain(double gain)
{
    gain_ = gain;
}

QImage MockCameraController::previewFrame() const
{
    return lastFrame_;
}

QString MockPotentiostatController::displayName() const
{
    return "Potentiostat";
}

core::DeviceState MockPotentiostatController::state() const
{
    return core::DeviceState::Simulated;
}

QString MockPotentiostatController::backendSummary() const
{
    return "Backend simule - future integration BioLogic EClib";
}

QString MockPotentiostatController::channelSummary() const
{
    return "Canal 1 - IP 169.254.3.150";
}

QString MockPotentiostatController::acquisitionSummary() const
{
    return "Chronoamperometrie spatiale - acquisition ponctuelle par position";
}

}  // namespace laserbench::hardware
