#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <optional>
#include <utility>
#include <vector>

namespace laserbench::ui {

class PotentiostatHeatmapWidget final : public QWidget
{
public:
    explicit PotentiostatHeatmapWidget(QWidget* parent = nullptr);

    void setGrid(
        int rows,
        int cols,
        std::vector<std::optional<double>> values,
        std::optional<std::pair<int, int>> highlightedCell = std::nullopt
    );
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static QColor valueToColor(double value, double minValue, double maxValue);
    static QString formatCurrent(double value);

    int rows_ {0};
    int cols_ {0};
    std::vector<std::optional<double>> values_;
    std::optional<std::pair<int, int>> highlightedCell_;
};

}  // namespace laserbench::ui
