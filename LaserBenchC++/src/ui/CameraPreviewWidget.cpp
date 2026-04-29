#include "ui/CameraPreviewWidget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <optional>

namespace laserbench::ui {

CameraPreviewWidget::CameraPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(520, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);  // receive mouseMoveEvent without button pressed
}

void CameraPreviewWidget::setFrame(const QImage& frame)
{
    if (!frame.isNull() && frame.format() != QImage::Format_RGB32) {
        frameImage_ = frame.convertToFormat(QImage::Format_RGB32);
    } else {
        frameImage_ = frame;
    }
    frameSize_ = frameImage_.size();
    rebuildScaledCache();
    update();
}

void CameraPreviewWidget::setLaserOverlay(const QPointF& pointPx, int radiusPx, bool visible)
{
    const int boundedRadius = std::max(radiusPx, 1);
    if (laserOverlayVisible_ == visible && laserRadiusPx_ == boundedRadius && laserPointPx_ == pointPx) {
        return;
    }

    laserPointPx_ = pointPx;
    laserRadiusPx_ = boundedRadius;
    laserOverlayVisible_ = visible;
    update();
}

void CameraPreviewWidget::clearLaserOverlay()
{
    if (!laserOverlayVisible_) {
        return;
    }

    laserOverlayVisible_ = false;
    update();
}

void CameraPreviewWidget::setSequenceOverlay(
    const QPointF& startPointPx,
    bool hasStartPoint,
    const QPointF& endPointPx,
    bool hasEndPoint,
    const QString& sizeText)
{
    if (sequenceStartVisible_ == hasStartPoint
        && sequenceEndVisible_ == hasEndPoint
        && sequenceStartPointPx_ == startPointPx
        && sequenceEndPointPx_ == endPointPx
        && sequenceSizeText_ == sizeText) {
        return;
    }

    sequenceStartPointPx_ = startPointPx;
    sequenceEndPointPx_ = endPointPx;
    sequenceStartVisible_ = hasStartPoint;
    sequenceEndVisible_ = hasEndPoint;
    sequenceSizeText_ = sizeText;
    update();
}

void CameraPreviewWidget::clearSequenceOverlay()
{
    if (!sequenceStartVisible_ && !sequenceEndVisible_) {
        return;
    }

    sequenceStartVisible_ = false;
    sequenceEndVisible_ = false;
    sequenceSizeText_.clear();
    update();
}

void CameraPreviewWidget::setWaypointOverlay(std::vector<QPointF> donePx, std::vector<QPointF> remainingPx)
{
    waypointsDonePx_ = std::move(donePx);
    waypointsRemainingPx_ = std::move(remainingPx);
    waypointOverlayVisible_ = true;
    update();
}

void CameraPreviewWidget::clearWaypointOverlay()
{
    if (!waypointOverlayVisible_ && waypointsDonePx_.empty() && waypointsRemainingPx_.empty()) {
        return;
    }
    waypointsDonePx_.clear();
    waypointsRemainingPx_.clear();
    waypointOverlayVisible_ = false;
    update();
}

void CameraPreviewWidget::setRulerOverlay(const QPointF& p1Px, bool hasP2, const QPointF& p2Px,
                                          const QString& distanceText)
{
    rulerP1Px_          = p1Px;
    rulerP1Visible_     = true;
    rulerP2Px_          = p2Px;
    rulerP2Visible_     = hasP2;
    rulerDistanceText_  = distanceText;
    update();
}

void CameraPreviewWidget::clearRulerOverlay()
{
    if (!rulerP1Visible_) return;
    rulerP1Visible_ = false;
    rulerP2Visible_ = false;
    rulerDistanceText_.clear();
    update();
}

void CameraPreviewWidget::setCircleOverlay(const QPointF& centerPx, bool hasEdge,
                                            const QPointF& edgePx, const QString& diameterText)
{
    circleCenterPx_      = centerPx;
    circleCenterVisible_ = true;
    circleEdgePx_        = edgePx;
    circleEdgeVisible_   = hasEdge;
    circleDiameterText_  = diameterText;
    update();
}

void CameraPreviewWidget::clearCircleOverlay()
{
    if (!circleCenterVisible_) return;
    circleCenterVisible_ = false;
    circleEdgeVisible_   = false;
    circleDiameterText_.clear();
    update();
}

void CameraPreviewWidget::setRectOverlay(const QPointF& p1Px, bool hasP2, const QPointF& p2Px,
                                          const QString& sizeText)
{
    rectP1Px_      = p1Px;
    rectP1Visible_ = true;
    rectP2Px_      = p2Px;
    rectP2Visible_ = hasP2;
    rectSizeText_  = sizeText;
    update();
}

void CameraPreviewWidget::clearRectOverlay()
{
    if (!rectP1Visible_) return;
    rectP1Visible_ = false;
    rectP2Visible_ = false;
    rectSizeText_.clear();
    update();
}

void CameraPreviewWidget::setZoomFactor(double zoomFactor)
{
    const double bounded = std::clamp(zoomFactor, 1.0, 12.0);
    if (qFuzzyCompare(zoomFactor_, bounded)) {
        return;
    }

    zoomFactor_ = bounded;
    clampPanOffset();
    emit zoomFactorChanged(zoomFactor_);
    invalidateScaledCache();
    update();
}

double CameraPreviewWidget::zoomFactor() const
{
    return zoomFactor_;
}

void CameraPreviewWidget::zoomIn()
{
    updateZoom(1.25);
}

void CameraPreviewWidget::zoomOut()
{
    updateZoom(0.8);
}

void CameraPreviewWidget::resetZoom()
{
    panOffset_ = QPointF(0.0, 0.0);
    setZoomFactor(1.0);
}

bool CameraPreviewWidget::computeDisplayGeometry(QRectF& targetRect, double& displayScale) const
{
    if (frameImage_.isNull() || frameSize_.isEmpty()) {
        return false;
    }

    const QSize available = size();
    const double coverScale = std::max(
        static_cast<double>(available.width()) / static_cast<double>(frameSize_.width()),
        static_cast<double>(available.height()) / static_cast<double>(frameSize_.height())
    );

    displayScale = coverScale * zoomFactor_;
    const QSizeF targetSize(
        static_cast<double>(frameSize_.width()) * displayScale,
        static_cast<double>(frameSize_.height()) * displayScale
    );

    targetRect = QRectF(
        (static_cast<double>(width()) - targetSize.width()) * 0.5 + panOffset_.x(),
        (static_cast<double>(height()) - targetSize.height()) * 0.5 + panOffset_.y(),
        targetSize.width(),
        targetSize.height()
    );
    return true;
}

// Helper used by both mouseMoveEvent and mousePressEvent
static std::optional<QPoint> widgetToFramePoint(const QPointF& widgetPos,
                                                  const QRectF& targetRect,
                                                  double displayScale,
                                                  const QSize& frameSize)
{
    if (!targetRect.contains(widgetPos) || displayScale <= 0.0 || frameSize.isEmpty())
        return std::nullopt;
    const int fx = std::clamp(
        static_cast<int>(std::lround((widgetPos.x() - targetRect.left()) / displayScale)),
        0, frameSize.width()  - 1);
    const int fy = std::clamp(
        static_cast<int>(std::lround((widgetPos.y() - targetRect.top())  / displayScale)),
        0, frameSize.height() - 1);
    return QPoint(fx, fy);
}

void CameraPreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event == nullptr) { QWidget::mouseMoveEvent(event); return; }

    QRectF targetRect;
    double displayScale = 1.0;
    if (computeDisplayGeometry(targetRect, displayScale)) {
        if (auto fp = widgetToFramePoint(event->position(), targetRect, displayScale, frameSize_)) {
            emit frameCursorMoved(*fp);
            event->accept();
            return;
        }
    }
    emit frameCursorLeft();
    event->accept();
}

void CameraPreviewWidget::leaveEvent(QEvent* event)
{
    emit frameCursorLeft();
    QWidget::leaveEvent(event);
}

void CameraPreviewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event == nullptr || event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    QRectF targetRect;
    double displayScale = 1.0;
    if (computeDisplayGeometry(targetRect, displayScale)) {
        if (auto fp = widgetToFramePoint(event->position(), targetRect, displayScale, frameSize_)) {
            emit frameDoubleClicked(*fp);
            event->accept();
            return;
        }
    }
    event->accept();
}

void CameraPreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event == nullptr || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    QRectF targetRect;
    double displayScale = 1.0;
    if (!computeDisplayGeometry(targetRect, displayScale)) {
        emit backgroundClicked();
        event->accept();
        return;
    }

    if (auto fp = widgetToFramePoint(event->position(), targetRect, displayScale, frameSize_)) {
        emit frameClicked(*fp);
    } else {
        emit backgroundClicked();
    }
    event->accept();
}

void CameraPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor("#0b1120"));

    if (frameImage_.isNull() || frameSize_.isEmpty()) {
        painter.setPen(QColor("#dbe4ee"));
        painter.drawText(rect(), Qt::AlignCenter, "Aucune image camera");
        return;
    }

    QRectF targetRect;
    double displayScale = 1.0;
    if (!computeDisplayGeometry(targetRect, displayScale)) {
        painter.setPen(QColor("#dbe4ee"));
        painter.drawText(rect(), Qt::AlignCenter, "Aucune image camera");
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (!scaledCacheValid_) {
        rebuildScaledCache();
    }
    if (scaledCacheValid_) {
        painter.drawPixmap(scaledCacheRect_.topLeft().toPoint(), scaledPixmap_);
    } else {
        painter.drawImage(targetRect, frameImage_);
    }

    painter.setPen(QColor(255, 255, 255, 72));
    painter.drawRect(targetRect.adjusted(0.0, 0.0, -1.0, -1.0));

    painter.setRenderHint(QPainter::Antialiasing, true);



    // Draw sequence zone rect
    if (sequenceStartVisible_) {
        const QPointF startPoint(
            targetRect.left() + sequenceStartPointPx_.x() * displayScale,
            targetRect.top() + sequenceStartPointPx_.y() * displayScale
        );
        const QPen sequencePen(QColor(0, 255, 128), 2.0);
        const QPen sequenceFillPen(QColor(0, 90, 40), 1.0);

        if (!sequenceEndVisible_ || sequenceEndPointPx_ == sequenceStartPointPx_) {
            painter.setPen(sequencePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(startPoint, 5.0, 5.0);
        } else {
            const QPointF endPoint(
                targetRect.left() + sequenceEndPointPx_.x() * displayScale,
                targetRect.top() + sequenceEndPointPx_.y() * displayScale
            );
            const QRectF sequenceRect(
                std::min(startPoint.x(), endPoint.x()),
                std::min(startPoint.y(), endPoint.y()),
                std::abs(endPoint.x() - startPoint.x()),
                std::abs(endPoint.y() - startPoint.y())
            );
            painter.setPen(sequencePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(sequenceRect);
            painter.setBrush(QColor(0, 255, 128));
            painter.setPen(sequenceFillPen);
            painter.drawEllipse(startPoint, 4.0, 4.0);
            painter.drawEllipse(endPoint, 4.0, 4.0);

            if (!sequenceSizeText_.isEmpty()) {
                QFont labelFont;
                labelFont.setPointSize(10);
                labelFont.setBold(true);
                painter.setFont(labelFont);
                const QFontMetricsF fm(labelFont);
                const QRectF textRect = fm.boundingRect(
                    QRectF(0, 0, 220, 80),
                    Qt::AlignLeft | Qt::TextWordWrap,
                    sequenceSizeText_);
                const double padH = 8.0;
                const double padV = 5.0;
                const double bw = textRect.width() + padH * 2.0;
                const double bh = textRect.height() + padV * 2.0;

                double left = sequenceRect.right() + 8.0;
                if (left + bw > width() - 4.0) {
                    left = sequenceRect.left() - bw - 8.0;
                }
                left = std::clamp(left, 4.0, std::max(4.0, static_cast<double>(width()) - bw - 4.0));
                const double top = std::clamp(
                    sequenceRect.top(),
                    4.0,
                    std::max(4.0, static_cast<double>(height()) - bh - 4.0));
                const QRectF bg(left, top, bw, bh);

                painter.setBrush(QColor(0, 0, 0, 170));
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(bg, 4.0, 4.0);
                painter.setPen(QColor(0, 255, 128));
                painter.drawText(bg.adjusted(padH, padV, -padH, -padV),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 sequenceSizeText_);
            }
        }
    }

    // Draw waypoint dots (done = red, remaining = green)
    if (waypointOverlayVisible_) {
        constexpr double kDotRadius = 3.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(220, 50, 50));
        for (const QPointF& pt : waypointsDonePx_) {
            painter.drawEllipse(
                QPointF(targetRect.left() + pt.x() * displayScale, targetRect.top() + pt.y() * displayScale),
                kDotRadius, kDotRadius
            );
        }
        painter.setBrush(QColor(50, 200, 80));
        for (const QPointF& pt : waypointsRemainingPx_) {
            painter.drawEllipse(
                QPointF(targetRect.left() + pt.x() * displayScale, targetRect.top() + pt.y() * displayScale),
                kDotRadius, kDotRadius
            );
        }
    }

    // ── Draw ruler overlay ───────────────────────────────────────────────────
    if (rulerP1Visible_) {
        const QColor kRulerColor(255, 220, 0);     // bright yellow
        const QColor kRulerShadow(0, 0, 0, 180);

        const QPointF p1s(
            targetRect.left() + rulerP1Px_.x() * displayScale,
            targetRect.top()  + rulerP1Px_.y() * displayScale
        );

        // Endpoint P1: crosshair + dot
        painter.setPen(QPen(QColor(0, 0, 0, 120), 3.0));
        painter.drawLine(QPointF(p1s.x() - 9, p1s.y()), QPointF(p1s.x() + 9, p1s.y()));
        painter.drawLine(QPointF(p1s.x(), p1s.y() - 9), QPointF(p1s.x(), p1s.y() + 9));
        painter.setPen(QPen(kRulerColor, 1.5));
        painter.drawLine(QPointF(p1s.x() - 8, p1s.y()), QPointF(p1s.x() + 8, p1s.y()));
        painter.drawLine(QPointF(p1s.x(), p1s.y() - 8), QPointF(p1s.x(), p1s.y() + 8));
        painter.setBrush(kRulerColor);
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
        painter.drawEllipse(p1s, 3.5, 3.5);

        if (rulerP2Visible_) {
            const QPointF p2s(
                targetRect.left() + rulerP2Px_.x() * displayScale,
                targetRect.top()  + rulerP2Px_.y() * displayScale
            );

            // Line with shadow
            painter.setPen(QPen(QColor(0, 0, 0, 140), 3.5));
            painter.drawLine(p1s, p2s);
            painter.setPen(QPen(kRulerColor, 2.0));
            painter.drawLine(p1s, p2s);

            // Perpendicular tick marks at both endpoints
            const QPointF dir = p2s - p1s;
            const double len = std::hypot(dir.x(), dir.y());
            if (len > 1.0) {
                const QPointF perp(-dir.y() / len * 7, dir.x() / len * 7);
                painter.setPen(QPen(QColor(0, 0, 0, 120), 3.0));
                painter.drawLine(p1s - perp, p1s + perp);
                painter.drawLine(p2s - perp, p2s + perp);
                painter.setPen(QPen(kRulerColor, 2.0));
                painter.drawLine(p1s - perp, p1s + perp);
                painter.drawLine(p2s - perp, p2s + perp);
            }

            // Endpoint P2 dot
            painter.setBrush(kRulerColor);
            painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
            painter.drawEllipse(p2s, 3.5, 3.5);

            // Distance label at midpoint
            if (!rulerDistanceText_.isEmpty()) {
                const QPointF mid = (p1s + p2s) * 0.5;

                QFont labelFont;
                labelFont.setPointSize(10);
                labelFont.setBold(true);
                painter.setFont(labelFont);
                const QFontMetricsF fm(labelFont);
                const QRectF textBr = fm.boundingRect(rulerDistanceText_);

                const double padH = 6.0, padV = 3.0;
                const double bw = textBr.width()  + padH * 2;
                const double bh = textBr.height() + padV * 2;
                // Position above the midpoint
                const QRectF bg(mid.x() - bw * 0.5, mid.y() - bh - 6.0, bw, bh);

                painter.setBrush(kRulerShadow);
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(bg, 4.0, 4.0);
                painter.setPen(kRulerColor);
                painter.drawText(bg, Qt::AlignCenter, rulerDistanceText_);
            }
        }
    }

    // ── Helper lambda: draw a labelled annotation ─────────────────────────────
    const auto drawLabel = [&](const QPointF& pos, const QString& text,
                                const QColor& color, double offsetY = -8.0) {
        if (text.isEmpty()) return;
        QFont f; f.setPointSize(10); f.setBold(true);
        painter.setFont(f);
        const QFontMetricsF fm(f);
        const QRectF tbr = fm.boundingRect(text);
        const double pw = tbr.width() + 10, ph = tbr.height() + 4;
        QRectF bg(pos.x() - pw * 0.5, pos.y() + offsetY - ph, pw, ph);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(bg, 4, 4);
        painter.setPen(color);
        painter.drawText(bg, Qt::AlignCenter, text);
    };

    // ── Draw circle overlay ───────────────────────────────────────────────────
    if (circleCenterVisible_) {
        const QColor kCircleColor(80, 200, 255);  // cyan-blue
        const QPointF cs(
            targetRect.left() + circleCenterPx_.x() * displayScale,
            targetRect.top()  + circleCenterPx_.y() * displayScale);

        // Center crosshair
        painter.setPen(QPen(QColor(0,0,0,120), 3.0));
        painter.drawLine(QPointF(cs.x()-9, cs.y()), QPointF(cs.x()+9, cs.y()));
        painter.drawLine(QPointF(cs.x(), cs.y()-9), QPointF(cs.x(), cs.y()+9));
        painter.setPen(QPen(kCircleColor, 1.5));
        painter.drawLine(QPointF(cs.x()-8, cs.y()), QPointF(cs.x()+8, cs.y()));
        painter.drawLine(QPointF(cs.x(), cs.y()-8), QPointF(cs.x(), cs.y()+8));
        painter.setBrush(kCircleColor);
        painter.setPen(QPen(QColor(0,0,0,160), 1.0));
        painter.drawEllipse(cs, 3.5, 3.5);

        if (circleEdgeVisible_) {
            const QPointF es(
                targetRect.left() + circleEdgePx_.x() * displayScale,
                targetRect.top()  + circleEdgePx_.y() * displayScale);
            const double rx = es.x() - cs.x();
            const double ry = es.y() - cs.y();
            const double r  = std::hypot(rx, ry);

            // Circle + dashed radius line
            painter.setPen(QPen(QColor(0,0,0,140), 3.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(cs, r, r);
            painter.setPen(QPen(kCircleColor, 2.0));
            painter.drawEllipse(cs, r, r);

            QPen dashPen(kCircleColor, 1.5, Qt::DashLine);
            painter.setPen(dashPen);
            painter.drawLine(cs, es);

            painter.setBrush(kCircleColor);
            painter.setPen(QPen(QColor(0,0,0,160), 1.0));
            painter.drawEllipse(es, 3.5, 3.5);

            // Label at top of circle
            drawLabel(QPointF(cs.x(), cs.y() - r), circleDiameterText_, kCircleColor, -4.0);
        }
    }

    // ── Draw rectangle overlay ────────────────────────────────────────────────
    if (rectP1Visible_) {
        const QColor kRectColor(255, 140, 0);  // orange
        const QPointF p1s(
            targetRect.left() + rectP1Px_.x() * displayScale,
            targetRect.top()  + rectP1Px_.y() * displayScale);

        // Corner P1 dot
        painter.setBrush(kRectColor);
        painter.setPen(QPen(QColor(0,0,0,160), 1.0));
        painter.drawEllipse(p1s, 3.5, 3.5);

        if (rectP2Visible_) {
            const QPointF p2s(
                targetRect.left() + rectP2Px_.x() * displayScale,
                targetRect.top()  + rectP2Px_.y() * displayScale);

            const QRectF rect(
                std::min(p1s.x(), p2s.x()), std::min(p1s.y(), p2s.y()),
                std::abs(p2s.x() - p1s.x()), std::abs(p2s.y() - p1s.y()));

            painter.setPen(QPen(QColor(0,0,0,140), 3.5));
            painter.setBrush(QColor(255, 140, 0, 30));
            painter.drawRect(rect);
            painter.setPen(QPen(kRectColor, 2.0));
            painter.setBrush(QColor(255, 140, 0, 30));
            painter.drawRect(rect);

            // Corner dots
            painter.setBrush(kRectColor);
            painter.setPen(QPen(QColor(0,0,0,160), 1.0));
            for (const QPointF& corner : {p1s, p2s,
                    QPointF(p1s.x(), p2s.y()), QPointF(p2s.x(), p1s.y())}) {
                painter.drawEllipse(corner, 3.5, 3.5);
            }

            // Label at center of rectangle
            if (!rectSizeText_.isEmpty()) {
                const QPointF mid = rect.center();
                drawLabel(mid, rectSizeText_, kRectColor, -4.0);
            }
        }
    }

    // Draw laser overlay
    if (laserOverlayVisible_) {
        const QPointF center(
            targetRect.left() + laserPointPx_.x() * displayScale,
            targetRect.top() + laserPointPx_.y() * displayScale
        );
        const double radius = std::max(1.0, static_cast<double>(laserRadiusPx_) * displayScale);
        const double cross = std::max(4.0, std::min(10.0, radius * 0.5));

        QPen laserPen(QColor(255, 0, 0), 2.0);
        painter.setPen(laserPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);

        QPen crossPen(QColor(255, 255, 255), 1.0);
        painter.setPen(crossPen);
        painter.drawLine(QPointF(center.x() - cross, center.y()), QPointF(center.x() + cross, center.y()));
        painter.drawLine(QPointF(center.x(), center.y() - cross), QPointF(center.x(), center.y() + cross));

        painter.setPen(laserPen);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawEllipse(center, 2.0, 2.0);
    }
}

void CameraPreviewWidget::wheelEvent(QWheelEvent* event)
{
    const double delta = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    const double newZoom = std::clamp(zoomFactor_ * delta, 1.0, 12.0);
    if (qFuzzyCompare(newZoom, zoomFactor_)) { event->accept(); return; }

    // Adjust panOffset so the pixel under the mouse stays fixed
    QRectF oldRect;
    double oldScale = 1.0;
    if (computeDisplayGeometry(oldRect, oldScale) && oldScale > 0.0) {
        const QPointF mousePos = event->position();
        // Frame pixel under the mouse (in old geometry)
        const double fpx = (mousePos.x() - oldRect.left()) / oldScale;
        const double fpy = (mousePos.y() - oldRect.top())  / oldScale;

        // New display scale = coverScale * newZoom
        const double newScale = (oldScale / zoomFactor_) * newZoom;

        // Where the new image origin (topLeft) must be so fp stays at mousePos
        const double newLeft = mousePos.x() - fpx * newScale;
        const double newTop  = mousePos.y() - fpy * newScale;

        // Where the centered origin would be at the new scale
        const double centeredLeft = (width()  - frameSize_.width()  * newScale) * 0.5;
        const double centeredTop  = (height() - frameSize_.height() * newScale) * 0.5;

        panOffset_.setX(newLeft - centeredLeft);
        panOffset_.setY(newTop  - centeredTop);
    }

    setZoomFactor(newZoom);  // clamps panOffset and triggers repaint
    event->accept();
}

void CameraPreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    clampPanOffset();
    rebuildScaledCache();
}

void CameraPreviewWidget::invalidateScaledCache()
{
    scaledCacheValid_ = false;
}

void CameraPreviewWidget::rebuildScaledCache()
{
    scaledCacheValid_ = false;
    if (frameImage_.isNull()) {
        return;
    }

    QRectF targetRect;
    double displayScale = 1.0;
    if (!computeDisplayGeometry(targetRect, displayScale) || displayScale <= 0.0) {
        return;
    }

    // Skip caching when zoomed in so much the pixmap would be huge.
    if (targetRect.width() > width() * 3 || targetRect.height() > height() * 3) {
        return;
    }

    const QSize targetSizeInt(
        std::max(1, static_cast<int>(std::lround(targetRect.width()))),
        std::max(1, static_cast<int>(std::lround(targetRect.height())))
    );

    scaledPixmap_ = QPixmap::fromImage(
        frameImage_.scaled(targetSizeInt, Qt::IgnoreAspectRatio, Qt::FastTransformation)
    );
    scaledCacheRect_ = targetRect;
    scaledCacheValid_ = true;
}

void CameraPreviewWidget::clampPanOffset()
{
    if (frameSize_.isEmpty()) { panOffset_ = QPointF(0.0, 0.0); return; }
    const double coverSc = std::max(
        static_cast<double>(width())  / static_cast<double>(frameSize_.width()),
        static_cast<double>(height()) / static_cast<double>(frameSize_.height())
    );
    const double s    = coverSc * zoomFactor_;
    const double maxX = std::max(0.0, (frameSize_.width()  * s - width())  * 0.5);
    const double maxY = std::max(0.0, (frameSize_.height() * s - height()) * 0.5);
    panOffset_.setX(std::clamp(panOffset_.x(), -maxX, maxX));
    panOffset_.setY(std::clamp(panOffset_.y(), -maxY, maxY));
}

void CameraPreviewWidget::updateZoom(double deltaFactor)
{
    setZoomFactor(zoomFactor_ * deltaFactor);
}

}  // namespace laserbench::ui
