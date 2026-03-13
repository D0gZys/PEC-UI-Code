#pragma once

#include <QImage>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "core/AppState.hpp"

namespace laserbench::hardware {

class IDeviceController
{
public:
    virtual ~IDeviceController() = default;

    virtual QString displayName() const = 0;
    virtual core::DeviceState state() const = 0;
    virtual QString backendSummary() const = 0;
};

class IStageController : public IDeviceController
{
public:
    ~IStageController() override = default;

    virtual QPointF positionMm() const = 0;
    virtual QString travelLimitsSummary() const = 0;
};

class ICameraController : public IDeviceController
{
public:
    ~ICameraController() override = default;

    virtual QString cameraIdentifier() const = 0;
    virtual QString previewSummary() const = 0;
    virtual QStringList discoverAvailableCameras() = 0;
    virtual QString selectedCamera() const = 0;
    virtual bool isConnected() const = 0;
    virtual bool isLive() const = 0;
    virtual void connectCamera(const QString& cameraId) = 0;
    virtual void disconnectCamera() = 0;
    virtual void startLive() = 0;
    virtual void stopLive() = 0;
    virtual bool refreshFrame() = 0;
    virtual double exposureTimeUs() const = 0;
    virtual double gain() const = 0;
    virtual void setExposureTimeUs(double exposureUs) = 0;
    virtual void setGain(double gain) = 0;
    virtual QImage previewFrame() const = 0;
};

class IPotentiostatController : public IDeviceController
{
public:
    ~IPotentiostatController() override = default;

    virtual QString channelSummary() const = 0;
    virtual QString acquisitionSummary() const = 0;
};

}  // namespace laserbench::hardware
