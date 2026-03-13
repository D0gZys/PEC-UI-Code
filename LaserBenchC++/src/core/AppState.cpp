#include "core/AppState.hpp"

namespace laserbench::core {

RuntimeSnapshot makeDefaultSnapshot()
{
    RuntimeSnapshot snapshot;
    snapshot.objective = ObjectivePreset {
        .name = "10x",
        .laserCenterPx = QPointF(2624.0, 1294.0),
        .laserRadiusPx = 20.0,
        .mmPerPixelX = 0.000345,
        .mmPerPixelY = 0.000345,
    };
    snapshot.scan = ScanRegion {
        .pattern = ScanPattern::Rectangle,
        .startMm = QPointF(2.0, 2.0),
        .endMm = QPointF(6.0, 5.0),
        .stepMm = 0.10,
        .dwellSeconds = 0.25,
    };
    return snapshot;
}

QString scanPatternLabel(ScanPattern pattern)
{
    switch (pattern) {
    case ScanPattern::Line:
        return "Lineaire";
    case ScanPattern::Rectangle:
        return "Rectangle";
    }

    return "Inconnu";
}

QString deviceStateLabel(DeviceState state)
{
    switch (state) {
    case DeviceState::Disconnected:
        return "Deconnecte";
    case DeviceState::Simulated:
        return "Simulation";
    case DeviceState::Connected:
        return "Connecte";
    case DeviceState::Error:
        return "Erreur";
    }

    return "Inconnu";
}

}  // namespace laserbench::core
