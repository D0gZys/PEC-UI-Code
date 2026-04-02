#include "ui/PotentiostatHeatmapWidget.hpp"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace laserbench::ui {

namespace {

constexpr int kLeftMargin   = 52;
constexpr int kRightMargin  = 115;  // room for color bar + tick labels (4 decimal places)
constexpr int kTopMargin    = 28;
constexpr int kBottomMargin = 20;

// Color bar geometry (relative to grid right edge)
constexpr double kBarGap   = 12.0;  // gap between grid and bar
constexpr double kBarWidth = 18.0;  // bar width in px
constexpr double kLabelGap =  4.0;  // gap between bar and text

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

PotentiostatHeatmapWidget::PotentiostatHeatmapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(260);
    setAutoFillBackground(false);
    setMouseTracking(true);
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

// ─────────────────────────────────────────────────────────────────────────────

QColor PotentiostatHeatmapWidget::valueToColor(double value, double minValue, double maxValue)
{
    if (maxValue <= minValue) return QColor("#f6d365");
    const double t   = std::clamp((value - minValue) / (maxValue - minValue), 0.0, 1.0);
    const double hue = 220.0 - (220.0 * t);            // blue → yellow/red
    const double sat = 0.75;
    const double lig = std::clamp(0.60 - 0.12 * t, 0.0, 1.0);
    return QColor::fromHslF(hue / 360.0, sat, lig);
}

QString PotentiostatHeatmapWidget::formatCurrent(double value)
{
    return QString::number(value, 'e', 4);
}

QRectF PotentiostatHeatmapWidget::gridRect() const
{
    return QRectF(
        kLeftMargin,
        kTopMargin,
        std::max(1, width()  - kLeftMargin - kRightMargin),
        std::max(1, height() - kTopMargin  - kBottomMargin)
    );
}

// ─────────────────────────────────────────────────────────────────────────────

void PotentiostatHeatmapWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor("#ffffff"));

    const QRectF gr = gridRect();

    // Title
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor("#111927"));
    painter.drawText(QRectF(0.0, 8.0, width(), 18.0), Qt::AlignCenter, "Carte 2D I(x, y)");

    // Grid background
    painter.setPen(QPen(QColor("#d8e0e8"), 1.0));
    painter.setBrush(QColor("#f8fafc"));
    painter.drawRoundedRect(gr, 6.0, 6.0);

    if (rows_ <= 0 || cols_ <= 0) {
        painter.setPen(QColor("#5c6570"));
        painter.drawText(gr, Qt::AlignCenter, "Aucune zone de balayage");
        return;
    }

    // Collect acquired values and compute min/max for colour mapping
    std::vector<double> acquired;
    acquired.reserve(values_.size());
    for (const auto& v : values_)
        if (v.has_value()) acquired.push_back(*v);

    const double minV = acquired.empty() ? 0.0
                       : *std::min_element(acquired.begin(), acquired.end());
    const double maxV = acquired.empty() ? 1.0
                       : *std::max_element(acquired.begin(), acquired.end());

    const double cw = gr.width()  / std::max(cols_, 1);
    const double ch = gr.height() / std::max(rows_, 1);

    // Draw cells
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            const auto idx = static_cast<std::size_t>(row * cols_ + col);
            const QRectF cell(gr.left() + col * cw, gr.top() + row * ch, cw, ch);

            QColor fill = QColor("#e5e7eb");
            if (idx < values_.size() && values_[idx].has_value())
                fill = valueToColor(*values_[idx], minV, maxV);

            painter.setPen(QPen(QColor("#c3ccd6"), 1.0));
            painter.setBrush(fill);
            painter.drawRect(cell);

            // Value text (only when cells are large enough)
            if (idx < values_.size() && values_[idx].has_value() && cw >= 52.0 && ch >= 24.0) {
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(QColor("#111927"));
                painter.drawText(cell.adjusted(4, 2, -4, -2), Qt::AlignCenter, formatCurrent(*values_[idx]));
                painter.setRenderHint(QPainter::Antialiasing, false);
            }

            // Highlight current point
            if (highlightedCell_ && highlightedCell_->first == row && highlightedCell_->second == col) {
                painter.setPen(QPen(QColor("#f59e0b"), 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(cell.adjusted(1, 1, -1, -1));
            }
        }
    }

    // ── Color scale bar ──────────────────────────────────────────────────────
    if (!acquired.empty()) {
        const double barX = gr.right() + kBarGap;
        const double barY = gr.top();
        const double barH = gr.height();

        // Gradient: fill pixel-by-pixel rows
        const int nStrips = std::max(1, static_cast<int>(std::ceil(barH)));
        for (int s = 0; s < nStrips; ++s) {
            // t=1 at top (max), t=0 at bottom (min)
            const double t = 1.0 - static_cast<double>(s) / std::max(1.0, static_cast<double>(nStrips - 1));
            painter.setPen(Qt::NoPen);
            painter.setBrush(valueToColor(minV + t * (maxV - minV), minV, maxV));
            painter.drawRect(QRectF(barX, barY + s, kBarWidth, 1.5));
        }

        // Bar border
        painter.setPen(QPen(QColor("#9ca3af"), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(barX, barY, kBarWidth, barH));

        // Tick labels: max (top), mid (centre), min (bottom)
        const double lx = barX + kBarWidth + kLabelGap;
        const double lw = 80.0;
        painter.setRenderHint(QPainter::Antialiasing, true);
        QFont f = painter.font(); f.setPointSizeF(7.5); painter.setFont(f);

        const struct { double t; double v; } ticks[] = {
            {0.0,  maxV},
            {0.5,  (minV + maxV) / 2.0},
            {1.0,  minV}
        };
        for (const auto& tick : ticks) {
            const double ty = barY + tick.t * barH;
            // Tick line
            painter.setPen(QPen(QColor("#9ca3af"), 1.0));
            painter.drawLine(QPointF(barX + kBarWidth, ty), QPointF(lx - 1.0, ty));
            // Label
            painter.setPen(QColor("#374151"));
            painter.drawText(QRectF(lx, ty - 7.0, lw, 14.0),
                             Qt::AlignLeft | Qt::AlignVCenter, formatCurrent(tick.v));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void PotentiostatHeatmapWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (rows_ > 0 && cols_ > 0 && !values_.empty()) {
        const QRectF gr = gridRect();
        const QPointF pos = event->position();

        if (gr.contains(pos)) {
            const int col = static_cast<int>((pos.x() - gr.left()) / (gr.width()  / cols_));
            const int row = static_cast<int>((pos.y() - gr.top())  / (gr.height() / rows_));

            if (row >= 0 && row < rows_ && col >= 0 && col < cols_) {
                const auto idx = static_cast<std::size_t>(row * cols_ + col);
                if (idx < values_.size() && values_[idx].has_value()) {
                    QToolTip::showText(
                        event->globalPosition().toPoint(),
                        QString("Ligne %1, Col %2\n%3 A")
                            .arg(row + 1).arg(col + 1)
                            .arg(formatCurrent(*values_[idx]))
                    );
                    QWidget::mouseMoveEvent(event);
                    return;
                }
            }
        }
    }
    QToolTip::hideText();
    QWidget::mouseMoveEvent(event);
}

void PotentiostatHeatmapWidget::leaveEvent(QEvent* event)
{
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

}  // namespace laserbench::ui
