#include "ui/Potentiostat3DWidget.hpp"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace laserbench::ui {

namespace {

struct Vec3
{
    double x {0.0};
    double y {0.0};
    double z {0.0};
};

QColor blendColor(const QColor& a, const QColor& b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor(
        static_cast<int>(std::lround(a.red() + (b.red() - a.red()) * t)),
        static_cast<int>(std::lround(a.green() + (b.green() - a.green()) * t)),
        static_cast<int>(std::lround(a.blue() + (b.blue() - a.blue()) * t))
    );
}

QColor valueToColor(double t)
{
    t = std::clamp(t, 0.0, 1.0);

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

QColor shadeColor(const QColor& color, double shade)
{
    shade = std::clamp(shade, 0.35, 1.25);
    return QColor(
        std::clamp(static_cast<int>(std::lround(color.red() * shade)), 0, 255),
        std::clamp(static_cast<int>(std::lround(color.green() * shade)), 0, 255),
        std::clamp(static_cast<int>(std::lround(color.blue() * shade)), 0, 255)
    );
}

Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

Vec3 normalize(Vec3 v)
{
    const double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (n <= 1e-12) return {0.0, 0.0, 1.0};
    return {v.x / n, v.y / n, v.z / n};
}

double dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

QString currentUnitLabel(double minValue, double maxValue, double* scaleOut)
{
    const double absMax = std::max(std::abs(minValue), std::abs(maxValue));
    if (absMax >= 1e-3) {
        *scaleOut = 1e3;
        return "mA";
    }
    if (absMax >= 1e-6) {
        *scaleOut = 1e6;
        return "uA";
    }
    if (absMax >= 1e-9) {
        *scaleOut = 1e9;
        return "nA";
    }
    *scaleOut = 1.0;
    return "A";
}

QString formatScaledCurrent(double value, double scale)
{
    const double scaled = value * scale;
    const double absScaled = std::abs(scaled);
    const int decimals = absScaled >= 100.0 ? 0 : (absScaled >= 10.0 ? 1 : 2);
    return QString::number(scaled, 'f', decimals);
}

void drawTextAlongLine(QPainter& painter, const QPointF& a, const QPointF& b, const QString& text, double offset)
{
    const QPointF delta = b - a;
    const double length = std::hypot(delta.x(), delta.y());
    if (length <= 1.0 || text.isEmpty()) return;

    double angleDeg = std::atan2(delta.y(), delta.x()) * 180.0 / M_PI;
    QPointF normal(-delta.y() / length, delta.x() / length);
    if (angleDeg > 90.0 || angleDeg < -90.0) {
        angleDeg += 180.0;
        normal = -normal;
    }

    const QPointF mid = (a + b) * 0.5 + normal * offset;
    painter.save();
    painter.translate(mid);
    painter.rotate(angleDeg);

    const QFontMetricsF metrics(painter.font());
    const QRectF textRect = metrics.boundingRect(text).adjusted(-4.0, -2.0, 4.0, 2.0);
    painter.drawText(QRectF(-textRect.width() * 0.5, -textRect.height() * 0.5,
                            textRect.width(), textRect.height()),
                     Qt::AlignCenter, text);
    painter.restore();
}

}  // namespace

Potentiostat3DWidget::Potentiostat3DWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(300);
    setAutoFillBackground(false);
}

void Potentiostat3DWidget::setGrid(
    int rows, int cols,
    std::vector<std::optional<double>> values,
    double xSpanMm, double ySpanMm)
{
    rows_    = rows;
    cols_    = cols;
    values_  = std::move(values);
    xSpanMm_ = std::max(xSpanMm, 1e-9);
    ySpanMm_ = std::max(ySpanMm, 1e-9);
    update();
}

void Potentiostat3DWidget::clear()
{
    rows_ = 0;
    cols_ = 0;
    values_.clear();
    update();
}

QPointF Potentiostat3DWidget::project(double x, double y, double z) const
{
    const double ca = std::cos(azimuth_);
    const double sa = std::sin(azimuth_);
    const double se = std::sin(elevation_);

    const double rx = x * ca + y * sa;
    const double ry = -x * sa + y * ca;
    return QPointF(
        origin_.x() + rx * scaleH_,
        origin_.y() - ry * se * scaleH_ - z * scaleV_
    );
}

void Potentiostat3DWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        dragging_ = true;
        dragStart_ = e->pos();
        azAtDragStart_ = azimuth_;
        elAtDragStart_ = elevation_;
        setMouseTracking(true);
    }
}

void Potentiostat3DWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!dragging_) return;

    azimuth_ = azAtDragStart_ + (e->pos().x() - dragStart_.x()) * 0.01;
    elevation_ = std::clamp(
        elAtDragStart_ - (e->pos().y() - dragStart_.y()) * 0.01,
        0.05,
        M_PI * 0.49);
    update();
}

void Potentiostat3DWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        dragging_ = false;
        setMouseTracking(false);
    }
}

void Potentiostat3DWidget::wheelEvent(QWheelEvent* e)
{
    zoom_ = std::clamp(zoom_ * std::pow(1.15, e->angleDelta().y() / 120.0), 0.25, 8.0);
    update();
}

void Potentiostat3DWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), Qt::white);

    constexpr int kLeft = 24;
    constexpr int kRight = 34;
    constexpr int kTop = 30;
    constexpr int kBottom = 34;
    const QRectF plotRect(
        kLeft,
        kTop,
        std::max(1, width() - kLeft - kRight),
        std::max(1, height() - kTop - kBottom));

    {
        QFont f = painter.font();
        f.setBold(true);
        painter.setFont(f);
        painter.setPen(QColor("#111927"));
        painter.drawText(QRectF(0, 6, width(), 20), Qt::AlignCenter, "Surface 3D  I(x, y)");
        f.setBold(false);
        painter.setFont(f);
    }

    if (rows_ < 2 || cols_ < 2) {
        painter.setPen(QColor("#5c6570"));
        painter.drawText(plotRect, Qt::AlignCenter, "Aucune donnee de mesure");
        return;
    }

    double zMin = std::numeric_limits<double>::max();
    double zMax = -std::numeric_limits<double>::max();
    for (const auto& value : values_) {
        if (value.has_value() && std::isfinite(*value)) {
            zMin = std::min(zMin, *value);
            zMax = std::max(zMax, *value);
        }
    }
    if (zMin == std::numeric_limits<double>::max()) {
        painter.setPen(QColor("#5c6570"));
        painter.drawText(plotRect, Qt::AlignCenter, "Aucune donnee de mesure");
        return;
    }
    if (zMax <= zMin) zMax = zMin + 1.0;
    const double zRange = zMax - zMin;

    const double cMax = static_cast<double>(std::max(1, cols_ - 1));
    const double rMax = static_cast<double>(std::max(1, rows_ - 1));
    const double maxSpanMm = std::max(xSpanMm_, ySpanMm_);
    const double xAspect = xSpanMm_ / maxSpanMm;
    const double yAspect = ySpanMm_ / maxSpanMm;
    constexpr double kZModelScale = 0.48;

    const auto valueAt = [&](int row, int col) -> double {
        const std::size_t idx = static_cast<std::size_t>(row * cols_ + col);
        return (idx < values_.size() && values_[idx].has_value() && std::isfinite(*values_[idx]))
            ? *values_[idx]
            : zMin;
    };

    const auto modelPoint = [&](double col, double row, double zNorm) -> Vec3 {
        return {
            (0.5 - (col / cMax)) * xAspect,
            ((row / rMax) - 0.5) * yAspect,
            zNorm * kZModelScale
        };
    };

    const auto zNorm = [&](double value) -> double {
        return (value - zMin) / zRange;
    };

    const auto projectCell = [&](double col, double row, double value) -> QPointF {
        const Vec3 p = modelPoint(col, row, zNorm(value));
        return project(p.x, p.y, p.z);
    };

    const double surfaceWidthUnits = std::abs(std::cos(azimuth_)) * xAspect
        + std::abs(std::sin(azimuth_)) * yAspect;
    const double surfaceDepthUnits = std::abs(std::sin(azimuth_)) * xAspect
        + std::abs(std::cos(azimuth_)) * yAspect;
    scaleH_ = zoom_ * std::min(
        plotRect.width() / std::max(0.2, surfaceWidthUnits + 0.24),
        plotRect.height() / std::max(0.2, surfaceDepthUnits * std::sin(elevation_) + kZModelScale + 0.22));
    scaleV_ = scaleH_;

    origin_ = QPointF(0.0, 0.0);
    QRectF bounds;
    bool hasBounds = false;
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            const QPointF p = projectCell(col, row, valueAt(row, col));
            if (!hasBounds) {
                bounds = QRectF(p, QSizeF(1.0, 1.0));
                hasBounds = true;
            } else {
                bounds = bounds.united(QRectF(p, QSizeF(1.0, 1.0)));
            }
        }
    }
    const std::array<QPointF, 8> boxPoints {
        projectCell(0, 0, zMin), projectCell(cMax, 0, zMin),
        projectCell(cMax, rMax, zMin), projectCell(0, rMax, zMin),
        projectCell(0, 0, zMax), projectCell(cMax, 0, zMax),
        projectCell(cMax, rMax, zMax), projectCell(0, rMax, zMax)
    };
    for (const QPointF& p : boxPoints) {
        bounds = bounds.united(QRectF(p, QSizeF(1.0, 1.0)));
    }
    origin_ = plotRect.center() - bounds.center();

    struct Quad
    {
        int row {0};
        int col {0};
        double depth {0.0};
    };

    std::vector<Quad> quads;
    quads.reserve(static_cast<std::size_t>((rows_ - 1) * (cols_ - 1)));

    const double ca = std::cos(azimuth_);
    const double sa = std::sin(azimuth_);
    const auto depthOf = [&](const Vec3& p) {
        return -p.x * sa + p.y * ca + p.z * 0.08;
    };

    for (int row = 0; row < rows_ - 1; ++row) {
        for (int col = 0; col < cols_ - 1; ++col) {
            const Vec3 p00 = modelPoint(col, row, zNorm(valueAt(row, col)));
            const Vec3 p10 = modelPoint(col + 1, row, zNorm(valueAt(row, col + 1)));
            const Vec3 p11 = modelPoint(col + 1, row + 1, zNorm(valueAt(row + 1, col + 1)));
            const Vec3 p01 = modelPoint(col, row + 1, zNorm(valueAt(row + 1, col)));
            const double depth = (depthOf(p00) + depthOf(p10) + depthOf(p11) + depthOf(p01)) * 0.25;
            quads.push_back({row, col, depth});
        }
    }
    std::sort(quads.begin(), quads.end(), [](const Quad& a, const Quad& b) {
        return a.depth > b.depth;
    });

    painter.setPen(Qt::NoPen);
    for (const Quad& quad : quads) {
        const int row = quad.row;
        const int col = quad.col;
        const double z00 = valueAt(row, col);
        const double z10 = valueAt(row, col + 1);
        const double z11 = valueAt(row + 1, col + 1);
        const double z01 = valueAt(row + 1, col);
        const double t = zNorm((z00 + z10 + z11 + z01) * 0.25);

        const Vec3 p00 = modelPoint(col, row, zNorm(z00));
        const Vec3 p10 = modelPoint(col + 1, row, zNorm(z10));
        const Vec3 p01 = modelPoint(col, row + 1, zNorm(z01));
        Vec3 normal = normalize(cross(p10 - p00, p01 - p00));
        if (normal.z < 0.0) normal = {-normal.x, -normal.y, -normal.z};
        const Vec3 light = normalize({-0.35, -0.55, 1.0});
        const double shade = 0.72 + 0.34 * std::max(0.0, dot(normal, light));
        const QColor fill = shadeColor(valueToColor(t), shade);

        QPolygonF poly;
        poly << projectCell(col, row, z00)
             << projectCell(col + 1, row, z10)
             << projectCell(col + 1, row + 1, z11)
             << projectCell(col, row + 1, z01);

        painter.setBrush(fill);
        painter.setPen(QPen(shadeColor(fill, 0.92), 0.25));
        painter.drawPolygon(poly);
    }

    const QPointF p00 = projectCell(0, 0, zMin);
    const QPointF p10 = projectCell(cMax, 0, zMin);
    const QPointF p11 = projectCell(cMax, rMax, zMin);
    const QPointF p01 = projectCell(0, rMax, zMin);

    painter.setPen(QPen(QColor("#31343a"), 1.1));
    painter.drawLine(p00, p10);
    painter.drawLine(p10, p11);
    painter.drawLine(p11, p01);
    painter.drawLine(p01, p00);

    QFont axisFont = painter.font();
    axisFont.setPointSizeF(8.5);
    painter.setFont(axisFont);
    painter.setPen(QColor("#2f3339"));

    const QPointF xEdgeA0 = p00;
    const QPointF xEdgeB0 = p10;
    const QPointF xEdgeA1 = p01;
    const QPointF xEdgeB1 = p11;
    const bool useXRowMax = ((xEdgeA1.y() + xEdgeB1.y()) > (xEdgeA0.y() + xEdgeB0.y()));
    drawTextAlongLine(
        painter,
        useXRowMax ? xEdgeA1 : xEdgeA0,
        useXRowMax ? xEdgeB1 : xEdgeB0,
        QString("x: %1 mm").arg(xSpanMm_, 0, 'f', 2),
        18.0);

    const QPointF yEdgeA0 = p00;
    const QPointF yEdgeB0 = p01;
    const QPointF yEdgeA1 = p10;
    const QPointF yEdgeB1 = p11;
    const bool useYColMax = ((yEdgeA1.y() + yEdgeB1.y()) > (yEdgeA0.y() + yEdgeB0.y()));
    drawTextAlongLine(
        painter,
        useYColMax ? yEdgeA1 : yEdgeA0,
        useYColMax ? yEdgeB1 : yEdgeB0,
        QString("y: %1 mm").arg(ySpanMm_, 0, 'f', 2),
        18.0);

    struct ZCorner
    {
        double col {0.0};
        double row {0.0};
        QPointF base;
    };
    std::array<ZCorner, 4> zCorners {{
        {0.0, 0.0, p00},
        {cMax, 0.0, p10},
        {cMax, rMax, p11},
        {0.0, rMax, p01}
    }};
    const ZCorner zCorner = *std::max_element(zCorners.begin(), zCorners.end(), [](const ZCorner& a, const ZCorner& b) {
        if (std::abs(a.base.x() - b.base.x()) > 1e-6) return a.base.x() < b.base.x();
        return a.base.y() < b.base.y();
    });

    const QPointF zBase = projectCell(zCorner.col, zCorner.row, zMin);
    const QPointF zTop = projectCell(zCorner.col, zCorner.row, zMax);
    painter.setPen(QPen(QColor("#31343a"), 1.2));
    painter.drawLine(zBase, zTop);

    double currentScale = 1.0;
    const QString currentUnit = currentUnitLabel(zMin, zMax, &currentScale);
    const QPointF tickDir = (zCorner.base.x() > plotRect.center().x()) ? QPointF(8.0, 0.0) : QPointF(-8.0, 0.0);
    const double labelX = tickDir.x() > 0.0 ? 6.0 : -74.0;
    const Qt::Alignment labelAlign = tickDir.x() > 0.0
        ? (Qt::AlignLeft | Qt::AlignVCenter)
        : (Qt::AlignRight | Qt::AlignVCenter);

    for (const auto& tick : {std::pair<double, QString>(zMin, formatScaledCurrent(zMin, currentScale)),
                             std::pair<double, QString>(zMax, formatScaledCurrent(zMax, currentScale))}) {
        const QPointF p = projectCell(zCorner.col, zCorner.row, tick.first);
        painter.drawLine(p, p + tickDir);
        painter.drawText(QRectF(p.x() + labelX, p.y() - 10.0, 68.0, 20.0),
                         labelAlign,
                         QString("%1 %2").arg(tick.second, currentUnit));
    }

    {
        QFont hintFont = painter.font();
        hintFont.setPointSizeF(7.0);
        painter.setFont(hintFont);
        painter.setPen(QColor("#9ca3af"));
        painter.drawText(QRectF(kLeft, height() - kBottom + 10, plotRect.width(), 16),
                         Qt::AlignCenter,
                         "Glisser : rotation autour du centre   Molette : zoom");
    }
}

}  // namespace laserbench::ui
