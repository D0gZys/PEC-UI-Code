#pragma once

#include <QString>
#include <QWidget>

#include <vector>

namespace laserbench::ui {

class PotentiostatGraphWidget final : public QWidget
{
public:
    enum class Mode
    {
        CurrentVsTime = 0,
        EweVsTime,
        CurrentVsEwe,
        EweVsCurrent
    };

    // Time interval annotated with motor state
    struct MotorPhase {
        double tStart  {0.0};
        double tEnd    {0.0};
        bool   moving  {false}; // true = motors moving (green), false = stopped/measuring (red)
    };

    explicit PotentiostatGraphWidget(QWidget* parent = nullptr);

    void setGraphMode(Mode mode);
    void setSeries(std::vector<double> times, std::vector<double> currents, std::vector<double> eweValues);
    void setPhases(std::vector<MotorPhase> phases);
    void setShowPhases(bool show);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString formatAxisValue(double value, double step) const;

    Mode mode_ {Mode::CurrentVsTime};
    std::vector<double>     times_;
    std::vector<double>     currents_;
    std::vector<double>     eweValues_;
    std::vector<MotorPhase> phases_;
    bool showPhases_ {false};
};

}  // namespace laserbench::ui
