#include "ui/Potentiostat3DWidget.hpp"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace laserbench::ui {

namespace {

QColor valueToColor(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const double hue = 220.0 - 220.0 * t;
    const double lig = std::clamp(0.60 - 0.12 * t, 0.0, 1.0);
    return QColor::fromHslF(hue / 360.0, 0.75, lig);
}

QColor darken(QColor c, double f)
{
    return QColor::fromHslF(c.hslHueF(), c.hslSaturationF(),
                             std::clamp(c.lightnessF() * f, 0.0, 1.0));
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

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
    rows_ = 0; cols_ = 0; values_.clear();
    update();
}

// ── Projection ───────────────────────────────────────────────────────────────

QPointF Potentiostat3DWidget::project(double col, double row, double z) const
{
    const double ca = std::cos(azimuth_), sa = std::sin(azimuth_);
    const double se = std::sin(elevation_);
    const double rx =  col * ca + row * sa;
    const double ry = -col * sa + row * ca;
    return QPointF(origin_.x() + rx * scaleH_,
                   origin_.y() - ry * se * scaleH_ - z * scaleV_);
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

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
    azimuth_   = azAtDragStart_  + (e->pos().x() - dragStart_.x()) * 0.01;
    elevation_ = std::clamp(elAtDragStart_ - (e->pos().y() - dragStart_.y()) * 0.01,
                             0.05, M_PI * 0.49);
    update();
}
void Potentiostat3DWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) { dragging_ = false; setMouseTracking(false); }
}
void Potentiostat3DWidget::wheelEvent(QWheelEvent* e)
{
    zoom_ = std::clamp(zoom_ * std::pow(1.15, e->angleDelta().y() / 120.0), 0.2, 8.0);
    update();
}

// ── paintEvent ───────────────────────────────────────────────────────────────

void Potentiostat3DWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), Qt::white);

    constexpr int kLeft = 62, kRight = 90, kTop = 30, kBottom = 44;
    const int W = width() - kLeft - kRight;
    const int H = height() - kTop  - kBottom;

    // Title
    {
        QFont f = painter.font(); f.setBold(true); painter.setFont(f);
        painter.setPen(QColor("#111927"));
        painter.drawText(QRectF(0, 6, width() - kRight, 20), Qt::AlignCenter, "Surface 3D  I(x, y)");
        f.setBold(false); painter.setFont(f);
    }

    if (rows_ < 2 || cols_ < 2) {
        painter.setPen(QColor("#5c6570"));
        painter.drawText(QRectF(kLeft, kTop, W, H), Qt::AlignCenter, "Aucune donnee de mesure");
        return;
    }

    // zMin / zMax
    double zMin =  std::numeric_limits<double>::max();
    double zMax = -std::numeric_limits<double>::max();
    for (const auto& v : values_)
        if (v) { zMin = std::min(zMin, *v); zMax = std::max(zMax, *v); }
    if (zMax <= zMin) zMax = zMin + 1.0;
    const double zRange = zMax - zMin;

    auto getZ = [&](int r, int c) -> double {
        const std::size_t i = static_cast<std::size_t>(r * cols_ + c);
        return (i < values_.size() && values_[i]) ? *values_[i] : zMin;
    };

    // Scales: maxDim data cells = 1 "unit"
    const double maxDim = static_cast<double>(std::max(rows_ - 1, cols_ - 1));
    scaleH_ = zoom_ * std::min(W, H) * 0.38;
    scaleV_ = scaleH_ * 0.68;
    origin_ = QPointF(kLeft + W * 0.50, kTop + H * 0.80);

    auto proj = [&](double c, double r, double val) -> QPointF {
        return project(c / maxDim, r / maxDim, (val - zMin) / zRange);
    };

    const double C = static_cast<double>(cols_ - 1);
    const double R = static_cast<double>(rows_ - 1);

    // Back-wall visibility (ry = -c*sin(az) + r*cos(az), positive = further)
    const double sa = std::sin(azimuth_), ca = std::cos(azimuth_);
    const bool wallC0   = sa > 0.0;
    const bool wallCMax = sa < 0.0;
    const bool wallR0   = ca < 0.0;
    const bool wallRMax = ca > 0.0;

    // ── 1. Floor ──────────────────────────────────────────────────────────
    {
        const QColor fill("#e4e7ec"), grid("#b0bac8");
        QPolygonF poly;
        poly << proj(0,0,zMin) << proj(C,0,zMin) << proj(C,R,zMin) << proj(0,R,zMin);
        painter.setPen(QPen(grid, 0.6)); painter.setBrush(fill);
        painter.drawPolygon(poly);

        painter.setPen(QPen(grid, 0.5));
        const int sc = std::max(1, (cols_-1)/5), sr = std::max(1, (rows_-1)/5);
        for (int c = 0; c <= cols_-1; c += sc) painter.drawLine(proj(c,0,zMin), proj(c,R,zMin));
        for (int r = 0; r <= rows_-1; r += sr) painter.drawLine(proj(0,r,zMin), proj(C,r,zMin));
    }

    // ── 2. Back walls ─────────────────────────────────────────────────────
    const QColor wFill("#dde1e9"), wGrid("#a8b2c0");
    auto drawWall4 = [&](QPointF p0, QPointF p1, QPointF p2, QPointF p3,
                          // 4 horizontal z-tick lines + vertical ticks
                          std::function<QPointF(double)> left,
                          std::function<QPointF(double)> right,
                          std::function<QPointF(double)> bot,
                          std::function<QPointF(double)> top_) {
        QPolygonF poly; poly << p0 << p1 << p2 << p3;
        painter.setPen(QPen(wGrid, 0.5)); painter.setBrush(wFill);
        painter.drawPolygon(poly);
        painter.setPen(QPen(wGrid, 0.5));
        for (int k = 1; k <= 4; ++k) {
            const double t = k / 4.0;
            painter.drawLine(left(t), right(t));
        }
        for (int k = 1; k <= 4; ++k) {
            const double t = k / 4.0;
            painter.drawLine(bot(t), top_(t));
        }
    };

    if (wallR0)
        drawWall4(proj(0,0,zMin), proj(C,0,zMin), proj(C,0,zMax), proj(0,0,zMax),
                  [&](double t){ return proj(0, 0, zMin + t*zRange); },
                  [&](double t){ return proj(C, 0, zMin + t*zRange); },
                  [&](double t){ return proj(t*C, 0, zMin); },
                  [&](double t){ return proj(t*C, 0, zMax); });
    if (wallRMax)
        drawWall4(proj(0,R,zMin), proj(C,R,zMin), proj(C,R,zMax), proj(0,R,zMax),
                  [&](double t){ return proj(0, R, zMin + t*zRange); },
                  [&](double t){ return proj(C, R, zMin + t*zRange); },
                  [&](double t){ return proj(t*C, R, zMin); },
                  [&](double t){ return proj(t*C, R, zMax); });
    if (wallC0)
        drawWall4(proj(0,0,zMin), proj(0,R,zMin), proj(0,R,zMax), proj(0,0,zMax),
                  [&](double t){ return proj(0, 0, zMin + t*zRange); },
                  [&](double t){ return proj(0, R, zMin + t*zRange); },
                  [&](double t){ return proj(0, t*R, zMin); },
                  [&](double t){ return proj(0, t*R, zMax); });
    if (wallCMax)
        drawWall4(proj(C,0,zMin), proj(C,R,zMin), proj(C,R,zMax), proj(C,0,zMax),
                  [&](double t){ return proj(C, 0, zMin + t*zRange); },
                  [&](double t){ return proj(C, R, zMin + t*zRange); },
                  [&](double t){ return proj(C, t*R, zMin); },
                  [&](double t){ return proj(C, t*R, zMax); });

    // ── 3. Surface quads (back → front) ───────────────────────────────────
    struct Quad { int r, c; double depth; };
    std::vector<Quad> quads;
    quads.reserve(static_cast<std::size_t>((rows_-1) * (cols_-1)));

    for (int r = 0; r < rows_-1; ++r)
        for (int c = 0; c < cols_-1; ++c) {
            // avg ry of 4 corners (in normalised coords)
            const double ry =
                ((-c/maxDim)*sa + (r/maxDim)*ca) +
                ((-(c+1)/maxDim)*sa + (r/maxDim)*ca) +
                ((-(c+1)/maxDim)*sa + ((r+1)/maxDim)*ca) +
                ((-c/maxDim)*sa + ((r+1)/maxDim)*ca);
            quads.push_back({r, c, ry * 0.25});
        }
    std::sort(quads.begin(), quads.end(), [](const Quad& a, const Quad& b){ return a.depth > b.depth; });

    for (const auto& q : quads) {
        const int r = q.r, c = q.c;
        const double z00 = getZ(r,   c),   z10 = getZ(r,   c+1);
        const double z11 = getZ(r+1, c+1), z01 = getZ(r+1, c);
        const double t   = ((z00+z10+z11+z01)*0.25 - zMin) / zRange;
        const QColor fill = valueToColor(t);

        QPolygonF poly;
        poly << proj(c,   r,   z00) << proj(c+1, r,   z10)
             << proj(c+1, r+1, z11) << proj(c,   r+1, z01);

        painter.setPen(QPen(darken(fill, 0.80), 0.35));
        painter.setBrush(fill);
        painter.drawPolygon(poly);
    }

    // ── 4. Box outline ────────────────────────────────────────────────────
    {
        painter.setPen(QPen(QColor("#6b7280"), 1.1));
        painter.setBrush(Qt::NoBrush);
        auto bl = [&](QPointF a, QPointF b){ painter.drawLine(a, b); };
        // Bottom
        bl(proj(0,0,zMin),proj(C,0,zMin)); bl(proj(C,0,zMin),proj(C,R,zMin));
        bl(proj(C,R,zMin),proj(0,R,zMin)); bl(proj(0,R,zMin),proj(0,0,zMin));
        // Verticals
        bl(proj(0,0,zMin),proj(0,0,zMax)); bl(proj(C,0,zMin),proj(C,0,zMax));
        bl(proj(C,R,zMin),proj(C,R,zMax)); bl(proj(0,R,zMin),proj(0,R,zMax));
        // Top
        bl(proj(0,0,zMax),proj(C,0,zMax)); bl(proj(C,0,zMax),proj(C,R,zMax));
        bl(proj(C,R,zMax),proj(0,R,zMax)); bl(proj(0,R,zMax),proj(0,0,zMax));
    }

    // ── 5. Axis labels ────────────────────────────────────────────────────
    QFont lf = painter.font(); lf.setPointSize(8); painter.setFont(lf);
    painter.setPen(QColor("#374151"));

    // X label: along column direction, midpoint of first row
    {
        const QPointF mid = proj(C/2.0, 0, zMin);
        painter.drawText(mid + QPointF(-22, 14),
                          QString("X  (%1 mm)").arg(xSpanMm_, 0, 'f', 2));
    }
    // Y label: along row direction, midpoint of first col
    {
        const QPointF mid = proj(0, R/2.0, zMin);
        painter.drawText(mid + QPointF(-58, 4),
                          QString("Y  (%1 mm)").arg(ySpanMm_, 0, 'f', 2));
    }
    // Z ticks (on the vertical edge at c=0, r=0)
    {
        for (int k = 0; k <= 5; ++k) {
            const double z = zMin + k * zRange / 5.0;
            const QPointF pt = proj(0, 0, z);
            painter.drawLine(pt, pt + QPointF(-3, 0));
            painter.drawText(QRectF(pt.x()-60, pt.y()-8, 54, 16),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(z, 'e', 1));
        }
        const QPointF top = proj(0, 0, zMax);
        painter.drawText(top + QPointF(-36, -14), "I (A)");
    }

    // ── 6. Colour bar ─────────────────────────────────────────────────────
    {
        const int bx = width() - kRight + 18;
        const int bh = static_cast<int>(H * 0.72);
        const int by = kTop + (H - bh) / 2;
        constexpr int bw = 16;

        for (int py = 0; py < bh; ++py) {
            painter.setPen(valueToColor(1.0 - double(py) / (bh - 1)));
            painter.drawLine(bx, by+py, bx+bw, by+py);
        }
        painter.setPen(QColor("#6b7280"));
        painter.drawRect(bx, by, bw, bh);

        painter.setPen(QColor("#374151"));
        for (int k = 0; k <= 5; ++k) {
            const int   py  = by + k * (bh-1) / 5;
            const double val = zMax - k * zRange / 5.0;
            painter.drawLine(bx+bw, py, bx+bw+3, py);
            painter.drawText(QRectF(bx+bw+5, py-8, 58, 16),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QString::number(val, 'e', 1));
        }
        painter.drawText(QRectF(bx-4, by-18, 80, 16), Qt::AlignLeft, "I (A)");
    }

    // Hint
    {
        QFont hf = painter.font(); hf.setPointSize(7); painter.setFont(hf);
        painter.setPen(QColor("#9ca3af"));
        painter.drawText(QRectF(kLeft, height()-kBottom+8, W, 16),
                         Qt::AlignCenter, "Glisser : rotation   Molette : zoom");
    }
}

}  // namespace laserbench::ui
