#include "ui/CameraPreviewWidget.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QWheelEvent>

#include <algorithm>

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
    framePixmap_ = QPixmap::fromImage(frame);
    frameSize_ = frame.size();
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

void CameraPreviewWidget::setZoomFactor(double zoomFactor)
{
    const double bounded = std::clamp(zoomFactor, 1.0, 12.0);
    if (qFuzzyCompare(zoomFactor_, bounded)) {
        return;
    }

    zoomFactor_ = bounded;
    emit zoomFactorChanged(zoomFactor_);
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

void CameraPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor("#0b1120"));

    if (framePixmap_.isNull() || frameSize_.isEmpty()) {
        painter.setPen(QColor("#dbe4ee"));
        painter.drawText(rect(), Qt::AlignCenter, "Aucune image camera");
        return;
    }

    const QSize available = size();
    const double coverScale = std::max(
        static_cast<double>(available.width()) / static_cast<double>(frameSize_.width()),
        static_cast<double>(available.height()) / static_cast<double>(frameSize_.height())
    );
    const double displayScale = coverScale * zoomFactor_;
    const QSize targetSize(
        static_cast<int>(frameSize_.width() * displayScale),
        static_cast<int>(frameSize_.height() * displayScale)
    );
    const QRect targetRect(
        (width() - targetSize.width()) / 2,
        (height() - targetSize.height()) / 2,
        targetSize.width(),
        targetSize.height()
    );

    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawPixmap(targetRect, framePixmap_);

    painter.setPen(QColor(255, 255, 255, 72));
    painter.drawRect(targetRect.adjusted(0, 0, -1, -1));

    if (!laserOverlayVisible_) {
        return;
    }

    const QPointF center(
        static_cast<double>(targetRect.left()) + (laserPointPx_.x() * displayScale),
        static_cast<double>(targetRect.top()) + (laserPointPx_.y() * displayScale)
    );
    const double radius = std::max(1.0, static_cast<double>(laserRadiusPx_) * displayScale);
    const double cross = std::max(4.0, std::min(10.0, radius * 0.5));

    painter.setRenderHint(QPainter::Antialiasing, true);

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

void CameraPreviewWidget::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() > 0) {
        zoomIn();
    } else if (event->angleDelta().y() < 0) {
        zoomOut();
    }
    event->accept();
}

void CameraPreviewWidget::updateZoom(double deltaFactor)
{
    setZoomFactor(zoomFactor_ * deltaFactor);
}

}  // namespace laserbench::ui
