#pragma once

#include <QImage>
#include <QPointF>
#include <QPixmap>
#include <QWidget>

namespace laserbench::ui {

class CameraPreviewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit CameraPreviewWidget(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void setLaserOverlay(const QPointF& pointPx, int radiusPx, bool visible = true);
    void clearLaserOverlay();
    void setZoomFactor(double zoomFactor);
    double zoomFactor() const;
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void zoomFactorChanged(double zoomFactor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateZoom(double deltaFactor);

    QPixmap framePixmap_;
    QSize frameSize_;
    QPointF laserPointPx_;
    int laserRadiusPx_ {0};
    bool laserOverlayVisible_ {false};
    double zoomFactor_ {1.0};
};

}  // namespace laserbench::ui
