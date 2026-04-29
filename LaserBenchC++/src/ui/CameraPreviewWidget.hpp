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
    void setSequenceOverlay(const QPointF& startPointPx, bool hasStartPoint, const QPointF& endPointPx, bool hasEndPoint,
                            const QString& sizeText = {});
    void clearSequenceOverlay();
    void setWaypointOverlay(std::vector<QPointF> donePx, std::vector<QPointF> remainingPx);
    void clearWaypointOverlay();
    // Ruler overlay: p1 always set first, p2 optional, distanceText shown at midpoint
    void setRulerOverlay(const QPointF& p1Px, bool hasP2, const QPointF& p2Px,
                         const QString& distanceText);
    void clearRulerOverlay();
    // Circle overlay: center + optional edge point, diameterText shown at center
    void setCircleOverlay(const QPointF& centerPx, bool hasEdge, const QPointF& edgePx,
                          const QString& diameterText);
    void clearCircleOverlay();
    // Rectangle overlay: corner1 + optional corner2, sizeText shown at center
    void setRectOverlay(const QPointF& p1Px, bool hasP2, const QPointF& p2Px,
                        const QString& sizeText);
    void clearRectOverlay();
    void setZoomFactor(double zoomFactor);
    double zoomFactor() const;
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void frameClicked(const QPoint& framePointPx);
    void backgroundClicked();
    void zoomFactorChanged(double zoomFactor);
    // Emitted when the mouse moves over the image area (frame-pixel coords)
    void frameCursorMoved(const QPoint& framePointPx);
    // Emitted when the mouse leaves the image area
    void frameCursorLeft();
    // Emitted on double-click inside the image (frame-pixel coords)
    void frameDoubleClicked(const QPoint& framePointPx);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
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
    QString sequenceSizeText_;
    std::vector<QPointF> waypointsDonePx_;
    std::vector<QPointF> waypointsRemainingPx_;
    bool waypointOverlayVisible_ {false};
    QPointF rulerP1Px_;
    QPointF rulerP2Px_;
    bool rulerP1Visible_ {false};
    bool rulerP2Visible_ {false};
    QString rulerDistanceText_;
    QPointF circleCenterPx_;
    QPointF circleEdgePx_;
    bool circleCenterVisible_ {false};
    bool circleEdgeVisible_ {false};
    QString circleDiameterText_;
    QPointF rectP1Px_;
    QPointF rectP2Px_;
    bool rectP1Visible_ {false};
    bool rectP2Visible_ {false};
    QString rectSizeText_;
    double zoomFactor_ {1.0};
    QPointF panOffset_ {0.0, 0.0};
    void clampPanOffset();
};

}  // namespace laserbench::ui
