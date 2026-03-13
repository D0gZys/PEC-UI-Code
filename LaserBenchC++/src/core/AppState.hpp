#pragma once

#include <QPointF>
#include <QString>

namespace laserbench::core {

enum class ScanPattern
{
    Line,
    Rectangle
};

enum class DeviceState
{
    Disconnected,
    Simulated,
    Connected,
    Error
};

struct ObjectivePreset
{
    QString name;
    QPointF laserCenterPx;
    double laserRadiusPx {20.0};
    double mmPerPixelX {0.0};
    double mmPerPixelY {0.0};
};

struct ScanRegion
{
    ScanPattern pattern {ScanPattern::Rectangle};
    QPointF startMm {0.0, 0.0};
    QPointF endMm {5.0, 5.0};
    double stepMm {0.10};
    double dwellSeconds {0.25};
};

struct RuntimeSnapshot
{
    ObjectivePreset objective;
    ScanRegion scan;
    bool gotoArmed {false};
    bool liveViewEnabled {false};
};

RuntimeSnapshot makeDefaultSnapshot();
QString scanPatternLabel(ScanPattern pattern);
QString deviceStateLabel(DeviceState state);

}  // namespace laserbench::core
