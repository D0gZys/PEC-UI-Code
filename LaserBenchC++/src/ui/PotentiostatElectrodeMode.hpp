#pragma once

namespace laserbench::ui {

enum class PotentiostatElectrodeMode
{
    Anode,
    Cathode
};

inline double displayCurrentForElectrode(double current, PotentiostatElectrodeMode mode) noexcept
{
    return mode == PotentiostatElectrodeMode::Cathode ? -current : current;
}

inline double currentFromElectrodeDisplay(double displayCurrent, PotentiostatElectrodeMode mode) noexcept
{
    if (displayCurrent == 0.0) {
        return 0.0;
    }
    return mode == PotentiostatElectrodeMode::Cathode ? -displayCurrent : displayCurrent;
}

}  // namespace laserbench::ui
