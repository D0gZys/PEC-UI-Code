#include "ui/PotentiostatGraphWidget.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace laserbench::ui {

namespace {

constexpr int kLeftMargin   = 64;
constexpr int kRightMargin  = 20;
constexpr int kTopMargin    = 32;
constexpr int kBottomMargin = 40;

// Pastel overlay colors (semi-transparent)
const QColor kColorMoving  {160, 230, 160, 80};  // pastel green
const QColor kColorStopped {255, 180, 180, 80};  // pastel red

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

PotentiostatGraphWidget::PotentiostatGraphWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(260);
    setAutoFillBackground(false);
}

void PotentiostatGraphWidget::setGraphMode(Mode mode)
{
    if (mode_ == mode) return;
    mode_ = mode;
    update();
}

void PotentiostatGraphWidget::setSeries(
    std::vector<double> times,
    std::vector<double> currents,
    std::vector<double> eweValues)
{
    times_     = std::move(times);
    currents_  = std::move(currents);
    eweValues_ = std::move(eweValues);
    update();
}

void PotentiostatGraphWidget::setPhases(std::vector<MotorPhase> phases)
{
    phases_ = std::move(phases);
    update();
}

void PotentiostatGraphWidget::setShowPhases(bool show)
{
    if (showPhases_ == show) return;
    showPhases_ = show;
    update();
}

void PotentiostatGraphWidget::clear()
{
    times_.clear();
    currents_.clear();
    eweValues_.clear();
    phases_.clear();
    update();
}

// ─────────────────────────────────────────────────────────────────────────────

QString PotentiostatGraphWidget::formatAxisValue(double value) const
{
    const double a = std::abs(value);
    if ((a > 0.0 && a < 0.01) || a >= 1000.0)
        return QString::number(value, 'e', 2);
    return QString::number(value, 'f', 3);
}

void PotentiostatGraphWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#ffffff"));

    const QRectF plotRect(
        kLeftMargin,
        kTopMargin,
        std::max(1, width()  - kLeftMargin - kRightMargin),
        std::max(1, height() - kTopMargin  - kBottomMargin)
    );

    // Determine series to display
    QString title;
    const std::vector<double>* xs = nullptr;
    const std::vector<double>* ys = nullptr;
    const bool timeBasedMode =
        (mode_ == Mode::CurrentVsTime || mode_ == Mode::EweVsTime);

    switch (mode_) {
    case Mode::CurrentVsTime:
        title = "I = f(t)";   xs = &times_;     ys = &currents_;  break;
    case Mode::EweVsTime:
        title = "Ewe = f(t)"; xs = &times_;     ys = &eweValues_; break;
    case Mode::CurrentVsEwe:
        title = "I = f(Ewe)"; xs = &eweValues_; ys = &currents_;  break;
    case Mode::EweVsCurrent:
        title = "Ewe = f(I)"; xs = &currents_;  ys = &eweValues_; break;
    }

    // Title
    painter.setPen(QColor("#111927"));
    painter.drawText(QRectF(0.0, 8.0, width(), 20.0), Qt::AlignCenter, title);

    // Plot background
    painter.setPen(QPen(QColor("#d8e0e8"), 1.0));
    painter.setBrush(QColor("#f8fafc"));
    painter.drawRoundedRect(plotRect, 10.0, 10.0);

    if (xs == nullptr || ys == nullptr ||
        xs->size() < 2 || ys->size() < 2 || xs->size() != ys->size())
    {
        painter.setPen(QColor("#5c6570"));
        painter.drawText(plotRect, Qt::AlignCenter, "Aucune donnee de mesure");
        return;
    }

    const auto [xMinIt, xMaxIt] = std::minmax_element(xs->begin(), xs->end());
    const auto [yMinIt, yMaxIt] = std::minmax_element(ys->begin(), ys->end());
    double xMin = *xMinIt, xMax = *xMaxIt;
    double yMin = *yMinIt, yMax = *yMaxIt;
    if (xMax <= xMin) xMax = xMin + 1.0;
    if (yMax <= yMin) yMax = yMin + 1.0;

    const double xRange = xMax - xMin;

    // ── Phase color overlays (time-based modes only) ─────────────────────────
    if (showPhases_ && timeBasedMode && !phases_.empty()) {
        painter.setRenderHint(QPainter::Antialiasing, false);

        // Clip to plot area so bands don't overflow the rounded rect
        painter.save();
        QPainterPath clip;
        clip.addRoundedRect(plotRect, 10.0, 10.0);
        painter.setClipPath(clip);

        for (const auto& ph : phases_) {
            const double nx1 = std::clamp((ph.tStart - xMin) / xRange, 0.0, 1.0);
            const double nx2 = std::clamp((ph.tEnd   - xMin) / xRange, 0.0, 1.0);
            const double bx1 = plotRect.left() + nx1 * plotRect.width();
            const double bx2 = plotRect.left() + nx2 * plotRect.width();
            if (bx2 <= bx1) continue;

            painter.setPen(Qt::NoPen);
            painter.setBrush(ph.moving ? kColorMoving : kColorStopped);
            painter.drawRect(QRectF(bx1, plotRect.top(), bx2 - bx1, plotRect.height()));
        }

        painter.restore();
        painter.setRenderHint(QPainter::Antialiasing, true);
    }

    // ── Curve ────────────────────────────────────────────────────────────────
    QPainterPath path;
    for (int i = 0; i < static_cast<int>(xs->size()); ++i) {
        const auto si = static_cast<std::size_t>(i);
        const double nx = ((*xs)[si] - xMin) / xRange;
        const double ny = ((*ys)[si] - yMin) / (yMax - yMin);
        const QPointF pt(plotRect.left() + nx * plotRect.width(),
                          plotRect.bottom() - ny * plotRect.height());
        i == 0 ? path.moveTo(pt) : path.lineTo(pt);
    }
    painter.setPen(QPen(QColor("#1f6feb"), 2.0));
    painter.drawPath(path);

    // ── Data points ──────────────────────────────────────────────────────────
    painter.setPen(QPen(QColor("#1f6feb"), 1.5));
    painter.setBrush(QColor("#ffffff"));
    for (int i = 0; i < static_cast<int>(xs->size()); ++i) {
        const auto si = static_cast<std::size_t>(i);
        const double nx = ((*xs)[si] - xMin) / xRange;
        const double ny = ((*ys)[si] - yMin) / (yMax - yMin);
        const QPointF pt(plotRect.left() + nx * plotRect.width(),
                          plotRect.bottom() - ny * plotRect.height());
        painter.drawEllipse(pt, 2.5, 2.5);
    }

    // ── Axis labels ──────────────────────────────────────────────────────────
    painter.setPen(QColor("#5c6570"));
    painter.drawText(QRectF(8.0, plotRect.top() - 2.0,   kLeftMargin - 16.0, 18.0), Qt::AlignRight | Qt::AlignVCenter, formatAxisValue(yMax));
    painter.drawText(QRectF(8.0, plotRect.bottom() - 18.0, kLeftMargin - 16.0, 18.0), Qt::AlignRight | Qt::AlignVCenter, formatAxisValue(yMin));
    painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 8.0, 120.0, 18.0), Qt::AlignLeft  | Qt::AlignVCenter, formatAxisValue(xMin));
    painter.drawText(QRectF(plotRect.right() - 120.0, plotRect.bottom() + 8.0, 120.0, 18.0), Qt::AlignRight | Qt::AlignVCenter, formatAxisValue(xMax));
}

}  // namespace laserbench::ui
