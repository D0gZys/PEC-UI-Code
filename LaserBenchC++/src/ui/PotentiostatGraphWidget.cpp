#include "ui/PotentiostatGraphWidget.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace laserbench::ui {

namespace {

constexpr double kCurrentToMilliAmp = 1e3;
constexpr int kTargetMajorTicks = 6;
constexpr int kMinorSubdivisionCount = 5;

const QColor kPlotBackground {252, 253, 255};
const QColor kPlotBorder     {178, 186, 198};
const QColor kMajorGrid      {178, 184, 194};
const QColor kMinorGrid      {214, 219, 227};
const QColor kZeroAxis       {245, 130, 32};
const QColor kCurveColor     {35, 102, 243};
const QColor kLastPointColor {245, 130, 32};
const QColor kAxisText       {19, 26, 35};
const QColor kTickText       {83, 93, 105};

const QColor kColorMoving  {160, 230, 160, 80};
const QColor kColorStopped {255, 180, 180, 80};

struct AxisTicks
{
    double min {0.0};
    double max {1.0};
    double step {1.0};
    std::vector<double> major;
    std::vector<double> minor;
};

struct DisplaySeries
{
    QString title;
    QString xLabel;
    QString yLabel;
    std::vector<double> xs;
    std::vector<double> ys;
    bool timeBasedMode {false};
};

double niceNumber(double value, bool roundValue)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return 1.0;
    }

    const double exponent = std::floor(std::log10(value));
    const double fraction = value / std::pow(10.0, exponent);

    double niceFraction = 1.0;
    if (roundValue) {
        if (fraction < 1.5) niceFraction = 1.0;
        else if (fraction < 3.0) niceFraction = 2.0;
        else if (fraction < 7.0) niceFraction = 5.0;
        else niceFraction = 10.0;
    } else {
        if (fraction <= 1.0) niceFraction = 1.0;
        else if (fraction <= 2.0) niceFraction = 2.0;
        else if (fraction <= 5.0) niceFraction = 5.0;
        else niceFraction = 10.0;
    }

    return niceFraction * std::pow(10.0, exponent);
}

AxisTicks buildAxisTicks(double dataMin, double dataMax, bool preferZeroAnchor)
{
    if (!std::isfinite(dataMin) || !std::isfinite(dataMax)) {
        return {};
    }

    if (dataMax < dataMin) {
        std::swap(dataMin, dataMax);
    }

    double minValue = dataMin;
    double maxValue = dataMax;
    double range = maxValue - minValue;

    if (range <= std::numeric_limits<double>::epsilon()) {
        const double pad = (std::abs(minValue) > 1e-9) ? std::abs(minValue) * 0.1 : 1.0;
        minValue -= pad;
        maxValue += pad;
        range = maxValue - minValue;
    } else {
        const double pad = range * 0.06;
        minValue -= pad;
        maxValue += pad;
        if (preferZeroAnchor) {
            if (dataMin >= 0.0 && minValue < 0.0 && dataMin < range * 0.2) {
                minValue = 0.0;
            }
            if (dataMax <= 0.0 && maxValue > 0.0 && std::abs(dataMax) < range * 0.2) {
                maxValue = 0.0;
            }
        }
        range = maxValue - minValue;
    }

    AxisTicks ticks;
    ticks.step = niceNumber(range / static_cast<double>(kTargetMajorTicks - 1), true);
    ticks.min = std::floor(minValue / ticks.step) * ticks.step;
    ticks.max = std::ceil(maxValue / ticks.step) * ticks.step;

    if (preferZeroAnchor) {
        if (dataMin >= 0.0 && ticks.min < 0.0 && std::abs(ticks.min) <= ticks.step * 0.75) {
            ticks.min = 0.0;
        }
        if (dataMax <= 0.0 && ticks.max > 0.0 && std::abs(ticks.max) <= ticks.step * 0.75) {
            ticks.max = 0.0;
        }
    }

    const double minorStep = ticks.step / static_cast<double>(kMinorSubdivisionCount);
    for (double value = ticks.min; value <= ticks.max + ticks.step * 0.5; value += ticks.step) {
        const double normalized = (std::abs(value) < ticks.step * 1e-6) ? 0.0 : value;
        ticks.major.push_back(normalized);
    }
    for (std::size_t i = 0; i + 1 < ticks.major.size(); ++i) {
        const double base = ticks.major[i];
        for (int subdiv = 1; subdiv < kMinorSubdivisionCount; ++subdiv) {
            const double value = base + static_cast<double>(subdiv) * minorStep;
            if (value <= ticks.min + minorStep * 0.25 || value >= ticks.max - minorStep * 0.25) {
                continue;
            }
            if (std::abs(value) < ticks.step * 1e-6) {
                continue;
            }
            ticks.minor.push_back(value);
        }
    }

    return ticks;
}

QString trimNumericLabel(QString text)
{
    if (!text.contains('.')) {
        return text;
    }
    while (text.endsWith('0')) {
        text.chop(1);
    }
    if (text.endsWith('.')) {
        text.chop(1);
    }
    if (text == "-0") {
        return "0";
    }
    return text;
}

bool axisContainsZero(const AxisTicks& axis)
{
    return axis.min <= 0.0 && axis.max >= 0.0;
}

double crisp(double value)
{
    return std::round(value) + 0.5;
}

}  // namespace

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

QString PotentiostatGraphWidget::formatAxisValue(double value, double step) const
{
    const double absValue = std::abs(value);
    const double absStep = std::abs(step);
    if ((absValue > 0.0 && absValue < 1e-4) || absValue >= 1e5
        || (absStep > 0.0 && (absStep < 1e-5 || absStep >= 1e4))) {
        return QString::number(value, 'e', 2);
    }

    int decimals = 0;
    double scaledStep = (absStep > 0.0) ? absStep : 1.0;
    while (decimals < 6 && std::abs(scaledStep - std::round(scaledStep)) > 1e-9) {
        scaledStep *= 10.0;
        ++decimals;
    }
    return trimNumericLabel(QString::number(value, 'f', decimals));
}

void PotentiostatGraphWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    DisplaySeries display;
    switch (mode_) {
    case Mode::CurrentVsTime:
        display.title = "I vs t";
        display.xLabel = "t / s";
        display.yLabel = "I / mA";
        display.timeBasedMode = true;
        display.xs = times_;
        display.ys.reserve(currents_.size());
        for (double value : currents_) {
            display.ys.push_back(value * kCurrentToMilliAmp);
        }
        break;
    case Mode::EweVsTime:
        display.title = "Ewe vs t";
        display.xLabel = "t / s";
        display.yLabel = "Ewe / V";
        display.timeBasedMode = true;
        display.xs = times_;
        display.ys = eweValues_;
        break;
    case Mode::CurrentVsEwe:
        display.title = "I vs Ewe";
        display.xLabel = "Ewe / V";
        display.yLabel = "I / mA";
        display.xs = eweValues_;
        display.ys.reserve(currents_.size());
        for (double value : currents_) {
            display.ys.push_back(value * kCurrentToMilliAmp);
        }
        break;
    case Mode::EweVsCurrent:
        display.title = "Ewe vs I";
        display.xLabel = "I / mA";
        display.yLabel = "Ewe / V";
        display.xs.reserve(currents_.size());
        for (double value : currents_) {
            display.xs.push_back(value * kCurrentToMilliAmp);
        }
        display.ys = eweValues_;
        break;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#ffffff"));

    QFont titleFont = painter.font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(std::max(titleFont.pointSizeF(), 11.0));

    QFont tickFont = painter.font();
    tickFont.setPointSizeF(std::max(tickFont.pointSizeF(), 9.5));

    QFont axisTitleFont = tickFont;
    axisTitleFont.setBold(true);

    painter.setFont(titleFont);
    QFontMetricsF titleMetrics(titleFont);
    painter.setPen(kAxisText);
    painter.drawText(QRectF(0.0, 8.0, width(), titleMetrics.height() + 4.0), Qt::AlignCenter, display.title);

    std::vector<double> finiteXs;
    std::vector<double> finiteYs;
    finiteXs.reserve(display.xs.size());
    finiteYs.reserve(display.ys.size());
    for (std::size_t i = 0; i < display.xs.size() && i < display.ys.size(); ++i) {
        const double x = display.xs[i];
        const double y = display.ys[i];
        if (!std::isfinite(x) || !std::isfinite(y)) {
            continue;
        }
        finiteXs.push_back(x);
        finiteYs.push_back(y);
    }

    if (finiteXs.size() < 2 || finiteYs.size() < 2 || finiteXs.size() != finiteYs.size()) {
        painter.setFont(tickFont);
        painter.setPen(kTickText);
        painter.drawText(rect(), Qt::AlignCenter, "Aucune donnee de mesure");
        return;
    }

    const auto [xMinIt, xMaxIt] = std::minmax_element(finiteXs.begin(), finiteXs.end());
    const auto [yMinIt, yMaxIt] = std::minmax_element(finiteYs.begin(), finiteYs.end());
    const AxisTicks xAxis = buildAxisTicks(*xMinIt, *xMaxIt, true);
    const AxisTicks yAxis = buildAxisTicks(*yMinIt, *yMaxIt, false);

    painter.setFont(tickFont);
    QFontMetricsF tickMetrics(tickFont);
    QFontMetricsF axisTitleMetrics(axisTitleFont);

    double maxXLabelWidth = 0.0;
    for (double tick : xAxis.major) {
        maxXLabelWidth = std::max(maxXLabelWidth, tickMetrics.horizontalAdvance(formatAxisValue(tick, xAxis.step)));
    }

    double maxYLabelWidth = 0.0;
    for (double tick : yAxis.major) {
        maxYLabelWidth = std::max(maxYLabelWidth, tickMetrics.horizontalAdvance(formatAxisValue(tick, yAxis.step)));
    }

    const double leftMargin = std::max(88.0, axisTitleMetrics.height() + maxYLabelWidth + 28.0);
    const double rightMargin = std::max(30.0, maxXLabelWidth * 0.5 + 14.0);
    const double topMargin = titleMetrics.height() + 26.0;
    const double bottomMargin = std::max(64.0, tickMetrics.height() + axisTitleMetrics.height() + 26.0);

    const QRectF plotRect(
        leftMargin,
        topMargin,
        std::max(40.0, width() - leftMargin - rightMargin),
        std::max(40.0, height() - topMargin - bottomMargin)
    );

    auto mapX = [&plotRect, &xAxis](double value) {
        return plotRect.left() + (value - xAxis.min) / (xAxis.max - xAxis.min) * plotRect.width();
    };
    auto mapY = [&plotRect, &yAxis](double value) {
        return plotRect.bottom() - (value - yAxis.min) / (yAxis.max - yAxis.min) * plotRect.height();
    };

    painter.setPen(QPen(kPlotBorder, 1.0));
    painter.setBrush(kPlotBackground);
    painter.drawRect(plotRect);

    if (showPhases_ && display.timeBasedMode && !phases_.empty()) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setClipRect(plotRect);
        for (const auto& phase : phases_) {
            const double x1 = std::clamp(mapX(phase.tStart), plotRect.left(), plotRect.right());
            const double x2 = std::clamp(mapX(phase.tEnd), plotRect.left(), plotRect.right());
            if (x2 <= x1) {
                continue;
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(phase.moving ? kColorMoving : kColorStopped);
            painter.drawRect(QRectF(x1, plotRect.top(), x2 - x1, plotRect.height()));
        }
        painter.restore();
        painter.setPen(QPen(kPlotBorder, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(plotRect);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(kMinorGrid, 1.0));
    for (double tick : xAxis.minor) {
        const double x = crisp(mapX(tick));
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
    }
    for (double tick : yAxis.minor) {
        const double y = crisp(mapY(tick));
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
    }

    painter.setPen(QPen(kMajorGrid, 1.0));
    for (double tick : xAxis.major) {
        const double x = crisp(mapX(tick));
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
    }
    for (double tick : yAxis.major) {
        const double y = crisp(mapY(tick));
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
    }

    if (axisContainsZero(xAxis)) {
        painter.setPen(QPen(kZeroAxis, 2.0));
        const double zeroX = crisp(mapX(0.0));
        painter.drawLine(QPointF(zeroX, plotRect.top()), QPointF(zeroX, plotRect.bottom()));
    }
    if (axisContainsZero(yAxis)) {
        painter.setPen(QPen(kZeroAxis, 2.0));
        const double zeroY = crisp(mapY(0.0));
        painter.drawLine(QPointF(plotRect.left(), zeroY), QPointF(plotRect.right(), zeroY));
    }
    painter.restore();

    QPainterPath path;
    for (int i = 0; i < static_cast<int>(finiteXs.size()); ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        const QPointF point(mapX(finiteXs[index]), mapY(finiteYs[index]));
        (i == 0) ? path.moveTo(point) : path.lineTo(point);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kCurveColor, 2.0));
    painter.drawPath(path);

    if (finiteXs.size() <= 2000) {
        painter.setPen(QPen(kCurveColor, 1.0));
        constexpr double markerRadius = 2.4;
        for (int i = 0; i < static_cast<int>(finiteXs.size()); ++i) {
            const std::size_t index = static_cast<std::size_t>(i);
            const QPointF point(mapX(finiteXs[index]), mapY(finiteYs[index]));
            painter.drawLine(QPointF(point.x() - markerRadius, point.y() - markerRadius),
                             QPointF(point.x() + markerRadius, point.y() + markerRadius));
            painter.drawLine(QPointF(point.x() - markerRadius, point.y() + markerRadius),
                             QPointF(point.x() + markerRadius, point.y() - markerRadius));
        }
    }

    {
        const QPointF lastPoint(mapX(finiteXs.back()), mapY(finiteYs.back()));
        painter.setPen(QPen(kLastPointColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(lastPoint, 6.0, 6.0);
        painter.setBrush(kLastPointColor);
        painter.drawEllipse(lastPoint, 2.5, 2.5);
    }

    painter.setPen(QPen(kPlotBorder, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plotRect);

    painter.setFont(tickFont);
    painter.setPen(kTickText);

    constexpr double majorTickLength = 6.0;
    constexpr double minorTickLength = 3.5;

    for (double tick : xAxis.minor) {
        const double x = mapX(tick);
        painter.drawLine(QPointF(x, plotRect.bottom()), QPointF(x, plotRect.bottom() + minorTickLength));
    }
    for (double tick : yAxis.minor) {
        const double y = mapY(tick);
        painter.drawLine(QPointF(plotRect.left() - minorTickLength, y), QPointF(plotRect.left(), y));
    }

    for (double tick : xAxis.major) {
        const double x = mapX(tick);
        painter.drawLine(QPointF(x, plotRect.bottom()), QPointF(x, plotRect.bottom() + majorTickLength));
        const QString label = formatAxisValue(tick, xAxis.step);
        const double labelWidth = tickMetrics.horizontalAdvance(label);
        const QRectF labelRect(x - labelWidth * 0.5 - 4.0, plotRect.bottom() + 8.0, labelWidth + 8.0, tickMetrics.height() + 2.0);
        painter.drawText(labelRect, Qt::AlignCenter, label);
    }
    for (double tick : yAxis.major) {
        const double y = mapY(tick);
        painter.drawLine(QPointF(plotRect.left() - majorTickLength, y), QPointF(plotRect.left(), y));
        const QString label = formatAxisValue(tick, yAxis.step);
        const QRectF labelRect(8.0, y - tickMetrics.height() * 0.5 - 1.0, leftMargin - axisTitleMetrics.height() - 18.0, tickMetrics.height() + 2.0);
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
    }

    painter.setFont(axisTitleFont);
    painter.setPen(kAxisText);
    painter.drawText(QRectF(plotRect.left(), height() - axisTitleMetrics.height() - 6.0, plotRect.width(), axisTitleMetrics.height() + 2.0),
                     Qt::AlignCenter, display.xLabel);

    painter.save();
    painter.translate(axisTitleMetrics.height() + 10.0, plotRect.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-plotRect.height() * 0.5, -axisTitleMetrics.height() * 0.5, plotRect.height(), axisTitleMetrics.height() + 2.0),
                     Qt::AlignCenter, display.yLabel);
    painter.restore();
}

}  // namespace laserbench::ui
