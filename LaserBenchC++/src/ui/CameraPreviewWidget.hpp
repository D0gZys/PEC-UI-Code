#pragma once

#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <vector>

QT_BEGIN_NAMESPACE
class QMouseEvent;
class QResizeEvent;
QT_END_NAMESPACE

namespace laserbench::ui {

class CameraPreviewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit CameraPreviewWidget(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void setLaserOverlay(const QPointF& pointPx, int radiusPx, bool visible = true);
    void clearLaserOverlay();
    void setSequenceOverlay(const QPointF& startPointPx, bool hasStartPoint, const QPointF& endPointPx, bool hasEndPoint);
    void clearSequenceOverlay();
    void setWaypointOverlay(std::vector<QPointF> donePx, std::vector<QPointF> remainingPx);
    void clearWaypointOverlay();
    void setZoomFactor(double zoomFactor);
    double zoomFactor() const;
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void frameClicked(const QPoint& framePointPx);
    void backgroundClicked();
    void zoomFactorChanged(double zoomFactor);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    bool computeDisplayGeometry(QRectF& targetRect, double& displayScale) const;
    void updateZoom(double deltaFactor);
    void rebuildScaledCache();
    void invalidateScaledCache();

    QImage frameImage_;
    QSize frameSize_;
    QPixmap scaledPixmap_;
    QRectF scaledCacheRect_;
    bool scaledCacheValid_ {false};
    QPointF laserPointPx_;
    int laserRadiusPx_ {0};
    bool laserOverlayVisible_ {false};
    QPointF sequenceStartPointPx_;
    QPointF sequenceEndPointPx_;
    bool sequenceStartVisible_ {false};
    bool sequenceEndVisible_ {false};
    std::vector<QPointF> waypointsDonePx_;
    std::vector<QPointF> waypointsRemainingPx_;
    bool waypointOverlayVisible_ {false};
    double zoomFactor_ {1.0};
};

}  // namespace laserbench::ui
