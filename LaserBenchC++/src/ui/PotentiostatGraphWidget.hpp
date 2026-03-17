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

    explicit PotentiostatGraphWidget(QWidget* parent = nullptr);

    void setGraphMode(Mode mode);
    void setSeries(std::vector<double> times, std::vector<double> currents, std::vector<double> eweValues);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString formatAxisValue(double value) const;

    Mode mode_ {Mode::CurrentVsTime};
    std::vector<double> times_;
    std::vector<double> currents_;
    std::vector<double> eweValues_;
};

}  // namespace laserbench::ui
