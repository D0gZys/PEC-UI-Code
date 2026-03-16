#include "ui/CameraPreviewWidget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace laserbench::ui {

CameraPreviewWidget::CameraPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(520, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
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

void CameraPreviewWidget::setSequenceOverlay(const QPointF& startPointPx, bool hasStartPoint, const QPointF& endPointPx, bool hasEndPoint)
{
    if (sequenceStartVisible_ == hasStartPoint
        && sequenceEndVisible_ == hasEndPoint
        && sequenceStartPointPx_ == startPointPx
        && sequenceEndPointPx_ == endPointPx) {
        return;
    }

    sequenceStartPointPx_ = startPointPx;
    sequenceEndPointPx_ = endPointPx;
    sequenceStartVisible_ = hasStartPoint;
    sequenceEndVisible_ = hasEndPoint;
    update();
}

void CameraPreviewWidget::clearSequenceOverlay()
{
    if (!sequenceStartVisible_ && !sequenceEndVisible_) {
        return;
    }

    sequenceStartVisible_ = false;
    sequenceEndVisible_ = false;
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

void CameraPreviewWidget::setZoomFactor(double zoomFactor)
{
    const double bounded = std::clamp(zoomFactor, 1.0, 12.0);
    if (qFuzzyCompare(zoomFactor_, bounded)) {
        return;
    }

    zoomFactor_ = bounded;
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
        (static_cast<double>(width()) - targetSize.width()) * 0.5,
        (static_cast<double>(height()) - targetSize.height()) * 0.5,
        targetSize.width(),
        targetSize.height()
    );
    return true;
}

void CameraPreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event == nullptr || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    QRectF targetRect;
    double displayScale = 1.0;
    if (!computeDisplayGeometry(targetRect, displayScale) || displayScale <= 0.0) {
        emit backgroundClicked();
        event->accept();
        return;
    }

    const QPointF position = event->position();
    if (!targetRect.contains(position)) {
        emit backgroundClicked();
        event->accept();
        return;
    }

    const int frameX = std::clamp(
        static_cast<int>(std::lround((position.x() - targetRect.left()) / displayScale)),
        0,
        frameSize_.width() - 1
    );
    const int frameY = std::clamp(
        static_cast<int>(std::lround((position.y() - targetRect.top()) / displayScale)),
        0,
        frameSize_.height() - 1
    );

    emit frameClicked(QPoint(frameX, frameY));
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
    if (event->angleDelta().y() > 0) {
        zoomIn();
    } else if (event->angleDelta().y() < 0) {
        zoomOut();
    }
    event->accept();
}

void CameraPreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
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

void CameraPreviewWidget::updateZoom(double deltaFactor)
{
    setZoomFactor(zoomFactor_ * deltaFactor);
}

}  // namespace laserbench::ui
