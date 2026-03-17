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

constexpr int kLeftMargin = 64;
constexpr int kRightMargin = 20;
constexpr int kTopMargin = 32;
constexpr int kBottomMargin = 40;

}  // namespace

PotentiostatGraphWidget::PotentiostatGraphWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(260);
    setAutoFillBackground(false);
}

void PotentiostatGraphWidget::setGraphMode(Mode mode)
{
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    update();
}

void PotentiostatGraphWidget::setSeries(std::vector<double> times, std::vector<double> currents, std::vector<double> eweValues)
{
    times_ = std::move(times);
    currents_ = std::move(currents);
    eweValues_ = std::move(eweValues);
    update();
}

void PotentiostatGraphWidget::clear()
{
    times_.clear();
    currents_.clear();
    eweValues_.clear();
    update();
}

QString PotentiostatGraphWidget::formatAxisValue(double value) const
{
    const double absValue = std::abs(value);
    if ((absValue > 0.0 && absValue < 0.01) || absValue >= 1000.0) {
        return QString::number(value, 'e', 2);
    }
    return QString::number(value, 'f', 3);
}

void PotentiostatGraphWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#ffffff"));

    QRectF plotRect(
        kLeftMargin,
        kTopMargin,
        std::max(1, width() - kLeftMargin - kRightMargin),
        std::max(1, height() - kTopMargin - kBottomMargin)
    );

    QString title;
    const std::vector<double>* xs = nullptr;
    const std::vector<double>* ys = nullptr;
    switch (mode_) {
    case Mode::CurrentVsTime:
        title = "I = f(t)";
        xs = &times_;
        ys = &currents_;
        break;
    case Mode::EweVsTime:
        title = "Ewe = f(t)";
        xs = &times_;
        ys = &eweValues_;
        break;
    case Mode::CurrentVsEwe:
        title = "I = f(Ewe)";
        xs = &eweValues_;
        ys = &currents_;
        break;
    case Mode::EweVsCurrent:
        title = "Ewe = f(I)";
        xs = &currents_;
        ys = &eweValues_;
        break;
    }

    painter.setPen(QColor("#111927"));
    painter.drawText(QRectF(0.0, 8.0, width(), 20.0), Qt::AlignCenter, title);

    painter.setPen(QPen(QColor("#d8e0e8"), 1.0));
    painter.setBrush(QColor("#f8fafc"));
    painter.drawRoundedRect(plotRect, 10.0, 10.0);

    if (xs == nullptr || ys == nullptr || xs->size() < 2 || ys->size() < 2 || xs->size() != ys->size()) {
        painter.setPen(QColor("#5c6570"));
        painter.drawText(plotRect, Qt::AlignCenter, "Aucune donnee de mesure");
        return;
    }

    const auto [xMinIt, xMaxIt] = std::minmax_element(xs->begin(), xs->end());
    const auto [yMinIt, yMaxIt] = std::minmax_element(ys->begin(), ys->end());
    double xMin = *xMinIt;
    double xMax = *xMaxIt;
    double yMin = *yMinIt;
    double yMax = *yMaxIt;
    if (xMax <= xMin) {
        xMax = xMin + 1.0;
    }
    if (yMax <= yMin) {
        yMax = yMin + 1.0;
    }

    QPainterPath path;
    for (int i = 0; i < static_cast<int>(xs->size()); ++i) {
        const double nx = ((*xs)[static_cast<std::size_t>(i)] - xMin) / (xMax - xMin);
        const double ny = ((*ys)[static_cast<std::size_t>(i)] - yMin) / (yMax - yMin);
        const QPointF point(
            plotRect.left() + (nx * plotRect.width()),
            plotRect.bottom() - (ny * plotRect.height())
        );
        if (i == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
    }

    painter.setPen(QPen(QColor("#1f6feb"), 2.0));
    painter.drawPath(path);

    painter.setPen(QPen(QColor("#1f6feb"), 1.5));
    painter.setBrush(QColor("#ffffff"));
    for (int i = 0; i < static_cast<int>(xs->size()); ++i) {
        const double nx = ((*xs)[static_cast<std::size_t>(i)] - xMin) / (xMax - xMin);
        const double ny = ((*ys)[static_cast<std::size_t>(i)] - yMin) / (yMax - yMin);
        const QPointF point(
            plotRect.left() + (nx * plotRect.width()),
            plotRect.bottom() - (ny * plotRect.height())
        );
        painter.drawEllipse(point, 2.5, 2.5);
    }

    painter.setPen(QColor("#5c6570"));
    painter.drawText(QRectF(8.0, plotRect.top() - 2.0, kLeftMargin - 16.0, 18.0), Qt::AlignRight | Qt::AlignVCenter, formatAxisValue(yMax));
    painter.drawText(QRectF(8.0, plotRect.bottom() - 18.0, kLeftMargin - 16.0, 18.0), Qt::AlignRight | Qt::AlignVCenter, formatAxisValue(yMin));
    painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 8.0, 120.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter, formatAxisValue(xMin));
    painter.drawText(QRectF(plotRect.right() - 120.0, plotRect.bottom() + 8.0, 120.0, 18.0), Qt::AlignRight | Qt::AlignVCenter, formatAxisValue(xMax));
}

}  // namespace laserbench::ui
