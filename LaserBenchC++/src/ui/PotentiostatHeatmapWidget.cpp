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

QColor blendColor(const QColor& a, const QColor& b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor(
        static_cast<int>(std::lround(a.red() + (b.red() - a.red()) * t)),
        static_cast<int>(std::lround(a.green() + (b.green() - a.green()) * t)),
        static_cast<int>(std::lround(a.blue() + (b.blue() - a.blue()) * t))
    );
}

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
    if (maxValue <= minValue) return QColor("#d97800");
    const double t = std::clamp((value - minValue) / (maxValue - minValue), 0.0, 1.0);

    struct Stop { double t; QColor c; };
    static const Stop stops[] = {
        {0.00, QColor("#000018")},
        {0.14, QColor("#001070")},
        {0.30, QColor("#003b88")},
        {0.44, QColor("#00721b")},
        {0.58, QColor("#49bd00")},
        {0.70, QColor("#c2d100")},
        {0.84, QColor("#d88900")},
        {1.00, QColor("#bf4a00")}
    };

    for (std::size_t i = 1; i < std::size(stops); ++i) {
        if (t <= stops[i].t) {
            const double localT = (t - stops[i - 1].t) / (stops[i].t - stops[i - 1].t);
            return blendColor(stops[i - 1].c, stops[i].c, localT);
        }
    }
    return stops[std::size(stops) - 1].c;
}

QString PotentiostatHeatmapWidget::formatCurrent(double value)
{
    const double absValue = std::abs(value);
    if (absValue >= 1e-3) return QString("%1 mA").arg(value * 1e3, 0, 'f', 2);
    if (absValue >= 1e-6) return QString("%1 uA").arg(value * 1e6, 0, 'f', 1);
    if (absValue >= 1e-9) return QString("%1 nA").arg(value * 1e9, 0, 'f', 1);
    return QString::number(value, 'e', 2) + " A";
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

    painter.fillRect(gr, QColor("#f8fafc"));

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

    // Draw cells without internal grid lines.
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            const auto idx = static_cast<std::size_t>(row * cols_ + col);
            const QRectF cell(gr.left() + col * cw, gr.top() + row * ch, cw, ch);

            QColor fill = QColor("#e5e7eb");
            if (idx < values_.size() && values_[idx].has_value())
                fill = valueToColor(*values_[idx], minV, maxV);

            painter.fillRect(cell.adjusted(-0.25, -0.25, 0.25, 0.25), fill);
        }
    }

    painter.setPen(QPen(QColor("#9ca3af"), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(gr.adjusted(0.0, 0.0, -1.0, -1.0));

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
                        QString("Ligne %1, Col %2\n%3")
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

void PotentiostatHeatmapWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rows_ > 0 && cols_ > 0 && !values_.empty()) {
        const QRectF gr = gridRect();
        const QPointF pos = event->position();
        if (gr.contains(pos)) {
            const int col = static_cast<int>((pos.x() - gr.left()) / (gr.width()  / cols_));
            const int row = static_cast<int>((pos.y() - gr.top())  / (gr.height() / rows_));
            if (row >= 0 && row < rows_ && col >= 0 && col < cols_) {
                const auto idx = static_cast<std::size_t>(row * cols_ + col);
                if (idx < values_.size() && values_[idx].has_value()) {
                    emit cellClicked(row, col);
                }
            }
        }
    }
    QWidget::mousePressEvent(event);
}

}  // namespace laserbench::ui
