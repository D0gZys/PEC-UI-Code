#include "ui/PotentiostatHeatmapWidget.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <algorithm>

namespace laserbench::ui {

namespace {

constexpr int kLeftMargin = 52;
constexpr int kRightMargin = 20;
constexpr int kTopMargin = 28;
constexpr int kBottomMargin = 20;

}  // namespace

PotentiostatHeatmapWidget::PotentiostatHeatmapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(260);
    setAutoFillBackground(false);
}

void PotentiostatHeatmapWidget::setGrid(
    int rows,
    int cols,
    std::vector<std::optional<double>> values,
    std::optional<std::pair<int, int>> highlightedCell
)
{
    rows_ = rows;
    cols_ = cols;
    values_ = std::move(values);
    highlightedCell_ = highlightedCell;
    update();
}

void PotentiostatHeatmapWidget::clear()
{
    rows_ = 0;
    cols_ = 0;
    values_.clear();
    highlightedCell_.reset();
    update();
}

QColor PotentiostatHeatmapWidget::valueToColor(double value, double minValue, double maxValue)
{
    if (maxValue <= minValue) {
        return QColor("#f6d365");
    }
    const double normalized = std::clamp((value - minValue) / (maxValue - minValue), 0.0, 1.0);
    const double hue = 220.0 - (220.0 * normalized);
    const double saturation = 0.75;
    const double lightness = 0.60 - (0.12 * normalized);
    return QColor::fromHslF(hue / 360.0, saturation, std::clamp(lightness, 0.0, 1.0));
}

QString PotentiostatHeatmapWidget::formatCurrent(double value)
{
    return QString::number(value, 'e', 2);
}

void PotentiostatHeatmapWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#ffffff"));

    QRectF gridRect(
        kLeftMargin,
        kTopMargin,
        std::max(1, width() - kLeftMargin - kRightMargin),
        std::max(1, height() - kTopMargin - kBottomMargin)
    );

    painter.setPen(QColor("#111927"));
    painter.drawText(QRectF(0.0, 8.0, width(), 18.0), Qt::AlignCenter, "Carte 2D I(x, y)");

    painter.setPen(QPen(QColor("#d8e0e8"), 1.0));
    painter.setBrush(QColor("#f8fafc"));
    painter.drawRoundedRect(gridRect, 10.0, 10.0);

    if (rows_ <= 0 || cols_ <= 0) {
        painter.setPen(QColor("#5c6570"));
        painter.drawText(gridRect, Qt::AlignCenter, "Aucune zone de balayage");
        return;
    }

    std::vector<double> actualValues;
    actualValues.reserve(values_.size());
    for (const auto& value : values_) {
        if (value.has_value()) {
            actualValues.push_back(*value);
        }
    }

    const double minValue = actualValues.empty() ? 0.0 : *std::min_element(actualValues.begin(), actualValues.end());
    const double maxValue = actualValues.empty() ? 1.0 : *std::max_element(actualValues.begin(), actualValues.end());
    const double cellWidth = gridRect.width() / std::max(cols_, 1);
    const double cellHeight = gridRect.height() / std::max(rows_, 1);

    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            const std::size_t index = static_cast<std::size_t>(row * cols_ + col);
            const QRectF cellRect(
                gridRect.left() + (static_cast<double>(col) * cellWidth),
                gridRect.top() + (static_cast<double>(row) * cellHeight),
                cellWidth,
                cellHeight
            );

            QColor fill = QColor("#e5e7eb");
            if (index < values_.size() && values_[index].has_value()) {
                fill = valueToColor(*values_[index], minValue, maxValue);
            }

            painter.setPen(QPen(QColor("#c3ccd6"), 1.0));
            painter.setBrush(fill);
            painter.drawRect(cellRect);

            if (index < values_.size() && values_[index].has_value() && cellWidth >= 52.0 && cellHeight >= 24.0) {
                painter.setPen(QColor("#111927"));
                painter.drawText(cellRect.adjusted(4.0, 2.0, -4.0, -2.0), Qt::AlignCenter, formatCurrent(*values_[index]));
            }

            if (highlightedCell_.has_value() && highlightedCell_->first == row && highlightedCell_->second == col) {
                painter.setPen(QPen(QColor("#f59e0b"), 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(cellRect.adjusted(1.0, 1.0, -1.0, -1.0));
            }
        }
    }

    painter.setPen(QColor("#5c6570"));
    painter.drawText(
        QRectF(gridRect.left(), gridRect.bottom() + 2.0, gridRect.width(), 16.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        QString("Min %1 A   Max %2 A").arg(formatCurrent(minValue)).arg(formatCurrent(maxValue))
    );
}

}  // namespace laserbench::ui
