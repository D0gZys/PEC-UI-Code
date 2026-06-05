#pragma once

#include <QImage>

#include "hardware/DeviceContracts.hpp"

namespace laserbench::hardware {

class MockStageController final : public IStageController
{
public:
    QString displayName() const override;
    core::DeviceState state() const override;
    QString backendSummary() const override;
    QPointF positionMm() const override;
    QString travelLimitsSummary() const override;
};

class MockCameraController final : public ICameraController
{
public:
    QString displayName() const override;
    core::DeviceState state() const override;
    QString backendSummary() const override;
    QString cameraIdentifier() const override;
    QString previewSummary() const override;
    QStringList discoverAvailableCameras() override;
    QString selectedCamera() const override;
    bool isConnected() const override;
    bool isLive() const override;
    void connectCamera(const QString& cameraId) override;
    void disconnectCamera() override;
    void startLive() override;
    void stopLive() override;
    bool refreshFrame() override;
    double exposureTimeUs() const override;
    double gain() const override;
    void setExposureTimeUs(double exposureUs) override;
    void setGain(double gain) override;
    QImage previewFrame() const override;

private:
    QString selectedCameraId_ {"SIM_CAM_01"};
    bool connected_ {false};
    bool live_ {false};
    double exposureUs_ {10000.0};
    double gain_ {0.0};
    QImage lastFrame_;
};

class MockPotentiostatController final : public IPotentiostatController
{
public:
    QString displayName() const override;
    core::DeviceState state() const override;
    QString backendSummary() const override;
    QString channelSummary() const override;
    QString acquisitionSummary() const override;
};

}  // namespace laserbench::hardware
