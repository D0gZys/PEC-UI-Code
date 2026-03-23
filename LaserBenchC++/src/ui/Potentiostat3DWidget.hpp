#pragma once

#include <QWidget>
#include <optional>
#include <vector>

namespace laserbench::ui {

class Potentiostat3DWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit Potentiostat3DWidget(QWidget* parent = nullptr);

    // Same data format as PotentiostatHeatmapWidget::setGrid
    // values: row-major [row * cols + col], may be nullopt for unmeasured cells
    void setGrid(int rows, int cols,
                 std::vector<std::optional<double>> values,
                 double xSpanMm, double ySpanMm);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QPointF project(double col, double row, double z) const;

    int    rows_     {0};
    int    cols_     {0};
    double xSpanMm_  {1.0};
    double ySpanMm_  {1.0};
    std::vector<std::optional<double>> values_;

    // View parameters
    double azimuth_   {0.7854};  // 45° in radians
    double elevation_ {0.5236};  // 30° in radians
    double zoom_      {1.0};

    // Mouse drag state
    bool   dragging_       {false};
    QPoint dragStart_;
    double azAtDragStart_  {0.0};
    double elAtDragStart_  {0.5236};

    // Projection state (updated each paintEvent)
    mutable QPointF origin_;
    mutable double  scaleH_ {1.0};
    mutable double  scaleV_ {1.0};
};

}  // namespace laserbench::ui
