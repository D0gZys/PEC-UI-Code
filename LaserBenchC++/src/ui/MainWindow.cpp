#include "ui/MainWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QThread>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <limits>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <map>
#include <numeric>
#include <thread>
#include <variant>

#include "ui/CameraPreviewWidget.hpp"
#include "ui/PotentiostatGraphWidget.hpp"
#include "ui/PotentiostatHeatmapWidget.hpp"
#include "ui/Potentiostat3DWidget.hpp"
#include "hardware/BioLogicController.hpp"
#include "hardware/MockHardware.hpp"
#include "hardware/ThorlabsCameraController.hpp"

namespace laserbench::ui {

namespace {

constexpr double kCameraPixelPitchUm = 3.45;
constexpr double kDefaultGotoMmPerPx = 0.001;
constexpr int kDefaultMotorTimeoutMs = 30000;
constexpr double kOverlayPredictionEpsilonMm = 0.002;
constexpr double kOverlayStabilityDeadbandMm = 0.00075;
constexpr double kScanPlanningEpsilonMm = 1e-9;
constexpr double kContinuousPollingPeriodS = 0.002;
constexpr double kContinuousGuaranteedSamplePeriodS = 0.02;
constexpr double kContinuousFreshValueTimeoutS = 0.25;
constexpr double kContinuousRunUpMm = 0.04;

QGroupBox* createGroupBox(const QString& title)
{
    auto* box = new QGroupBox(title);
    box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    return box;
}

QFrame* createPlaceholderPanel(const QString& title, const QString& body)
{
    auto* frame = new QFrame;
    frame->setObjectName("placeholderPanel");
    frame->setFrameShape(QFrame::NoFrame);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName("panelTitle");

    auto* bodyLabel = new QLabel(body);
    bodyLabel->setWordWrap(true);
    bodyLabel->setObjectName("panelBody");

    layout->addWidget(titleLabel);
    layout->addWidget(bodyLabel);
    layout->addStretch(1);

    return frame;
}

QPushButton* createActionButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setMinimumHeight(34);
    return button;
}

using RectangleStartCorner = MainWindow::ScanConfig::RectangleStartCorner;
using RectanglePrimaryAxis = MainWindow::ScanConfig::RectanglePrimaryAxis;

QString rectangleStartCornerLabel(RectangleStartCorner corner)
{
    switch (corner) {
    case RectangleStartCorner::TopLeft:
        return "Coin superieur gauche";
    case RectangleStartCorner::TopRight:
        return "Coin superieur droit";
    case RectangleStartCorner::BottomLeft:
        return "Coin inferieur gauche";
    case RectangleStartCorner::BottomRight:
        return "Coin inferieur droit";
    }

    return "Coin inconnu";
}

QString rectanglePrimaryAxisLabel(RectanglePrimaryAxis axis)
{
    switch (axis) {
    case RectanglePrimaryAxis::Horizontal:
        return "Gauche-droite";
    case RectanglePrimaryAxis::Vertical:
        return "Haut-bas";
    }

    return "Axe inconnu";
}

bool rectangleStartCornerStartsTop(RectangleStartCorner corner)
{
    return corner == RectangleStartCorner::TopLeft || corner == RectangleStartCorner::TopRight;
}

bool rectangleStartCornerStartsLeft(RectangleStartCorner corner)
{
    return corner == RectangleStartCorner::TopLeft || corner == RectangleStartCorner::BottomLeft;
}

RectangleStartCorner rectangleStartCornerFromEdges(bool startsTop, bool startsLeft)
{
    if (startsTop) {
        return startsLeft ? RectangleStartCorner::TopLeft : RectangleStartCorner::TopRight;
    }
    return startsLeft ? RectangleStartCorner::BottomLeft : RectangleStartCorner::BottomRight;
}

RectangleStartCorner legacyRectangleStartCornerForScale(double scaleX, double scaleY)
{
    const bool startsLeft = scaleX > 0.0;
    const bool startsTop = scaleY > 0.0;
    return rectangleStartCornerFromEdges(startsTop, startsLeft);
}

RectanglePrimaryAxis legacyRectanglePrimaryAxis()
{
    return RectanglePrimaryAxis::Horizontal;
}

struct RectangleTraversalSelection
{
    RectangleStartCorner startCorner {RectangleStartCorner::TopLeft};
    RectanglePrimaryAxis primaryAxis {RectanglePrimaryAxis::Horizontal};
};

struct GridTraversalEntry
{
    int row {0};
    int col {0};
};

std::vector<GridTraversalEntry> buildRectangleTraversalEntries(
    int rows,
    int cols,
    RectangleStartCorner startCorner,
    RectanglePrimaryAxis primaryAxis)
{
    std::vector<GridTraversalEntry> order;
    if (rows <= 0 || cols <= 0) {
        return order;
    }

    const bool startsTop = rectangleStartCornerStartsTop(startCorner);
    const bool startsLeft = rectangleStartCornerStartsLeft(startCorner);

    order.reserve(static_cast<std::size_t>(rows * cols));
    if (primaryAxis == RectanglePrimaryAxis::Horizontal) {
        for (int rowOffset = 0; rowOffset < rows; ++rowOffset) {
            const int visualRow = startsTop ? rowOffset : (rows - 1 - rowOffset);
            const bool leftToRight = (rowOffset % 2) == 0 ? startsLeft : !startsLeft;
            if (leftToRight) {
                for (int col = 0; col < cols; ++col) {
                    order.push_back({visualRow, col});
                }
            } else {
                for (int col = cols - 1; col >= 0; --col) {
                    order.push_back({visualRow, col});
                }
            }
        }
        return order;
    }

    for (int colOffset = 0; colOffset < cols; ++colOffset) {
        const int visualCol = startsLeft ? colOffset : (cols - 1 - colOffset);
        const bool topToBottom = (colOffset % 2) == 0 ? startsTop : !startsTop;
        if (topToBottom) {
            for (int row = 0; row < rows; ++row) {
                order.push_back({row, visualCol});
            }
        } else {
            for (int row = rows - 1; row >= 0; --row) {
                order.push_back({row, visualCol});
            }
        }
    }

    return order;
}

class RectangleTraversalPreviewWidget final : public QWidget
{
public:
    explicit RectangleTraversalPreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(320, 320);
        setMouseTracking(true);
    }

    void setSelection(RectangleTraversalSelection selection)
    {
        selection_ = selection;
        update();
    }

    RectangleTraversalSelection selection() const
    {
        return selection_;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().window());

        const QRectF zoneRect = previewZoneRect();
        painter.setPen(QPen(QColor("#111927"), 3.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(zoneRect);

        drawPreviewPath(painter, zoneRect);

        options_ = buildOptions(zoneRect);
        for (const PreviewOption& option : options_) {
            const bool selected = option.startCorner == selection_.startCorner
                && option.primaryAxis == selection_.primaryAxis;
            const bool hovered = option.hitRect.contains(lastMousePos_);
            drawArrowButton(painter, option, selected, hovered);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        const QPointF pos = event->position();
        for (const PreviewOption& option : options_) {
            if (option.hitRect.contains(pos)) {
                selection_ = {option.startCorner, option.primaryAxis};
                update();
                event->accept();
                return;
            }
        }

        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        lastMousePos_ = event->position();
        update();
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        lastMousePos_ = QPointF(-1000.0, -1000.0);
        update();
        QWidget::leaveEvent(event);
    }

private:
    struct PreviewOption
    {
        RectangleStartCorner startCorner;
        RectanglePrimaryAxis primaryAxis;
        QPointF center;
        QPointF direction;
        QRectF hitRect;
    };

    QRectF previewZoneRect() const
    {
        const QRectF bounds = rect().adjusted(24, 24, -24, -24);
        const double side = std::min(bounds.width(), bounds.height()) - 96.0;
        const double clampedSide = std::max(140.0, side);
        return QRectF(
            bounds.center().x() - clampedSide / 2.0,
            bounds.center().y() - clampedSide / 2.0,
            clampedSide,
            clampedSide);
    }

    std::vector<PreviewOption> buildOptions(const QRectF& zoneRect) const
    {
        const double arrowOffset = 42.0;
        const double hitHalfSize = 24.0;
        const QPointF topLeft = zoneRect.topLeft();
        const QPointF topRight = zoneRect.topRight();
        const QPointF bottomLeft = zoneRect.bottomLeft();
        const QPointF bottomRight = zoneRect.bottomRight();

        auto makeOption = [hitHalfSize](RectangleStartCorner corner, RectanglePrimaryAxis axis, QPointF center, QPointF direction) {
            return PreviewOption {
                corner,
                axis,
                center,
                direction,
                QRectF(center.x() - hitHalfSize, center.y() - hitHalfSize, hitHalfSize * 2.0, hitHalfSize * 2.0)
            };
        };

        return {
            makeOption(RectangleStartCorner::TopLeft, RectanglePrimaryAxis::Vertical, QPointF(topLeft.x() + 22.0, topLeft.y() - arrowOffset), QPointF(0.0, 1.0)),
            makeOption(RectangleStartCorner::TopLeft, RectanglePrimaryAxis::Horizontal, QPointF(topLeft.x() - arrowOffset, topLeft.y() + 22.0), QPointF(1.0, 0.0)),
            makeOption(RectangleStartCorner::TopRight, RectanglePrimaryAxis::Vertical, QPointF(topRight.x() - 22.0, topRight.y() - arrowOffset), QPointF(0.0, 1.0)),
            makeOption(RectangleStartCorner::TopRight, RectanglePrimaryAxis::Horizontal, QPointF(topRight.x() + arrowOffset, topRight.y() + 22.0), QPointF(-1.0, 0.0)),
            makeOption(RectangleStartCorner::BottomLeft, RectanglePrimaryAxis::Horizontal, QPointF(bottomLeft.x() - arrowOffset, bottomLeft.y() - 22.0), QPointF(1.0, 0.0)),
            makeOption(RectangleStartCorner::BottomLeft, RectanglePrimaryAxis::Vertical, QPointF(bottomLeft.x() + 22.0, bottomLeft.y() + arrowOffset), QPointF(0.0, -1.0)),
            makeOption(RectangleStartCorner::BottomRight, RectanglePrimaryAxis::Horizontal, QPointF(bottomRight.x() + arrowOffset, bottomRight.y() - 22.0), QPointF(-1.0, 0.0)),
            makeOption(RectangleStartCorner::BottomRight, RectanglePrimaryAxis::Vertical, QPointF(bottomRight.x() - 22.0, bottomRight.y() + arrowOffset), QPointF(0.0, -1.0)),
        };
    }

    void drawArrowButton(QPainter& painter, const PreviewOption& option, bool selected, bool hovered) const
    {
        const QColor stroke = selected ? QColor("#1f6feb") : (hovered ? QColor("#2563eb") : QColor("#111927"));
        const QColor fill = selected ? QColor("#dbeafe") : (hovered ? QColor("#eff6ff") : QColor("#ffffff"));

        painter.save();
        painter.setPen(QPen(stroke, 2.5));
        painter.setBrush(fill);
        painter.drawRoundedRect(option.hitRect, 10.0, 10.0);

        const QPointF unit = option.direction;
        const QPointF perpendicular(-unit.y(), unit.x());
        const QPointF tailCenter = option.center - unit * 6.0;
        const QPointF headBase = option.center + unit * 6.0;

        QPolygonF polygon;
        polygon << (tailCenter - perpendicular * 5.0 - unit * 8.0)
                << (tailCenter + perpendicular * 5.0 - unit * 8.0)
                << (tailCenter + perpendicular * 5.0)
                << (headBase + perpendicular * 8.0)
                << (headBase + unit * 10.0)
                << (headBase - perpendicular * 8.0)
                << (tailCenter - perpendicular * 5.0);
        painter.drawPolygon(polygon);
        painter.restore();
    }

    void drawPreviewPath(QPainter& painter, const QRectF& zoneRect) const
    {
        constexpr int kPreviewRows = 4;
        constexpr int kPreviewCols = 4;

        const auto traversal = buildRectangleTraversalEntries(
            kPreviewRows,
            kPreviewCols,
            selection_.startCorner,
            selection_.primaryAxis);
        if (traversal.empty()) {
            return;
        }

        auto pointForCell = [&zoneRect](int row, int col) {
            const double x = zoneRect.left() + (static_cast<double>(col) + 0.5) * zoneRect.width() / static_cast<double>(kPreviewCols);
            const double y = zoneRect.top() + (static_cast<double>(row) + 0.5) * zoneRect.height() / static_cast<double>(kPreviewRows);
            return QPointF(x, y);
        };

        painter.save();
        painter.setPen(QPen(QColor("#93c5fd"), 2.0, Qt::DashLine));
        for (int row = 1; row < kPreviewRows; ++row) {
            const double y = zoneRect.top() + static_cast<double>(row) * zoneRect.height() / static_cast<double>(kPreviewRows);
            painter.drawLine(QPointF(zoneRect.left(), y), QPointF(zoneRect.right(), y));
        }
        for (int col = 1; col < kPreviewCols; ++col) {
            const double x = zoneRect.left() + static_cast<double>(col) * zoneRect.width() / static_cast<double>(kPreviewCols);
            painter.drawLine(QPointF(x, zoneRect.top()), QPointF(x, zoneRect.bottom()));
        }

        painter.setPen(QPen(QColor("#1f6feb"), 3.0));
        for (int i = 1; i < static_cast<int>(traversal.size()); ++i) {
            const QPointF p0 = pointForCell(traversal[static_cast<std::size_t>(i - 1)].row, traversal[static_cast<std::size_t>(i - 1)].col);
            const QPointF p1 = pointForCell(traversal[static_cast<std::size_t>(i)].row, traversal[static_cast<std::size_t>(i)].col);
            painter.drawLine(p0, p1);
        }

        const QPointF startPoint = pointForCell(traversal.front().row, traversal.front().col);
        painter.setBrush(QColor("#1d4ed8"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(startPoint, 6.0, 6.0);
        painter.restore();
    }

    RectangleTraversalSelection selection_ {};
    mutable std::vector<PreviewOption> options_;
    QPointF lastMousePos_ {-1000.0, -1000.0};
};

QString formatMm(double value)
{
    return QString::number(value, 'f', 3) + " mm";
}

double durationSecondsFromEdits(const QLineEdit* hoursEdit, const QLineEdit* minutesEdit, const QLineEdit* secondsEdit, bool* ok = nullptr)
{
    bool hoursOk = false;
    bool minutesOk = false;
    bool secondsOk = false;
    const double hours = hoursEdit != nullptr ? hoursEdit->text().trimmed().toDouble(&hoursOk) : 0.0;
    const double minutes = minutesEdit != nullptr ? minutesEdit->text().trimmed().toDouble(&minutesOk) : 0.0;
    const double seconds = secondsEdit != nullptr ? secondsEdit->text().trimmed().toDouble(&secondsOk) : 0.0;
    const bool allOk = hoursOk && minutesOk && secondsOk && hours >= 0.0 && minutes >= 0.0 && seconds >= 0.0;
    if (ok != nullptr) {
        *ok = allOk;
    }
    return allOk ? (hours * 3600.0 + minutes * 60.0 + seconds) : 0.0;
}

double scanRateToMilliVoltsPerSecond(double value, const QString& unit)
{
    return unit.trimmed().compare("V/s", Qt::CaseInsensitive) == 0 ? value * 1000.0 : value;
}

bool comboUsesInitialReference(const QComboBox* combo)
{
    if (combo == nullptr) {
        return false;
    }
    return combo->currentText().trimmed().compare("Ref", Qt::CaseInsensitive) != 0;
}

QString snapshotPositionText(const hardware::MotorAxisSnapshot& snapshot)
{
    if (!snapshot.connected) {
        return "-";
    }
    if (!snapshot.positionValid) {
        return snapshot.issue.isEmpty() ? "..." : "Erreur";
    }
    return formatMm(snapshot.positionMm);
}

QString axisConnectionSummary(const hardware::MotorAxisSnapshot& xSnapshot, const hardware::MotorAxisSnapshot& ySnapshot)
{
    if (!xSnapshot.connected && !ySnapshot.connected) {
        return "Moteurs: deconnectes";
    }

    return QString("Moteurs: X=%1 | Y=%2")
        .arg(xSnapshot.connected ? snapshotPositionText(xSnapshot) : QString("off"))
        .arg(ySnapshot.connected ? snapshotPositionText(ySnapshot) : QString("off"));
}

QString formatMotorPositionLogText(const hardware::MotorAxisSnapshot& xSnapshot, const hardware::MotorAxisSnapshot& ySnapshot)
{
    const QString xText = xSnapshot.positionValid
        ? QString::number(xSnapshot.positionMm, 'f', 4)
        : QString("---");
    const QString yText = ySnapshot.positionValid
        ? QString::number(ySnapshot.positionMm, 'f', 4)
        : QString("---");
    return QString("X=%1 Y=%2 mm").arg(xText, yText);
}

QString currentMotorPositionLogText(const std::shared_ptr<hardware::NewportConexController>& controller)
{
    if (controller == nullptr) {
        return "X=--- Y=--- mm";
    }

    try {
        const auto both = controller->snapshotBoth();
        return formatMotorPositionLogText(both.x, both.y);
    } catch (...) {
        return "X=--- Y=--- mm";
    }
}

QString currentLogTimestampText()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
}

QString yesNoText(bool value)
{
    return value ? "oui" : "non";
}

QString sanitizeLogFileComponent(QString text)
{
    for (QChar& ch : text) {
        if (!ch.isLetterOrNumber()) {
            ch = QChar('_');
        }
    }

    while (text.contains("__")) {
        text.replace("__", "_");
    }

    text = text.trimmed();
    while (text.startsWith('_')) {
        text.remove(0, 1);
    }
    while (text.endsWith('_')) {
        text.chop(1);
    }
    return text;
}

QString formatPointMm(const QPointF& point)
{
    return QString("(%1,%2) mm")
        .arg(point.x(), 0, 'f', 4)
        .arg(point.y(), 0, 'f', 4);
}

QString formatPointPx(const QPoint& point)
{
    return QString("(%1,%2) px").arg(point.x()).arg(point.y());
}

QString measurementModeLabel(bool simpleMeasurement, MainWindow::ScanConfig::AcquisitionMode mode)
{
    if (simpleMeasurement) {
        return "Mesure simple";
    }
    return mode == MainWindow::ScanConfig::AcquisitionMode::Continuous
        ? "Balayage continu"
        : "Balayage pas a pas";
}

QString measurementModeSlug(bool simpleMeasurement, MainWindow::ScanConfig::AcquisitionMode mode)
{
    if (simpleMeasurement) {
        return "simple";
    }
    return mode == MainWindow::ScanConfig::AcquisitionMode::Continuous
        ? "continu"
        : "pas_a_pas";
}

QString zoneShapeLabel(bool simpleMeasurement, bool rectangleMode)
{
    if (simpleMeasurement) {
        return "Sans zone";
    }
    return rectangleMode ? "Rectangle" : "Lineaire";
}

QString continuousTriggerLabel(MainWindow::ScanConfig::ContinuousTrigger trigger)
{
    return trigger == MainWindow::ScanConfig::ContinuousTrigger::Distance ? "Distance" : "Temps";
}

int motionDirection(double deltaMm)
{
    if (deltaMm > kOverlayPredictionEpsilonMm) {
        return 1;
    }
    if (deltaMm < -kOverlayPredictionEpsilonMm) {
        return -1;
    }
    return 0;
}

bool axisStateLooksMoving(const QString& stateCode)
{
    const QString upper = stateCode.trimmed().toUpper();
    return upper == "1E"
        || upper == "1F"
        || upper == "28"
        || upper == "29"
        || upper == "2A"
        || upper == "2B"
        || upper == "46"
        || upper == "47";
}

struct ObjectivePreset
{
    const char* name;
    double magnification;
    int laserX;
    int laserY;
    int laserRadiusPx;
};

std::array<ObjectivePreset, 3> kObjectivePresets {{
    {"4x",  4.0,  2588, 1350, 32},
    {"10x", 10.0, 2588, 1350, 32},
    {"50x", 50.0, 2588, 1350, 32},
}};

ObjectivePreset* findObjectivePreset(const QString& name)
{
    for (ObjectivePreset& preset : kObjectivePresets) {
        if (name == QLatin1String(preset.name)) {
            return &preset;
        }
    }
    return nullptr;
}

struct RuntimeDependencySpec
{
    const char* area;
    const char* relativePath;
};

std::array<RuntimeDependencySpec, 24> kRuntimeDependencySpecs {{
    {"Application", "calibration.json"},
    {"Application", "Qt6Core.dll"},
    {"Application", "Qt6Gui.dll"},
    {"Application", "Qt6Widgets.dll"},
    {"Application", "platforms/qwindows.dll"},
    {"Moteurs", "Newport.CONEXCC.CommandInterface.dll"},
    {"Moteurs", "NewportConexHelper.exe"},
    {"Camera", "thorlabs_tsi_camera_sdk.dll"},
    {"Camera", "thorlabs_tsi_mono_to_color_processing.dll"},
    {"Camera", "thorlabs_ccd_tsi_usb.dll"},
    {"Potentiostat", "EClib64.dll"},
    {"Potentiostat", "blfind64.dll"},
    {"Potentiostat", "ca.ecc"},
    {"Potentiostat", "ca4.ecc"},
    {"Potentiostat", "ca5.ecc"},
    {"Potentiostat", "ocv.ecc"},
    {"Potentiostat", "ocv4.ecc"},
    {"Potentiostat", "ocv5.ecc"},
    {"Potentiostat", "biovscan.ecc"},
    {"Potentiostat", "kernel.bin"},
    {"Potentiostat", "kernel4.bin"},
    {"Potentiostat", "kernel5.bin"},
    {"Potentiostat", "Vmp_ii_0437_a6.xlx"},
    {"Potentiostat", "Vmp_iv_0395_aa.xlx"},
}};

struct ContinuousRasterRow
{
    bool primaryAxisHorizontal {true};
    QPointF startPointMm;
    QPointF endPointMm;
    QPointF unitDirection;
    double lengthMm {0.0};
    std::vector<QPointF> samplePoints;
    std::vector<std::pair<int, int>> sampleCells;
};

struct ContinuousRasterPlan
{
    bool rectangleMode {false};
    double samplePitchMm {0.0};
    double rowStepMm {0.0};
    double originalWidthMm {0.0};
    double originalHeightMm {0.0};
    double effectiveWidthMm {0.0};
    double effectiveHeightMm {0.0};
    double xMinMm {0.0};
    double xMaxMm {0.0};
    double yMinMm {0.0};
    double yMaxMm {0.0};
    int rows {0};
    int cols {0};
    std::vector<ContinuousRasterRow> linePlans;
    std::vector<QPointF> sampleWaypoints;
    std::vector<std::pair<int, int>> order;
};

int intervalCountForSpan(double spanMm, double stepMm)
{
    if (stepMm <= 0.0 || spanMm <= kScanPlanningEpsilonMm) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::ceil((spanMm - kScanPlanningEpsilonMm) / stepMm)));
}

std::vector<double> buildInclusiveAxis(double startMm, double stepMm, int intervals)
{
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(std::max(0, intervals) + 1));
    for (int i = 0; i <= intervals; ++i) {
        values.push_back(startMm + static_cast<double>(i) * stepMm);
    }
    return values;
}

ContinuousRasterPlan buildContinuousRectanglePlan(
    const QPointF& startMm,
    const QPointF& endMm,
    double samplePitchMm,
    double rowStepMm,
    RectangleStartCorner startCorner,
    RectanglePrimaryAxis primaryAxis,
    bool visualLeftToRightIsIncreasingX,
    bool visualTopToBottomIsIncreasingY)
{
    ContinuousRasterPlan plan;
    plan.rectangleMode = true;
    plan.samplePitchMm = samplePitchMm;
    plan.rowStepMm = rowStepMm;
    plan.xMinMm = std::min(startMm.x(), endMm.x());
    plan.yMinMm = std::min(startMm.y(), endMm.y());
    plan.originalWidthMm = std::abs(endMm.x() - startMm.x());
    plan.originalHeightMm = std::abs(endMm.y() - startMm.y());

    const bool primaryHorizontal = primaryAxis == RectanglePrimaryAxis::Horizontal;
    const double xStepMm = primaryHorizontal ? samplePitchMm : rowStepMm;
    const double yStepMm = primaryHorizontal ? rowStepMm : samplePitchMm;

    const int xIntervals = intervalCountForSpan(plan.originalWidthMm, xStepMm);
    const int yIntervals = intervalCountForSpan(plan.originalHeightMm, yStepMm);
    plan.cols = xIntervals + 1;
    plan.rows = yIntervals + 1;
    plan.effectiveWidthMm = xIntervals > 0 ? static_cast<double>(xIntervals) * xStepMm : 0.0;
    plan.effectiveHeightMm = yIntervals > 0 ? static_cast<double>(yIntervals) * yStepMm : 0.0;
    plan.xMaxMm = plan.xMinMm + plan.effectiveWidthMm;
    plan.yMaxMm = plan.yMinMm + plan.effectiveHeightMm;
    plan.linePlans.reserve(static_cast<std::size_t>(primaryHorizontal ? plan.rows : plan.cols));
    plan.sampleWaypoints.reserve(static_cast<std::size_t>(plan.rows * plan.cols));
    plan.order.reserve(static_cast<std::size_t>(plan.rows * plan.cols));

    std::vector<double> xAxisVisual;
    xAxisVisual.reserve(static_cast<std::size_t>(plan.cols));
    for (int i = 0; i < plan.cols; ++i) {
        const double offsetMm = static_cast<double>(i) * xStepMm;
        xAxisVisual.push_back(
            visualLeftToRightIsIncreasingX
                ? (plan.xMinMm + offsetMm)
                : (plan.xMaxMm - offsetMm));
    }

    std::vector<double> yAxisVisual;
    yAxisVisual.reserve(static_cast<std::size_t>(plan.rows));
    for (int i = 0; i < plan.rows; ++i) {
        const double offsetMm = static_cast<double>(i) * yStepMm;
        yAxisVisual.push_back(
            visualTopToBottomIsIncreasingY
                ? (plan.yMinMm + offsetMm)
                : (plan.yMaxMm - offsetMm));
    }

    if (primaryAxis == RectanglePrimaryAxis::Horizontal) {
        const bool startsTop = rectangleStartCornerStartsTop(startCorner);
        const bool startsLeft = rectangleStartCornerStartsLeft(startCorner);

        for (int rowOffset = 0; rowOffset < plan.rows; ++rowOffset) {
            const int visualRow = startsTop ? rowOffset : (plan.rows - 1 - rowOffset);
            const bool leftToRight = (rowOffset % 2) == 0 ? startsLeft : !startsLeft;
            ContinuousRasterRow rowPlan;
            rowPlan.primaryAxisHorizontal = true;
            const double rowYmm = yAxisVisual[static_cast<std::size_t>(visualRow)];
            rowPlan.startPointMm = QPointF(leftToRight ? xAxisVisual.front() : xAxisVisual.back(), rowYmm);
            rowPlan.endPointMm = QPointF(leftToRight ? xAxisVisual.back() : xAxisVisual.front(), rowYmm);
            const QPointF motionVector = rowPlan.endPointMm - rowPlan.startPointMm;
            const double motionLengthMm = std::hypot(motionVector.x(), motionVector.y());
            rowPlan.unitDirection = motionLengthMm > kScanPlanningEpsilonMm
                ? QPointF(motionVector.x() / motionLengthMm, motionVector.y() / motionLengthMm)
                : QPointF(0.0, 0.0);
            rowPlan.lengthMm = plan.effectiveWidthMm;
            rowPlan.samplePoints.reserve(static_cast<std::size_t>(plan.cols));
            rowPlan.sampleCells.reserve(static_cast<std::size_t>(plan.cols));

            if (leftToRight) {
                for (int col = 0; col < plan.cols; ++col) {
                    const QPointF point(xAxisVisual[static_cast<std::size_t>(col)], rowYmm);
                    rowPlan.samplePoints.push_back(point);
                    rowPlan.sampleCells.emplace_back(visualRow, col);
                    plan.sampleWaypoints.push_back(point);
                    plan.order.emplace_back(visualRow, col);
                }
            } else {
                for (int offset = 0; offset < plan.cols; ++offset) {
                    const int col = plan.cols - 1 - offset;
                    const QPointF point(xAxisVisual[static_cast<std::size_t>(col)], rowYmm);
                    rowPlan.samplePoints.push_back(point);
                    rowPlan.sampleCells.emplace_back(visualRow, col);
                    plan.sampleWaypoints.push_back(point);
                    plan.order.emplace_back(visualRow, col);
                }
            }

            plan.linePlans.push_back(std::move(rowPlan));
        }
        return plan;
    }

    const bool startsTop = rectangleStartCornerStartsTop(startCorner);
    const bool startsLeft = rectangleStartCornerStartsLeft(startCorner);

    for (int colOffset = 0; colOffset < plan.cols; ++colOffset) {
        const int visualCol = startsLeft ? colOffset : (plan.cols - 1 - colOffset);
        const bool topToBottom = (colOffset % 2) == 0 ? startsTop : !startsTop;
        ContinuousRasterRow rowPlan;
        rowPlan.primaryAxisHorizontal = false;
        const double colXmm = xAxisVisual[static_cast<std::size_t>(visualCol)];
        rowPlan.startPointMm = QPointF(colXmm, topToBottom ? yAxisVisual.front() : yAxisVisual.back());
        rowPlan.endPointMm = QPointF(colXmm, topToBottom ? yAxisVisual.back() : yAxisVisual.front());
        const QPointF motionVector = rowPlan.endPointMm - rowPlan.startPointMm;
        const double motionLengthMm = std::hypot(motionVector.x(), motionVector.y());
        rowPlan.unitDirection = motionLengthMm > kScanPlanningEpsilonMm
            ? QPointF(motionVector.x() / motionLengthMm, motionVector.y() / motionLengthMm)
            : QPointF(0.0, 0.0);
        rowPlan.lengthMm = plan.effectiveHeightMm;
        rowPlan.samplePoints.reserve(static_cast<std::size_t>(plan.rows));
        rowPlan.sampleCells.reserve(static_cast<std::size_t>(plan.rows));

        if (topToBottom) {
            for (int row = 0; row < plan.rows; ++row) {
                const QPointF point(colXmm, yAxisVisual[static_cast<std::size_t>(row)]);
                rowPlan.samplePoints.push_back(point);
                rowPlan.sampleCells.emplace_back(row, visualCol);
                plan.sampleWaypoints.push_back(point);
                plan.order.emplace_back(row, visualCol);
            }
        } else {
            for (int offset = 0; offset < plan.rows; ++offset) {
                const int row = plan.rows - 1 - offset;
                const QPointF point(colXmm, yAxisVisual[static_cast<std::size_t>(row)]);
                rowPlan.samplePoints.push_back(point);
                rowPlan.sampleCells.emplace_back(row, visualCol);
                plan.sampleWaypoints.push_back(point);
                plan.order.emplace_back(row, visualCol);
            }
        }

        plan.linePlans.push_back(std::move(rowPlan));
    }

    return plan;
}

ContinuousRasterPlan buildContinuousLinearPlan(const QPointF& startMm, const QPointF& endMm, double samplePitchMm)
{
    ContinuousRasterPlan plan;
    plan.rectangleMode = false;
    plan.samplePitchMm = samplePitchMm;
    plan.rowStepMm = 0.0;
    const double dx = endMm.x() - startMm.x();
    const double dy = endMm.y() - startMm.y();
    const double distanceMm = std::hypot(dx, dy);
    const int intervals = intervalCountForSpan(distanceMm, samplePitchMm);

    plan.rows = 1;
    plan.cols = intervals + 1;
    plan.originalWidthMm = distanceMm;
    plan.effectiveWidthMm = intervals > 0 ? static_cast<double>(intervals) * samplePitchMm : 0.0;
    plan.originalHeightMm = 0.0;
    plan.effectiveHeightMm = 0.0;
    plan.sampleWaypoints.reserve(static_cast<std::size_t>(plan.cols));
    plan.order.reserve(static_cast<std::size_t>(plan.cols));

    ContinuousRasterRow rowPlan;
    rowPlan.primaryAxisHorizontal = true;
    rowPlan.samplePoints.reserve(static_cast<std::size_t>(plan.cols));
    rowPlan.sampleCells.reserve(static_cast<std::size_t>(plan.cols));

    if (distanceMm <= kScanPlanningEpsilonMm) {
        rowPlan.startPointMm = startMm;
        rowPlan.endPointMm = startMm;
        rowPlan.unitDirection = QPointF(0.0, 0.0);
        rowPlan.lengthMm = 0.0;
        rowPlan.samplePoints.push_back(startMm);
        rowPlan.sampleCells.emplace_back(0, 0);
        plan.sampleWaypoints.push_back(startMm);
        plan.order.emplace_back(0, 0);
    } else {
        const double ux = dx / distanceMm;
        const double uy = dy / distanceMm;
        rowPlan.startPointMm = startMm;
        rowPlan.endPointMm = QPointF(
            startMm.x() + ux * plan.effectiveWidthMm,
            startMm.y() + uy * plan.effectiveWidthMm);
        rowPlan.unitDirection = QPointF(ux, uy);
        rowPlan.lengthMm = plan.effectiveWidthMm;
        for (int i = 0; i <= intervals; ++i) {
            const double distanceAlongLineMm = static_cast<double>(i) * samplePitchMm;
            const QPointF point(
                startMm.x() + ux * distanceAlongLineMm,
                startMm.y() + uy * distanceAlongLineMm);
            rowPlan.samplePoints.push_back(point);
            rowPlan.sampleCells.emplace_back(0, i);
            plan.sampleWaypoints.push_back(point);
            plan.order.emplace_back(0, i);
        }
    }

    plan.linePlans.push_back(std::move(rowPlan));
    if (!plan.sampleWaypoints.empty()) {
        plan.xMinMm = plan.xMaxMm = plan.sampleWaypoints.front().x();
        plan.yMinMm = plan.yMaxMm = plan.sampleWaypoints.front().y();
        for (const QPointF& point : plan.sampleWaypoints) {
            plan.xMinMm = std::min(plan.xMinMm, point.x());
            plan.xMaxMm = std::max(plan.xMaxMm, point.x());
            plan.yMinMm = std::min(plan.yMinMm, point.y());
            plan.yMaxMm = std::max(plan.yMaxMm, point.y());
        }
    }

    return plan;
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal uncompressed baseline TIFF writer (RGB24, little-endian).
// Indépendant du plugin Qt imageformats/qtiff.
// ─────────────────────────────────────────────────────────────────────────────
bool saveTiff(const QImage& src, const QString& filePath)
{
    const QImage img = src.convertToFormat(QImage::Format_RGB32);
    const int W = img.width(), H = img.height();
    if (W <= 0 || H <= 0) return false;

    // Offsets dans le fichier :
    //   0   : header TIFF (8 octets)
    //   8   : IFD  (2 + 11×12 + 4 = 138 octets)
    //   146 : BitsPerSample extra (3 × uint16 = 6 octets)
    //   152 : XResolution rational (2 × uint32 = 8 octets)
    //   160 : YResolution rational (2 × uint32 = 8 octets)
    //   168 : données pixel (W × H × 3 octets)
    constexpr quint32 kIfdOffset  = 8;
    constexpr quint32 kBpsOffset  = 146;
    constexpr quint32 kXResOffset = 152;
    constexpr quint32 kYResOffset = 160;
    constexpr quint32 kDataOffset = 168;
    const quint32 dataSize = static_cast<quint32>(W) * static_cast<quint32>(H) * 3u;

    QByteArray buf(static_cast<int>(kDataOffset + dataSize), '\0');

    auto w16 = [&](int off, quint16 v) {
        buf[off]     = static_cast<char>(v & 0xFF);
        buf[off + 1] = static_cast<char>((v >> 8) & 0xFF);
    };
    auto w32 = [&](int off, quint32 v) {
        buf[off]     = static_cast<char>(v & 0xFF);
        buf[off + 1] = static_cast<char>((v >>  8) & 0xFF);
        buf[off + 2] = static_cast<char>((v >> 16) & 0xFF);
        buf[off + 3] = static_cast<char>((v >> 24) & 0xFF);
    };
    auto wEntry = [&](int off, quint16 tag, quint16 type, quint32 count, quint32 val) {
        w16(off, tag); w16(off + 2, type); w32(off + 4, count); w32(off + 8, val);
    };

    buf[0] = 'I'; buf[1] = 'I';
    w16(2, 42); w32(4, kIfdOffset);

    constexpr int kNEntries = 11;
    w16(static_cast<int>(kIfdOffset), kNEntries);
    int e = static_cast<int>(kIfdOffset) + 2;
    wEntry(e, 256, 4, 1, static_cast<quint32>(W));  e += 12; // ImageWidth
    wEntry(e, 257, 4, 1, static_cast<quint32>(H));  e += 12; // ImageLength
    wEntry(e, 258, 3, 3, kBpsOffset);               e += 12; // BitsPerSample (offset)
    wEntry(e, 259, 3, 1, 1);                        e += 12; // Compression: none
    wEntry(e, 262, 3, 1, 2);                        e += 12; // PhotometricInterp: RGB
    wEntry(e, 273, 4, 1, kDataOffset);              e += 12; // StripOffsets
    wEntry(e, 277, 3, 1, 3);                        e += 12; // SamplesPerPixel
    wEntry(e, 278, 4, 1, static_cast<quint32>(H));  e += 12; // RowsPerStrip
    wEntry(e, 279, 4, 1, dataSize);                 e += 12; // StripByteCounts
    wEntry(e, 282, 5, 1, kXResOffset);              e += 12; // XResolution
    wEntry(e, 283, 5, 1, kYResOffset);              e += 12; // YResolution
    w32(e, 0); // next IFD = 0

    w16(static_cast<int>(kBpsOffset),     8);
    w16(static_cast<int>(kBpsOffset) + 2, 8);
    w16(static_cast<int>(kBpsOffset) + 4, 8);
    w32(static_cast<int>(kXResOffset),     96); w32(static_cast<int>(kXResOffset) + 4, 1);
    w32(static_cast<int>(kYResOffset),     96); w32(static_cast<int>(kYResOffset) + 4, 1);

    int pos = static_cast<int>(kDataOffset);
    for (int y = 0; y < H; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < W; ++x) {
            buf[pos++] = static_cast<char>(qRed(line[x]));
            buf[pos++] = static_cast<char>(qGreen(line[x]));
            buf[pos++] = static_cast<char>(qBlue(line[x]));
        }
    }
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(buf) == static_cast<qint64>(buf.size());
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , snapshot_(core::makeDefaultSnapshot())
    , motorController_(std::make_shared<hardware::NewportConexController>())
    , cameraController_(std::make_unique<hardware::ThorlabsCameraController>())
    , potentiostatController_(std::make_shared<hardware::BioLogicController>())
{
    setWindowTitle("LaserBench");
    resize(1500, 920);
    setMinimumSize(1180, 740);

    setStyleSheet(
        "QMainWindow { background:#f3f5f7; }"
        "QWidget { font-family:'Segoe UI'; font-size:10pt; color:#18212b; }"
        "QMenuBar { background:#f3f5f7; padding:4px 10px; }"
        "QMenuBar::item { background:transparent; padding:6px 10px; border-radius:8px; }"
        "QMenuBar::item:selected { background:#e5ebf2; }"
        "QMenu {"
        "background:#ffffff;"
        "border:1px solid #d8e0e8;"
        "border-radius:10px;"
        "padding:6px;"
        "color:#18212b;"
        "}"
        "QMenu::item {"
        "padding:8px 18px;"
        "border-radius:8px;"
        "background:transparent;"
        "color:#18212b;"
        "}"
        "QMenu::item:selected {"
        "background:#eef4ff;"
        "color:#111927;"
        "}"
        "QMenu::separator {"
        "height:1px;"
        "background:#d8e0e8;"
        "margin:6px 8px;"
        "}"
        "QDialog { background:#f3f5f7; color:#18212b; }"
        "QStatusBar { background:#ffffff; border-top:1px solid #d8e0e8; color:#52606d; }"
        "QGroupBox {"
        "font-weight:600;"
        "border:1px solid #d8e0e8;"
        "border-radius:16px;"
        "margin-top:18px;"
        "padding-top:14px;"
        "background:#ffffff;"
        "}"
        "QGroupBox::title {"
        "subcontrol-origin: margin;"
        "left:16px;"
        "padding:0 6px;"
        "color:#44515d;"
        "}"
        "QPushButton {"
        "background:#ffffff;"
        "border:1px solid #cfd8e3;"
        "border-radius:10px;"
        "padding:7px 14px;"
        "color:#18212b;"
        "}"
        "QPushButton:hover { background:#f3f7fb; border-color:#b8c4d1; }"
        "QPushButton:pressed { background:#e8eef5; }"
        "QPushButton:disabled { background:#f0f2f5; border-color:#dde3ea; color:#a0aab4; }"
        "QPushButton[accent='true'] { background:#1f6feb; border-color:#1f6feb; color:#ffffff; }"
        "QPushButton[accent='true']:hover { background:#1b63d3; border-color:#1b63d3; }"
        "QPushButton[accent='true']:pressed { background:#195cc4; }"
        "QPushButton[accent='true']:disabled { background:#8ab4e8; border-color:#8ab4e8; color:#dce8f5; }"
        "QLineEdit, QComboBox {"
        "background:#ffffff;"
        "border:1px solid #cfd8e3;"
        "border-radius:10px;"
        "padding:7px 10px;"
        "}"
        "QLineEdit:focus, QComboBox:focus { border-color:#7aa2e3; }"
        "QComboBox QAbstractItemView {"
        "background:#ffffff;"
        "border:1px solid #d8e0e8;"
        "selection-background-color:#eef4ff;"
        "selection-color:#111927;"
        "color:#18212b;"
        "outline:none;"
        "}"
        "QTabWidget::pane { border:1px solid #d8e0e8; border-radius:18px; background:#ffffff; }"
        "QTabBar::tab {"
        "background:transparent;"
        "padding:10px 16px;"
        "margin-right:4px;"
        "border:none;"
        "border-bottom:2px solid transparent;"
        "color:#5b6773;"
        "}"
        "QTabBar::tab:selected { color:#18212b; border-bottom:2px solid #1f6feb; }"
        "QPlainTextEdit {"
        "background:#ffffff;"
        "color:#24303b;"
        "border:1px solid #d8e0e8;"
        "border-radius:14px;"
        "padding:10px;"
        "}"
        "QSplitter::handle { background:#d8e0e8; }"
        "QSplitter::handle:horizontal { width:2px; }"
        "QSplitter::handle:vertical { height:6px; }"
        "QFrame#placeholderPanel {"
        "background:#ffffff;"
        "border:1px solid #d8e0e8;"
        "border-radius:16px;"
        "}"
        "QLabel#panelTitle { font-size:18px; font-weight:600; color:#111927; }"
        "QLabel#panelBody { font-size:10pt; color:#5d6a76; }"
    );

    buildMenus();
    buildUi();
    initializeSessionLog();
    scheduleRuntimeDependencyCheck();
    refreshSummaries();
    startMotorPolling();

    qApp->installEventFilter(this);

    motorPollTimer_ = new QTimer(this);
    motorPollTimer_->setTimerType(Qt::CoarseTimer);
    motorPollTimer_->setInterval(33);
    connect(motorPollTimer_, &QTimer::timeout, this, &MainWindow::refreshSummaries);
    motorPollTimer_->start();

    cameraPollTimer_ = new QTimer(this);
    cameraPollTimer_->setTimerType(Qt::CoarseTimer);
    cameraPollTimer_->setInterval(33);
    connect(cameraPollTimer_, &QTimer::timeout, this, &MainWindow::refreshCameraUi);

    appendLog("Interface moteurs Qt6 initialisee.");
    appendLog("La DLL Newport est chargee automatiquement depuis MotorController/lib via le pont .NET.");
    appendLog("Camera Thorlabs: menu Camera pour recherche, connexion et live.");

    // Show the unified connection dialog at startup (non-modal)
    QTimer::singleShot(150, this, [this]() { openStartupConnectionDialog(); });
}

MainWindow::~MainWindow()
{
    sequenceStopRequested_.store(true);
    if (sequenceThread_.joinable()) {
        sequenceThread_.join();
    }
    if (potentiostatThread_.joinable()) {
        potentiostatThread_.join();
    }
    if (motorPollTimer_ != nullptr) {
        motorPollTimer_->stop();
    }
    if (cameraPollTimer_ != nullptr) {
        cameraPollTimer_->stop();
    }
    stopCameraPolling();
    stopContinuousJog(hardware::AxisId::X);
    stopContinuousJog(hardware::AxisId::Y);
    stopMotorPolling();
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (motorTaskRunning_ || sequenceRunning_) {
        QMessageBox::warning(this, "LaserBench", "Une operation moteur est en cours. Attendre la fin avant de fermer.");
        event->ignore();
        return;
    }

    appendLog("Fermeture application.");
    stopMotorPolling();
    stopContinuousJog(hardware::AxisId::X);
    stopContinuousJog(hardware::AxisId::Y);

    try {
        motorController_->disconnectAxes(false);
    } catch (...) {
    }
    try {
        if (cameraPollTimer_ != nullptr) {
            cameraPollTimer_->stop();
        }
        stopCameraPolling();
        cameraController_->disconnectCamera();
    } catch (...) {
    }

    event->accept();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (event == nullptr) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
        return QMainWindow::eventFilter(watched, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->modifiers() != Qt::NoModifier) {
        return QMainWindow::eventFilter(watched, event);
    }

    const int key = keyEvent->key();
    if (key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (!shouldHandleJogKeys()) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        if (keyEvent->isAutoRepeat()) {
            event->accept();
            return true;
        }

        if (key == Qt::Key_Left) leftKeyHeld_ = true;
        if (key == Qt::Key_Right) rightKeyHeld_ = true;
        if (key == Qt::Key_Up) upKeyHeld_ = true;
        if (key == Qt::Key_Down) downKeyHeld_ = true;

        if (key == Qt::Key_Left) startContinuousJog(hardware::AxisId::X, +1);
        if (key == Qt::Key_Right) startContinuousJog(hardware::AxisId::X, -1);
        if (key == Qt::Key_Up) startContinuousJog(hardware::AxisId::Y, -1);
        if (key == Qt::Key_Down) startContinuousJog(hardware::AxisId::Y, +1);
        event->accept();
        return true;
    }

    if (keyEvent->isAutoRepeat()) {
        event->accept();
        return true;
    }

    if (key == Qt::Key_Left) leftKeyHeld_ = false;
    if (key == Qt::Key_Right) rightKeyHeld_ = false;
    if (key == Qt::Key_Up) upKeyHeld_ = false;
    if (key == Qt::Key_Down) downKeyHeld_ = false;

    if (key == Qt::Key_Left || key == Qt::Key_Right) stopContinuousJog(hardware::AxisId::X);
    if (key == Qt::Key_Up || key == Qt::Key_Down) stopContinuousJog(hardware::AxisId::Y);
    event->accept();
    return true;
}

void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu("&Fichier");
    auto* quitAction = fileMenu->addAction("Quitter");
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* deviceMenu = menuBar()->addMenu("&Moteurs");
    auto* connectionAction = deviceMenu->addAction("Connexion moteurs...");
    deviceMenu->addSeparator();
    auto* scanAction = deviceMenu->addAction("Chercher les ports COM");
    auto* connectAction = deviceMenu->addAction("Connecter les moteurs");
    auto* homeAction = deviceMenu->addAction("Initialiser / Homing");
    auto* disconnectAction = deviceMenu->addAction("Deconnecter");

    connect(connectionAction, &QAction::triggered, this, &MainWindow::openMotorConnectionDialog);
    connect(scanAction, &QAction::triggered, this, &MainWindow::onScanPorts);
    connect(connectAction, &QAction::triggered, this, &MainWindow::onConnectAxes);
    connect(homeAction, &QAction::triggered, this, &MainWindow::onHomeAxes);
    connect(disconnectAction, &QAction::triggered, this, &MainWindow::onDisconnectAxes);

    auto* cameraMenu = menuBar()->addMenu("&Camera");
    auto* cameraConnectionAction = cameraMenu->addAction("Connexion camera...");
    auto* cameraDiscoverAction = cameraMenu->addAction("Chercher les cameras");
    auto* cameraConnectAction = cameraMenu->addAction("Connecter la camera");
    auto* cameraDisconnectAction = cameraMenu->addAction("Deconnecter la camera");
    cameraMenu->addSeparator();
    auto* cameraStartLiveAction = cameraMenu->addAction("Live");
    auto* cameraStopLiveAction = cameraMenu->addAction("Stop");
    cameraMenu->addSeparator();
    auto* cameraSettingsAction = cameraMenu->addAction("Parametres...");
    connect(cameraConnectionAction, &QAction::triggered, this, &MainWindow::openCameraConnectionDialog);
    connect(cameraDiscoverAction, &QAction::triggered, this, &MainWindow::openCameraConnectionDialog);
    connect(cameraConnectAction, &QAction::triggered, this, &MainWindow::openCameraConnectionDialog);
    connect(cameraStartLiveAction, &QAction::triggered, this, &MainWindow::startCameraLive);
    connect(cameraStopLiveAction, &QAction::triggered, this, &MainWindow::stopCameraLive);
    connect(cameraSettingsAction, &QAction::triggered, this, &MainWindow::openCameraSettingsDialog);
    connect(cameraDisconnectAction, &QAction::triggered, this, [this]() {
        try {
            stopCameraLive();
            cameraController_->disconnectCamera();
            appendLog("Camera deconnectee.");
            statusBar()->showMessage("Camera deconnectee.", 3000);
            refreshSummaries();
        } catch (const std::exception& ex) {
            QMessageBox::warning(this, "Camera", QString::fromUtf8(ex.what()));
        }
    });

    auto* helpMenu = menuBar()->addMenu("&Aide");
    auto* calibrationAction = helpMenu->addAction("Calibrage");
    auto* runtimeCheckAction = helpMenu->addAction("Diagnostic runtime");
    auto* openLogsAction = helpMenu->addAction("Ouvrir dossier logs");
    auto* aboutAction = helpMenu->addAction("A propos");
    connect(calibrationAction, &QAction::triggered, this, &MainWindow::openCalibrationDialog);
    connect(runtimeCheckAction, &QAction::triggered, this, &MainWindow::runRuntimeDependencyCheck);
    connect(openLogsAction, &QAction::triggered, this, [this]() {
        if (sessionLogPath_.isEmpty()) {
            QMessageBox::information(this, "Logs", "Aucun journal de session n'est disponible.");
            return;
        }

        const QString logsDir = QFileInfo(sessionLogPath_).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(logsDir));
    });
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            "A propos de LaserBench",
            "Premiere etape Qt6: scan COM, connexion moteurs Newport, homing, jog, mouvement absolu et lecture continue des positions."
        );
    });
}

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(18, 12, 18, 14);
    mainLayout->setSpacing(10);

    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(1000);
    logView_->hide();

    tabWidget_ = new QTabWidget;
    tabWidget_->addTab(buildSetupTab(), "Camera");
    tabWidget_->addTab(buildPotentiostatTab(), "Potentiostat");
    tabWidget_->addTab(buildMeasureTab(), "Resultat");
    tabWidget_->addTab(buildImportTab(), "Import");
    mainLayout->addWidget(tabWidget_, 1);

    setCentralWidget(central);

    stageSummaryLabel_ = new QLabel;
    cameraSummaryLabel_ = new QLabel;
    potentiostatSummaryLabel_ = new QLabel;

    mouseCoordsLabel_ = new QLabel;
    mouseCoordsLabel_->setStyleSheet("color:#c8d0db; font-family:monospace; padding-right:6px;");
    mouseCoordsLabel_->setMinimumWidth(220);
    mouseCoordsLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    statusBar()->addPermanentWidget(stageSummaryLabel_, 1);
    statusBar()->addPermanentWidget(cameraSummaryLabel_, 1);
    statusBar()->addPermanentWidget(potentiostatSummaryLabel_, 1);
    statusBar()->addPermanentWidget(mouseCoordsLabel_);
    statusBar()->showMessage("Pret");
}

void MainWindow::initializeSessionLog()
{
    const QString logsDirPath = QDir(QCoreApplication::applicationDirPath()).filePath("logs");
    QDir().mkpath(logsDirPath);

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    sessionLogPath_ = QDir(logsDirPath).filePath(QString("LaserBench_%1.log").arg(timestamp));

    QFile logFile(sessionLogPath_);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        sessionLogPath_.clear();
        return;
    }

    QTextStream stream(&logFile);
    stream << "LaserBench session log" << '\n';
    stream << "Started: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << '\n';
    stream << "AppDir: " << QDir::toNativeSeparators(QCoreApplication::applicationDirPath()) << '\n';
    stream << "----------------------------------------" << '\n';
    stream.flush();

    appendLog(QString("Journal de session: %1").arg(QDir::toNativeSeparators(sessionLogPath_)));
}

void MainWindow::scheduleRuntimeDependencyCheck()
{
    QTimer::singleShot(450, this, &MainWindow::runRuntimeDependencyCheck);
}

void MainWindow::runRuntimeDependencyCheck()
{
    const QStringList issues = collectRuntimeDependencyIssues();
    if (issues.isEmpty()) {
        appendLog("Auto-check runtime: dependances principales detectees.");
        return;
    }

    appendLog(QString("Auto-check runtime: %1 fichier(s) manquant(s).").arg(issues.size()));
    for (const QString& issue : issues) {
        appendLog(" - " + issue);
    }
    statusBar()->showMessage(
        QString("Diagnostic runtime: %1 dependance(s) manquante(s).").arg(issues.size()),
        12000
    );

    QMessageBox box(
        QMessageBox::Warning,
        "Diagnostic runtime",
        "Des fichiers de runtime sont manquants dans le dossier de l'application.\n\n"
        "L'interface peut s'ouvrir, mais certaines fonctions risquent d'etre indisponibles.\n\n"
        "Ouvrir les details pour voir la liste exacte.",
        QMessageBox::Ok,
        this
    );
    box.setDetailedText(issues.join("\n"));
    box.exec();
}

QStringList MainWindow::collectRuntimeDependencyIssues() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList issues;

    for (const RuntimeDependencySpec& spec : kRuntimeDependencySpecs) {
        const QString relativePath = QLatin1String(spec.relativePath);
        const QString absolutePath = QDir(appDir).filePath(relativePath);
        if (!QFileInfo::exists(absolutePath)) {
            issues.append(
                QString("%1 : %2")
                    .arg(QLatin1String(spec.area), QDir::toNativeSeparators(relativePath))
            );
        }
    }

    return issues;
}

QWidget* MainWindow::buildSetupTab()
{
    auto* page = new QWidget;
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto* splitter = new QSplitter(Qt::Horizontal);
    layout->addWidget(splitter, 1);

    // ── Left panel ────────────────────────────────────────────────
    auto* leftPanel = new QWidget;
    leftPanel->setMinimumWidth(300);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    auto* cameraBox = createGroupBox("Camera");
    auto* cameraLayout = new QGridLayout(cameraBox);
    cameraLayout->setSpacing(3);
    cameraLayout->setContentsMargins(6, 4, 6, 4);

    cameraPageStatusLabel_ = new QLabel("Deconnectee");
    cameraPageStatusLabel_->setStyleSheet("color:#5c6570; font-size:9pt;");
    auto* openCameraConnBtn = createActionButton("Connexion...");
    openCameraConnBtn->setMaximumWidth(110);
    cameraLayout->addWidget(new QLabel("Etat :"), 0, 0);
    cameraLayout->addWidget(cameraPageStatusLabel_, 0, 1, 1, 2);
    cameraLayout->addWidget(openCameraConnBtn, 0, 3);

    cameraLayout->addWidget(new QLabel("Exposure (us)"), 1, 0);
    cameraExposureEdit_ = new QLineEdit(QString::number(cameraController_->exposureTimeUs(), 'f', 0));
    cameraLayout->addWidget(cameraExposureEdit_, 1, 1, 1, 3);

    cameraLayout->addWidget(new QLabel("Gain"), 2, 0);
    cameraGainEdit_ = new QLineEdit(QString::number(cameraController_->gain(), 'f', 1));
    cameraLayout->addWidget(cameraGainEdit_, 2, 1, 1, 3);

    applyCameraSettingsButton_ = createActionButton("Appliquer");
    applyCameraSettingsButton_->setProperty("accent", true);
    cameraPageLiveButton_ = createActionButton("Live");
    cameraPageStopButton_ = createActionButton("Stop");
    cameraPageLiveButton_->setProperty("accent", true);
    cameraPageLiveButton_->setEnabled(false);
    cameraPageStopButton_->setEnabled(false);
    cameraLayout->addWidget(applyCameraSettingsButton_, 3, 0, 1, 2);
    cameraLayout->addWidget(cameraPageLiveButton_, 3, 2);
    cameraLayout->addWidget(cameraPageStopButton_, 3, 3);

    connect(openCameraConnBtn, &QPushButton::clicked, this, &MainWindow::openStartupConnectionDialog);
    connect(applyCameraSettingsButton_, &QPushButton::clicked, this, &MainWindow::applyCameraSettings);
    connect(cameraExposureEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyCameraSettings);
    connect(cameraGainEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyCameraSettings);
    connect(cameraPageLiveButton_, &QPushButton::clicked, this, &MainWindow::startCameraLive);
    connect(cameraPageStopButton_, &QPushButton::clicked, this, &MainWindow::stopCameraLive);

    leftLayout->addWidget(cameraBox);

    // Objectif / GoTo
    auto* laserBox = createGroupBox("Objectif / GoTo");
    auto* laserLayout = new QGridLayout(laserBox);
    laserLayout->setSpacing(3);
    laserLayout->setContentsMargins(6, 4, 6, 4);
    laserLayout->addWidget(new QLabel("Objectif"), 0, 0);
    objectiveCombo_ = new QComboBox;
    for (const ObjectivePreset& preset : kObjectivePresets) {
        objectiveCombo_->addItem(QLatin1String(preset.name));
    }
    objectiveCombo_->setCurrentText("4x");
    auto* applyObjectiveButton = createActionButton("Appliquer");
    laserLayout->addWidget(objectiveCombo_, 0, 1);
    laserLayout->addWidget(applyObjectiveButton, 0, 2, 1, 2);

    laserLayout->addWidget(new QLabel("Vit. GoTo (mm/s)"), 1, 0);
    gotoVelocityEdit_ = new QLineEdit("0.1");
    laserLayout->addWidget(gotoVelocityEdit_, 1, 1);
    gotoButton_ = createActionButton("GoTo");
    gotoButton_->setProperty("accent", true);
    laserLayout->addWidget(gotoButton_, 1, 2, 1, 2);

    gotoStatusLabel_ = new QLabel("GoTo : inactif");
    gotoStatusLabel_->setStyleSheet("color:#1f6feb; font-size:9pt;");
    laserLayout->addWidget(gotoStatusLabel_, 2, 0, 1, 4);

    // ── Measurement tools ─────────────────────────────────────────────────────
    auto* measBox = createGroupBox("Mesure (clic image)");
    auto* measLayout = new QGridLayout(measBox);
    measLayout->setSpacing(3);
    measLayout->setContentsMargins(6, 4, 6, 4);

    rulerButton_ = createActionButton("Regle");
    rulerDistanceLabel_ = new QLabel("---");
    rulerDistanceLabel_->setStyleSheet("font-size:9pt; font-weight:600; color:#f0c040;");
    measLayout->addWidget(rulerButton_,         0, 0);
    measLayout->addWidget(rulerDistanceLabel_,  0, 1, 1, 3);

    circleButton_ = createActionButton("Cercle");
    circleDiameterLabel_ = new QLabel("---");
    circleDiameterLabel_->setStyleSheet("font-size:9pt; font-weight:600; color:#50c8ff;");
    measLayout->addWidget(circleButton_,        1, 0);
    measLayout->addWidget(circleDiameterLabel_, 1, 1, 1, 3);

    rectButton_ = createActionButton("Rect.");
    rectSizeLabel_ = new QLabel("---");
    rectSizeLabel_->setStyleSheet("font-size:9pt; font-weight:600; color:#ff8c00;");
    measLayout->addWidget(rectButton_,   2, 0);
    measLayout->addWidget(rectSizeLabel_, 2, 1, 1, 3);

    leftLayout->addWidget(laserBox);
    leftLayout->addWidget(measBox);

    connect(objectiveCombo_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        applyObjectivePreset();
        updateRulerOverlay();
        updateCircleOverlay();
        updateRectOverlay();
    });
    connect(applyObjectiveButton, &QPushButton::clicked, this, [this]() {
        applyObjectivePreset();
        updateRulerOverlay();
        updateCircleOverlay();
        updateRectOverlay();
    });
    connect(gotoButton_, &QPushButton::clicked, this, &MainWindow::onArmGoto);
    connect(rulerButton_,  &QPushButton::clicked, this, &MainWindow::onToggleRuler);
    connect(circleButton_, &QPushButton::clicked, this, &MainWindow::onToggleCircle);
    connect(rectButton_,   &QPushButton::clicked, this, &MainWindow::onToggleRect);

    // Sequence
    auto* sequenceBox = createGroupBox("Sequence balayage");
    auto* sequenceLayout = new QGridLayout(sequenceBox);
    sequenceLayout->setSpacing(3);
    sequenceLayout->setContentsMargins(6, 4, 6, 4);
    sequenceLayout->addWidget(new QLabel("Mode"), 0, 0);
    sequenceModeCombo_ = new QComboBox;
    sequenceModeCombo_->addItems({"Lineaire", "Rectangle"});
    sequenceLayout->addWidget(sequenceModeCombo_, 0, 1);
    sequenceLayout->addWidget(new QLabel("Pas (mm)"), 0, 2);
    sequenceStepMmEdit_ = new QLineEdit("0.05");
    sequenceLayout->addWidget(sequenceStepMmEdit_, 0, 3);

    sequenceLayout->addWidget(new QLabel("Duree/pt (s)"), 1, 0);
    sequenceDurationEdit_ = new QLineEdit("0.5");
    sequenceLayout->addWidget(sequenceDurationEdit_, 1, 1);
    auto* durationHintLabel = new QLabel("= temps d'arret moteur par point");
    durationHintLabel->setStyleSheet("color:#5c6570; font-size:8pt;");
    sequenceLayout->addWidget(durationHintLabel, 1, 2, 1, 2);

    sequenceStartLabel_ = new QLabel("Depart  : ---");
    sequenceStartLabel_->setStyleSheet("color:#1a7f37; font-size:9pt;");
    sequenceLayout->addWidget(sequenceStartLabel_, 2, 0, 1, 4);
    sequenceEndLabel_ = new QLabel("Arrivee : ---");
    sequenceEndLabel_->setStyleSheet("color:#8b2020; font-size:9pt;");
    sequenceLayout->addWidget(sequenceEndLabel_, 3, 0, 1, 4);

    sequenceSetStartButton_ = createActionButton("Set Depart");
    sequenceSetEndButton_ = createActionButton("Set Arrivee");
    sequenceLayout->addWidget(sequenceSetStartButton_, 4, 0, 1, 2);
    sequenceLayout->addWidget(sequenceSetEndButton_, 4, 2, 1, 2);

    sequenceRunButton_ = createActionButton("Lancer");
    sequenceRunButton_->setProperty("accent", true);
    sequenceStopButton_ = createActionButton("Stop");
    sequenceLayout->addWidget(sequenceRunButton_, 5, 0, 1, 2);
    sequenceLayout->addWidget(sequenceStopButton_, 5, 2, 1, 2);

    sequenceStatusLabel_ = new QLabel;
    sequenceStatusLabel_->setStyleSheet("color:#1f6feb; font-size:9pt;");
    sequenceLayout->addWidget(sequenceStatusLabel_, 6, 0, 1, 4);

    connect(sequenceSetStartButton_, &QPushButton::clicked, this, &MainWindow::onSetSequenceStart);
    connect(sequenceSetEndButton_, &QPushButton::clicked, this, &MainWindow::onSetSequenceEnd);
    connect(sequenceRunButton_, &QPushButton::clicked, this, &MainWindow::onRunSequence);
    connect(sequenceStopButton_, &QPushButton::clicked, this, &MainWindow::onStopSequence);

    // The scan sequence controls are kept alive for the acquisition workflow
    // but hidden from the setup panel because they are not used in normal operation.
    sequenceBox->hide();
    leftLayout->addWidget(sequenceBox);
    leftLayout->addStretch();

    // ── Right area ────────────────────────────────────────────────
    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    cameraPreviewWidget_ = new CameraPreviewWidget;
    cameraPreviewWidget_->setStyleSheet("background:#0b1120; border:1px solid #d8e0e8; border-radius:16px;");
    cameraPreviewWidget_->setFrame(cameraController_->previewFrame());
    rightLayout->addWidget(cameraPreviewWidget_, 1);
    connect(cameraPreviewWidget_, &CameraPreviewWidget::zoomFactorChanged, this, [this](double zoomFactor) {
        if (cameraZoomLabel_ != nullptr) {
            cameraZoomLabel_->setText(QString("Zoom: %1x").arg(zoomFactor, 0, 'f', 2));
        }
    });
    connect(cameraPreviewWidget_, &CameraPreviewWidget::frameClicked, this, &MainWindow::onPreviewFrameClicked);
    connect(cameraPreviewWidget_, &CameraPreviewWidget::backgroundClicked, this, &MainWindow::onPreviewBackgroundClicked);
    connect(cameraPreviewWidget_, &CameraPreviewWidget::frameCursorMoved, this, [this](const QPoint& fp) {
        if (mouseCoordsLabel_ == nullptr) return;
        const QString obj = (objectiveCombo_ != nullptr)
            ? objectiveCombo_->currentText().trimmed() : QString("4x");
        const double mmPerPx = autoMmPerPxForObjective(obj);
        const double xUm = fp.x() * mmPerPx * 1000.0;
        const double yUm = fp.y() * mmPerPx * 1000.0;
        mouseCoordsLabel_->setText(
            QString("X: %1 px  |  %2 µm      Y: %3 px  |  %4 µm")
                .arg(fp.x(), 4)
                .arg(xUm,    0, 'f', 1)
                .arg(fp.y(), 4)
                .arg(yUm,    0, 'f', 1));
    });
    connect(cameraPreviewWidget_, &CameraPreviewWidget::frameCursorLeft, this, [this]() {
        if (mouseCoordsLabel_ != nullptr) mouseCoordsLabel_->clear();
    });
    connect(cameraPreviewWidget_, &CameraPreviewWidget::frameDoubleClicked,
            this, &MainWindow::onPreviewFrameDoubleClicked);
    loadCalibrationPresets();
    applyObjectivePreset();

    // Bottom strip: motor controls + zone selection
    auto* bottomStrip = new QWidget;
    auto* bottomLayout = new QHBoxLayout(bottomStrip);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(8);

    // Motor controls
    auto* motorBox = createGroupBox("Moteurs");
    auto* motorLayout = new QGridLayout(motorBox);

    motorLayout->addWidget(new QLabel("X :"), 0, 0);
    xPositionValueLabel_ = new QLabel("-");
    xPositionValueLabel_->setStyleSheet("font-size:16px; font-weight:600; color:#111927; min-width:64px;");
    motorLayout->addWidget(xPositionValueLabel_, 0, 1);
    motorLayout->addWidget(new QLabel("mm"), 0, 2);
    motorLayout->addWidget(new QLabel("Y :"), 0, 3);
    yPositionValueLabel_ = new QLabel("-");
    yPositionValueLabel_->setStyleSheet("font-size:16px; font-weight:600; color:#111927; min-width:64px;");
    motorLayout->addWidget(yPositionValueLabel_, 0, 4);
    motorLayout->addWidget(new QLabel("mm"), 0, 5);

    motorLayout->addWidget(new QLabel("Vit. (mm/s)"), 1, 0);
    jogStepEdit_ = new QLineEdit("0.500");
    jogStepEdit_->setMaximumWidth(66);
    motorLayout->addWidget(jogStepEdit_, 1, 1, 1, 2);
    auto* xMinBtn = createActionButton("X-");
    auto* xPlusBtn = createActionButton("X+");
    auto* yMinBtn = createActionButton("Y-");
    auto* yPlusBtn = createActionButton("Y+");
    motorLayout->addWidget(xMinBtn, 1, 3);
    motorLayout->addWidget(xPlusBtn, 1, 4);
    motorLayout->addWidget(yMinBtn, 1, 5);
    motorLayout->addWidget(yPlusBtn, 1, 6);

    motorLayout->addWidget(new QLabel("Abs X"), 2, 0);
    absXEdit_ = new QLineEdit("0.000");
    absXEdit_->setMaximumWidth(66);
    motorLayout->addWidget(absXEdit_, 2, 1, 1, 2);
    motorLayout->addWidget(new QLabel("Y"), 2, 3);
    absYEdit_ = new QLineEdit("0.000");
    absYEdit_->setMaximumWidth(66);
    motorLayout->addWidget(absYEdit_, 2, 4, 1, 2);
    moveAbsXYButton_ = createActionButton("Aller XY");
    motorLayout->addWidget(moveAbsXYButton_, 2, 6);

    connect(xMinBtn, &QPushButton::clicked, this, [this]() { onJogAxis(hardware::AxisId::X, -1); });
    connect(xPlusBtn, &QPushButton::clicked, this, [this]() { onJogAxis(hardware::AxisId::X, +1); });
    connect(yMinBtn, &QPushButton::clicked, this, [this]() { onJogAxis(hardware::AxisId::Y, -1); });
    connect(yPlusBtn, &QPushButton::clicked, this, [this]() { onJogAxis(hardware::AxisId::Y, +1); });
    connect(moveAbsXYButton_, &QPushButton::clicked, this, &MainWindow::onMoveAbsoluteBoth);

    bottomLayout->addWidget(motorBox, 1);

    // Capture position tool
    auto* captureBox = createGroupBox("Capture");
    auto* captureLayout = new QVBoxLayout(captureBox);
    captureLayout->setSpacing(4);
    captureButton_ = createActionButton("Capturer ref.");
    captureButton_->setProperty("accent", true);
    captureLayout->addWidget(captureButton_);
    captureDeltaLabel_ = new QLabel("---");
    captureDeltaLabel_->setAlignment(Qt::AlignCenter);
    captureDeltaLabel_->setWordWrap(true);
    captureDeltaLabel_->setStyleSheet("font-size:9pt; color:#374151;");
    captureLayout->addWidget(captureDeltaLabel_);
    connect(captureButton_, &QPushButton::clicked, this, &MainWindow::onCapturePosition);
    bottomLayout->addWidget(captureBox);

    // Zone selection
    auto* zoneBox = createGroupBox("Zone image");
    auto* zoneLayout = new QVBoxLayout(zoneBox);
    sequencePickButton_ = createActionButton("Definir zone");
    sequencePickButton_->setProperty("accent", true);
    zoneLayout->addWidget(sequencePickButton_);
    showZoneCheck_ = new QCheckBox("Zone visible");
    showZoneCheck_->setChecked(true);
    showZoneCheck_->setStyleSheet("font-size:9pt;");
    hideWaypointsCheck_ = new QCheckBox("Masquer points");
    hideWaypointsCheck_->setStyleSheet("font-size:9pt;");
    zoneLayout->addWidget(showZoneCheck_);
    zoneLayout->addWidget(hideWaypointsCheck_);

    connect(sequencePickButton_, &QPushButton::clicked, this, &MainWindow::onArmSequenceRectangle);

    bottomLayout->addWidget(zoneBox);

    rightLayout->addWidget(bottomStrip);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({340, 1020});

    return page;
}

QWidget* MainWindow::buildPotentiostatTab()
{
    auto* page = new QWidget;
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(10, 10, 10, 10);
    pageLayout->setSpacing(0);

    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);

    auto* potBox = createGroupBox("Potentiostat – Parametres");
    potBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* potLayout = new QGridLayout(potBox);
    potLayout->setSpacing(3);
    potLayout->setContentsMargins(6, 4, 6, 4);

    const QStringList eRangeOptions = {"-2.5 V; 2.5 V", "-5 V; 5 V", "-10 V; 10 V", "Auto"};
    const QStringList iRangeOptions = {
        "100 pA", "1 nA", "10 nA", "100 nA",
        "1 uA", "10 uA", "100 uA",
        "1 mA", "10 mA", "100 mA", "1 A",
        "Booster", "Auto"
    };

    const auto configureCompactEdit = [](QLineEdit* edit, int width = 72) {
        edit->setMaximumWidth(width);
        edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    };

    const auto makeTimeEditor = [&](const QString& hoursDefault,
                                    const QString& minutesDefault,
                                    const QString& secondsDefault,
                                    QLineEdit*& hoursEdit,
                                    QLineEdit*& minutesEdit,
                                    QLineEdit*& secondsEdit) -> QWidget*
    {
        auto* widget = new QWidget;
        auto* timeLayout = new QHBoxLayout(widget);
        timeLayout->setContentsMargins(0, 0, 0, 0);
        timeLayout->setSpacing(4);

        hoursEdit = new QLineEdit(hoursDefault);
        minutesEdit = new QLineEdit(minutesDefault);
        secondsEdit = new QLineEdit(secondsDefault);
        configureCompactEdit(hoursEdit, 48);
        configureCompactEdit(minutesEdit, 48);
        configureCompactEdit(secondsEdit, 70);

        timeLayout->addWidget(hoursEdit);
        timeLayout->addWidget(new QLabel("h"));
        timeLayout->addWidget(minutesEdit);
        timeLayout->addWidget(new QLabel("mn"));
        timeLayout->addWidget(secondsEdit);
        timeLayout->addWidget(new QLabel("s"));
        timeLayout->addStretch(1);
        return widget;
    };

    const auto makeRangeCombo = [](const QStringList& items, const QString& currentText) {
        auto* combo = new QComboBox;
        combo->addItems(items);
        combo->setCurrentText(currentText);
        return combo;
    };

    potentiostatStatusLabel_ = new QLabel("Deconnecte");
    potentiostatStatusLabel_->setStyleSheet("color:#5c6570; font-size:9pt;");
    auto* openConnBtn = createActionButton("Connexion...");
    openConnBtn->setMaximumWidth(110);
    connect(openConnBtn, &QPushButton::clicked, this, &MainWindow::openStartupConnectionDialog);
    potLayout->addWidget(new QLabel("Etat :"), 0, 0);
    potLayout->addWidget(potentiostatStatusLabel_, 0, 1, 1, 2);
    potLayout->addWidget(openConnBtn, 0, 3);

    potLayout->addWidget(new QLabel("Technique"), 1, 0);
    potentiostatTechniqueCombo_ = new QComboBox;
    potentiostatTechniqueCombo_->addItems({"CA", "OCV", "CVA"});
    potLayout->addWidget(potentiostatTechniqueCombo_, 1, 1, 1, 3);

    potentiostatTechniqueStack_ = new QStackedWidget;
    potentiostatTechniqueStack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* caPage = new QWidget;
    auto* caLayout = new QGridLayout(caPage);
    caLayout->setContentsMargins(0, 0, 0, 0);
    caLayout->setHorizontalSpacing(8);
    caLayout->setVerticalSpacing(4);

    caLayout->addWidget(new QLabel("Ewe (V)"), 0, 0);
    potentiostatVoltageEdit_ = new QLineEdit("0.500");
    caLayout->addWidget(potentiostatVoltageEdit_, 0, 1);
    caLayout->addWidget(new QLabel("vs"), 0, 2);
    potentiostatVsCombo_ = new QComboBox;
    potentiostatVsCombo_->addItems({"Ref", "Pref"});
    caLayout->addWidget(potentiostatVsCombo_, 0, 3);

    caLayout->addWidget(new QLabel("E Range"), 1, 0);
    potentiostatErangeCombo_ = makeRangeCombo(eRangeOptions, "-2.5 V; 2.5 V");
    caLayout->addWidget(potentiostatErangeCombo_, 1, 1, 1, 3);

    caLayout->addWidget(new QLabel("I Range"), 2, 0);
    potentiostatCurrentRangeCombo_ = makeRangeCombo(iRangeOptions, "Auto");
    caLayout->addWidget(potentiostatCurrentRangeCombo_, 2, 1, 1, 3);

    caLayout->addWidget(new QLabel("Bandwidth"), 3, 0);
    potentiostatBandwidthCombo_ = new QComboBox;
    for (int i = 1; i <= 9; ++i)
        potentiostatBandwidthCombo_->addItem(QString::number(i));
    potentiostatBandwidthCombo_->setCurrentText("8");
    caLayout->addWidget(potentiostatBandwidthCombo_, 3, 1);

    caLayout->addWidget(new QLabel("Cycles"), 4, 0);
    potentiostatNbCyclesEdit_ = new QLineEdit("0");
    configureCompactEdit(potentiostatNbCyclesEdit_, 60);
    caLayout->addWidget(potentiostatNbCyclesEdit_, 4, 1);
    caLayout->setColumnStretch(1, 1);
    caLayout->setColumnStretch(3, 1);

    auto* ocvPage = new QWidget;
    auto* ocvLayout = new QGridLayout(ocvPage);
    ocvLayout->setContentsMargins(0, 0, 0, 0);
    ocvLayout->setHorizontalSpacing(8);
    ocvLayout->setVerticalSpacing(4);

    ocvLayout->addWidget(new QLabel("Repos tR"), 0, 0);
    ocvLayout->addWidget(makeTimeEditor("0", "1", "0.000", potentiostatOcvRestHoursEdit_, potentiostatOcvRestMinutesEdit_, potentiostatOcvRestSecondsEdit_), 0, 1);

    ocvLayout->addWidget(new QLabel("Record every dE (mV)"), 1, 0);
    potentiostatOcvRecordDEEdit_ = new QLineEdit("10.0");
    configureCompactEdit(potentiostatOcvRecordDEEdit_);
    ocvLayout->addWidget(potentiostatOcvRecordDEEdit_, 1, 1);

    ocvLayout->addWidget(new QLabel("Record every dT (s)"), 2, 0);
    potentiostatOcvRecordDtEdit_ = new QLineEdit("0.500");
    configureCompactEdit(potentiostatOcvRecordDtEdit_);
    ocvLayout->addWidget(potentiostatOcvRecordDtEdit_, 2, 1);

    ocvLayout->addWidget(new QLabel("E Range"), 3, 0);
    potentiostatOcvErangeCombo_ = makeRangeCombo(eRangeOptions, "-10 V; 10 V");
    ocvLayout->addWidget(potentiostatOcvErangeCombo_, 3, 1);
    ocvLayout->setColumnStretch(1, 1);

    auto* cvaPage = new QWidget;
    auto* cvaPageLayout = new QVBoxLayout(cvaPage);
    cvaPageLayout->setContentsMargins(0, 0, 0, 0);
    cvaPageLayout->setSpacing(6);

    auto* cvaInitBox = createGroupBox("Initialisation");
    auto* cvaInitLayout = new QGridLayout(cvaInitBox);
    cvaInitLayout->addWidget(new QLabel("Ei (V)"), 0, 0);
    potentiostatCvaEiEdit_ = new QLineEdit("0.000");
    cvaInitLayout->addWidget(potentiostatCvaEiEdit_, 0, 1);
    cvaInitLayout->addWidget(new QLabel("vs"), 0, 2);
    potentiostatCvaEiVsCombo_ = new QComboBox;
    potentiostatCvaEiVsCombo_->addItems({"Ref", "Eoc"});
    cvaInitLayout->addWidget(potentiostatCvaEiVsCombo_, 0, 3);
    cvaInitLayout->addWidget(new QLabel("Hold Ei"), 1, 0);
    cvaInitLayout->addWidget(makeTimeEditor("0", "0", "5.000", potentiostatCvaTiHoursEdit_, potentiostatCvaTiMinutesEdit_, potentiostatCvaTiSecondsEdit_), 1, 1, 1, 3);
    cvaInitLayout->addWidget(new QLabel("Record dti (s)"), 2, 0);
    potentiostatCvaDtiEdit_ = new QLineEdit("1.000");
    configureCompactEdit(potentiostatCvaDtiEdit_);
    cvaInitLayout->addWidget(potentiostatCvaDtiEdit_, 2, 1);

    auto* cvaScanBox = createGroupBox("Balayage");
    auto* cvaScanLayout = new QGridLayout(cvaScanBox);
    cvaScanLayout->addWidget(new QLabel("dE/dt"), 0, 0);
    potentiostatCvaScanRateEdit_ = new QLineEdit("80.000");
    cvaScanLayout->addWidget(potentiostatCvaScanRateEdit_, 0, 1);
    potentiostatCvaScanRateUnitCombo_ = new QComboBox;
    potentiostatCvaScanRateUnitCombo_->addItems({"mV/s", "V/s"});
    cvaScanLayout->addWidget(potentiostatCvaScanRateUnitCombo_, 0, 2);

    cvaScanLayout->addWidget(new QLabel("E1 (V)"), 1, 0);
    potentiostatCvaE1Edit_ = new QLineEdit("2.500");
    cvaScanLayout->addWidget(potentiostatCvaE1Edit_, 1, 1);
    cvaScanLayout->addWidget(new QLabel("vs"), 1, 2);
    potentiostatCvaE1VsCombo_ = new QComboBox;
    potentiostatCvaE1VsCombo_->addItems({"Ref", "Ei"});
    cvaScanLayout->addWidget(potentiostatCvaE1VsCombo_, 1, 3);
    cvaScanLayout->addWidget(new QLabel("Hold E1"), 2, 0);
    cvaScanLayout->addWidget(makeTimeEditor("0", "0", "0.000", potentiostatCvaT1HoursEdit_, potentiostatCvaT1MinutesEdit_, potentiostatCvaT1SecondsEdit_), 2, 1, 1, 3);
    cvaScanLayout->addWidget(new QLabel("Record dt1 (s)"), 3, 0);
    potentiostatCvaDt1Edit_ = new QLineEdit("0.100");
    configureCompactEdit(potentiostatCvaDt1Edit_);
    cvaScanLayout->addWidget(potentiostatCvaDt1Edit_, 3, 1);

    cvaScanLayout->addWidget(new QLabel("E2 (V)"), 4, 0);
    potentiostatCvaE2Edit_ = new QLineEdit("-0.200");
    cvaScanLayout->addWidget(potentiostatCvaE2Edit_, 4, 1);
    cvaScanLayout->addWidget(new QLabel("vs"), 4, 2);
    potentiostatCvaE2VsCombo_ = new QComboBox;
    potentiostatCvaE2VsCombo_->addItems({"Ref", "Ei"});
    cvaScanLayout->addWidget(potentiostatCvaE2VsCombo_, 4, 3);
    cvaScanLayout->addWidget(new QLabel("Hold E2"), 5, 0);
    cvaScanLayout->addWidget(makeTimeEditor("0", "0", "0.000", potentiostatCvaT2HoursEdit_, potentiostatCvaT2MinutesEdit_, potentiostatCvaT2SecondsEdit_), 5, 1, 1, 3);
    cvaScanLayout->addWidget(new QLabel("Record dt2 (s)"), 6, 0);
    potentiostatCvaDt2Edit_ = new QLineEdit("0.100");
    configureCompactEdit(potentiostatCvaDt2Edit_);
    cvaScanLayout->addWidget(potentiostatCvaDt2Edit_, 6, 1);

    cvaScanLayout->addWidget(new QLabel("Mesure I sur les derniers (%)"), 7, 0);
    potentiostatCvaMeasurePercentEdit_ = new QLineEdit("50");
    configureCompactEdit(potentiostatCvaMeasurePercentEdit_, 60);
    cvaScanLayout->addWidget(potentiostatCvaMeasurePercentEdit_, 7, 1);

    cvaScanLayout->addWidget(new QLabel("Moyenne sur N pas"), 8, 0);
    potentiostatCvaAverageNStepsEdit_ = new QLineEdit("20");
    configureCompactEdit(potentiostatCvaAverageNStepsEdit_, 60);
    cvaScanLayout->addWidget(potentiostatCvaAverageNStepsEdit_, 8, 1);

    cvaScanLayout->addWidget(new QLabel("Repeter nC"), 9, 0);
    potentiostatCvaRepeatCyclesEdit_ = new QLineEdit("1");
    configureCompactEdit(potentiostatCvaRepeatCyclesEdit_, 60);
    cvaScanLayout->addWidget(potentiostatCvaRepeatCyclesEdit_, 9, 1);

    auto* cvaRangeBox = createGroupBox("Plages");
    auto* cvaRangeLayout = new QGridLayout(cvaRangeBox);
    cvaRangeLayout->addWidget(new QLabel("E Range"), 0, 0);
    potentiostatCvaErangeCombo_ = makeRangeCombo(eRangeOptions, "-2.5 V; 2.5 V");
    cvaRangeLayout->addWidget(potentiostatCvaErangeCombo_, 0, 1, 1, 2);
    cvaRangeLayout->addWidget(new QLabel("I Range"), 1, 0);
    potentiostatCvaCurrentRangeCombo_ = makeRangeCombo(iRangeOptions, "Auto");
    cvaRangeLayout->addWidget(potentiostatCvaCurrentRangeCombo_, 1, 1, 1, 2);
    cvaRangeLayout->addWidget(new QLabel("Bandwidth"), 2, 0);
    potentiostatCvaBandwidthCombo_ = new QComboBox;
    for (int i = 1; i <= 9; ++i)
        potentiostatCvaBandwidthCombo_->addItem(QString::number(i));
    potentiostatCvaBandwidthCombo_->setCurrentText("5");
    cvaRangeLayout->addWidget(potentiostatCvaBandwidthCombo_, 2, 1);

    auto* cvaFinalBox = createGroupBox("Fin de balayage");
    auto* cvaFinalLayout = new QGridLayout(cvaFinalBox);
    potentiostatCvaEndScanCheck_ = new QCheckBox("End scan to Ef");
    potentiostatCvaEndScanCheck_->setChecked(true);
    cvaFinalLayout->addWidget(potentiostatCvaEndScanCheck_, 0, 0);
    potentiostatCvaEfEdit_ = new QLineEdit("0.000");
    cvaFinalLayout->addWidget(potentiostatCvaEfEdit_, 0, 1);
    cvaFinalLayout->addWidget(new QLabel("vs"), 0, 2);
    potentiostatCvaEfVsCombo_ = new QComboBox;
    potentiostatCvaEfVsCombo_->addItems({"Ref", "Eoc", "Ei"});
    potentiostatCvaEfVsCombo_->setCurrentText("Eoc");
    cvaFinalLayout->addWidget(potentiostatCvaEfVsCombo_, 0, 3);
    cvaFinalLayout->addWidget(new QLabel("Hold Ef"), 1, 0);
    cvaFinalLayout->addWidget(makeTimeEditor("0", "0", "5.000", potentiostatCvaTfHoursEdit_, potentiostatCvaTfMinutesEdit_, potentiostatCvaTfSecondsEdit_), 1, 1, 1, 3);
    cvaFinalLayout->addWidget(new QLabel("Record dtf (s)"), 2, 0);
    potentiostatCvaDtfEdit_ = new QLineEdit("0.100");
    configureCompactEdit(potentiostatCvaDtfEdit_);
    cvaFinalLayout->addWidget(potentiostatCvaDtfEdit_, 2, 1);

    const auto updateCvaFinalEnabled = [this](bool enabled) {
        if (potentiostatCvaEfEdit_ != nullptr) potentiostatCvaEfEdit_->setEnabled(enabled);
        if (potentiostatCvaEfVsCombo_ != nullptr) potentiostatCvaEfVsCombo_->setEnabled(enabled);
        if (potentiostatCvaTfHoursEdit_ != nullptr) potentiostatCvaTfHoursEdit_->setEnabled(enabled);
        if (potentiostatCvaTfMinutesEdit_ != nullptr) potentiostatCvaTfMinutesEdit_->setEnabled(enabled);
        if (potentiostatCvaTfSecondsEdit_ != nullptr) potentiostatCvaTfSecondsEdit_->setEnabled(enabled);
        if (potentiostatCvaDtfEdit_ != nullptr) potentiostatCvaDtfEdit_->setEnabled(enabled);
    };
    connect(potentiostatCvaEndScanCheck_, &QCheckBox::toggled, this, updateCvaFinalEnabled);
    updateCvaFinalEnabled(potentiostatCvaEndScanCheck_->isChecked());

    cvaPageLayout->addWidget(cvaInitBox);
    cvaPageLayout->addWidget(cvaScanBox);
    cvaPageLayout->addWidget(cvaRangeBox);
    cvaPageLayout->addWidget(cvaFinalBox);

    potentiostatTechniqueStack_->addWidget(caPage);
    potentiostatTechniqueStack_->addWidget(ocvPage);
    potentiostatTechniqueStack_->addWidget(cvaPage);
    potLayout->addWidget(potentiostatTechniqueStack_, 2, 0, 1, 4);

    connect(potentiostatTechniqueCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        syncPotentiostatTechniqueUi();
    });

    contentLayout->addWidget(potBox);
    contentLayout->addStretch();
    scrollArea->setWidget(content);
    pageLayout->addWidget(scrollArea, 1);

    syncPotentiostatTechniqueUi();
    return page;
}

void MainWindow::openMotorConnectionDialog()
{
    openStartupConnectionDialog();
}

void MainWindow::openCameraConnectionDialog()
{
    openStartupConnectionDialog();
}

void MainWindow::openStartupConnectionDialog()
{
    if (startupConnectionDialog_ == nullptr) {
        startupConnectionDialog_ = new QDialog(this);
        startupConnectionDialog_->setWindowTitle("Connexion des appareils");
        startupConnectionDialog_->setModal(false);
        startupConnectionDialog_->setMinimumWidth(640);

        auto* layout = new QVBoxLayout(startupConnectionDialog_);
        layout->setContentsMargins(16, 16, 16, 10);
        layout->setSpacing(10);

        // ── Moteurs Newport CONEX-CC ──────────────────────────────
        auto* motorBox = createGroupBox("Moteurs Newport CONEX-CC");
        auto* motorGrid = new QGridLayout(motorBox);
        motorGrid->addWidget(new QLabel("Port X"), 0, 0);
        xPortCombo_ = new QComboBox;
        xPortCombo_->setEditable(true);
        motorGrid->addWidget(xPortCombo_, 0, 1);
        auto* swapPortsButton = createActionButton("Inv.");
        swapPortsButton->setFixedWidth(52);
        swapPortsButton->setToolTip("Inverser les ports X et Y");
        motorGrid->addWidget(swapPortsButton, 0, 2);
        motorGrid->addWidget(new QLabel("Port Y"), 0, 3);
        yPortCombo_ = new QComboBox;
        yPortCombo_->setEditable(true);
        motorGrid->addWidget(yPortCombo_, 0, 4);

        connect(swapPortsButton, &QPushButton::clicked, this, [this]() {
            if (xPortCombo_ == nullptr || yPortCombo_ == nullptr) return;
            const QString xText = xPortCombo_->currentText();
            const QString yText = yPortCombo_->currentText();
            xPortCombo_->setCurrentText(yText);
            yPortCombo_->setCurrentText(xText);
        });

        scanPortsButton_      = createActionButton("Chercher COM");
        connectAxesButton_    = createActionButton("Connecter");
        homeAxesButton_       = createActionButton("Init / Home");
        disconnectAxesButton_ = createActionButton("Deconnecter");
        scanPortsButton_->setProperty("accent", true);
        connectAxesButton_->setProperty("accent", true);
        motorGrid->addWidget(scanPortsButton_,      1, 0);
        motorGrid->addWidget(connectAxesButton_,    1, 1);
        motorGrid->addWidget(homeAxesButton_,       1, 2);
        motorGrid->addWidget(disconnectAxesButton_, 1, 3);

        auto* motorInfo = new QLabel(
            "La dependance Newport est chargee automatiquement depuis le depot."
        );
        motorInfo->setWordWrap(true);
        motorInfo->setStyleSheet("color:#5c6570; font-size:9pt;");
        motorGrid->addWidget(motorInfo, 2, 0, 1, 4);

        connect(scanPortsButton_,      &QPushButton::clicked, this, &MainWindow::onScanPorts);
        connect(connectAxesButton_,    &QPushButton::clicked, this, &MainWindow::onConnectAxes);
        connect(homeAxesButton_,       &QPushButton::clicked, this, &MainWindow::onHomeAxes);
        connect(disconnectAxesButton_, &QPushButton::clicked, this, &MainWindow::onDisconnectAxes);
        layout->addWidget(motorBox);

        // ── Camera Thorlabs ───────────────────────────────────────
        auto* camBox = createGroupBox("Camera Thorlabs");
        auto* camGrid = new QGridLayout(camBox);
        camGrid->addWidget(new QLabel("Camera"), 0, 0);
        cameraSerialCombo_ = new QComboBox;
        cameraSerialCombo_->setEditable(false);
        camGrid->addWidget(cameraSerialCombo_, 0, 1, 1, 3);

        scanCameraButton_       = createActionButton("Chercher");
        connectCameraButton_    = createActionButton("Connecter");
        disconnectCameraButton_ = createActionButton("Deconnecter");
        startCameraLiveButton_  = createActionButton("Live");
        stopCameraLiveButton_   = createActionButton("Stop");
        scanCameraButton_->setProperty("accent", true);
        connectCameraButton_->setProperty("accent", true);
        startCameraLiveButton_->setProperty("accent", true);
        camGrid->addWidget(scanCameraButton_,       1, 0);
        camGrid->addWidget(connectCameraButton_,    1, 1);
        camGrid->addWidget(disconnectCameraButton_, 1, 2);
        camGrid->addWidget(startCameraLiveButton_,  1, 3);
        camGrid->addWidget(stopCameraLiveButton_,   1, 4);

        connect(scanCameraButton_, &QPushButton::clicked, this, [this]() {
            try {
                const QStringList cameras = cameraController_->discoverAvailableCameras();
                const QString previous = cameraSerialCombo_ != nullptr ? cameraSerialCombo_->currentText() : QString {};
                if (cameraSerialCombo_ != nullptr) {
                    cameraSerialCombo_->clear();
                    cameraSerialCombo_->addItems(cameras);
                    if (cameras.contains(previous)) {
                        cameraSerialCombo_->setCurrentText(previous);
                    } else if (!cameras.isEmpty()) {
                        cameraSerialCombo_->setCurrentIndex(0);
                    }
                }
                appendLog(cameras.isEmpty() ? "Aucune camera detectee." : QString("Cameras detectees: %1").arg(cameras.join(", ")));
                refreshSummaries();
            } catch (const std::exception& ex) {
                QMessageBox::warning(this, "Camera", QString::fromUtf8(ex.what()));
            }
        });
        connect(connectCameraButton_, &QPushButton::clicked, this, [this]() {
            if (cameraSerialCombo_ == nullptr || cameraSerialCombo_->currentText().trimmed().isEmpty()) {
                QMessageBox::warning(this, "Camera", "Aucune camera selectionnee.");
                return;
            }
            try {
                cameraController_->connectCamera(cameraSerialCombo_->currentText().trimmed());
                appendLog(QString("Camera connectee: %1").arg(cameraController_->cameraIdentifier()));
                statusBar()->showMessage("Camera connectee.", 3000);
                refreshSummaries();
            } catch (const std::exception& ex) {
                QMessageBox::warning(this, "Camera", QString::fromUtf8(ex.what()));
            }
        });
        connect(disconnectCameraButton_, &QPushButton::clicked, this, [this]() {
            try {
                stopCameraLive();
                cameraController_->disconnectCamera();
                appendLog("Camera deconnectee.");
                statusBar()->showMessage("Camera deconnectee.", 3000);
                refreshSummaries();
            } catch (const std::exception& ex) {
                QMessageBox::warning(this, "Camera", QString::fromUtf8(ex.what()));
            }
        });
        connect(startCameraLiveButton_, &QPushButton::clicked, this, &MainWindow::startCameraLive);
        connect(stopCameraLiveButton_,  &QPushButton::clicked, this, &MainWindow::stopCameraLive);
        layout->addWidget(camBox);

        // ── Potentiostat BioLogic ─────────────────────────────────
        auto* potConnBox = createGroupBox("Potentiostat BioLogic");
        auto* potConnGrid = new QGridLayout(potConnBox);
        potConnGrid->setSpacing(4);

        // DLL path
        potConnGrid->addWidget(new QLabel("Chemin DLL"), 0, 0);
        potentiostatDllPathEdit_ = new QLineEdit("C:\\EC-Lab Development Package\\lib");
        potentiostatDllPathEdit_->setPlaceholderText("Chemin vers EClib64.dll...");
        potConnGrid->addWidget(potentiostatDllPathEdit_, 0, 1, 1, 2);
        auto* browseDllButton = createActionButton("...");
        browseDllButton->setMaximumWidth(30);
        potConnGrid->addWidget(browseDllButton, 0, 3);

        // IP + channel
        potConnGrid->addWidget(new QLabel("IP"), 1, 0);
        potentiostatAddressEdit_ = new QLineEdit("192.109.209.128");
        potConnGrid->addWidget(potentiostatAddressEdit_, 1, 1);
        potConnGrid->addWidget(new QLabel("Canal"), 1, 2);
        potentiostatChannelCombo_ = new QComboBox;
        for (int i = 1; i <= 16; ++i)
            potentiostatChannelCombo_->addItem(QString::number(i));
        potConnGrid->addWidget(potentiostatChannelCombo_, 1, 3);

        // Connect / Firmware / Disconnect
        potentiostatConnectButton_    = createActionButton("Connecter");
        potentiostatFirmwareButton_   = createActionButton("Firmware");
        potentiostatDisconnectButton_ = createActionButton("Deconnecter");
        potentiostatConnectButton_->setProperty("accent", true);
        potentiostatFirmwareButton_->setEnabled(false);
        potentiostatDisconnectButton_->setEnabled(false);
        potConnGrid->addWidget(potentiostatConnectButton_,    2, 0, 1, 2);
        potConnGrid->addWidget(potentiostatFirmwareButton_,   2, 2);
        potConnGrid->addWidget(potentiostatDisconnectButton_, 2, 3);

        // Status (shared with Parametrage tab small status label via slot)
        auto* potDialogStatusLabel = new QLabel("Deconnecte");
        potDialogStatusLabel->setStyleSheet("color:#5c6570; font-size:9pt;");
        potConnGrid->addWidget(potDialogStatusLabel, 3, 0, 1, 4);

        connect(browseDllButton, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, "Selectionner EClib64.dll", QString(), "DLL (*.dll)");
            if (!path.isEmpty() && potentiostatDllPathEdit_ != nullptr)
                potentiostatDllPathEdit_->setText(QDir::toNativeSeparators(path));
        });
        connect(potentiostatConnectButton_,    &QPushButton::clicked, this, &MainWindow::onConnectPotentiostat);
        connect(potentiostatFirmwareButton_,   &QPushButton::clicked, this, &MainWindow::onLoadFirmware);
        connect(potentiostatDisconnectButton_, &QPushButton::clicked, this, &MainWindow::onDisconnectPotentiostat);

        // Keep the dialog status label in sync with potentiostatStatusLabel_
        connect(potentiostatConnectButton_, &QPushButton::clicked, potDialogStatusLabel, [this, potDialogStatusLabel]() {
            // will be updated by onConnectPotentiostat via potentiostatStatusLabel_
            // mirror the same text once the operation finishes
            if (potentiostatStatusLabel_ != nullptr)
                QTimer::singleShot(200, potDialogStatusLabel, [this, potDialogStatusLabel]() {
                    if (potentiostatStatusLabel_ != nullptr)
                        potDialogStatusLabel->setText(potentiostatStatusLabel_->text());
                });
        });
        connect(potentiostatDisconnectButton_, &QPushButton::clicked, potDialogStatusLabel, [this, potDialogStatusLabel]() {
            if (potentiostatStatusLabel_ != nullptr)
                QTimer::singleShot(200, potDialogStatusLabel, [this, potDialogStatusLabel]() {
                    if (potentiostatStatusLabel_ != nullptr)
                        potDialogStatusLabel->setText(potentiostatStatusLabel_->text());
                });
        });
        layout->addWidget(potConnBox);

        // ── Close button ──────────────────────────────────────────
        auto* closeButton = createActionButton("Fermer");
        closeButton->setMinimumWidth(120);
        connect(closeButton, &QPushButton::clicked, startupConnectionDialog_, &QDialog::hide);
        auto* closeRow = new QHBoxLayout;
        closeRow->addStretch();
        closeRow->addWidget(closeButton);
        layout->addLayout(closeRow);
    }

    refreshMotorUi();
    refreshCameraUi();
    startupConnectionDialog_->adjustSize();
    startupConnectionDialog_->show();
    startupConnectionDialog_->raise();
    startupConnectionDialog_->activateWindow();
}

void MainWindow::openCameraSettingsDialog()
{
    if (tabWidget_ != nullptr) {
        tabWidget_->setCurrentIndex(0);
    }
    if (cameraExposureEdit_ != nullptr) {
        cameraExposureEdit_->setFocus(Qt::OtherFocusReason);
        cameraExposureEdit_->selectAll();
    }
}

void MainWindow::openCalibrationDialog()
{
    if (calibrationDialog_ == nullptr) {
        calibrationDialog_ = new QDialog(this);
        calibrationDialog_->setWindowTitle("Calibrage");
        calibrationDialog_->setModal(false);
        calibrationDialog_->setMinimumWidth(560);

        auto* layout = new QVBoxLayout(calibrationDialog_);
        layout->setContentsMargins(18, 18, 18, 18);
        layout->setSpacing(12);

        auto* gotoBox = createGroupBox("Corrections GoTo");
        auto* gotoLayout = new QGridLayout(gotoBox);

        gotoInvertXCheck_ = new QCheckBox("Inv X");
        gotoInvertYCheck_ = new QCheckBox("Inv Y");
        gotoLayout->addWidget(gotoInvertXCheck_, 0, 2);
        gotoLayout->addWidget(gotoInvertYCheck_, 0, 3);

        gotoLayout->addWidget(new QLabel("Corr X+"), 1, 0);
        gotoCorrXpEdit_ = new QLineEdit("0");
        gotoLayout->addWidget(gotoCorrXpEdit_, 1, 1);
        gotoLayout->addWidget(new QLabel("Corr X-"), 1, 2);
        gotoCorrXmEdit_ = new QLineEdit("0");
        gotoLayout->addWidget(gotoCorrXmEdit_, 1, 3);

        gotoLayout->addWidget(new QLabel("Corr Y+"), 2, 0);
        gotoCorrYpEdit_ = new QLineEdit("0");
        gotoLayout->addWidget(gotoCorrYpEdit_, 2, 1);
        gotoLayout->addWidget(new QLabel("Corr Y-"), 2, 2);
        gotoCorrYmEdit_ = new QLineEdit("0");
        gotoLayout->addWidget(gotoCorrYmEdit_, 2, 3);

        auto* laserBox = createGroupBox("Cible laser");
        auto* laserLayout = new QGridLayout(laserBox);

        laserLayout->addWidget(new QLabel("Objectif :"), 0, 0);
        calibObjectiveCombo_ = new QComboBox;
        for (const ObjectivePreset& preset : kObjectivePresets) {
            calibObjectiveCombo_->addItem(QLatin1String(preset.name));
        }
        const QString curObj = objectiveCombo_ != nullptr ? objectiveCombo_->currentText() : QString("4x");
        calibObjectiveCombo_->setCurrentText(curObj);
        laserLayout->addWidget(calibObjectiveCombo_, 0, 1, 1, 3);

        laserLayout->addWidget(new QLabel("Pas (px)"), 1, 0);
        laserMoveStepEdit_ = new QLineEdit("10");
        laserLayout->addWidget(laserMoveStepEdit_, 1, 1);

        auto* moveXMinusButton = createActionButton("X -");
        auto* moveXPlusButton = createActionButton("X +");
        auto* moveYMinusButton = createActionButton("Y -");
        auto* moveYPlusButton = createActionButton("Y +");
        auto* sizeMinusButton = createActionButton("Taille -");
        auto* sizePlusButton = createActionButton("Taille +");
        laserLayout->addWidget(moveXMinusButton, 2, 0);
        laserLayout->addWidget(moveXPlusButton,  2, 1);
        laserLayout->addWidget(moveYMinusButton, 2, 2);
        laserLayout->addWidget(moveYPlusButton,  2, 3);
        laserLayout->addWidget(sizeMinusButton,  3, 0, 1, 2);
        laserLayout->addWidget(sizePlusButton,   3, 2, 1, 2);

        laserLayout->addWidget(new QLabel("X cible"), 4, 0);
        laserXEdit_ = new QLineEdit(QString::number(laserPointPx_.x()));
        laserLayout->addWidget(laserXEdit_, 4, 1);
        laserLayout->addWidget(new QLabel("Y cible"), 4, 2);
        laserYEdit_ = new QLineEdit(QString::number(laserPointPx_.y()));
        laserLayout->addWidget(laserYEdit_, 4, 3);

        laserLayout->addWidget(new QLabel("Taille"), 5, 0);
        laserSizeEdit_ = new QLineEdit(QString::number(laserRadiusPx_));
        laserLayout->addWidget(laserSizeEdit_, 5, 1);
        auto* applyLaserButton = createActionButton("Appliquer cible");
        applyLaserButton->setProperty("accent", true);
        laserLayout->addWidget(applyLaserButton, 5, 2, 1, 2);

        layout->addWidget(gotoBox);
        layout->addWidget(laserBox);

        connect(moveXMinusButton, &QPushButton::clicked, this, [this]() { nudgeLaserTarget(-1, 0, 0); });
        connect(moveXPlusButton, &QPushButton::clicked, this, [this]() { nudgeLaserTarget(1, 0, 0); });
        connect(moveYMinusButton, &QPushButton::clicked, this, [this]() { nudgeLaserTarget(0, -1, 0); });
        connect(moveYPlusButton, &QPushButton::clicked, this, [this]() { nudgeLaserTarget(0, 1, 0); });
        connect(sizeMinusButton, &QPushButton::clicked, this, [this]() { nudgeLaserTarget(0, 0, -1); });
        connect(sizePlusButton, &QPushButton::clicked, this, [this]() { nudgeLaserTarget(0, 0, 1); });
        connect(applyLaserButton, &QPushButton::clicked, this, &MainWindow::applyLaserCalibrationEdits);
        connect(laserXEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyLaserCalibrationEdits);
        connect(laserYEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyLaserCalibrationEdits);
        connect(laserSizeEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyLaserCalibrationEdits);
        // Chargement des valeurs du preset quand l'objectif change dans le dialog
        connect(calibObjectiveCombo_, &QComboBox::currentTextChanged,
                this, [this](const QString& name) {
            const ObjectivePreset* preset = findObjectivePreset(name);
            if (preset == nullptr) return;
            if (laserXEdit_)    laserXEdit_->setText(QString::number(preset->laserX));
            if (laserYEdit_)    laserYEdit_->setText(QString::number(preset->laserY));
            if (laserSizeEdit_) laserSizeEdit_->setText(QString::number(preset->laserRadiusPx));
        });
    }

    syncCalibrationUi();
    calibrationDialog_->adjustSize();
    calibrationDialog_->show();
    calibrationDialog_->raise();
    calibrationDialog_->activateWindow();
}

void MainWindow::applyObjectivePreset()
{
    const QString objectiveName = objectiveCombo_ != nullptr ? objectiveCombo_->currentText().trimmed() : QString("4x");
    const ObjectivePreset* preset = findObjectivePreset(objectiveName);
    if (preset == nullptr) {
        return;
    }

    laserPointPx_.setX(preset->laserX);
    laserPointPx_.setY(preset->laserY);
    laserRadiusPx_ = std::max(preset->laserRadiusPx, 1);
    syncCalibrationUi();
    updateLaserLabel();
    syncLaserOverlay();
    syncSequenceOverlay();
}

void MainWindow::loadCalibrationPresets()
{
    const QString path = QCoreApplication::applicationDirPath() + "/calibration.json";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;
    const QJsonArray arr = doc.object().value("objectives").toArray();
    for (const QJsonValue& val : arr) {
        const QJsonObject obj = val.toObject();
        ObjectivePreset* preset = findObjectivePreset(obj.value("name").toString());
        if (preset == nullptr) continue;
        preset->laserX        = obj.value("laserX").toInt(preset->laserX);
        preset->laserY        = obj.value("laserY").toInt(preset->laserY);
        preset->laserRadiusPx = std::max(obj.value("laserRadiusPx").toInt(preset->laserRadiusPx), 1);
    }
}

void MainWindow::saveCalibrationPresets()
{
    QJsonArray arr;
    for (const ObjectivePreset& preset : kObjectivePresets) {
        QJsonObject obj;
        obj["name"]          = QLatin1String(preset.name);
        obj["laserX"]        = preset.laserX;
        obj["laserY"]        = preset.laserY;
        obj["laserRadiusPx"] = preset.laserRadiusPx;
        arr.append(obj);
    }
    QJsonObject root;
    root["objectives"] = arr;
    const QString path = QCoreApplication::applicationDirPath() + "/calibration.json";
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson());
}

void MainWindow::syncLaserOverlay(const QSize& frameSize)
{
    if (cameraPreviewWidget_ == nullptr) {
        return;
    }

    QSize effectiveFrameSize = frameSize;
    if (!effectiveFrameSize.isValid()) {
        effectiveFrameSize = cameraController_->previewFrame().size();
    }

    if (effectiveFrameSize.width() > 0 && effectiveFrameSize.height() > 0) {
        laserPointPx_.setX(std::clamp(laserPointPx_.x(), 0, effectiveFrameSize.width() - 1));
        laserPointPx_.setY(std::clamp(laserPointPx_.y(), 0, effectiveFrameSize.height() - 1));
    }

    cameraPreviewWidget_->setLaserOverlay(QPointF(laserPointPx_), laserRadiusPx_, true);
    syncCalibrationUi();
    updateLaserLabel();
}

void MainWindow::updateLaserLabel()
{
    if (laserPointLabel_ == nullptr) {
        return;
    }

    laserPointLabel_->setText(
        QString("X=%1  Y=%2  |  Taille=%3 px")
            .arg(laserPointPx_.x())
            .arg(laserPointPx_.y())
            .arg(laserRadiusPx_)
    );
}

void MainWindow::updatePreviewCursor()
{
    if (cameraPreviewWidget_ == nullptr) {
        return;
    }

    cameraPreviewWidget_->setCursor(
        (gotoArmed_ || sequenceSelectArmed_ || rulerArmed_ || circleArmed_ || rectArmed_)
        ? Qt::CrossCursor : Qt::ArrowCursor);
}

void MainWindow::setGotoArmed(bool armed)
{
    gotoArmed_ = armed;
    if (gotoStatusLabel_ != nullptr) {
        gotoStatusLabel_->setText(gotoArmed_ ? "GoTo : clique dans l'image" : "GoTo : inactif");
    }
    updatePreviewCursor();
}

void MainWindow::setSequenceSelectArmed(bool armed)
{
    sequenceSelectArmed_ = armed;
    if (sequencePickButton_ != nullptr) {
        sequencePickButton_->setText(sequenceSelectArmed_ ? "Annuler zone" : "Zone image");
    }
    updatePreviewCursor();
}

void MainWindow::syncCalibrationUi()
{
    // Aligner la combo objectif du dialog sur l'objectif courant
    if (calibObjectiveCombo_ != nullptr && objectiveCombo_ != nullptr) {
        const QString cur = objectiveCombo_->currentText();
        if (calibObjectiveCombo_->currentText() != cur) {
            QSignalBlocker blocker(calibObjectiveCombo_);
            calibObjectiveCombo_->setCurrentText(cur);
        }
    }

    // Afficher les valeurs runtime (laserPointPx_ / laserRadiusPx_)
    if (laserXEdit_ != nullptr && !laserXEdit_->hasFocus()) {
        laserXEdit_->setText(QString::number(laserPointPx_.x()));
    }
    if (laserYEdit_ != nullptr && !laserYEdit_->hasFocus()) {
        laserYEdit_->setText(QString::number(laserPointPx_.y()));
    }
    if (laserSizeEdit_ != nullptr && !laserSizeEdit_->hasFocus()) {
        laserSizeEdit_->setText(QString::number(laserRadiusPx_));
    }
}

void MainWindow::clearSequencePreviewSelection()
{
    sequenceFirstFramePoint_.reset();
    sequenceRectStartFrame_.reset();
    sequenceRectEndFrame_.reset();
    sequenceBaseMotorMm_.reset();
    sequenceRectFollowSample_ = false;
    waypointsMm_.clear();
    currentWaypointIndex_ = -1;
    syncSequenceOverlay();
}

void MainWindow::updateSequenceLabels(const QPointF& startMm, const QPointF& endMm)
{
    sequenceStartMotorMm_ = startMm;
    sequenceEndMotorMm_ = endMm;
    if (sequenceStartLabel_ != nullptr) {
        sequenceStartLabel_->setText(QString("Depart  : X=%1  Y=%2 mm").arg(startMm.x(), 0, 'f', 4).arg(startMm.y(), 0, 'f', 4));
    }
    if (sequenceEndLabel_ != nullptr) {
        sequenceEndLabel_->setText(QString("Arrivee : X=%1  Y=%2 mm").arg(endMm.x(), 0, 'f', 4).arg(endMm.y(), 0, 'f', 4));
    }
}

void MainWindow::setSequenceRunning(bool running)
{
    sequenceRunning_ = running;
    if (sequenceRunButton_ != nullptr) {
        sequenceRunButton_->setEnabled(!sequenceRunning_);
    }
    if (sequenceStopButton_ != nullptr) {
        sequenceStopButton_->setEnabled(sequenceRunning_);
    }
}

std::optional<QPointF> MainWindow::latestPolledMotorPosition()
{
    std::lock_guard<std::mutex> lock(motorSnapshotMutex_);
    if (polledXSnapshot_.positionValid && polledYSnapshot_.positionValid) {
        return QPointF(polledXSnapshot_.positionMm, polledYSnapshot_.positionMm);
    }
    if (cachedMotorMm_.has_value()) {
        return cachedMotorMm_;
    }
    return std::nullopt;
}

void MainWindow::startPredictedMotorMotion(
    std::optional<double> startXmm,
    std::optional<double> startYmm,
    std::optional<double> targetXmm,
    std::optional<double> targetYmm,
    std::optional<double> speedXmmPerS,
    std::optional<double> speedYmmPerS
)
{
    const auto now = std::chrono::steady_clock::now();

    auto startAxis = [now](AxisMotionPrediction& prediction,
                           std::optional<double> startMm,
                           std::optional<double> targetMm,
                           std::optional<double> speedMmPerS) {
        if (!startMm.has_value()) {
            return;
        }

        prediction.basePositionMm = *startMm;
        prediction.targetPositionMm = targetMm;
        prediction.velocityMmPerS = std::max(0.0, speedMmPerS.value_or(prediction.velocityMmPerS));
        prediction.direction = targetMm.has_value() ? motionDirection(*targetMm - *startMm) : 0;
        prediction.baseTimestamp = now;
        prediction.active = targetMm.has_value() || prediction.velocityMmPerS > 0.0;

        if (targetMm.has_value() && prediction.direction == 0) {
            prediction.basePositionMm = *targetMm;
            prediction.targetPositionMm = *targetMm;
            prediction.velocityMmPerS = 0.0;
            prediction.active = true;
        }
    };

    std::lock_guard<std::mutex> lock(predictedMotionMutex_);
    startAxis(predictedXMotion_, startXmm, targetXmm, speedXmmPerS);
    startAxis(predictedYMotion_, startYmm, targetYmm, speedYmmPerS);
}

void MainWindow::stopPredictedMotorMotion(std::optional<double> holdXmm, std::optional<double> holdYmm)
{
    const auto now = std::chrono::steady_clock::now();
    const bool affectAllAxes = !holdXmm.has_value() && !holdYmm.has_value();

    auto holdAxis = [now](AxisMotionPrediction& prediction, std::optional<double> holdMm) {
        if (!holdMm.has_value()) {
            if (!prediction.active) {
                prediction.targetPositionMm.reset();
                prediction.velocityMmPerS = 0.0;
                prediction.direction = 0;
                return;
            }

            holdMm = prediction.basePositionMm;
            if (prediction.velocityMmPerS > 0.0) {
                const double elapsedS = std::max(0.0, std::chrono::duration<double>(now - prediction.baseTimestamp).count());
                double estimate = prediction.basePositionMm + (prediction.direction * prediction.velocityMmPerS * elapsedS);
                if (prediction.targetPositionMm.has_value()) {
                    const double low = std::min(prediction.basePositionMm, *prediction.targetPositionMm);
                    const double high = std::max(prediction.basePositionMm, *prediction.targetPositionMm);
                    estimate = std::clamp(estimate, low, high);
                }
                holdMm = estimate;
            }
        }

        prediction.basePositionMm = *holdMm;
        prediction.targetPositionMm = *holdMm;
        prediction.velocityMmPerS = 0.0;
        prediction.direction = 0;
        prediction.baseTimestamp = now;
        prediction.active = true;
    };

    std::lock_guard<std::mutex> lock(predictedMotionMutex_);
    if (affectAllAxes || holdXmm.has_value()) {
        holdAxis(predictedXMotion_, holdXmm);
    }
    if (affectAllAxes || holdYmm.has_value()) {
        holdAxis(predictedYMotion_, holdYmm);
    }
}

void MainWindow::updatePredictedMotorMotion(const hardware::MotorAxisSnapshot& xSnapshot, const hardware::MotorAxisSnapshot& ySnapshot)
{
    if (!xSnapshot.positionValid || !ySnapshot.positionValid) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(predictedMotionMutex_);

    const std::optional<QPointF> previousMeasured = lastMeasuredMotorMm_;
    const std::optional<std::chrono::steady_clock::time_point> previousTimestamp = lastMeasuredMotorTimestamp_;

    auto updateAxis = [now, previousMeasured, previousTimestamp](AxisMotionPrediction& prediction,
                                                                 double currentMm,
                                                                 std::optional<double> previousMm) {
        if (!prediction.active) {
            return;
        }

        if (prediction.targetPositionMm.has_value() && prediction.direction == 0) {
            prediction.direction = motionDirection(*prediction.targetPositionMm - currentMm);
        }

        if (previousMm.has_value() && previousTimestamp.has_value()) {
            const double dtS = std::chrono::duration<double>(now - *previousTimestamp).count();
            if (dtS > 1e-4) {
                const double measuredSpeed = std::abs(currentMm - *previousMm) / dtS;
                if (measuredSpeed > 1e-4) {
                    prediction.velocityMmPerS = measuredSpeed;
                }
            }
        }

        prediction.basePositionMm = currentMm;
        prediction.baseTimestamp = now;

        if (prediction.targetPositionMm.has_value()) {
            if (std::abs(*prediction.targetPositionMm - currentMm) <= kOverlayPredictionEpsilonMm) {
                prediction.basePositionMm = *prediction.targetPositionMm;
                prediction.velocityMmPerS = 0.0;
                prediction.direction = 0;
                prediction.active = false;
            } else if (prediction.direction == 0) {
                prediction.direction = motionDirection(*prediction.targetPositionMm - currentMm);
            }
        }
    };

    updateAxis(
        predictedXMotion_,
        xSnapshot.positionMm,
        previousMeasured.has_value() ? std::optional<double>(previousMeasured->x()) : std::nullopt
    );
    updateAxis(
        predictedYMotion_,
        ySnapshot.positionMm,
        previousMeasured.has_value() ? std::optional<double>(previousMeasured->y()) : std::nullopt
    );

    lastMeasuredMotorMm_ = QPointF(xSnapshot.positionMm, ySnapshot.positionMm);
    lastMeasuredMotorTimestamp_ = now;
}

std::optional<QPointF> MainWindow::estimatedMotorPositionForOverlay() const
{
    QPointF displayPosition;
    if (cachedMotorMm_.has_value()) {
        displayPosition = *cachedMotorMm_;
    } else if (lastMeasuredMotorMm_.has_value()) {
        displayPosition = *lastMeasuredMotorMm_;
    } else {
        return std::nullopt;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(predictedMotionMutex_);

    auto estimateAxis = [now](const AxisMotionPrediction& prediction, double fallbackMm) {
        if (!prediction.active) {
            return fallbackMm;
        }

        double estimate = prediction.basePositionMm;
        if (prediction.velocityMmPerS > 0.0 && prediction.direction != 0) {
            const double elapsedS = std::max(0.0, std::chrono::duration<double>(now - prediction.baseTimestamp).count());
            estimate += prediction.direction * prediction.velocityMmPerS * elapsedS;
        }

        if (prediction.targetPositionMm.has_value()) {
            const double low = std::min(prediction.basePositionMm, *prediction.targetPositionMm);
            const double high = std::max(prediction.basePositionMm, *prediction.targetPositionMm);
            estimate = std::clamp(estimate, low, high);
        }
        return estimate;
    };

    displayPosition.setX(estimateAxis(predictedXMotion_, displayPosition.x()));
    displayPosition.setY(estimateAxis(predictedYMotion_, displayPosition.y()));
    return displayPosition;
}

std::optional<QPointF> MainWindow::overlayMotorPositionForDisplay() const
{
    if (stableOverlayMotorMm_.has_value()) {
        return stableOverlayMotorMm_;
    }
    return estimatedMotorPositionForOverlay();
}

void MainWindow::syncSequenceOverlay()
{
    if (cameraPreviewWidget_ == nullptr) {
        return;
    }

    const bool showZone = !sequenceRunning_ || (showZoneCheck_ == nullptr || showZoneCheck_->isChecked());
    const bool showWaypoints = !waypointsMm_.empty()
        && (!sequenceRunning_ || (hideWaypointsCheck_ == nullptr || !hideWaypointsCheck_->isChecked()));

    const std::optional<QPointF> displayMotorMm = cachedMotorMm_;

    // Zone rect
    if (!showZone) {
        cameraPreviewWidget_->clearSequenceOverlay();
    } else {
        std::optional<QPoint> startPoint = sequenceRectStartFrame_.has_value() ? sequenceRectStartFrame_ : sequenceFirstFramePoint_;
        std::optional<QPoint> endPoint = sequenceRectEndFrame_;

        if (sequenceRectFollowSample_ && sequenceStartMotorMm_.has_value() && sequenceEndMotorMm_.has_value() && displayMotorMm.has_value()) {
            try {
                startPoint = motorTargetToFramePoint(*sequenceStartMotorMm_, *displayMotorMm);
                endPoint = motorTargetToFramePoint(*sequenceEndMotorMm_, *displayMotorMm);
            } catch (...) {
            }
        }

        if (!startPoint.has_value()) {
            cameraPreviewWidget_->clearSequenceOverlay();
        } else {
            cameraPreviewWidget_->setSequenceOverlay(
                QPointF(*startPoint),
                true,
                endPoint.has_value() ? QPointF(*endPoint) : QPointF(*startPoint),
                endPoint.has_value()
            );
        }
    }

    // Waypoint dots
    if (!showWaypoints || !displayMotorMm.has_value()) {
        cameraPreviewWidget_->clearWaypointOverlay();
    } else {
        std::vector<QPointF> donePx, remainingPx;
        const int splitIdx = std::max(0, currentWaypointIndex_);
        for (int i = 0; i < static_cast<int>(waypointsMm_.size()); ++i) {
            try {
                const QPoint px = motorTargetToFramePoint(waypointsMm_[static_cast<std::size_t>(i)], *displayMotorMm);
                if (i < splitIdx) {
                    donePx.emplace_back(px.x(), px.y());
                } else {
                    remainingPx.emplace_back(px.x(), px.y());
                }
            } catch (...) {
            }
        }
        cameraPreviewWidget_->setWaypointOverlay(std::move(donePx), std::move(remainingPx));
    }
}

void MainWindow::applyLaserCalibrationEdits()
{
    if (laserXEdit_ == nullptr || laserYEdit_ == nullptr || laserSizeEdit_ == nullptr) {
        return;
    }

    bool xOk = false;
    bool yOk = false;
    bool sizeOk = false;
    const int targetX = laserXEdit_->text().trimmed().toInt(&xOk);
    const int targetY = laserYEdit_->text().trimmed().toInt(&yOk);
    const int targetSize = laserSizeEdit_->text().trimmed().toInt(&sizeOk);

    if (!xOk || !yOk || !sizeOk || targetSize <= 0) {
        QMessageBox::warning(this, "Calibrage", "Les valeurs X, Y et Taille doivent etre des entiers valides.");
        syncCalibrationUi();
        return;
    }

    // Sauvegarde dans le preset de l'objectif sélectionné dans le dialog
    const QString targetObj = calibObjectiveCombo_ != nullptr
        ? calibObjectiveCombo_->currentText()
        : (objectiveCombo_ != nullptr ? objectiveCombo_->currentText() : QString());
    ObjectivePreset* preset = findObjectivePreset(targetObj);
    if (preset != nullptr) {
        preset->laserX       = targetX;
        preset->laserY       = targetY;
        preset->laserRadiusPx = std::max(targetSize, 1);
    }

    // Si l'objectif calibré est le courant, mettre à jour les valeurs runtime
    const QString currentObj = objectiveCombo_ != nullptr ? objectiveCombo_->currentText() : QString();
    if (targetObj == currentObj || targetObj.isEmpty()) {
        laserPointPx_.setX(targetX);
        laserPointPx_.setY(targetY);
        laserRadiusPx_ = std::max(targetSize, 1);
        syncLaserOverlay();
    }
    saveCalibrationPresets();
    appendLog(QString("Cible laser calibree [%1]: X=%2 Y=%3 Taille=%4")
        .arg(targetObj).arg(targetX).arg(targetY).arg(std::max(targetSize, 1)));
}

void MainWindow::nudgeLaserTarget(int dxPx, int dyPx, int dRadiusPx)
{
    int stepPx = 1;
    if (laserMoveStepEdit_ != nullptr) {
        bool ok = false;
        const int parsed = laserMoveStepEdit_->text().trimmed().toInt(&ok);
        if (ok && parsed > 0) {
            stepPx = parsed;
        }
    }

    laserPointPx_.setX(laserPointPx_.x() + (dxPx * stepPx));
    laserPointPx_.setY(laserPointPx_.y() + (dyPx * stepPx));
    if (dRadiusPx != 0) {
        laserRadiusPx_ = std::max(1, laserRadiusPx_ + (dRadiusPx * stepPx));
    }
    syncLaserOverlay();
}

double MainWindow::autoMmPerPxForObjective(const QString& objectiveName) const
{
    const ObjectivePreset* preset = findObjectivePreset(objectiveName.trimmed());
    if (preset == nullptr || preset->magnification <= 0.0) {
        return kDefaultGotoMmPerPx;
    }

    return kCameraPixelPitchUm / (preset->magnification * 1000.0);
}

std::pair<double, double> MainWindow::currentGotoScale() const
{
    const QString objectiveName = objectiveCombo_ != nullptr ? objectiveCombo_->currentText().trimmed() : QString("4x");
    const double mmPerPx = autoMmPerPxForObjective(objectiveName);
    if (mmPerPx <= 0.0) {
        throw std::runtime_error("Les valeurs mm/px X et Y doivent etre positives.");
    }

    double sx = -1.0;
    double sy = 1.0;
    if (gotoInvertXCheck_ != nullptr && gotoInvertXCheck_->isChecked()) {
        sx *= -1.0;
    }
    if (gotoInvertYCheck_ != nullptr && gotoInvertYCheck_->isChecked()) {
        sy *= -1.0;
    }

    return {mmPerPx * sx, mmPerPx * sy};
}

QPointF MainWindow::readMotorPositionsOrThrow() const
{
    hardware::MotorAxisSnapshot xSnapshot;
    hardware::MotorAxisSnapshot ySnapshot;
    {
        std::lock_guard<std::mutex> lock(motorSnapshotMutex_);
        xSnapshot = polledXSnapshot_;
        ySnapshot = polledYSnapshot_;
    }
    if (!xSnapshot.connected || !ySnapshot.connected) {
        throw std::runtime_error("Les moteurs X et Y doivent etre connectes.");
    }
    if (!xSnapshot.positionValid || !ySnapshot.positionValid) {
        throw std::runtime_error("Impossible de lire la position courante des moteurs.");
    }

    return QPointF(xSnapshot.positionMm, ySnapshot.positionMm);
}

QPointF MainWindow::framePointToMotorTarget(const QPoint& framePointPx, const QPointF& baseMotorMm) const
{
    const auto scale = currentGotoScale();
    const int deltaXPx = framePointPx.x() - laserPointPx_.x();
    const int deltaYPx = framePointPx.y() - laserPointPx_.y();

    const double targetX = baseMotorMm.x() + (static_cast<double>(deltaXPx) * scale.first);
    const double targetY = baseMotorMm.y() + (static_cast<double>(deltaYPx) * scale.second);
    if (targetX < 0.0 || targetX > 25.0) {
        throw std::runtime_error(QString("Zone X hors limites (%1 mm)").arg(targetX, 0, 'f', 4).toStdString());
    }
    if (targetY < 0.0 || targetY > 25.0) {
        throw std::runtime_error(QString("Zone Y hors limites (%1 mm)").arg(targetY, 0, 'f', 4).toStdString());
    }

    return QPointF(targetX, targetY);
}

QPoint MainWindow::motorTargetToFramePoint(const QPointF& motorTargetMm, const QPointF& currentMotorMm) const
{
    const auto scale = currentGotoScale();
    if (scale.first == 0.0 || scale.second == 0.0) {
        throw std::runtime_error("Echelle GoTo invalide");
    }

    const int frameX = static_cast<int>(std::lround(static_cast<double>(laserPointPx_.x()) + ((motorTargetMm.x() - currentMotorMm.x()) / scale.first)));
    const int frameY = static_cast<int>(std::lround(static_cast<double>(laserPointPx_.y()) + ((motorTargetMm.y() - currentMotorMm.y()) / scale.second)));
    return QPoint(frameX, frameY);
}

std::vector<QPointF> MainWindow::buildWaypointsLinear(const QPointF& startMm, const QPointF& endMm, double stepMm) const
{
    const double dx = endMm.x() - startMm.x();
    const double dy = endMm.y() - startMm.y();
    const double distance = std::hypot(dx, dy);
    if (distance < 1e-9) {
        return {startMm};
    }

    const int steps = std::max(1, static_cast<int>(std::lround(distance / stepMm)));
    std::vector<QPointF> waypoints;
    waypoints.reserve(static_cast<std::size_t>(steps + 1));
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        waypoints.emplace_back(startMm.x() + (dx * t), startMm.y() + (dy * t));
    }
    return waypoints;
}

std::vector<QPointF> MainWindow::buildWaypointsRect(const QPointF& startMm, const QPointF& endMm, double stepMm) const
{
    const double xMin = std::min(startMm.x(), endMm.x());
    const double xMax = std::max(startMm.x(), endMm.x());
    const double yMin = std::min(startMm.y(), endMm.y());
    const double yMax = std::max(startMm.y(), endMm.y());

    // Use ceil to ensure the zone covers all points at the exact step distance.
    // The zone is slightly enlarged if the span is not an exact multiple of stepMm.
    const int xIntervals = std::max(1, static_cast<int>(std::ceil((xMax - xMin) / stepMm - 1e-9)));
    const int yIntervals = std::max(1, static_cast<int>(std::ceil((yMax - yMin) / stepMm - 1e-9)));
    const int cols = xIntervals + 1;
    const int rows = yIntervals + 1;
    const double effectiveXSpan = static_cast<double>(xIntervals) * stepMm;
    const double effectiveYSpan = static_cast<double>(yIntervals) * stepMm;

    const auto scale = currentGotoScale();
    const bool visualLeftToRightIsIncreasingX = scale.first > 0.0;
    const bool visualTopToBottomIsIncreasingY = scale.second > 0.0;

    std::vector<double> xRangeVisual;
    xRangeVisual.reserve(static_cast<std::size_t>(cols));
    for (int i = 0; i < cols; ++i) {
        const double pos = xMin + static_cast<double>(i) * stepMm;
        xRangeVisual.push_back(
            visualLeftToRightIsIncreasingX
                ? pos
                : (xMin + effectiveXSpan - static_cast<double>(i) * stepMm));
    }

    std::vector<double> yRangeVisual;
    yRangeVisual.reserve(static_cast<std::size_t>(rows));
    for (int j = 0; j < rows; ++j) {
        const double pos = yMin + static_cast<double>(j) * stepMm;
        yRangeVisual.push_back(
            visualTopToBottomIsIncreasingY
                ? pos
                : (yMin + effectiveYSpan - static_cast<double>(j) * stepMm));
    }

    std::vector<QPointF> waypoints;
    waypoints.reserve(static_cast<std::size_t>(rows * cols));
    for (const auto& [row, col] : buildRectangleTraversalOrder(rows, cols)) {
        waypoints.emplace_back(
            xRangeVisual[static_cast<std::size_t>(col)],
            yRangeVisual[static_cast<std::size_t>(row)]);
    }

    return waypoints;
}

MainWindow::ScanConfig::RectangleStartCorner MainWindow::effectiveRectangleStartCorner() const
{
    if (scanConfig_.rectangleTraversalExplicit) {
        return scanConfig_.rectangleStartCorner;
    }

    try {
        const auto scale = currentGotoScale();
        return legacyRectangleStartCornerForScale(scale.first, scale.second);
    } catch (...) {
        return ScanConfig::RectangleStartCorner::TopLeft;
    }
}

MainWindow::ScanConfig::RectanglePrimaryAxis MainWindow::effectiveRectanglePrimaryAxis() const
{
    if (scanConfig_.rectangleTraversalExplicit) {
        return scanConfig_.rectanglePrimaryAxis;
    }

    return legacyRectanglePrimaryAxis();
}

std::vector<std::pair<int, int>> MainWindow::buildRectangleTraversalOrder(int rows, int cols) const
{
    std::vector<std::pair<int, int>> order;
    for (const GridTraversalEntry& entry : buildRectangleTraversalEntries(
             rows,
             cols,
             effectiveRectangleStartCorner(),
             effectiveRectanglePrimaryAxis())) {
        order.emplace_back(entry.row, entry.col);
    }
    return order;
}

// ── Ruler measurement tool ────────────────────────────────────────────────────

void MainWindow::onToggleRuler()
{
    rulerArmed_ = !rulerArmed_;

    if (rulerArmed_) {
        // Disarm other modes
        if (gotoArmed_)            setGotoArmed(false);
        if (sequenceSelectArmed_) { setSequenceSelectArmed(false); clearSequencePreviewSelection(); }
        rulerHasP1_ = false;
        rulerHasP2_ = false;
        if (cameraPreviewWidget_ != nullptr) cameraPreviewWidget_->clearRulerOverlay();
        if (rulerDistanceLabel_ != nullptr)  rulerDistanceLabel_->setText("Cliquez P1");
        if (rulerButton_ != nullptr)         rulerButton_->setText("Regle ON");
    } else {
        if (rulerButton_ != nullptr) rulerButton_->setText("Regle");
        if (rulerDistanceLabel_ != nullptr && !rulerHasP2_)
            rulerDistanceLabel_->setText("---");
    }
    updatePreviewCursor();
}

QString MainWindow::computeRulerDistanceText() const
{
    if (!rulerHasP1_ || !rulerHasP2_) return {};

    const QPointF delta = rulerP2Px_ - rulerP1Px_;
    const double distPx = std::hypot(delta.x(), delta.y());

    const QString objName = (objectiveCombo_ != nullptr)
        ? objectiveCombo_->currentText().trimmed()
        : QString("4x");
    const double mmPerPx = autoMmPerPxForObjective(objName);
    const double distUm  = distPx * mmPerPx * 1000.0;

    if (distUm >= 1000.0)
        return QString("%1 mm").arg(distUm / 1000.0, 0, 'f', 3);
    return QString("%1 µm").arg(distUm, 0, 'f', 1);
}

void MainWindow::updateRulerOverlay()
{
    if (cameraPreviewWidget_ == nullptr) return;

    if (!rulerHasP1_) {
        cameraPreviewWidget_->clearRulerOverlay();
        return;
    }

    const QString distText = computeRulerDistanceText();
    cameraPreviewWidget_->setRulerOverlay(rulerP1Px_, rulerHasP2_, rulerP2Px_, distText);

    if (rulerDistanceLabel_ != nullptr) {
        rulerDistanceLabel_->setText(rulerHasP2_ ? distText : "Cliquez P2");
    }
}

// ── Helper: disarm all measure tools ─────────────────────────────────────────

void MainWindow::disarmAllMeasureTools()
{
    if (rulerArmed_) {
        rulerArmed_ = false;
        if (rulerButton_ != nullptr) rulerButton_->setText("Regle");
    }
    if (circleArmed_) {
        circleArmed_ = false;
        if (circleButton_ != nullptr) circleButton_->setText("Cercle");
    }
    if (rectArmed_) {
        rectArmed_ = false;
        if (rectButton_ != nullptr) rectButton_->setText("Rect.");
    }
}

// ── Circle measurement tool ───────────────────────────────────────────────────

void MainWindow::onToggleCircle()
{
    circleArmed_ = !circleArmed_;
    if (circleArmed_) {
        if (gotoArmed_)            setGotoArmed(false);
        if (sequenceSelectArmed_) { setSequenceSelectArmed(false); clearSequencePreviewSelection(); }
        rulerArmed_ = false;
        if (rulerButton_ != nullptr) rulerButton_->setText("Regle");
        rectArmed_ = false;
        if (rectButton_ != nullptr) rectButton_->setText("Rect.");
        circleHasCenter_ = false;
        circleHasEdge_   = false;
        if (cameraPreviewWidget_ != nullptr) cameraPreviewWidget_->clearCircleOverlay();
        if (circleDiameterLabel_ != nullptr) circleDiameterLabel_->setText("Cliquez centre");
        if (circleButton_ != nullptr)        circleButton_->setText("Cercle ON");
    } else {
        if (circleButton_ != nullptr) circleButton_->setText("Cercle");
        if (circleDiameterLabel_ != nullptr && !circleHasEdge_)
            circleDiameterLabel_->setText("---");
    }
    updatePreviewCursor();
}

QString MainWindow::computeCircleDiameterText() const
{
    if (!circleHasCenter_ || !circleHasEdge_) return {};
    const QPointF delta = circleEdgePx_ - circleCenterPx_;
    const double radiusPx  = std::hypot(delta.x(), delta.y());
    const double diameterPx = radiusPx * 2.0;
    const QString obj = (objectiveCombo_ != nullptr)
        ? objectiveCombo_->currentText().trimmed() : QString("4x");
    const double mmPerPx  = autoMmPerPxForObjective(obj);
    const double diamUm   = diameterPx * mmPerPx * 1000.0;
    if (diamUm >= 1000.0)
        return QString("d = %1 mm").arg(diamUm / 1000.0, 0, 'f', 3);
    return QString("d = %1 µm").arg(diamUm, 0, 'f', 1);
}

void MainWindow::updateCircleOverlay()
{
    if (cameraPreviewWidget_ == nullptr) return;
    if (!circleHasCenter_) { cameraPreviewWidget_->clearCircleOverlay(); return; }
    const QString text = computeCircleDiameterText();
    cameraPreviewWidget_->setCircleOverlay(circleCenterPx_, circleHasEdge_, circleEdgePx_, text);
    if (circleDiameterLabel_ != nullptr)
        circleDiameterLabel_->setText(circleHasEdge_ ? text : "Cliquez bord");
}

// ── Rectangle measurement tool ────────────────────────────────────────────────

void MainWindow::onToggleRect()
{
    rectArmed_ = !rectArmed_;
    if (rectArmed_) {
        if (gotoArmed_)            setGotoArmed(false);
        if (sequenceSelectArmed_) { setSequenceSelectArmed(false); clearSequencePreviewSelection(); }
        rulerArmed_ = false;
        if (rulerButton_ != nullptr) rulerButton_->setText("Regle");
        circleArmed_ = false;
        if (circleButton_ != nullptr) circleButton_->setText("Cercle");
        rectHasP1_ = false;
        rectHasP2_ = false;
        if (cameraPreviewWidget_ != nullptr) cameraPreviewWidget_->clearRectOverlay();
        if (rectSizeLabel_ != nullptr) rectSizeLabel_->setText("Cliquez coin 1");
        if (rectButton_ != nullptr)    rectButton_->setText("Rect. ON");
    } else {
        if (rectButton_ != nullptr) rectButton_->setText("Rect.");
        if (rectSizeLabel_ != nullptr && !rectHasP2_)
            rectSizeLabel_->setText("---");
    }
    updatePreviewCursor();
}

QString MainWindow::computeRectSizeText() const
{
    if (!rectHasP1_ || !rectHasP2_) return {};
    const QPointF delta = rectP2Px_ - rectP1Px_;
    const QString obj = (objectiveCombo_ != nullptr)
        ? objectiveCombo_->currentText().trimmed() : QString("4x");
    const double mmPerPx = autoMmPerPxForObjective(obj);
    const double wUm = std::abs(delta.x()) * mmPerPx * 1000.0;
    const double hUm = std::abs(delta.y()) * mmPerPx * 1000.0;
    const auto fmt = [](double v) -> QString {
        if (v >= 1000.0) return QString("%1 mm").arg(v / 1000.0, 0, 'f', 3);
        return QString("%1 µm").arg(v, 0, 'f', 1);
    };
    return QString("%1 x %2").arg(fmt(wUm), fmt(hUm));
}

void MainWindow::updateRectOverlay()
{
    if (cameraPreviewWidget_ == nullptr) return;
    if (!rectHasP1_) { cameraPreviewWidget_->clearRectOverlay(); return; }
    const QString text = computeRectSizeText();
    cameraPreviewWidget_->setRectOverlay(rectP1Px_, rectHasP2_, rectP2Px_, text);
    if (rectSizeLabel_ != nullptr)
        rectSizeLabel_->setText(rectHasP2_ ? text : "Cliquez coin 2");
}

// ── Capture position ─────────────────────────────────────────────────────────

void MainWindow::onCapturePosition()
{
    const auto pos = latestPolledMotorPosition();

    if (!capturedMotorPos_) {
        // ── State A → B: store reference ──────────────────────────────────────
        if (!pos) {
            QMessageBox::warning(this, "Capture", "Position moteur non disponible.\nConnectez les moteurs et attendez la première lecture.");
            return;
        }
        capturedMotorPos_ = pos;
        captureButton_->setText("Delta");
        captureButton_->setProperty("accent", false);
        captureButton_->setStyleSheet(
            "QPushButton { background:#f59e0b; color:#fff; border-radius:6px; "
            "padding:5px 10px; font-weight:600; } "
            "QPushButton:hover { background:#d97706; }");
        captureDeltaLabel_->setText(
            QString("Ref: X=%1\nY=%2 mm")
                .arg(pos->x(), 0, 'f', 4)
                .arg(pos->y(), 0, 'f', 4));
    } else {
        // ── State B → A: compute and show delta ───────────────────────────────
        if (pos) {
            const double dx = pos->x() - capturedMotorPos_->x();
            const double dy = pos->y() - capturedMotorPos_->y();
            const double dist = std::hypot(dx, dy);
            captureDeltaLabel_->setText(
                QString("\u0394X=%1\n\u0394Y=%2\nDist=%3 mm")
                    .arg(dx,   0, 'f', 4)
                    .arg(dy,   0, 'f', 4)
                    .arg(dist, 0, 'f', 4));
        } else {
            captureDeltaLabel_->setText("Position indisponible");
        }
        // Reset button
        capturedMotorPos_.reset();
        captureButton_->setText("Capturer ref.");
        captureButton_->setProperty("accent", true);
        captureButton_->setStyleSheet("");
        captureButton_->style()->unpolish(captureButton_);
        captureButton_->style()->polish(captureButton_);
    }
}

// ── Double-click: delete nearest shape ───────────────────────────────────────

void MainWindow::onPreviewFrameDoubleClicked(const QPoint& framePointPx)
{
    const QPointF p(framePointPx);
    constexpr double kTol = 18.0;  // frame-pixel tolerance for hit detection

    // Distance from point to segment AB
    const auto distToSegment = [](const QPointF& pt, const QPointF& a, const QPointF& b) {
        const QPointF ab = b - a;
        const double len2 = ab.x()*ab.x() + ab.y()*ab.y();
        if (len2 < 1e-9) return std::hypot(pt.x()-a.x(), pt.y()-a.y());
        const double t = std::clamp(((pt.x()-a.x())*ab.x() + (pt.y()-a.y())*ab.y()) / len2,
                                     0.0, 1.0);
        return std::hypot(pt.x() - (a.x() + t*ab.x()), pt.y() - (a.y() + t*ab.y()));
    };

    // ── Ruler ──
    if (rulerHasP1_) {
        bool hit = std::hypot(p.x()-rulerP1Px_.x(), p.y()-rulerP1Px_.y()) < kTol;
        if (!hit && rulerHasP2_)
            hit = distToSegment(p, rulerP1Px_, rulerP2Px_) < kTol;
        if (hit) {
            rulerHasP1_ = rulerHasP2_ = false;
            if (cameraPreviewWidget_ != nullptr) cameraPreviewWidget_->clearRulerOverlay();
            if (rulerDistanceLabel_ != nullptr)  rulerDistanceLabel_->setText("---");
            return;
        }
    }

    // ── Circle ──
    if (circleHasCenter_) {
        bool hit = std::hypot(p.x()-circleCenterPx_.x(), p.y()-circleCenterPx_.y()) < kTol;
        if (!hit && circleHasEdge_) {
            const double r = std::hypot(circleEdgePx_.x()-circleCenterPx_.x(),
                                         circleEdgePx_.y()-circleCenterPx_.y());
            const double dFromCenter = std::hypot(p.x()-circleCenterPx_.x(),
                                                   p.y()-circleCenterPx_.y());
            hit = std::abs(dFromCenter - r) < kTol          // near outline
               || std::hypot(p.x()-circleEdgePx_.x(),       // near edge point
                              p.y()-circleEdgePx_.y()) < kTol;
        }
        if (hit) {
            circleHasCenter_ = circleHasEdge_ = false;
            if (cameraPreviewWidget_ != nullptr) cameraPreviewWidget_->clearCircleOverlay();
            if (circleDiameterLabel_ != nullptr) circleDiameterLabel_->setText("---");
            return;
        }
    }

    // ── Rectangle ──
    if (rectHasP1_) {
        bool hit = std::hypot(p.x()-rectP1Px_.x(), p.y()-rectP1Px_.y()) < kTol;
        if (!hit && rectHasP2_) {
            // Check proximity to any of the 4 sides
            const QPointF tl(std::min(rectP1Px_.x(), rectP2Px_.x()),
                              std::min(rectP1Px_.y(), rectP2Px_.y()));
            const QPointF br(std::max(rectP1Px_.x(), rectP2Px_.x()),
                              std::max(rectP1Px_.y(), rectP2Px_.y()));
            const QPointF tr(br.x(), tl.y()), bl(tl.x(), br.y());
            hit = distToSegment(p, tl, tr) < kTol
               || distToSegment(p, tr, br) < kTol
               || distToSegment(p, br, bl) < kTol
               || distToSegment(p, bl, tl) < kTol;
        }
        if (hit) {
            rectHasP1_ = rectHasP2_ = false;
            if (cameraPreviewWidget_ != nullptr) cameraPreviewWidget_->clearRectOverlay();
            if (rectSizeLabel_ != nullptr) rectSizeLabel_->setText("---");
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::onArmGoto()
{
    if (sequenceSelectArmed_) {
        setSequenceSelectArmed(false);
        clearSequencePreviewSelection();
    }

    if (!cameraController_->isLive()) {
        setGotoArmed(false);
        QMessageBox::warning(this, "GoTo", "Demarrer d'abord le flux live.");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(motorSnapshotMutex_);
        if (!polledXSnapshot_.connected || !polledYSnapshot_.connected) {
            setGotoArmed(false);
            QMessageBox::warning(this, "GoTo", "Connecter d'abord les moteurs X et Y.");
            return;
        }
    }

    try {
        currentGotoScale();
    } catch (const std::exception& ex) {
        setGotoArmed(false);
        QMessageBox::warning(this, "GoTo", QString::fromUtf8(ex.what()));
        return;
    }

    setGotoArmed(true);
    statusBar()->showMessage("GoTo arme : cliquer dans l'image", 3000);
    appendLog("GoTo arme. Clique dans l'image pour deplacer la platine.");
}

void MainWindow::onSetSequenceStart()
{
    try {
        const QPointF positionMm = readMotorPositionsOrThrow();
        sequenceStartMotorMm_ = positionMm;
        clearSequencePreviewSelection();
        if (sequenceStartLabel_ != nullptr) {
            sequenceStartLabel_->setText(QString("Depart  : X=%1  Y=%2 mm").arg(positionMm.x(), 0, 'f', 4).arg(positionMm.y(), 0, 'f', 4));
        }
        appendLog(QString("Point de depart enregistre : X=%1 Y=%2 mm").arg(positionMm.x(), 0, 'f', 4).arg(positionMm.y(), 0, 'f', 4));
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "Zone image", QString::fromUtf8(ex.what()));
    }
}

void MainWindow::onSetSequenceEnd()
{
    try {
        const QPointF positionMm = readMotorPositionsOrThrow();
        sequenceEndMotorMm_ = positionMm;
        clearSequencePreviewSelection();
        if (sequenceEndLabel_ != nullptr) {
            sequenceEndLabel_->setText(QString("Arrivee : X=%1  Y=%2 mm").arg(positionMm.x(), 0, 'f', 4).arg(positionMm.y(), 0, 'f', 4));
        }
        appendLog(QString("Point d'arrivee enregistre : X=%1 Y=%2 mm").arg(positionMm.x(), 0, 'f', 4).arg(positionMm.y(), 0, 'f', 4));
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "Zone image", QString::fromUtf8(ex.what()));
    }
}

void MainWindow::onArmSequenceRectangle()
{
    if (sequenceSelectArmed_) {
        setSequenceSelectArmed(false);
        clearSequencePreviewSelection();
        appendLog("Zone image annulee.");
        return;
    }

    if (!cameraController_->isLive()) {
        QMessageBox::warning(this, "Zone image", "Demarrer d'abord le flux live.");
        return;
    }

    try {
        const QPointF baseMotorMm = readMotorPositionsOrThrow();
        currentGotoScale();
        setGotoArmed(false);
        sequenceFirstFramePoint_.reset();
        sequenceRectStartFrame_.reset();
        sequenceRectEndFrame_.reset();
        sequenceBaseMotorMm_ = baseMotorMm;
        sequenceRectFollowSample_ = false;
        cachedMotorMm_ = baseMotorMm;
        stableOverlayMotorMm_ = baseMotorMm;
        statusBar()->showMessage("Zone image : cliquer le 1er coin", 4000);
        setSequenceSelectArmed(true);
        appendLog(QString("Zone image armee depuis X=%1 Y=%2 mm.").arg(baseMotorMm.x(), 0, 'f', 4).arg(baseMotorMm.y(), 0, 'f', 4));
        syncSequenceOverlay();
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "Zone image", QString::fromUtf8(ex.what()));
    }
}

void MainWindow::onRunSequence()
{
    if (sequenceRunning_) {
        QMessageBox::warning(this, "Sequence", "Une sequence est deja en cours.");
        return;
    }
    if (motorTaskRunning_) {
        QMessageBox::warning(this, "Sequence", "Une autre operation moteur est en cours.");
        return;
    }
    if (!sequenceStartMotorMm_.has_value()) {
        QMessageBox::warning(this, "Sequence", "Definir d'abord le point de depart.");
        return;
    }
    if (!sequenceEndMotorMm_.has_value()) {
        QMessageBox::warning(this, "Sequence", "Definir d'abord le point d'arrivee.");
        return;
    }

    bool stepOk = false;
    bool durationOk = false;
    const double stepMm = sequenceStepMmEdit_ != nullptr ? sequenceStepMmEdit_->text().trimmed().toDouble(&stepOk) : 0.0;
    const double durationS = sequenceDurationEdit_ != nullptr ? sequenceDurationEdit_->text().trimmed().toDouble(&durationOk) : 0.0;
    if (!stepOk || !durationOk || stepMm <= 0.0 || durationS <= 0.0) {
        QMessageBox::warning(this, "Sequence", "Pas (mm) et Duree/pt doivent etre des nombres positifs.");
        return;
    }

    const QPointF startMm = *sequenceStartMotorMm_;
    const QPointF endMm = *sequenceEndMotorMm_;
    const QString mode = sequenceModeCombo_ != nullptr ? sequenceModeCombo_->currentText() : QString("Lineaire");
    std::vector<QPointF> waypoints = mode == "Rectangle"
        ? buildWaypointsRect(startMm, endMm, stepMm)
        : buildWaypointsLinear(startMm, endMm, stepMm);

    if (waypoints.empty()) {
        QMessageBox::warning(this, "Sequence", "Depart et arrivee sont au meme endroit.");
        return;
    }

    double stageSpeedMmPerS = 0.0;
    try {
        stageSpeedMmPerS = readJogSpeedMmPerS();
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "Sequence", QString::fromUtf8(ex.what()));
        return;
    }

    waypointsMm_ = waypoints;
    currentWaypointIndex_ = 0;
    sequenceStopRequested_.store(false);
    setSequenceRunning(true);
    const int total = static_cast<int>(waypoints.size());
    if (sequenceStatusLabel_ != nullptr) {
        sequenceStatusLabel_->setText(QString("0 / %1 points").arg(total));
    }
    appendLog(
        QString("Sequence %1 : (%2,%3) -> (%4,%5) | %6 points | pas=%7 mm | %8 s/pt | moteur=(%9)")
            .arg(mode == "Rectangle" ? "rectangle (serpentin)" : "lineaire")
            .arg(startMm.x(), 0, 'f', 4)
            .arg(startMm.y(), 0, 'f', 4)
            .arg(endMm.x(), 0, 'f', 4)
            .arg(endMm.y(), 0, 'f', 4)
            .arg(total)
            .arg(stepMm, 0, 'f', 4)
            .arg(durationS, 0, 'f', 3)
            .arg(currentMotorPositionLogText(motorController_))
    );

    if (sequenceThread_.joinable()) {
        sequenceThread_.join();
    }

    auto controller = motorController_;
    sequenceThread_ = std::thread([this, controller, waypoints = std::move(waypoints), durationS, total, stageSpeedMmPerS]() mutable {
        using namespace std::chrono_literals;
        try {
            const QPointF firstPoint = waypoints.front();
            const auto initialBoth = controller->snapshotBoth();
            const hardware::MotorAxisSnapshot& initialXSnapshot = initialBoth.x;
            const hardware::MotorAxisSnapshot& initialYSnapshot = initialBoth.y;
            controller->setVelocity(hardware::AxisId::X, stageSpeedMmPerS);
            controller->setVelocity(hardware::AxisId::Y, stageSpeedMmPerS);
            if (initialXSnapshot.positionValid && initialYSnapshot.positionValid) {
                startPredictedMotorMotion(
                    initialXSnapshot.positionMm,
                    initialYSnapshot.positionMm,
                    firstPoint.x(),
                    firstPoint.y(),
                    stageSpeedMmPerS,
                    stageSpeedMmPerS
                );
            }
            const QString initialMotorText = formatMotorPositionLogText(initialXSnapshot, initialYSnapshot);
            QMetaObject::invokeMethod(this, [this, firstPoint, initialMotorText]() {
                if (sequenceStatusLabel_ != nullptr) {
                    sequenceStatusLabel_->setText("Mise en position...");
                }
                appendLog(QString("Mise en position vers depart (%1, %2) mm... moteur=(%3)")
                    .arg(firstPoint.x(), 0, 'f', 4)
                    .arg(firstPoint.y(), 0, 'f', 4)
                    .arg(initialMotorText));
            }, Qt::QueuedConnection);

            controller->moveAbsoluteNoWait(hardware::AxisId::X, firstPoint.x());
            controller->moveAbsoluteNoWait(hardware::AxisId::Y, firstPoint.y());
            controller->waitAxis(hardware::AxisId::X, kDefaultMotorTimeoutMs);
            controller->waitAxis(hardware::AxisId::Y, kDefaultMotorTimeoutMs);
            stopPredictedMotorMotion(firstPoint.x(), firstPoint.y());

            if (sequenceStopRequested_.load()) {
                stopPredictedMotorMotion();
                QMetaObject::invokeMethod(this, [this]() {
                    if (sequenceStatusLabel_ != nullptr) {
                        sequenceStatusLabel_->setText("Arrete.");
                    }
                    appendLog("Sequence arretee par l'utilisateur.");
                    setSequenceRunning(false);
                }, Qt::QueuedConnection);
                return;
            }

            const QString startMotorText = currentMotorPositionLogText(controller);
            QMetaObject::invokeMethod(this, [this, startMotorText]() {
                appendLog(QString("Point de depart atteint. Debut du balayage. moteur=(%1)")
                    .arg(startMotorText));
            }, Qt::QueuedConnection);

            for (int index = 0; index < total; ++index) {
                if (sequenceStopRequested_.load()) {
                    stopPredictedMotorMotion();
                    QMetaObject::invokeMethod(this, [this]() {
                        if (sequenceStatusLabel_ != nullptr) {
                            sequenceStatusLabel_->setText("Arrete.");
                        }
                        appendLog("Sequence arretee par l'utilisateur.");
                        setSequenceRunning(false);
                    }, Qt::QueuedConnection);
                    return;
                }

                const QPointF waypoint = waypoints[static_cast<std::size_t>(index)];
                // Determine which axes actually need to move by comparing to the
                // previous waypoint (avoids motor noise / settling issues with live snapshot).
                const bool needMoveX = (index == 0)
                    || std::abs(waypoint.x() - waypoints[static_cast<std::size_t>(index - 1)].x()) > 1e-6;
                const bool needMoveY = (index == 0)
                    || std::abs(waypoint.y() - waypoints[static_cast<std::size_t>(index - 1)].y()) > 1e-6;
                QMetaObject::invokeMethod(this, [this, index, total, waypoint]() {
                    currentWaypointIndex_ = index;
                    if (sequenceStatusLabel_ != nullptr) {
                        sequenceStatusLabel_->setText(QString("%1 / %2 - (%3, %4)")
                            .arg(index + 1)
                            .arg(total)
                            .arg(waypoint.x(), 0, 'f', 4)
                            .arg(waypoint.y(), 0, 'f', 4));
                    }
                }, Qt::QueuedConnection);

                const auto stepBoth = controller->snapshotBoth();

                if (stepBoth.x.positionValid && stepBoth.y.positionValid
                    && (needMoveX || needMoveY)) {
                    startPredictedMotorMotion(
                        stepBoth.x.positionMm,
                        stepBoth.y.positionMm,
                        waypoint.x(),
                        waypoint.y(),
                        stageSpeedMmPerS,
                        stageSpeedMmPerS
                    );
                }

                if (needMoveX) controller->moveAbsoluteNoWait(hardware::AxisId::X, waypoint.x());
                if (needMoveY) controller->moveAbsoluteNoWait(hardware::AxisId::Y, waypoint.y());
                if (needMoveX) controller->waitAxis(hardware::AxisId::X, kDefaultMotorTimeoutMs);
                if (needMoveY) controller->waitAxis(hardware::AxisId::Y, kDefaultMotorTimeoutMs);
                stopPredictedMotorMotion(waypoint.x(), waypoint.y());
                const QString waypointMotorText = currentMotorPositionLogText(controller);
                QMetaObject::invokeMethod(this, [this, index, total, waypoint, waypointMotorText]() {
                    appendLog(QString("[SEQ] pt=%1/%2 cible=(%3,%4) mm moteur=(%5)")
                        .arg(index + 1)
                        .arg(total)
                        .arg(waypoint.x(), 0, 'f', 4)
                        .arg(waypoint.y(), 0, 'f', 4)
                        .arg(waypointMotorText));
                }, Qt::QueuedConnection);

                if (sequenceStopRequested_.load()) {
                    stopPredictedMotorMotion();
                    QMetaObject::invokeMethod(this, [this]() {
                        if (sequenceStatusLabel_ != nullptr) {
                            sequenceStatusLabel_->setText("Arrete.");
                        }
                        setSequenceRunning(false);
                    }, Qt::QueuedConnection);
                    return;
                }

                const auto dwell = std::chrono::duration<double>(durationS);
                const auto dwellStart = std::chrono::steady_clock::now();
                while (std::chrono::steady_clock::now() - dwellStart < dwell) {
                    if (sequenceStopRequested_.load()) {
                        stopPredictedMotorMotion();
                        QMetaObject::invokeMethod(this, [this]() {
                            if (sequenceStatusLabel_ != nullptr) {
                                sequenceStatusLabel_->setText("Arrete.");
                            }
                            appendLog("Sequence arretee par l'utilisateur.");
                            setSequenceRunning(false);
                        }, Qt::QueuedConnection);
                        return;
                    }
                    std::this_thread::sleep_for(20ms);
                }
            }

            const QString finalMotorText = currentMotorPositionLogText(controller);
            QMetaObject::invokeMethod(this, [this, total, finalMotorText]() {
                currentWaypointIndex_ = total;
                if (sequenceStatusLabel_ != nullptr) {
                    sequenceStatusLabel_->setText(QString("Termine (%1 points).").arg(total));
                }
                appendLog(QString("Sequence terminee. moteur=(%1)").arg(finalMotorText));
                setSequenceRunning(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            stopPredictedMotorMotion();
            QMetaObject::invokeMethod(this, [this, message = QString::fromUtf8(ex.what())]() {
                if (sequenceStatusLabel_ != nullptr) {
                    sequenceStatusLabel_->setText("ERREUR : " + message);
                }
                appendLog("Sequence echouee : " + message);
                setSequenceRunning(false);
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::onStopSequence()
{
    if (!sequenceRunning_) {
        return;
    }

    sequenceStopRequested_.store(true);
    if (const auto estimatedPosition = estimatedMotorPositionForOverlay(); estimatedPosition.has_value()) {
        stopPredictedMotorMotion(estimatedPosition->x(), estimatedPosition->y());
    } else {
        stopPredictedMotorMotion();
    }
    try {
        motorController_->stopAxis(hardware::AxisId::X);
        motorController_->stopAxis(hardware::AxisId::Y);
    } catch (...) {
    }
    if (sequenceStatusLabel_ != nullptr) {
        sequenceStatusLabel_->setText("Arret demande...");
    }
}

void MainWindow::onPreviewFrameClicked(const QPoint& framePointPx)
{
    // ── Ruler mode ────────────────────────────────────────────────────────────
    if (rulerArmed_) {
        if (!rulerHasP1_ || rulerHasP2_) {
            rulerP1Px_ = QPointF(framePointPx);
            rulerHasP1_ = true;
            rulerHasP2_ = false;
        } else {
            rulerP2Px_ = QPointF(framePointPx);
            rulerHasP2_ = true;
        }
        updateRulerOverlay();
        return;
    }

    // ── Circle mode ───────────────────────────────────────────────────────────
    if (circleArmed_) {
        if (!circleHasCenter_ || circleHasEdge_) {
            circleCenterPx_  = QPointF(framePointPx);
            circleHasCenter_ = true;
            circleHasEdge_   = false;
        } else {
            circleEdgePx_  = QPointF(framePointPx);
            circleHasEdge_ = true;
        }
        updateCircleOverlay();
        return;
    }

    // ── Rectangle mode ────────────────────────────────────────────────────────
    if (rectArmed_) {
        if (!rectHasP1_ || rectHasP2_) {
            rectP1Px_  = QPointF(framePointPx);
            rectHasP1_ = true;
            rectHasP2_ = false;
        } else {
            rectP2Px_  = QPointF(framePointPx);
            rectHasP2_ = true;
        }
        updateRectOverlay();
        return;
    }

    if (sequenceSelectArmed_) {
        if (!sequenceBaseMotorMm_.has_value()) {
            setSequenceSelectArmed(false);
            return;
        }

        if (!sequenceFirstFramePoint_.has_value()) {
            sequenceFirstFramePoint_ = framePointPx;
            sequenceRectStartFrame_ = framePointPx;
            sequenceRectEndFrame_ = framePointPx;
            statusBar()->showMessage("Zone image : cliquer le 2ème coin", 4000);
            appendLog(QString("Zone image: premier coin px=(%1,%2)").arg(framePointPx.x()).arg(framePointPx.y()));
            syncSequenceOverlay();
            return;
        }

        try {
            const QPointF startMm = framePointToMotorTarget(*sequenceFirstFramePoint_, *sequenceBaseMotorMm_);
            const QPointF endMm = framePointToMotorTarget(framePointPx, *sequenceBaseMotorMm_);
            sequenceRectStartFrame_ = *sequenceFirstFramePoint_;
            sequenceRectEndFrame_ = framePointPx;
            sequenceFirstFramePoint_.reset();
            sequenceBaseMotorMm_.reset();
            sequenceRectFollowSample_ = true;
            updateSequenceLabels(startMm, endMm);
            if (sequenceModeCombo_ != nullptr) {
                sequenceModeCombo_->setCurrentText("Rectangle");
            }
            statusBar()->showMessage("Zone image : zone definie", 3000);
            setSequenceSelectArmed(false);
            appendLog(
                QString("Zone image definie: (%1,%2) -> (%3,%4) mm")
                    .arg(startMm.x(), 0, 'f', 4)
                    .arg(startMm.y(), 0, 'f', 4)
                    .arg(endMm.x(), 0, 'f', 4)
                    .arg(endMm.y(), 0, 'f', 4)
            );
            syncSequenceOverlay();
            QTimer::singleShot(0, this, &MainWindow::showScanConfigDialog);
        } catch (const std::exception& ex) {
            setSequenceSelectArmed(false);
            clearSequencePreviewSelection();
            QMessageBox::warning(this, "Zone image", QString::fromUtf8(ex.what()));
        }
        return;
    }

    if (!gotoArmed_) {
        return;
    }

    setGotoArmed(false);

    const int targetXPx = framePointPx.x();
    const int targetYPx = framePointPx.y();
    const int deltaXPx = targetXPx - laserPointPx_.x();
    const int deltaYPx = targetYPx - laserPointPx_.y();

    if (deltaXPx == 0 && deltaYPx == 0) {
        appendLog("GoTo: le point clique est deja sur le laser");
        return;
    }

    double scaleX = 0.0;
    double scaleY = 0.0;
    try {
        const auto scale = currentGotoScale();
        scaleX = scale.first;
        scaleY = scale.second;
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "GoTo", QString::fromUtf8(ex.what()));
        return;
    }

    double moveXMm = static_cast<double>(deltaXPx) * scaleX;
    double moveYMm = static_cast<double>(deltaYPx) * scaleY;

    bool ok = false;
    if (gotoCorrXpEdit_ != nullptr && moveXMm > 0.0) {
        const double correction = gotoCorrXpEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            moveXMm += correction;
        }
    } else if (gotoCorrXmEdit_ != nullptr && moveXMm < 0.0) {
        const double correction = gotoCorrXmEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            moveXMm += correction;
        }
    }

    ok = false;
    if (gotoCorrYpEdit_ != nullptr && moveYMm > 0.0) {
        const double correction = gotoCorrYpEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            moveYMm += correction;
        }
    } else if (gotoCorrYmEdit_ != nullptr && moveYMm < 0.0) {
        const double correction = gotoCorrYmEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            moveYMm += correction;
        }
    }

    appendLog(
        QString("GoTo: cible px=(%1,%2) delta_px=(%3,%4) delta_mm=(%5,%6)")
            .arg(targetXPx)
            .arg(targetYPx)
            .arg(deltaXPx)
            .arg(deltaYPx)
            .arg(moveXMm, 0, 'f', 4)
            .arg(moveYMm, 0, 'f', 4)
    );

    bool velocityOk = false;
    const double gotoVelocityMmPerS = gotoVelocityEdit_ != nullptr ? gotoVelocityEdit_->text().trimmed().toDouble(&velocityOk) : 0.0;
    if (const auto currentPosition = latestPolledMotorPosition(); currentPosition.has_value()) {
        startPredictedMotorMotion(
            currentPosition->x(),
            currentPosition->y(),
            currentPosition->x() + moveXMm,
            currentPosition->y() + moveYMm,
            velocityOk && gotoVelocityMmPerS > 0.0 ? std::optional<double>(gotoVelocityMmPerS) : std::nullopt,
            velocityOk && gotoVelocityMmPerS > 0.0 ? std::optional<double>(gotoVelocityMmPerS) : std::nullopt
        );
    }
    auto controller = motorController_;

    runMotorTask("GoTo", [controller, moveXMm, moveYMm, gotoVelocityMmPerS, velocityOk, this]() {
        const auto gotoBoth = controller->snapshotBoth();
        const hardware::MotorAxisSnapshot& xSnapshot = gotoBoth.x;
        const hardware::MotorAxisSnapshot& ySnapshot = gotoBoth.y;

        if (!xSnapshot.connected || !ySnapshot.connected) {
            throw std::runtime_error("Les moteurs X et Y doivent etre connectes.");
        }
        if (!xSnapshot.positionValid || !ySnapshot.positionValid) {
            throw std::runtime_error("Impossible de lire la position courante des moteurs.");
        }

        const double targetX = xSnapshot.positionMm + moveXMm;
        const double targetY = ySnapshot.positionMm + moveYMm;
        if (targetX < 0.0 || targetX > 25.0) {
            throw std::runtime_error(QString("GoTo X hors limites (%1 mm)").arg(targetX, 0, 'f', 4).toStdString());
        }
        if (targetY < 0.0 || targetY > 25.0) {
            throw std::runtime_error(QString("GoTo Y hors limites (%1 mm)").arg(targetY, 0, 'f', 4).toStdString());
        }

        startPredictedMotorMotion(
            xSnapshot.positionMm,
            ySnapshot.positionMm,
            targetX,
            targetY,
            velocityOk && gotoVelocityMmPerS > 0.0 ? std::optional<double>(gotoVelocityMmPerS) : std::nullopt,
            velocityOk && gotoVelocityMmPerS > 0.0 ? std::optional<double>(gotoVelocityMmPerS) : std::nullopt
        );

        if (velocityOk && gotoVelocityMmPerS > 0.0) {
            controller->setVelocity(hardware::AxisId::X, gotoVelocityMmPerS);
            controller->setVelocity(hardware::AxisId::Y, gotoVelocityMmPerS);
        }

        controller->moveAbsoluteNoWait(hardware::AxisId::X, targetX);
        controller->moveAbsoluteNoWait(hardware::AxisId::Y, targetY);
        controller->waitAxis(hardware::AxisId::X, kDefaultMotorTimeoutMs);
        controller->waitAxis(hardware::AxisId::Y, kDefaultMotorTimeoutMs);
        stopPredictedMotorMotion(targetX, targetY);

        return MotorTaskResult {
            true,
            QString("GoTo termine: X=%1 Y=%2 mm (point rouge fixe)")
                .arg(targetX, 0, 'f', 4)
                .arg(targetY, 0, 'f', 4),
            {}
        };
    });
}

void MainWindow::onPreviewBackgroundClicked()
{
    if (!gotoArmed_ && !sequenceSelectArmed_) {
        return;
    }

    appendLog(sequenceSelectArmed_ ? "Zone image: clic en dehors de l'image" : "GoTo: clic en dehors de l'image affichee");
}

MainWindow::PotentiostatTechnique MainWindow::selectedPotentiostatTechnique() const
{
    const QString technique = potentiostatTechniqueCombo_ != nullptr
        ? potentiostatTechniqueCombo_->currentText().trimmed().toUpper()
        : QString("CA");
    if (technique == "OCV") {
        return PotentiostatTechnique::OCV;
    }
    if (technique == "CVA") {
        return PotentiostatTechnique::CVA;
    }
    return PotentiostatTechnique::CA;
}

QString MainWindow::selectedPotentiostatTechniqueLabel() const
{
    switch (selectedPotentiostatTechnique()) {
    case PotentiostatTechnique::OCV:
        return "OCV";
    case PotentiostatTechnique::CVA:
        return "CVA";
    case PotentiostatTechnique::CA:
    default:
        return "CA";
    }
}

void MainWindow::syncPotentiostatTechniqueUi()
{
    int stackIndex = 0;
    switch (selectedPotentiostatTechnique()) {
    case PotentiostatTechnique::OCV:
        stackIndex = 1;
        break;
    case PotentiostatTechnique::CVA:
        stackIndex = 2;
        break;
    case PotentiostatTechnique::CA:
    default:
        stackIndex = 0;
        break;
    }

    if (potentiostatTechniqueStack_ != nullptr) {
        potentiostatTechniqueStack_->setCurrentIndex(stackIndex);
        if (QWidget* currentPage = potentiostatTechniqueStack_->currentWidget(); currentPage != nullptr) {
            currentPage->adjustSize();
            const int targetHeight = std::max(currentPage->sizeHint().height(), currentPage->minimumSizeHint().height());
            potentiostatTechniqueStack_->setMinimumHeight(targetHeight);
            potentiostatTechniqueStack_->setMaximumHeight(QWIDGETSIZE_MAX);
            potentiostatTechniqueStack_->updateGeometry();
        }
    }
    if (potentiostatRunButton_ != nullptr) {
        potentiostatRunButton_->setText(QString("Lancer %1").arg(selectedPotentiostatTechniqueLabel()));
    }
}

QWidget* MainWindow::buildMeasureTab()
{
    auto* page = new QWidget;
    auto* outerLayout = new QHBoxLayout(page);
    outerLayout->setContentsMargins(10, 10, 10, 10);
    outerLayout->setSpacing(10);

    auto* splitter = new QSplitter(Qt::Horizontal);
    outerLayout->addWidget(splitter, 1);

    // ── Left: acquisition controls ────────────────────────────────
    auto* leftPanel = new QWidget;
    leftPanel->setMinimumWidth(280);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    // Potentiostat status
    auto* potStatusBox = createGroupBox("Potentiostat – Acquisition");
    auto* potStatusLayout = new QGridLayout(potStatusBox);

    potentiostatRunButton_ = createActionButton("Lancer acquisition");
    potentiostatRunButton_->setProperty("accent", true);
    potentiostatStopButton_ = createActionButton("Arreter");
    potStatusLayout->addWidget(potentiostatRunButton_, 0, 0, 1, 2);
    potStatusLayout->addWidget(potentiostatStopButton_, 0, 2, 1, 2);

    potStatusLayout->addWidget(new QLabel("Courant mesure :"), 1, 0, 1, 2);
    potentiostatCurrentLabel_ = new QLabel("---  A");
    potentiostatCurrentLabel_->setStyleSheet("font-size:20px; font-weight:700; color:#1f6feb;");
    potStatusLayout->addWidget(potentiostatCurrentLabel_, 1, 2, 1, 2);

    potentiostatMeasureStateLabel_ = new QLabel("Etat : non connecte");
    potentiostatMeasureStateLabel_->setStyleSheet("color:#5c6570; font-size:9pt;");
    potStatusLayout->addWidget(potentiostatMeasureStateLabel_, 2, 0, 1, 4);

    potStatusLayout->addWidget(new QLabel("Points acquis :"), 3, 0, 1, 2);
    potentiostatPointCountLabel_ = new QLabel("0");
    potentiostatPointCountLabel_->setStyleSheet("font-weight:600; color:#111927;");
    potStatusLayout->addWidget(potentiostatPointCountLabel_, 3, 2, 1, 2);

    potStatusLayout->addWidget(new QLabel("Progression :"), 4, 0, 1, 2);
    potentiostatProgressLabel_ = new QLabel("En attente");
    potentiostatProgressLabel_->setStyleSheet("font-weight:600; color:#111927;");
    potStatusLayout->addWidget(potentiostatProgressLabel_, 4, 2, 1, 2);

    potentiostatExportButton_ = createActionButton("Exporter...");
    potStatusLayout->addWidget(potentiostatExportButton_, 5, 0, 1, 4);

    connect(potentiostatRunButton_,    &QPushButton::clicked, this, &MainWindow::onStartCaPotentiostat);
    connect(potentiostatStopButton_,   &QPushButton::clicked, this, &MainWindow::onStopCaPotentiostat);
    connect(potentiostatExportButton_, &QPushButton::clicked, this, &MainWindow::onExportPotentiostat);
    potentiostatRunButton_->setEnabled(false);
    potentiostatStopButton_->setEnabled(false);
    potentiostatExportButton_->setEnabled(false);
    syncPotentiostatTechniqueUi();

    leftLayout->addWidget(potStatusBox);

    auto* graphModeBox = createGroupBox("Graphe");
    auto* graphModeLayout = new QGridLayout(graphModeBox);
    graphModeLayout->addWidget(new QLabel("Type"), 0, 0);
    potentiostatGraphTypeCombo_ = new QComboBox;
    potentiostatGraphTypeCombo_->addItems({"I = f(t)", "Ewe = f(t)", "I = f(Ewe)", "Ewe = f(I)"});
    graphModeLayout->addWidget(potentiostatGraphTypeCombo_, 0, 1);
    connect(potentiostatGraphTypeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshPotentiostatVisualization();
    });

    colorGraphButton_ = createActionButton("ColorGraph");
    colorGraphButton_->setCheckable(true);
    colorGraphButton_->setChecked(false);
    colorGraphButton_->setToolTip("Afficher les zones de deplacement moteur (vert) et d'arret (rouge) sur le graphe");
    graphModeLayout->addWidget(colorGraphButton_, 1, 0, 1, 2);
    connect(colorGraphButton_, &QPushButton::toggled, this, [this](bool checked) {
        if (potentiostatGraphWidget_ != nullptr)
            potentiostatGraphWidget_->setShowPhases(checked);
    });

    view3DButton_ = createActionButton("Vue 3D");
    view3DButton_->setCheckable(true);
    view3DButton_->setChecked(false);
    view3DButton_->setToolTip("Basculer entre la vue 2D (graphe + carte) et la surface 3D I(x,y)");
    graphModeLayout->addWidget(view3DButton_, 2, 0, 1, 2);
    connect(view3DButton_, &QPushButton::toggled, this, [this](bool is3D) {
        if (measureRightStack_ != nullptr)
            measureRightStack_->setCurrentIndex(is3D ? 1 : 0);
        view3DButton_->setText(is3D ? "Vue 2D" : "Vue 3D");
    });
    leftLayout->addWidget(graphModeBox);

    leftLayout->addStretch();

    // ── Right: graphs / map placeholders ─────────────────────────
    // ── Right: stacked widget (2D view / 3D view) ─────────────────────────────
    measureRightStack_ = new QStackedWidget;

    // Index 0 — classic 2D (graph + heatmap)
    auto* rightPanel2D = new QWidget;
    auto* rightLayout2D = new QVBoxLayout(rightPanel2D);
    rightLayout2D->setContentsMargins(0, 0, 0, 0);
    rightLayout2D->setSpacing(8);

    potentiostatGraphBox_ = createGroupBox("Courbes");
    potentiostatGraphBox_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* graphLayout = new QVBoxLayout(potentiostatGraphBox_);
    potentiostatGraphWidget_ = new PotentiostatGraphWidget;
    graphLayout->addWidget(potentiostatGraphWidget_, 1);
    rightLayout2D->addWidget(potentiostatGraphBox_, 1);

    potentiostatMapBox_ = createGroupBox("Cartographie");
    potentiostatMapBox_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* mapLayout = new QVBoxLayout(potentiostatMapBox_);
    potentiostatHeatmapWidget_ = new PotentiostatHeatmapWidget;
    mapLayout->addWidget(potentiostatHeatmapWidget_, 1);
    connect(potentiostatHeatmapWidget_, &PotentiostatHeatmapWidget::cellClicked,
            this, &MainWindow::showCellDetailDialog);
    rightLayout2D->addWidget(potentiostatMapBox_, 1);

    measureRightStack_->addWidget(rightPanel2D);  // index 0

    // Index 1 — 3D surface
    auto* map3DBox = createGroupBox("Surface 3D I(x, y)");
    auto* map3DLayout = new QVBoxLayout(map3DBox);
    potentiostat3DWidget_ = new Potentiostat3DWidget;
    map3DLayout->addWidget(potentiostat3DWidget_, 1);
    measureRightStack_->addWidget(map3DBox);  // index 1

    splitter->addWidget(leftPanel);
    splitter->addWidget(measureRightStack_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 900});

    return page;
}

// ══════════════════════════════════════════════════════════════════════════════
//  Import tab — independent visualization for imported CSV data
// ══════════════════════════════════════════════════════════════════════════════
QWidget* MainWindow::buildImportTab()
{
    auto* page = new QWidget;
    auto* outerLayout = new QHBoxLayout(page);
    outerLayout->setContentsMargins(10, 10, 10, 10);
    outerLayout->setSpacing(10);

    auto* splitter = new QSplitter(Qt::Horizontal);
    outerLayout->addWidget(splitter);

    // ── Left panel ────────────────────────────────────────────────────────────
    auto* leftPanel = new QWidget;
    leftPanel->setMinimumWidth(280);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    auto* importBox = createGroupBox("Importer");
    auto* importLayout = new QVBoxLayout(importBox);
    importButton_ = createActionButton("Importer CSV...");
    importLayout->addWidget(importButton_);
    importInfoLabel_ = new QLabel("Aucun fichier importé");
    importInfoLabel_->setWordWrap(true);
    importLayout->addWidget(importInfoLabel_);
    leftLayout->addWidget(importBox);

    auto* graphModeBox = createGroupBox("Graphe");
    auto* graphModeLayout = new QGridLayout(graphModeBox);
    graphModeLayout->addWidget(new QLabel("Type"), 0, 0);
    importGraphTypeCombo_ = new QComboBox;
    importGraphTypeCombo_->addItems({"I = f(t)", "Ewe = f(t)", "I = f(Ewe)", "Ewe = f(I)"});
    graphModeLayout->addWidget(importGraphTypeCombo_, 0, 1);
    connect(importGraphTypeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshImportVisualization();
    });
    leftLayout->addWidget(graphModeBox);

    importView3DButton_ = createActionButton("Vue 3D");
    importView3DButton_->setCheckable(true);
    importView3DButton_->setVisible(false);
    connect(importView3DButton_, &QPushButton::toggled, this, [this](bool checked) {
        if (importRightStack_ != nullptr) {
            importRightStack_->setCurrentIndex(checked ? 1 : 0);
        }
        importView3DButton_->setText(checked ? "Vue 2D" : "Vue 3D");
    });
    leftLayout->addWidget(importView3DButton_);

    leftLayout->addStretch();

    // ── Right panel ───────────────────────────────────────────────────────────
    importRightStack_ = new QStackedWidget;

    // Page 0: 2D graph + heatmap
    auto* page2D = new QWidget;
    auto* layout2D = new QVBoxLayout(page2D);
    layout2D->setContentsMargins(0, 0, 0, 0);

    importGraphBox_ = createGroupBox("Courbes");
    auto* graphLayout = new QVBoxLayout(importGraphBox_);
    importGraphWidget_ = new PotentiostatGraphWidget;
    graphLayout->addWidget(importGraphWidget_, 1);
    layout2D->addWidget(importGraphBox_, 1);

    importMapBox_ = createGroupBox("Cartographie");
    importMapBox_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* mapLayout = new QVBoxLayout(importMapBox_);
    importHeatmapWidget_ = new PotentiostatHeatmapWidget;
    mapLayout->addWidget(importHeatmapWidget_, 1);
    connect(importHeatmapWidget_, &PotentiostatHeatmapWidget::cellClicked,
            this, &MainWindow::showImportCellDetailDialog);
    layout2D->addWidget(importMapBox_, 1);
    importRightStack_->addWidget(page2D);  // index 0

    // Page 1: 3D surface
    auto* map3DBox = createGroupBox("Surface 3D");
    auto* map3DLayout = new QVBoxLayout(map3DBox);
    import3DWidget_ = new Potentiostat3DWidget;
    map3DLayout->addWidget(import3DWidget_, 1);
    importRightStack_->addWidget(map3DBox);  // index 1

    splitter->addWidget(leftPanel);
    splitter->addWidget(importRightStack_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 900});

    connect(importButton_, &QPushButton::clicked, this, &MainWindow::onImportCsv);

    return page;
}

void MainWindow::clearPotentiostatVisualization()
{
    potentiostatPlotTimes_.clear();
    potentiostatPlotCurrents_.clear();
    potentiostatPlotEwe_.clear();
    potentiostatMatrix_.clear();
    potentiostatCellCurrentSamples_.clear();
    potentiostatCellEweSamples_.clear();
    potentiostatEweMatrix_.clear();
    potentiostatCellPositions_.clear();
    potentiostatCellTimes_.clear();
    potentiostatScanOrder_.clear();
    potentiostatRows_ = 0;
    potentiostatCols_ = 0;
    potentiostatSampleCount_ = 0;
    potentiostatLastSampledCell_ = {0, 0};
    potentiostatXMin_ = potentiostatXMax_ = potentiostatYMin_ = potentiostatYMax_ = 0.0;
    if (potentiostatExportButton_ != nullptr) potentiostatExportButton_->setEnabled(false);

    if (potentiostatPointCountLabel_ != nullptr) {
        potentiostatPointCountLabel_->setText("0");
    }
    if (potentiostatProgressLabel_ != nullptr) {
        potentiostatProgressLabel_->setText("En attente");
    }
    if (potentiostatGraphWidget_ != nullptr) {
        potentiostatGraphWidget_->clear();
    }
    if (potentiostatHeatmapWidget_ != nullptr) {
        potentiostatHeatmapWidget_->clear();
    }
}

void MainWindow::resetPotentiostatVisualization(int rows, int cols, const std::vector<std::pair<int, int>>& order)
{
    clearPotentiostatVisualization();
    potentiostatRows_ = rows;
    potentiostatCols_ = cols;
    potentiostatScanOrder_ = order;
    potentiostatMatrix_.assign(static_cast<std::size_t>(std::max(0, rows * cols)), std::nullopt);
    potentiostatCellCurrentSamples_.assign(static_cast<std::size_t>(std::max(0, rows * cols)), {});
    potentiostatEweMatrix_.assign(static_cast<std::size_t>(std::max(0, rows * cols)), std::nullopt);
    potentiostatCellEweSamples_.assign(static_cast<std::size_t>(std::max(0, rows * cols)), {});
    potentiostatCellPositions_.assign(static_cast<std::size_t>(std::max(0, rows * cols)), QPointF());
    potentiostatCellTimes_.assign(static_cast<std::size_t>(std::max(0, rows * cols)), 0.0);
    refreshPotentiostatVisualization();
}

void MainWindow::appendPotentiostatVisualizationSample(
    int index,
    int total,
    int row,
    int col,
    const QPointF& waypointMm,
    double elapsedTime,
    double ewe,
    double current,
    std::vector<double> cellCurrentSamples,
    std::vector<double> cellEweSamples
)
{
    potentiostatPlotTimes_.push_back(elapsedTime);
    potentiostatPlotCurrents_.push_back(current);
    potentiostatPlotEwe_.push_back(ewe);
    potentiostatSampleCount_ = static_cast<int>(potentiostatPlotTimes_.size());

    if (row >= 0 && row < potentiostatRows_ && col >= 0 && col < potentiostatCols_) {
        const std::size_t matrixIndex = static_cast<std::size_t>(row * potentiostatCols_ + col);
        if (matrixIndex < potentiostatMatrix_.size()) {
            potentiostatMatrix_[matrixIndex] = current;
        }
        if (matrixIndex < potentiostatEweMatrix_.size()) {
            potentiostatEweMatrix_[matrixIndex] = ewe;
        }
        if (matrixIndex < potentiostatCellPositions_.size()) {
            potentiostatCellPositions_[matrixIndex] = waypointMm;
        }
        if (matrixIndex < potentiostatCellTimes_.size()) {
            potentiostatCellTimes_[matrixIndex] = elapsedTime;
        }
        if (matrixIndex < potentiostatCellCurrentSamples_.size() && !cellCurrentSamples.empty()) {
            potentiostatCellCurrentSamples_[matrixIndex] = std::move(cellCurrentSamples);
        }
        if (matrixIndex < potentiostatCellEweSamples_.size() && !cellEweSamples.empty()) {
            potentiostatCellEweSamples_[matrixIndex] = std::move(cellEweSamples);
        }
        potentiostatLastSampledCell_ = {row, col};
    }

    if (potentiostatCurrentLabel_ != nullptr) {
        potentiostatCurrentLabel_->setText(QString("%1 A").arg(current, 0, 'e', 3));
    }
    if (potentiostatPointCountLabel_ != nullptr) {
        potentiostatPointCountLabel_->setText(QString::number(potentiostatSampleCount_));
    }
    if (potentiostatProgressLabel_ != nullptr) {
        if (total > 0) {
            potentiostatProgressLabel_->setText(QString("%1 / %2").arg(index + 1).arg(total));
        } else {
            potentiostatProgressLabel_->setText(QString("%1 pt(s)").arg(index + 1));
        }
    }
    if (sequenceStatusLabel_ != nullptr) {
        sequenceStatusLabel_->setText(QString("%1 / %2 - (%3, %4)")
            .arg(index + 1).arg(total)
            .arg(waypointMm.x(), 0, 'f', 4)
            .arg(waypointMm.y(), 0, 'f', 4));
    }

    refreshPotentiostatVisualization();
}

void MainWindow::refreshPotentiostatVisualization()
{
    if (potentiostatGraphWidget_ != nullptr) {
        PotentiostatGraphWidget::Mode graphMode = PotentiostatGraphWidget::Mode::CurrentVsTime;
        const int modeIndex = potentiostatGraphTypeCombo_ != nullptr ? potentiostatGraphTypeCombo_->currentIndex() : 0;
        switch (modeIndex) {
        case 1:
            graphMode = PotentiostatGraphWidget::Mode::EweVsTime;
            break;
        case 2:
            graphMode = PotentiostatGraphWidget::Mode::CurrentVsEwe;
            break;
        case 3:
            graphMode = PotentiostatGraphWidget::Mode::EweVsCurrent;
            break;
        default:
            graphMode = PotentiostatGraphWidget::Mode::CurrentVsTime;
            break;
        }
        potentiostatGraphWidget_->setGraphMode(graphMode);
        potentiostatGraphWidget_->setSeries(potentiostatPlotTimes_, potentiostatPlotCurrents_, potentiostatPlotEwe_);
    }

    if (potentiostatHeatmapWidget_ != nullptr) {
        std::optional<std::pair<int, int>> highlightedCell;
        if (potentiostatSampleCount_ > 0) {
            highlightedCell = potentiostatLastSampledCell_;
        }
        potentiostatHeatmapWidget_->setGrid(
            potentiostatRows_,
            potentiostatCols_,
            potentiostatMatrix_,
            highlightedCell
        );
    }

    if (potentiostat3DWidget_ != nullptr) {
        const double xSpan = potentiostatCols_ > 0
            ? std::max(1e-6, std::abs(potentiostatXMax_ - potentiostatXMin_))
            : 1.0;
        const double ySpan = potentiostatRows_ > 0
            ? std::max(1e-6, std::abs(potentiostatYMax_ - potentiostatYMin_))
            : 1.0;
        potentiostat3DWidget_->setGrid(
            potentiostatRows_,
            potentiostatCols_,
            potentiostatMatrix_,
            xSpan,
            ySpan
        );
    }
}

void MainWindow::showCellDetailDialog(int row, int col)
{
    const std::size_t cellIdx = static_cast<std::size_t>(row * potentiostatCols_ + col);
    if (cellIdx >= potentiostatCellCurrentSamples_.size()) return;
    const std::vector<double>& samples = potentiostatCellCurrentSamples_[cellIdx];
    if (samples.empty()) return;

    const int n = static_cast<int>(samples.size());
    const double meanI = std::accumulate(samples.begin(), samples.end(), 0.0) / n;

    QDialog dlg(this);
    dlg.setWindowTitle(QString("D\u00e9tail cellule \u2014 ligne %1, col %2").arg(row + 1).arg(col + 1));
    dlg.setMinimumSize(500, 320);
    auto* vl = new QVBoxLayout(&dlg);

    auto* infoLabel = new QLabel(
        QString("%1 mesure(s) sur %2 s  |  moyenne : %3 A")
            .arg(n)
            .arg(potentiostatLastDwellS_, 0, 'f', 3)
            .arg(meanI, 0, 'e', 4));
    infoLabel->setAlignment(Qt::AlignCenter);
    vl->addWidget(infoLabel);

    auto* graphWidget = new PotentiostatGraphWidget;
    graphWidget->setMinimumHeight(220);

    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(n));
    const double intervalS = (n > 0 ? potentiostatLastDwellS_ / n : 1.0);
    for (int s = 0; s < n; ++s) {
        times.push_back((s + 1) * intervalS);
    }
    graphWidget->setSeries(std::move(times), samples,
                           std::vector<double>(static_cast<std::size_t>(n), 0.0));
    vl->addWidget(graphWidget, 1);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(btnBox, &QDialogButtonBox::accepted,  &dlg, &QDialog::accept);
    vl->addWidget(btnBox);

    dlg.exec();
}

// ══════════════════════════════════════════════════════════════════════════════
//  Import tab — visualization refresh
// ══════════════════════════════════════════════════════════════════════════════
void MainWindow::refreshImportVisualization()
{
    if (importGraphWidget_ != nullptr) {
        PotentiostatGraphWidget::Mode graphMode = PotentiostatGraphWidget::Mode::CurrentVsTime;
        const int modeIndex = importGraphTypeCombo_ != nullptr ? importGraphTypeCombo_->currentIndex() : 0;
        switch (modeIndex) {
        case 1: graphMode = PotentiostatGraphWidget::Mode::EweVsTime; break;
        case 2: graphMode = PotentiostatGraphWidget::Mode::CurrentVsEwe; break;
        case 3: graphMode = PotentiostatGraphWidget::Mode::EweVsCurrent; break;
        default: break;
        }
        importGraphWidget_->setGraphMode(graphMode);
        importGraphWidget_->setSeries(importPlotTimes_, importPlotCurrents_, importPlotEwe_);
    }

    if (importHeatmapWidget_ != nullptr) {
        importHeatmapWidget_->setGrid(importRows_, importCols_, importMatrix_, std::nullopt);
    }

    if (import3DWidget_ != nullptr) {
        const double xSpan = importCols_ > 0
            ? std::max(1e-6, std::abs(importXMax_ - importXMin_)) : 1.0;
        const double ySpan = importRows_ > 0
            ? std::max(1e-6, std::abs(importYMax_ - importYMin_)) : 1.0;
        import3DWidget_->setGrid(importRows_, importCols_, importMatrix_, xSpan, ySpan);
    }
}

void MainWindow::showImportCellDetailDialog(int row, int col)
{
    const std::size_t cellIdx = static_cast<std::size_t>(row * importCols_ + col);
    if (cellIdx >= importCellCurrentSamples_.size()) return;
    const std::vector<double>& samples = importCellCurrentSamples_[cellIdx];
    if (samples.empty()) return;

    const int n = static_cast<int>(samples.size());
    const double meanI = std::accumulate(samples.begin(), samples.end(), 0.0) / n;

    QDialog dlg(this);
    dlg.setWindowTitle(QString("D\u00e9tail cellule import\u00e9e \u2014 ligne %1, col %2").arg(row + 1).arg(col + 1));
    dlg.setMinimumSize(500, 320);
    auto* vl = new QVBoxLayout(&dlg);

    auto* infoLabel = new QLabel(
        importLastDwellS_ > 0.0
            ? QString("%1 mesure(s) sur %2 s  |  moyenne : %3 A")
                  .arg(n).arg(importLastDwellS_, 0, 'f', 3).arg(meanI, 0, 'e', 4)
            : QString("%1 mesure(s)  |  moyenne : %2 A")
                  .arg(n).arg(meanI, 0, 'e', 4));
    infoLabel->setAlignment(Qt::AlignCenter);
    vl->addWidget(infoLabel);

    auto* graphWidget = new PotentiostatGraphWidget;
    graphWidget->setMinimumHeight(220);

    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(n));
    const double intervalS = importLastDwellS_ > 0.0 ? importLastDwellS_ / n : 1.0;
    for (int s = 0; s < n; ++s) {
        times.push_back((s + 1) * intervalS);
    }
    const std::vector<double>& eweSamples = (cellIdx < importCellEweSamples_.size())
        ? importCellEweSamples_[cellIdx] : samples;
    graphWidget->setSeries(std::move(times), samples,
                           eweSamples.size() == static_cast<std::size_t>(n)
                               ? eweSamples
                               : std::vector<double>(static_cast<std::size_t>(n), 0.0));
    vl->addWidget(graphWidget, 1);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(btnBox, &QDialogButtonBox::accepted,  &dlg, &QDialog::accept);
    vl->addWidget(btnBox);

    dlg.exec();
}

void MainWindow::refreshSummaries()
{
    using namespace std::chrono_literals;
    const auto now = std::chrono::steady_clock::now();
    const bool refreshMotorPanel = !lastMotorUiRefresh_.has_value() || (now - *lastMotorUiRefresh_) >= 66ms;
    const bool refreshCameraPanel = !cameraController_->isLive()
        && (!lastCameraUiRefresh_.has_value() || (now - *lastCameraUiRefresh_) >= 150ms);
    const bool refreshStatusBar = !lastStatusRefresh_.has_value() || (now - *lastStatusRefresh_) >= 250ms;

    if (refreshMotorPanel) {
        refreshMotorUi();
        lastMotorUiRefresh_ = now;
    } else {
        syncSequenceOverlay();
    }

    if (refreshCameraPanel) {
        refreshCameraUi();
        lastCameraUiRefresh_ = now;
    }

    if (refreshStatusBar) {
        const QString cameraSummary = QString("Camera: %1 | %2 | Exp %3 us | Gain %4")
            .arg(core::deviceStateLabel(cameraController_->state()))
            .arg(cameraController_->cameraIdentifier())
            .arg(cameraController_->exposureTimeUs(), 0, 'f', 0)
            .arg(cameraController_->gain(), 0, 'f', 1);
        const QString potentiostatSummary = QString("Potentiostat: %1 | %2")
            .arg(core::deviceStateLabel(potentiostatController_->state()))
            .arg(potentiostatController_->channelSummary());

        if (cameraSummaryLabel_ != nullptr && cameraSummaryLabel_->text() != cameraSummary) {
            cameraSummaryLabel_->setText(cameraSummary);
        }
        if (potentiostatSummaryLabel_ != nullptr && potentiostatSummaryLabel_->text() != potentiostatSummary) {
            potentiostatSummaryLabel_->setText(potentiostatSummary);
        }
        lastStatusRefresh_ = now;
    }
}

void MainWindow::refreshMotorUi()
{
    hardware::MotorAxisSnapshot xSnapshot;
    xSnapshot.axis = hardware::AxisId::X;

    hardware::MotorAxisSnapshot ySnapshot;
    ySnapshot.axis = hardware::AxisId::Y;

    QString pollError;
    {
        std::lock_guard<std::mutex> lock(motorSnapshotMutex_);
        if (motorSnapshotsReady_) {
            xSnapshot = polledXSnapshot_;
            ySnapshot = polledYSnapshot_;
        }
        if (!pendingMotorPollError_.isEmpty()) {
            pollError = pendingMotorPollError_;
            pendingMotorPollError_.clear();
        }
    }

    if (!pollError.isEmpty()) {
        appendLog("Erreur polling moteurs: " + pollError);
        statusBar()->showMessage(pollError, 5000);
    }

    const QString xText = snapshotPositionText(xSnapshot);
    const QString yText = snapshotPositionText(ySnapshot);
    const QString stageSummary = axisConnectionSummary(xSnapshot, ySnapshot);
    auto setLabelTextIfChanged = [](QLabel* label, const QString& text) {
        if (label != nullptr && label->text() != text) {
            label->setText(text);
        }
    };
    auto setWidgetEnabledIfChanged = [](QWidget* widget, bool enabled) {
        if (widget != nullptr && widget->isEnabled() != enabled) {
            widget->setEnabled(enabled);
        }
    };

    setLabelTextIfChanged(xPositionValueLabel_, xText);
    setLabelTextIfChanged(yPositionValueLabel_, yText);
    setLabelTextIfChanged(stageSummaryLabel_, stageSummary);
    if (xSnapshot.positionValid && ySnapshot.positionValid) {
        cachedMotorMm_ = QPointF(xSnapshot.positionMm, ySnapshot.positionMm);
        updatePredictedMotorMotion(xSnapshot, ySnapshot);

        bool predictedMotionActive = false;
        {
            std::lock_guard<std::mutex> lock(predictedMotionMutex_);
            predictedMotionActive = predictedXMotion_.active || predictedYMotion_.active;
        }

        const bool motorsMoving = axisStateLooksMoving(xSnapshot.stateCode)
            || axisStateLooksMoving(ySnapshot.stateCode)
            || predictedMotionActive;

        if (const auto estimatedMotorMm = estimatedMotorPositionForOverlay(); estimatedMotorMm.has_value()) {
            if (!stableOverlayMotorMm_.has_value()) {
                stableOverlayMotorMm_ = *estimatedMotorMm;
            } else if (motorsMoving) {
                stableOverlayMotorMm_ = *estimatedMotorMm;
            } else {
                const double dx = estimatedMotorMm->x() - stableOverlayMotorMm_->x();
                const double dy = estimatedMotorMm->y() - stableOverlayMotorMm_->y();
                if (std::hypot(dx, dy) >= kOverlayStabilityDeadbandMm) {
                    stableOverlayMotorMm_ = *estimatedMotorMm;
                }
            }
        }
    }
    syncSequenceOverlay();

    const bool anyConnected = xSnapshot.connected || ySnapshot.connected;
    const bool bothConnected = xSnapshot.connected && ySnapshot.connected;
    const bool enableCommands = bothConnected && !motorTaskRunning_ && !sequenceRunning_;

    setWidgetEnabledIfChanged(scanPortsButton_, !motorTaskRunning_ && !sequenceRunning_);
    setWidgetEnabledIfChanged(connectAxesButton_, !motorTaskRunning_ && !sequenceRunning_);
    setWidgetEnabledIfChanged(homeAxesButton_, bothConnected && !motorTaskRunning_ && !sequenceRunning_);
    setWidgetEnabledIfChanged(disconnectAxesButton_, anyConnected && !motorTaskRunning_ && !sequenceRunning_);
    setWidgetEnabledIfChanged(moveAbsXYButton_, enableCommands);
    setWidgetEnabledIfChanged(gotoButton_, !motorTaskRunning_ && !sequenceRunning_);
    setWidgetEnabledIfChanged(sequenceSetStartButton_, !motorTaskRunning_ && !sequenceRunning_);
    setWidgetEnabledIfChanged(sequenceSetEndButton_, !motorTaskRunning_ && !sequenceRunning_);
    setWidgetEnabledIfChanged(sequencePickButton_, !motorTaskRunning_ && !sequenceRunning_);
    setWidgetEnabledIfChanged(jogStepEdit_, !motorTaskRunning_ && !sequenceRunning_);
    setWidgetEnabledIfChanged(absXEdit_, enableCommands);
    setWidgetEnabledIfChanged(absYEdit_, enableCommands);
}

void MainWindow::refreshCameraUi()
{
    const bool live = cameraController_->isLive();
    const bool connected = cameraController_->isConnected();
    if (live) {
        flushLatestCameraFrameToUi();
    } else if (cameraPreviewWidget_ != nullptr) {
        const QImage frame = cameraController_->previewFrame();
        cameraPreviewWidget_->setFrame(frame);
        syncLaserOverlay(frame.size());
        syncSequenceOverlay();
    }

    auto setLineEditTextIfChanged = [](QLineEdit* edit, const QString& text) {
        if (edit != nullptr && edit->text() != text) {
            edit->setText(text);
        }
    };
    auto setWidgetEnabledIfChanged = [](QWidget* widget, bool enabled) {
        if (widget != nullptr && widget->isEnabled() != enabled) {
            widget->setEnabled(enabled);
        }
    };
    auto setLabelTextIfChanged = [](QLabel* label, const QString& text) {
        if (label != nullptr && label->text() != text) {
            label->setText(text);
        }
    };
    auto setLabelStyleIfChanged = [](QLabel* label, const QString& style) {
        if (label != nullptr && label->styleSheet() != style) {
            label->setStyleSheet(style);
        }
    };

    QString cameraStatus = "Deconnectee";
    QString cameraStatusStyle = "color:#5c6570; font-size:9pt;";
    if (connected) {
        cameraStatus = live ? "Connectee - live" : "Connectee";
        cameraStatusStyle = live
            ? "color:#1a7f37; font-size:9pt;"
            : "color:#1f6feb; font-size:9pt;";
    }
    setLabelTextIfChanged(cameraPageStatusLabel_, cameraStatus);
    setLabelStyleIfChanged(cameraPageStatusLabel_, cameraStatusStyle);

    setWidgetEnabledIfChanged(scanCameraButton_, true);
    setWidgetEnabledIfChanged(connectCameraButton_, !connected);
    setWidgetEnabledIfChanged(disconnectCameraButton_, connected);
    setWidgetEnabledIfChanged(startCameraLiveButton_, connected && !live);
    setWidgetEnabledIfChanged(stopCameraLiveButton_, live);
    setWidgetEnabledIfChanged(cameraPageLiveButton_, connected && !live);
    setWidgetEnabledIfChanged(cameraPageStopButton_, live);

    // In live mode this method is called at ~60 Hz; avoid line-edit churn there.
    if (live) {
        return;
    }

    if (cameraExposureEdit_ != nullptr && !cameraExposureEdit_->hasFocus()) {
        setLineEditTextIfChanged(cameraExposureEdit_, QString::number(cameraController_->exposureTimeUs(), 'f', 0));
    }
    if (cameraGainEdit_ != nullptr && !cameraGainEdit_->hasFocus()) {
        setLineEditTextIfChanged(cameraGainEdit_, QString::number(cameraController_->gain(), 'f', 1));
    }
}

void MainWindow::flushLatestCameraFrameToUi()
{
    bool frameUpdated = false;
    QImage latestFrame;
    QString pollError;

    {
        std::lock_guard<std::mutex> lock(cameraSnapshotMutex_);
        if (cameraFrameReady_) {
            latestFrame = polledCameraFrame_;
            cameraFrameReady_ = false;
            frameUpdated = true;
        }
        if (!pendingCameraPollError_.isEmpty()) {
            pollError = pendingCameraPollError_;
            pendingCameraPollError_.clear();
        }
    }

    if (!pollError.isEmpty()) {
        if (cameraPollTimer_ != nullptr) {
            cameraPollTimer_->stop();
        }
        stopCameraPolling();
        appendLog("Erreur camera: " + pollError);
        statusBar()->showMessage(pollError, 5000);
        return;
    }

    if (frameUpdated && cameraPreviewWidget_ != nullptr) {
        cameraPreviewWidget_->setFrame(latestFrame);
        syncLaserOverlay(latestFrame.size());
        syncSequenceOverlay();
    }
}

void MainWindow::applyCameraSettings()
{
    bool exposureOk = true;
    bool gainOk = true;
    double exposureUs = cameraController_->exposureTimeUs();
    double gain = cameraController_->gain();
    if (cameraExposureEdit_ != nullptr) {
        exposureUs = cameraExposureEdit_->text().trimmed().toDouble(&exposureOk);
    }
    if (cameraGainEdit_ != nullptr) {
        gain = cameraGainEdit_->text().trimmed().toDouble(&gainOk);
    }

    if (!exposureOk || exposureUs <= 0.0) {
        QMessageBox::warning(this, "Camera", "Exposure Time doit etre un nombre strictement positif en microsecondes.");
        return;
    }
    if (!gainOk || gain < 0.0) {
        QMessageBox::warning(this, "Camera", "Gain doit etre un nombre valide superieur ou egal a 0.");
        return;
    }

    try {
        cameraController_->setExposureTimeUs(exposureUs);
        cameraController_->setGain(gain);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "Camera", QString::fromUtf8(ex.what()));
        return;
    }
    appendLog(QString("Camera: Exposure=%1 us, Gain=%2").arg(exposureUs, 0, 'f', 0).arg(gain, 0, 'f', 1));
    statusBar()->showMessage("Parametres camera appliques.", 3000);
    refreshSummaries();
}

void MainWindow::startCameraLive()
{
    try {
        applyCameraSettings();
        cameraController_->startLive();
        startCameraPolling();
        if (cameraPollTimer_ != nullptr) {
            cameraPollTimer_->stop();
        }
        appendLog("Camera live demarre.");
        statusBar()->showMessage("Camera live demarre.", 3000);
        refreshSummaries();
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "Camera", QString::fromUtf8(ex.what()));
    }
}

void MainWindow::stopCameraLive()
{
    try {
        setGotoArmed(false);
        setSequenceSelectArmed(false);
        clearSequencePreviewSelection();
        if (cameraPollTimer_ != nullptr) {
            cameraPollTimer_->stop();
        }
        stopCameraPolling();
        cameraController_->stopLive();
        appendLog("Camera live stop.");
        statusBar()->showMessage("Camera live stop.", 3000);
        refreshSummaries();
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "Camera", QString::fromUtf8(ex.what()));
    }
}

void MainWindow::appendLog(const QString& message)
{
    const QString line = QString("[%1] %2").arg(currentLogTimestampText(), message);

    {
        std::lock_guard<std::mutex> lock(logMutex_);
        if (!sessionLogPath_.isEmpty()) {
            QFile logFile(sessionLogPath_);
            if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                QTextStream stream(&logFile);
                stream << line << '\n';
                stream.flush();
            }
        }
    }

    const auto appendToUi = [this, line]() {
        if (logView_ != nullptr) {
            logView_->appendPlainText(line);
        }
    };

    if (QThread::currentThread() == thread()) {
        appendToUi();
    } else {
        QMetaObject::invokeMethod(this, appendToUi, Qt::QueuedConnection);
    }
}

void MainWindow::initializeMeasurementLog(const QString& fileStem, const QStringList& headerLines)
{
    {
        std::lock_guard<std::mutex> lock(logMutex_);
        measurementLogActive_ = false;
        measurementLogPath_.clear();
    }

    const QString logsDirPath = QDir(QCoreApplication::applicationDirPath()).filePath("logs/measurements");
    QDir().mkpath(logsDirPath);

    QString safeStem = sanitizeLogFileComponent(fileStem);
    if (safeStem.isEmpty()) {
        safeStem = "measurement";
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    const QString path = QDir(logsDirPath).filePath(QString("LaserBenchMeasure_%1_%2.log").arg(timestamp, safeStem));

    QFile logFile(path);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        appendLog(QString("Impossible de creer le journal de mesure : %1").arg(QDir::toNativeSeparators(path)));
        return;
    }

    QTextStream stream(&logFile);
    stream << "LaserBench measurement log" << '\n';
    stream << "Started: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << '\n';
    stream << "AppDir: " << QDir::toNativeSeparators(QCoreApplication::applicationDirPath()) << '\n';
    for (const QString& line : headerLines) {
        stream << line << '\n';
    }
    stream << "----------------------------------------" << '\n';
    stream.flush();

    {
        std::lock_guard<std::mutex> lock(logMutex_);
        measurementLogPath_ = path;
        measurementLogActive_ = true;
    }

    appendLog(QString("Journal de mesure: %1").arg(QDir::toNativeSeparators(path)));
    appendMeasurementLogEvent("START", "Journal de mesure initialise.");
}

void MainWindow::appendMeasurementLog(const QString& message)
{
    const QString line = QString("[%1] %2").arg(currentLogTimestampText(), message);

    std::lock_guard<std::mutex> lock(logMutex_);
    if (!measurementLogActive_ || measurementLogPath_.isEmpty()) {
        return;
    }

    QFile logFile(measurementLogPath_);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&logFile);
        stream << line << '\n';
        stream.flush();
    }
}

void MainWindow::appendMeasurementLogEvent(const QString& category, const QString& message)
{
    appendMeasurementLog(QString("[%1] %2").arg(category, message));
}

void MainWindow::finalizeMeasurementLog(const QString& outcome)
{
    if (!outcome.isEmpty()) {
        appendMeasurementLogEvent("END", outcome);
    }
    appendMeasurementLog("----------------------------------------");

    std::lock_guard<std::mutex> lock(logMutex_);
    measurementLogActive_ = false;
}

void MainWindow::runMotorTask(const QString& label, std::function<MotorTaskResult()> worker)
{
    if (motorTaskRunning_) {
        appendLog("Une operation moteur est deja en cours.");
        return;
    }

    motorTaskRunning_ = true;
    refreshMotorUi();
    statusBar()->showMessage(label + "...", 2000);
    appendLog(label + "...");

    const QPointer<MainWindow> self(this);

    std::thread([self, worker = std::move(worker)]() mutable {
        MotorTaskResult result;
        try {
            result = worker();
        } catch (const std::exception& ex) {
            result.success = false;
            result.message = QString::fromUtf8(ex.what());
        } catch (...) {
            result.success = false;
            result.message = "Erreur inconnue";
        }

        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(self, [self, result]() mutable {
            if (!self) {
                return;
            }

            self->motorTaskRunning_ = false;
            if (result.success) {
                if (result.uiContinuation) {
                    result.uiContinuation();
                }
                if (!result.message.isEmpty()) {
                    self->appendLog(result.message);
                    self->statusBar()->showMessage(result.message, 3500);
                }
            } else {
                if (const auto currentPosition = self->latestPolledMotorPosition(); currentPosition.has_value()) {
                    self->stopPredictedMotorMotion(currentPosition->x(), currentPosition->y());
                } else {
                    self->stopPredictedMotorMotion();
                }
                self->appendLog("Erreur: " + result.message);
                self->statusBar()->showMessage(result.message, 6000);
            }
            self->refreshSummaries();
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::startMotorPolling()
{
    stopMotorPolling();
    motorPollStopRequested_.store(false);
    motorUiUpdatePending_.store(false);
    motorPollThread_ = std::thread(&MainWindow::motorPollingLoop, this);
}

void MainWindow::stopMotorPolling()
{
    motorPollStopRequested_.store(true);
    if (motorPollThread_.joinable()) {
        motorPollThread_.join();
    }
    motorPollStopRequested_.store(false);
    motorUiUpdatePending_.store(false);
}

void MainWindow::startCameraPolling()
{
    stopCameraPolling();
    {
        std::lock_guard<std::mutex> lock(cameraSnapshotMutex_);
        cameraFrameReady_ = false;
        pendingCameraPollError_.clear();
    }
    cameraUiUpdatePending_.store(false);
    cameraPollStopRequested_.store(false);
    cameraPollThread_ = std::thread(&MainWindow::cameraPollingLoop, this);
}

void MainWindow::stopCameraPolling()
{
    cameraPollStopRequested_.store(true);
    if (cameraPollThread_.joinable()) {
        cameraPollThread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(cameraSnapshotMutex_);
        cameraFrameReady_ = false;
    }
    cameraUiUpdatePending_.store(false);
    cameraPollStopRequested_.store(false);
}

void MainWindow::cameraPollingLoop()
{
    using namespace std::chrono_literals;

    while (!cameraPollStopRequested_.load()) {
        if (!cameraController_->isLive()) {
            std::this_thread::sleep_for(5ms);
            continue;
        }

        try {
            const bool frameUpdated = cameraController_->refreshFrame();
            if (!frameUpdated) {
                std::this_thread::yield();
                continue;
            }

            QImage frame = cameraController_->previewFrame();
            {
                std::lock_guard<std::mutex> lock(cameraSnapshotMutex_);
                polledCameraFrame_ = std::move(frame);
                cameraFrameReady_ = true;
            }

            if (!cameraUiUpdatePending_.exchange(true)) {
                QMetaObject::invokeMethod(this, [this]() {
                    cameraUiUpdatePending_.store(false);
                    flushLatestCameraFrameToUi();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& ex) {
            {
                std::lock_guard<std::mutex> lock(cameraSnapshotMutex_);
                pendingCameraPollError_ = QString::fromUtf8(ex.what());
            }

            if (!cameraUiUpdatePending_.exchange(true)) {
                QMetaObject::invokeMethod(this, [this]() {
                    cameraUiUpdatePending_.store(false);
                    flushLatestCameraFrameToUi();
                }, Qt::QueuedConnection);
            }

            cameraPollStopRequested_.store(true);
            break;
        }
    }
}

void MainWindow::motorPollingLoop()
{
    using namespace std::chrono_literals;

    while (!motorPollStopRequested_.load()) {
        hardware::MotorAxisSnapshot xSnapshot;
        xSnapshot.axis = hardware::AxisId::X;

        hardware::MotorAxisSnapshot ySnapshot;
        ySnapshot.axis = hardware::AxisId::Y;

        QString errorMessage;

        try {
            const auto both = motorController_->snapshotBoth();
            xSnapshot = both.x;
            ySnapshot = both.y;
        } catch (const std::exception& ex) {
            errorMessage = QString::fromUtf8(ex.what());
        }

        {
            std::lock_guard<std::mutex> lock(motorSnapshotMutex_);
            polledXSnapshot_ = xSnapshot;
            polledYSnapshot_ = ySnapshot;
            motorSnapshotsReady_ = true;
            if (!errorMessage.isEmpty()) {
                pendingMotorPollError_ = errorMessage;
            }
        }

        if ((sequenceRectFollowSample_ || sequenceRunning_) && !motorUiUpdatePending_.exchange(true)) {
            QMetaObject::invokeMethod(this, [this]() {
                motorUiUpdatePending_.store(false);
                if (!cameraController_->isLive()) {
                    syncSequenceOverlay();
                }
            }, Qt::QueuedConnection);
        }

        std::this_thread::sleep_for((sequenceRectFollowSample_ || sequenceRunning_) ? 10ms : 33ms);
    }
}

void MainWindow::startContinuousJog(hardware::AxisId axis, int direction)
{
    if (direction == 0) {
        return;
    }

    const bool active = axis == hardware::AxisId::X ? xContinuousJogActive_ : yContinuousJogActive_;
    const int currentDirection = axis == hardware::AxisId::X ? xContinuousJogDirection_ : yContinuousJogDirection_;
    if (active && currentDirection == direction) {
        return;
    }

    stopContinuousJog(axis);

    double speedMmPerS = 0.0;
    try {
        speedMmPerS = readJogSpeedMmPerS();
    } catch (const std::exception& ex) {
        appendLog("Erreur jog continu: " + QString::fromUtf8(ex.what()));
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }

    const double limitMm = direction > 0 ? 25.0 : 0.0;
    if (const auto currentPosition = latestPolledMotorPosition(); currentPosition.has_value()) {
        if (axis == hardware::AxisId::X) {
            startPredictedMotorMotion(
                currentPosition->x(),
                std::nullopt,
                limitMm,
                std::nullopt,
                speedMmPerS,
                std::nullopt
            );
        } else {
            startPredictedMotorMotion(
                std::nullopt,
                currentPosition->y(),
                std::nullopt,
                limitMm,
                std::nullopt,
                speedMmPerS
            );
        }
    }
    auto controller = motorController_;
    std::mutex* axisMutex = axis == hardware::AxisId::X ? &xContinuousJogMutex_ : &yContinuousJogMutex_;
    const std::uint64_t generation = axis == hardware::AxisId::X
        ? ++xContinuousJogGeneration_
        : ++yContinuousJogGeneration_;

    if (axis == hardware::AxisId::X) {
        xContinuousJogActive_ = true;
        xContinuousJogDirection_ = direction;
    } else {
        yContinuousJogActive_ = true;
        yContinuousJogDirection_ = direction;
    }

    std::thread([this, controller, axis, axisMutex, generation, speedMmPerS, limitMm]() {
        std::lock_guard<std::mutex> axisLock(*axisMutex);

        const std::uint64_t currentGeneration = axis == hardware::AxisId::X
            ? xContinuousJogGeneration_.load()
            : yContinuousJogGeneration_.load();
        if (generation != currentGeneration) {
            return;
        }

        try {
            controller->setVelocity(axis, speedMmPerS);

            const std::uint64_t refreshedGeneration = axis == hardware::AxisId::X
                ? xContinuousJogGeneration_.load()
                : yContinuousJogGeneration_.load();
            if (generation != refreshedGeneration) {
                return;
            }

            controller->moveAbsoluteNoWait(axis, limitMm);
        } catch (const std::exception& ex) {
            QMetaObject::invokeMethod(this, [this, axis, message = QString::fromUtf8(ex.what())]() {
                if (axis == hardware::AxisId::X) {
                    xContinuousJogActive_ = false;
                    xContinuousJogDirection_ = 0;
                } else {
                    yContinuousJogActive_ = false;
                    yContinuousJogDirection_ = 0;
                }
                appendLog("Erreur jog continu: " + message);
                statusBar()->showMessage(message, 4000);
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void MainWindow::stopContinuousJog(hardware::AxisId axis)
{
    bool wasActive = false;
    std::mutex* axisMutex = axis == hardware::AxisId::X ? &xContinuousJogMutex_ : &yContinuousJogMutex_;
    const std::uint64_t generation = axis == hardware::AxisId::X
        ? ++xContinuousJogGeneration_
        : ++yContinuousJogGeneration_;
    if (axis == hardware::AxisId::X) {
        wasActive = xContinuousJogActive_;
        xContinuousJogActive_ = false;
        xContinuousJogDirection_ = 0;
    } else {
        wasActive = yContinuousJogActive_;
        yContinuousJogDirection_ = 0;
        yContinuousJogActive_ = false;
    }

    if (!wasActive) {
        return;
    }

    if (const auto estimatedPosition = estimatedMotorPositionForOverlay(); estimatedPosition.has_value()) {
        if (axis == hardware::AxisId::X) {
            stopPredictedMotorMotion(estimatedPosition->x(), std::nullopt);
        } else {
            stopPredictedMotorMotion(std::nullopt, estimatedPosition->y());
        }
    } else {
        stopPredictedMotorMotion();
    }

    auto controller = motorController_;
    std::thread([this, controller, axis, axisMutex, generation]() {
        std::lock_guard<std::mutex> axisLock(*axisMutex);

        const std::uint64_t currentGeneration = axis == hardware::AxisId::X
            ? xContinuousJogGeneration_.load()
            : yContinuousJogGeneration_.load();
        if (generation != currentGeneration) {
            return;
        }

        try {
            controller->stopAxis(axis);
        } catch (const std::exception& ex) {
            QMetaObject::invokeMethod(this, [this, message = QString::fromUtf8(ex.what())]() {
                appendLog("Erreur stop jog: " + message);
                statusBar()->showMessage(message, 4000);
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void MainWindow::updateHeldJogTimer()
{
}

void MainWindow::triggerHeldJog()
{
}

bool MainWindow::shouldHandleJogKeys() const
{
    if (!isActiveWindow()) {
        return false;
    }

    if (QApplication::activeModalWidget() != nullptr) {
        return false;
    }
    if (QApplication::activePopupWidget() != nullptr) {
        return false;
    }

    QWidget* focus = QApplication::focusWidget();
    if (qobject_cast<QLineEdit*>(focus) != nullptr
        || qobject_cast<QComboBox*>(focus) != nullptr
        || qobject_cast<QPlainTextEdit*>(focus) != nullptr) {
        return false;
    }

    return true;
}

void MainWindow::onScanPorts()
{
    if (xPortCombo_ == nullptr || yPortCombo_ == nullptr) {
        openMotorConnectionDialog();
    }

    auto controller = motorController_;
    runMotorTask("Recherche des ports COM", [this, controller]() {
        const QStringList ports = controller->scanAvailablePorts();
        MotorTaskResult result;
        result.success = true;
        result.message = ports.isEmpty() ? "Aucun port Newport detecte." : QString("Ports detectes: %1").arg(ports.join(", "));
        result.uiContinuation = [this, ports]() {
            if (xPortCombo_ == nullptr || yPortCombo_ == nullptr) {
                return;
            }
            const QString previousX = xPortCombo_->currentText();
            const QString previousY = yPortCombo_->currentText();

            xPortCombo_->clear();
            yPortCombo_->clear();
            xPortCombo_->addItems(ports);
            yPortCombo_->addItems(ports);

            if (ports.contains(previousX)) {
                xPortCombo_->setCurrentText(previousX);
            } else if (!ports.isEmpty()) {
                xPortCombo_->setCurrentIndex(0);
            }

            if (ports.contains(previousY)) {
                yPortCombo_->setCurrentText(previousY);
            } else if (ports.size() > 1) {
                yPortCombo_->setCurrentIndex(1);
            } else if (!ports.isEmpty()) {
                yPortCombo_->setCurrentIndex(0);
            }
        };
        return result;
    });
}

void MainWindow::onConnectAxes()
{
    if (xPortCombo_ == nullptr || yPortCombo_ == nullptr) {
        openMotorConnectionDialog();
        return;
    }

    const QString xPort = xPortCombo_->currentText().trimmed();
    const QString yPort = yPortCombo_->currentText().trimmed();
    auto controller = motorController_;

    runMotorTask(QString("Connexion moteurs X=%1 Y=%2").arg(xPort, yPort), [controller, xPort, yPort]() {
        controller->connectAxes(xPort, yPort, 1);
        return MotorTaskResult {true, QString("Moteurs connectes: X=%1, Y=%2").arg(xPort, yPort), {}};
    });
}

void MainWindow::onHomeAxes()
{
    stopContinuousJog(hardware::AxisId::X);
    stopContinuousJog(hardware::AxisId::Y);
    stopPredictedMotorMotion();

    auto controller = motorController_;
    runMotorTask("Initialisation / homing X+Y", [controller]() {
        controller->homeAll(90000);
        return MotorTaskResult {true, "Homing X+Y termine.", {}};
    });
}

void MainWindow::onDisconnectAxes()
{
    setGotoArmed(false);
    setSequenceSelectArmed(false);
    clearSequencePreviewSelection();
    leftKeyHeld_ = false;
    rightKeyHeld_ = false;
    upKeyHeld_ = false;
    downKeyHeld_ = false;
    stopContinuousJog(hardware::AxisId::X);
    stopContinuousJog(hardware::AxisId::Y);
    stopPredictedMotorMotion();

    auto controller = motorController_;
    runMotorTask("Deconnexion moteurs", [controller]() {
        controller->disconnectAxes(false);
        return MotorTaskResult {true, "Moteurs deconnectes.", {}};
    });
}

void MainWindow::onJogAxis(hardware::AxisId axis, int direction)
{
    const double stepMm = readJogStepMm();
    const double delta = direction < 0 ? -stepMm : stepMm;
    const QString axisLabel = hardware::axisIdLabel(axis);
    auto controller = motorController_;

    runMotorTask(QString("Jog %1").arg(axisLabel), [controller, axis, axisLabel, delta]() {
        controller->moveRelative(axis, delta, 30000);
        return MotorTaskResult {true, QString("Jog %1 applique: %2 mm").arg(axisLabel).arg(delta, 0, 'f', 3), {}};
    });
}

void MainWindow::onMoveAbsoluteBoth()
{
    stopContinuousJog(hardware::AxisId::X);
    stopContinuousJog(hardware::AxisId::Y);
    double targetXmm = 0.0;
    double targetYmm = 0.0;
    double stageSpeedMmPerS = 0.0;
    try {
        targetXmm = readAbsoluteTargetMm(hardware::AxisId::X);
        targetYmm = readAbsoluteTargetMm(hardware::AxisId::Y);
        stageSpeedMmPerS = readJogSpeedMmPerS();
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "Mouvement absolu XY", QString::fromUtf8(ex.what()));
        return;
    }

    if (const auto currentPosition = latestPolledMotorPosition(); currentPosition.has_value()) {
        startPredictedMotorMotion(
            currentPosition->x(),
            currentPosition->y(),
            targetXmm,
            targetYmm,
            stageSpeedMmPerS,
            stageSpeedMmPerS
        );
    }
    auto controller = motorController_;

    runMotorTask("Mouvement absolu XY", [controller, targetXmm, targetYmm, stageSpeedMmPerS, this]() {
        const auto absBoth = controller->snapshotBoth();
        if (absBoth.x.positionValid && absBoth.y.positionValid) {
            startPredictedMotorMotion(
                absBoth.x.positionMm,
                absBoth.y.positionMm,
                targetXmm,
                targetYmm,
                stageSpeedMmPerS,
                stageSpeedMmPerS
            );
        }
        controller->setVelocity(hardware::AxisId::X, stageSpeedMmPerS);
        controller->setVelocity(hardware::AxisId::Y, stageSpeedMmPerS);
        controller->moveAbsoluteNoWait(hardware::AxisId::X, targetXmm);
        controller->moveAbsoluteNoWait(hardware::AxisId::Y, targetYmm);
        controller->waitAxis(hardware::AxisId::X, 30000);
        controller->waitAxis(hardware::AxisId::Y, 30000);
        stopPredictedMotorMotion(targetXmm, targetYmm);
        return MotorTaskResult {
            true,
            QString("Mouvement absolu XY -> X=%1 mm | Y=%2 mm").arg(targetXmm, 0, 'f', 3).arg(targetYmm, 0, 'f', 3),
            {}
        };
    });
}

double MainWindow::readJogSpeedMmPerS() const
{
    bool ok = false;
    const double speedMmPerS = jogStepEdit_->text().trimmed().toDouble(&ok);
    if (!ok || speedMmPerS <= 0.0) {
        throw std::runtime_error("La vitesse de jog doit etre un nombre strictement positif.");
    }
    return speedMmPerS;
}

double MainWindow::readJogStepMm() const
{
    bool ok = false;
    const double stepMm = jogStepEdit_->text().trimmed().toDouble(&ok);
    if (!ok || stepMm <= 0.0) {
        throw std::runtime_error("Le pas de jog doit etre un nombre strictement positif.");
    }
    return stepMm;
}

double MainWindow::readAbsoluteTargetMm(hardware::AxisId axis) const
{
    const QLineEdit* edit = axis == hardware::AxisId::X ? absXEdit_ : absYEdit_;
    bool ok = false;
    const double targetMm = edit->text().trimmed().toDouble(&ok);
    if (!ok) {
        throw std::runtime_error("La position absolue doit etre un nombre valide.");
    }
    return targetMm;
}

void MainWindow::onConnectPotentiostat()
{
    if (potentiostatBusy_.load() || potentiostatController_ == nullptr) {
        return;
    }
    potentiostatBusy_.store(true);
    if (potentiostatConnectButton_    != nullptr) potentiostatConnectButton_->setEnabled(false);
    if (potentiostatFirmwareButton_   != nullptr) potentiostatFirmwareButton_->setEnabled(false);
    if (potentiostatDisconnectButton_ != nullptr) potentiostatDisconnectButton_->setEnabled(false);
    if (potentiostatStatusLabel_      != nullptr) potentiostatStatusLabel_->setText("Connexion...");

    const QString dllPath = potentiostatDllPathEdit_  != nullptr ? potentiostatDllPathEdit_->text()  : QString();
    const QString address = potentiostatAddressEdit_  != nullptr ? potentiostatAddressEdit_->text()  : "169.254.3.150";
    const int channel     = potentiostatChannelCombo_ != nullptr ? potentiostatChannelCombo_->currentIndex() + 1 : 1;

    auto ctrl = potentiostatController_;
    if (potentiostatThread_.joinable()) {
        potentiostatThread_.join();
    }
    potentiostatThread_ = std::thread([this, ctrl, dllPath, address, channel]() {
        const bool ok = ctrl->connect(dllPath, address, channel);
        const QString msg = ok ? QString("Connecte - %1").arg(ctrl->connectedModel()) : ctrl->lastError();
        QMetaObject::invokeMethod(this, [this, ok, msg]() {
            potentiostatBusy_.store(false);
            const QString style = ok ? "color:#1a7f37; font-size:9pt;" : "color:#c0392b; font-size:9pt;";
            if (potentiostatStatusLabel_      != nullptr) { potentiostatStatusLabel_->setText(msg);      potentiostatStatusLabel_->setStyleSheet(style); }
            if (potentiostatMeasureStateLabel_ != nullptr) { potentiostatMeasureStateLabel_->setText(ok ? QString("Etat : %1").arg(msg) : "Etat : non connecte"); potentiostatMeasureStateLabel_->setStyleSheet(style); }
            if (potentiostatRunButton_        != nullptr) potentiostatRunButton_->setEnabled(ok);
            if (potentiostatStopButton_       != nullptr) potentiostatStopButton_->setEnabled(false);
            if (potentiostatConnectButton_    != nullptr) potentiostatConnectButton_->setEnabled(!ok);
            if (potentiostatFirmwareButton_   != nullptr) potentiostatFirmwareButton_->setEnabled(ok);
            if (potentiostatDisconnectButton_ != nullptr) potentiostatDisconnectButton_->setEnabled(ok);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onDisconnectPotentiostat()
{
    if (potentiostatBusy_.load() || potentiostatController_ == nullptr) {
        return;
    }
    potentiostatBusy_.store(true);
    if (potentiostatConnectButton_    != nullptr) potentiostatConnectButton_->setEnabled(false);
    if (potentiostatFirmwareButton_   != nullptr) potentiostatFirmwareButton_->setEnabled(false);
    if (potentiostatDisconnectButton_ != nullptr) potentiostatDisconnectButton_->setEnabled(false);

    auto ctrl = potentiostatController_;
    if (potentiostatThread_.joinable()) {
        potentiostatThread_.join();
    }
    potentiostatThread_ = std::thread([this, ctrl]() {
        ctrl->disconnect();
        QMetaObject::invokeMethod(this, [this]() {
            potentiostatBusy_.store(false);
            constexpr auto kStyleOff = "color:#5c6570; font-size:9pt;";
            if (potentiostatStatusLabel_       != nullptr) { potentiostatStatusLabel_->setText("Deconnecte");       potentiostatStatusLabel_->setStyleSheet(kStyleOff); }
            if (potentiostatMeasureStateLabel_ != nullptr) { potentiostatMeasureStateLabel_->setText("Etat : non connecte"); potentiostatMeasureStateLabel_->setStyleSheet(kStyleOff); }
            if (potentiostatRunButton_         != nullptr) potentiostatRunButton_->setEnabled(false);
            if (potentiostatStopButton_        != nullptr) potentiostatStopButton_->setEnabled(false);
            if (potentiostatConnectButton_     != nullptr) potentiostatConnectButton_->setEnabled(true);
            if (potentiostatFirmwareButton_    != nullptr) potentiostatFirmwareButton_->setEnabled(false);
            if (potentiostatDisconnectButton_  != nullptr) potentiostatDisconnectButton_->setEnabled(false);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onLoadFirmware()
{
    if (potentiostatBusy_.load() || potentiostatController_ == nullptr || !potentiostatController_->isConnected()) {
        return;
    }
    potentiostatBusy_.store(true);
    if (potentiostatFirmwareButton_   != nullptr) potentiostatFirmwareButton_->setEnabled(false);
    if (potentiostatStatusLabel_      != nullptr) potentiostatStatusLabel_->setText("Chargement firmware...");

    const int channel = potentiostatChannelCombo_ != nullptr ? potentiostatChannelCombo_->currentIndex() + 1 : 1;
    auto ctrl = potentiostatController_;
    if (potentiostatThread_.joinable()) {
        potentiostatThread_.join();
    }
    potentiostatThread_ = std::thread([this, ctrl, channel]() {
        QString msg;
        try {
            ctrl->loadFirmware(channel);
            msg = QString("Firmware charge - %1").arg(ctrl->connectedModel());
        } catch (const std::exception& ex) {
            msg = QString("Erreur firmware : %1").arg(QString::fromUtf8(ex.what()));
        }
        const bool stillOk = ctrl->isConnected();
        QMetaObject::invokeMethod(this, [this, msg, stillOk]() {
            potentiostatBusy_.store(false);
            if (potentiostatStatusLabel_ != nullptr) {
                potentiostatStatusLabel_->setText(msg);
            }
            if (potentiostatFirmwareButton_ != nullptr) {
                potentiostatFirmwareButton_->setEnabled(stillOk);
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onStartCaPotentiostat()
{
    const PotentiostatTechnique technique = selectedPotentiostatTechnique();
    const QString techniqueLabel = selectedPotentiostatTechniqueLabel();
    const bool hasScanZone = sequenceStartMotorMm_.has_value() && sequenceEndMotorMm_.has_value();

    if (potentiostatController_ == nullptr || !potentiostatController_->isConnected()) {
        QMessageBox::warning(this, techniqueLabel, "Connecter d'abord le potentiostat.");
        return;
    }
    if (hasScanZone && motorController_ == nullptr) {
        QMessageBox::warning(this, techniqueLabel, "Connecter d'abord les moteurs.");
        return;
    }
    if (potentiostatBusy_.load() || sequenceRunning_) {
        return;
    }

    bool simpleMeasurement = !hasScanZone;
    if (simpleMeasurement) {
        const auto reply = QMessageBox::question(
            this,
            techniqueLabel,
            "Aucune zone de mesure n'est definie.\n\n"
            "Lancer une mesure simple sans deplacement moteur ?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    const QPointF startMm = hasScanZone ? *sequenceStartMotorMm_ : QPointF();
    const QPointF endMm   = hasScanZone ? *sequenceEndMotorMm_ : QPointF();
    const QString mode = sequenceModeCombo_ != nullptr ? sequenceModeCombo_->currentText() : QString("Lineaire");
    const bool rectangleMode = hasScanZone && (mode == "Rectangle");
    const ScanConfig cfg = scanConfig_;
    const ScanConfig::RectangleStartCorner rectangleStartCorner = effectiveRectangleStartCorner();
    const ScanConfig::RectanglePrimaryAxis rectanglePrimaryAxis = effectiveRectanglePrimaryAxis();

    double stepMm = 0.0;
    double durationS = 0.0;
    double caSimpleDurationS = 60.0;
    double simpleMeasurementSamplePeriodS = 1.0;
    double continuousSamplePitchMm = 0.0;
    double continuousRowStepMm = 0.0;
    ContinuousRasterPlan continuousPlan;
    std::vector<QPointF> waypoints;
    std::vector<std::pair<int, int>> order;
    int rows = 0;
    int cols = 0;
    double scanXMinMm = hasScanZone ? std::min(startMm.x(), endMm.x()) : 0.0;
    double scanXMaxMm = hasScanZone ? std::max(startMm.x(), endMm.x()) : 0.0;
    double scanYMinMm = hasScanZone ? std::min(startMm.y(), endMm.y()) : 0.0;
    double scanYMaxMm = hasScanZone ? std::max(startMm.y(), endMm.y()) : 0.0;

    if (!simpleMeasurement && cfg.mode == ScanConfig::AcquisitionMode::PointByPoint) {
        bool stepOk = false;
        bool durationOk = false;
        stepMm = sequenceStepMmEdit_ != nullptr ? sequenceStepMmEdit_->text().trimmed().toDouble(&stepOk) : 0.0;
        durationS = sequenceDurationEdit_ != nullptr ? sequenceDurationEdit_->text().trimmed().toDouble(&durationOk) : 0.0;
        if (!stepOk || !durationOk || stepMm <= 0.0 || durationS <= 0.0) {
            QMessageBox::warning(this, techniqueLabel, "Pas (mm) et Duree/pt doivent etre des nombres positifs.");
            return;
        }

        waypoints = rectangleMode
            ? buildWaypointsRect(startMm, endMm, stepMm)
            : buildWaypointsLinear(startMm, endMm, stepMm);
        cols = rectangleMode
            ? std::max(1, static_cast<int>(std::ceil(std::abs(endMm.x() - startMm.x()) / stepMm - 1e-9))) + 1
            : static_cast<int>(waypoints.size());
        rows = rectangleMode
            ? std::max(1, static_cast<int>(std::ceil(std::abs(endMm.y() - startMm.y()) / stepMm - 1e-9))) + 1
            : 1;
        // Update scan bounds to the effective (possibly enlarged) zone
        if (rectangleMode) {
            scanXMaxMm = scanXMinMm + static_cast<double>(cols - 1) * stepMm;
            scanYMaxMm = scanYMinMm + static_cast<double>(rows - 1) * stepMm;
        }

        if (rectangleMode) {
            order = buildRectangleTraversalOrder(rows, cols);
        } else {
            order.reserve(static_cast<std::size_t>(std::max(1, cols)));
            for (int col = 0; col < cols; ++col) {
                order.emplace_back(0, col);
            }
        }
    } else if (!simpleMeasurement) {
        if (cfg.scanSpeedMmPerS <= 0.0) {
            QMessageBox::warning(this, techniqueLabel, "La vitesse de balayage continu doit etre strictement positive.");
            return;
        }
        if (cfg.trigger == ScanConfig::ContinuousTrigger::Distance) {
            if (cfg.triggerDistanceMm <= 0.0) {
                QMessageBox::warning(this, techniqueLabel, "Le pas d'acquisition en mode continu doit etre strictement positif.");
                return;
            }
            const double recommendedMaxSpeed = cfg.triggerDistanceMm / kContinuousGuaranteedSamplePeriodS;
            if (cfg.scanSpeedMmPerS > recommendedMaxSpeed + kScanPlanningEpsilonMm) {
                QMessageBox::warning(
                    this,
                    techniqueLabel,
                    QString("Vitesse trop elevee pour garantir un echantillonnage tous les %1 mm.\n"
                            "Vitesse maximale recommandee : %2 mm/s.")
                        .arg(cfg.triggerDistanceMm, 0, 'f', 3)
                        .arg(recommendedMaxSpeed, 0, 'f', 3));
                return;
            }
            continuousSamplePitchMm = cfg.triggerDistanceMm;
        } else {
            if (cfg.triggerTimeS <= 0.0) {
                QMessageBox::warning(this, techniqueLabel, "L'intervalle d'acquisition temporel doit etre strictement positif.");
                return;
            }
            if (cfg.triggerTimeS < kContinuousGuaranteedSamplePeriodS - kScanPlanningEpsilonMm) {
                QMessageBox::warning(
                    this,
                    techniqueLabel,
                    QString("Intervalle temporel trop court pour garantir un balayage continu precis.\n"
                            "Intervalle minimal recommande : %1 s.")
                        .arg(kContinuousGuaranteedSamplePeriodS, 0, 'f', 3));
                return;
            }
            continuousSamplePitchMm = cfg.scanSpeedMmPerS * cfg.triggerTimeS;
        }

        if (continuousSamplePitchMm <= 0.0) {
            QMessageBox::warning(this, techniqueLabel, "Intervalle d'acquisition continu invalide.");
            return;
        }

        if (rectangleMode) {
            continuousRowStepMm = cfg.rowStepMm;
            if (continuousRowStepMm <= 0.0) {
                QMessageBox::warning(this, techniqueLabel, "Le saut de ligne doit etre strictement positif en mode continu.");
                return;
            }
            const auto scale = currentGotoScale();
            continuousPlan = buildContinuousRectanglePlan(
                startMm,
                endMm,
                continuousSamplePitchMm,
                continuousRowStepMm,
                rectangleStartCorner,
                rectanglePrimaryAxis,
                scale.first > 0.0,
                scale.second > 0.0);
        } else {
            continuousPlan = buildContinuousLinearPlan(startMm, endMm, continuousSamplePitchMm);
        }

        waypoints = continuousPlan.sampleWaypoints;
        order = continuousPlan.order;
        rows = continuousPlan.rows;
        cols = continuousPlan.cols;
        stepMm = continuousSamplePitchMm;
        durationS = 0.0;
        scanXMinMm = continuousPlan.xMinMm;
        scanXMaxMm = continuousPlan.xMaxMm;
        scanYMinMm = continuousPlan.yMinMm;
        scanYMaxMm = continuousPlan.yMaxMm;
    }

    if (!simpleMeasurement && waypoints.empty()) {
        QMessageBox::warning(this, techniqueLabel, "Aucun point de balayage (depart == arrivee ?).");
        return;
    }

    if (simpleMeasurement && technique == PotentiostatTechnique::CA) {
        bool durationOk = false;
        caSimpleDurationS = QInputDialog::getDouble(
            this,
            techniqueLabel,
            "Duree de la mesure simple CA (s)",
            60.0,
            0.1,
            86400.0,
            1,
            &durationOk);
        if (!durationOk) {
            return;
        }
    }

    double stageSpeedMmPerS = 1.0;
    if (!simpleMeasurement) {
        try { stageSpeedMmPerS = readJogSpeedMmPerS(); } catch (...) {}
    }
    const int nPoints = simpleMeasurement ? 0 : static_cast<int>(waypoints.size());
    const int channel = potentiostatChannelCombo_ != nullptr ? potentiostatChannelCombo_->currentIndex() + 1 : 1;

    std::variant<hardware::CaParams, hardware::OcvParams, hardware::CvaParams> techniqueParams;
    QString techniqueLogSummary;

    if (technique == PotentiostatTechnique::CA) {
        hardware::CaParams params;
        params.channel   = channel;
        params.voltage   = potentiostatVoltageEdit_       != nullptr ? potentiostatVoltageEdit_->text().toDouble()    : 0.5;
        params.vsInit    = potentiostatVsCombo_           != nullptr && potentiostatVsCombo_->currentText() == "Pref";
        params.eRange    = potentiostatErangeCombo_       != nullptr ? potentiostatErangeCombo_->currentIndex()       : 0;
        params.iRange    = potentiostatCurrentRangeCombo_ != nullptr ? potentiostatCurrentRangeCombo_->currentIndex() : 12;
        params.bandwidth = potentiostatBandwidthCombo_    != nullptr ? potentiostatBandwidthCombo_->currentText().toInt() : 8;
        params.nCycles   = potentiostatNbCyclesEdit_      != nullptr ? potentiostatNbCyclesEdit_->text().toInt()      : 0;

        if (simpleMeasurement) {
            params.duration = caSimpleDurationS;
            params.recordDt = 1.0;
            simpleMeasurementSamplePeriodS = params.recordDt;
        } else if (cfg.mode == ScanConfig::AcquisitionMode::PointByPoint) {
            const double motorTimeoutS = kDefaultMotorTimeoutMs / 1000.0;
            params.duration = nPoints * durationS + std::max(0, nPoints - 1) * 2.0 * motorTimeoutS + 5.0;
        } else {
            double totalLineTimeS = 0.0;
            for (const ContinuousRasterRow& rowPlan : continuousPlan.linePlans) {
                const double extendedLengthMm = rowPlan.lengthMm + 2.0 * kContinuousRunUpMm;
                totalLineTimeS += (cfg.scanSpeedMmPerS > 0.0) ? (extendedLengthMm / cfg.scanSpeedMmPerS) : 0.0;
            }
            const double transitionDistanceMm = (rectangleMode && rows > 1) ? continuousRowStepMm : 0.0;
            const double repoExtraMm = 2.0 * kContinuousRunUpMm; // extendedEnd → next extendedStart
            const int nSweepLines = static_cast<int>(continuousPlan.linePlans.size());
            const double transitionTimeS = (nSweepLines > 1 && stageSpeedMmPerS > 0.0)
                ? ((transitionDistanceMm + repoExtraMm) / stageSpeedMmPerS + 1.0)
                : 0.0;
            params.duration = totalLineTimeS + std::max(0, nSweepLines - 1) * transitionTimeS + 15.0;
        }
        if (!simpleMeasurement) {
            params.recordDt = std::max(60.0, params.duration);
        }
        techniqueLogSummary = QString("tension=%1 V, duree=%2 s, record_dt=%3 s")
            .arg(params.voltage, 0, 'f', 3)
            .arg(params.duration, 0, 'f', 1)
            .arg(params.recordDt, 0, 'f', 1);
        techniqueParams = params;
    } else if (technique == PotentiostatTechnique::OCV) {
        bool restOk = false;
        bool recordDEOk = false;
        bool recordDtOk = false;
        const double restS = durationSecondsFromEdits(
            potentiostatOcvRestHoursEdit_, potentiostatOcvRestMinutesEdit_, potentiostatOcvRestSecondsEdit_, &restOk);
        const double recordDEmV = potentiostatOcvRecordDEEdit_ != nullptr
            ? potentiostatOcvRecordDEEdit_->text().trimmed().toDouble(&recordDEOk)
            : 0.0;
        const double recordDtS = potentiostatOcvRecordDtEdit_ != nullptr
            ? potentiostatOcvRecordDtEdit_->text().trimmed().toDouble(&recordDtOk)
            : 0.0;
        if (!restOk || !recordDEOk || !recordDtOk || restS <= 0.0 || recordDEmV < 0.0 || recordDtS <= 0.0) {
            QMessageBox::warning(this, techniqueLabel, "Les parametres OCV doivent etre des nombres valides et strictement positifs.");
            return;
        }

        hardware::OcvParams params;
        params.channel  = channel;
        params.duration = restS;
        params.recordDE = recordDEmV * 1e-3;
        params.recordDt = recordDtS;
        params.eRange   = potentiostatOcvErangeCombo_ != nullptr ? potentiostatOcvErangeCombo_->currentIndex() : 3;
        simpleMeasurementSamplePeriodS = params.recordDt;

        techniqueLogSummary = QString("repos=%1 s, record_dE=%2 V, record_dt=%3 s")
            .arg(params.duration, 0, 'f', 3)
            .arg(params.recordDE, 0, 'f', 6)
            .arg(params.recordDt, 0, 'f', 3);
        techniqueParams = params;
    } else {
        bool eiOk = false;
        bool e1Ok = false;
        bool e2Ok = false;
        bool efOk = false;
        bool scanRateOk = false;
        bool dtiOk = false;
        bool dt1Ok = false;
        bool dt2Ok = false;
        bool dtfOk = false;
        bool measurePercentOk = false;
        bool repeatCyclesOk = false;
        bool tiOk = false;
        bool t1Ok = false;
        bool t2Ok = false;
        bool tfOk = false;

        const double ei = potentiostatCvaEiEdit_ != nullptr ? potentiostatCvaEiEdit_->text().trimmed().toDouble(&eiOk) : 0.0;
        const double e1 = potentiostatCvaE1Edit_ != nullptr ? potentiostatCvaE1Edit_->text().trimmed().toDouble(&e1Ok) : 0.0;
        const double e2 = potentiostatCvaE2Edit_ != nullptr ? potentiostatCvaE2Edit_->text().trimmed().toDouble(&e2Ok) : 0.0;
        const double ef = potentiostatCvaEfEdit_ != nullptr ? potentiostatCvaEfEdit_->text().trimmed().toDouble(&efOk) : 0.0;
        const double scanRateValue = potentiostatCvaScanRateEdit_ != nullptr ? potentiostatCvaScanRateEdit_->text().trimmed().toDouble(&scanRateOk) : 0.0;
        const double dti = potentiostatCvaDtiEdit_ != nullptr ? potentiostatCvaDtiEdit_->text().trimmed().toDouble(&dtiOk) : 0.0;
        const double dt1 = potentiostatCvaDt1Edit_ != nullptr ? potentiostatCvaDt1Edit_->text().trimmed().toDouble(&dt1Ok) : 0.0;
        const double dt2 = potentiostatCvaDt2Edit_ != nullptr ? potentiostatCvaDt2Edit_->text().trimmed().toDouble(&dt2Ok) : 0.0;
        const double dtf = potentiostatCvaDtfEdit_ != nullptr ? potentiostatCvaDtfEdit_->text().trimmed().toDouble(&dtfOk) : 0.0;
        const double measurePercent = potentiostatCvaMeasurePercentEdit_ != nullptr
            ? potentiostatCvaMeasurePercentEdit_->text().trimmed().toDouble(&measurePercentOk)
            : 50.0;
        const int repeatCycles = potentiostatCvaRepeatCyclesEdit_ != nullptr
            ? potentiostatCvaRepeatCyclesEdit_->text().trimmed().toInt(&repeatCyclesOk)
            : 1;
        const double ti = durationSecondsFromEdits(
            potentiostatCvaTiHoursEdit_, potentiostatCvaTiMinutesEdit_, potentiostatCvaTiSecondsEdit_, &tiOk);
        const double t1 = durationSecondsFromEdits(
            potentiostatCvaT1HoursEdit_, potentiostatCvaT1MinutesEdit_, potentiostatCvaT1SecondsEdit_, &t1Ok);
        const double t2 = durationSecondsFromEdits(
            potentiostatCvaT2HoursEdit_, potentiostatCvaT2MinutesEdit_, potentiostatCvaT2SecondsEdit_, &t2Ok);
        const double tf = durationSecondsFromEdits(
            potentiostatCvaTfHoursEdit_, potentiostatCvaTfMinutesEdit_, potentiostatCvaTfSecondsEdit_, &tfOk);

        if (!eiOk || !e1Ok || !e2Ok || !efOk || !scanRateOk
            || !dtiOk || !dt1Ok || !dt2Ok || !dtfOk
            || !measurePercentOk || !repeatCyclesOk
            || !tiOk || !t1Ok || !t2Ok || !tfOk) {
            QMessageBox::warning(this, techniqueLabel, "Les parametres CVA doivent etre des nombres valides.");
            return;
        }
        if (scanRateValue <= 0.0 || dti <= 0.0 || dt1 <= 0.0 || dt2 <= 0.0 || measurePercent < 0.0 || measurePercent > 100.0 || repeatCycles <= 0) {
            QMessageBox::warning(this, techniqueLabel, "Verifier la vitesse de scan, les intervalles d'enregistrement et le nombre de cycles CVA.");
            return;
        }
        if (potentiostatCvaEndScanCheck_ != nullptr && potentiostatCvaEndScanCheck_->isChecked() && (dtf <= 0.0 || tf < 0.0)) {
            QMessageBox::warning(this, techniqueLabel, "Les parametres de fin de balayage CVA sont invalides.");
            return;
        }

        const double scanRateMvPerS = scanRateToMilliVoltsPerSecond(
            scanRateValue,
            potentiostatCvaScanRateUnitCombo_ != nullptr ? potentiostatCvaScanRateUnitCombo_->currentText() : QString("mV/s"));
        const double scanRecordDtS = std::min(dt1, dt2);

        hardware::CvaParams params;
        params.channel          = channel;
        params.vsInitialScan    = {
            comboUsesInitialReference(potentiostatCvaEiVsCombo_),
            comboUsesInitialReference(potentiostatCvaE1VsCombo_),
            comboUsesInitialReference(potentiostatCvaE2VsCombo_),
            comboUsesInitialReference(potentiostatCvaEfVsCombo_)
        };
        params.voltageScan      = {
            ei,
            e1,
            e2,
            (potentiostatCvaEndScanCheck_ != nullptr && potentiostatCvaEndScanCheck_->isChecked()) ? ef : ei
        };
        params.scanRateMvPerS   = {scanRateMvPerS, scanRateMvPerS, scanRateMvPerS, scanRateMvPerS};
        params.recordDE         = std::max(scanRateMvPerS * 1e-3 * scanRecordDtS, 1e-6);
        params.averageOverDE    = true;
        params.nCycles          = repeatCycles;
        params.beginMeasuringI  = std::clamp(1.0 - (measurePercent / 100.0), 0.0, 1.0);
        params.endMeasuringI    = 1.0;
        params.vsInitialStep    = {
            comboUsesInitialReference(potentiostatCvaEiVsCombo_),
            comboUsesInitialReference(potentiostatCvaEfVsCombo_)
        };
        params.voltageStep      = {
            ei,
            (potentiostatCvaEndScanCheck_ != nullptr && potentiostatCvaEndScanCheck_->isChecked()) ? ef : ei
        };
        params.durationStep     = {
            ti,
            (potentiostatCvaEndScanCheck_ != nullptr && potentiostatCvaEndScanCheck_->isChecked()) ? tf : 0.0
        };
        params.recordDtStep     = {
            dti,
            (potentiostatCvaEndScanCheck_ != nullptr && potentiostatCvaEndScanCheck_->isChecked()) ? dtf : scanRecordDtS
        };
        params.recordDt         = scanRecordDtS;
        params.recordDI         = 0.0;
        params.trigOnOff        = false;
        params.iRange           = potentiostatCvaCurrentRangeCombo_ != nullptr ? potentiostatCvaCurrentRangeCombo_->currentIndex() : 12;
        params.eRange           = potentiostatCvaErangeCombo_ != nullptr ? potentiostatCvaErangeCombo_->currentIndex() : 0;
        params.bandwidth        = potentiostatCvaBandwidthCombo_ != nullptr ? potentiostatCvaBandwidthCombo_->currentText().toInt() : 5;
        simpleMeasurementSamplePeriodS = params.recordDt;

        Q_UNUSED(t1);
        Q_UNUSED(t2);

        techniqueLogSummary = QString("Ei=%1 V, E1=%2 V, E2=%3 V, Ef=%4 V, rate=%5 mV/s, cycles=%6")
            .arg(ei, 0, 'f', 3)
            .arg(e1, 0, 'f', 3)
            .arg(e2, 0, 'f', 3)
            .arg(params.voltageScan[3], 0, 'f', 3)
            .arg(scanRateMvPerS, 0, 'f', 3)
            .arg(repeatCycles);
        techniqueParams = params;
    }

    potentiostatBusy_.store(true);
    potentiostatStopRequested_.store(false);
    waypointsMm_ = waypoints;
    currentWaypointIndex_ = simpleMeasurement ? -1 : 0;
    if (measureRightStack_ != nullptr) {
        measureRightStack_->setCurrentIndex(0);
    }
    if (potentiostatGraphBox_ != nullptr) {
        potentiostatGraphBox_->setTitle(simpleMeasurement ? "Courbes - mesure simple" : "Courbes");
    }
    if (simpleMeasurement && potentiostatGraphTypeCombo_ != nullptr) {
        const int targetGraphIndex =
            (technique == PotentiostatTechnique::OCV) ? 1 :
            (technique == PotentiostatTechnique::CVA) ? 2 : 0;
        potentiostatGraphTypeCombo_->setCurrentIndex(targetGraphIndex);
    }
    if (view3DButton_ != nullptr) {
        if (view3DButton_->isChecked()) {
            view3DButton_->setChecked(false);
        }
        view3DButton_->setText("Vue 3D");
        view3DButton_->setVisible(!simpleMeasurement);
        view3DButton_->setEnabled(!simpleMeasurement);
    }
    if (potentiostatMapBox_ != nullptr) {
        potentiostatMapBox_->setVisible(!simpleMeasurement);
    }
    resetPotentiostatVisualization(rows, cols, order);
    potentiostatXMin_ = scanXMinMm;
    potentiostatXMax_ = scanXMaxMm;
    potentiostatYMin_ = scanYMinMm;
    potentiostatYMax_ = scanYMaxMm;
    if (!simpleMeasurement && cfg.mode == ScanConfig::AcquisitionMode::PointByPoint) {
        potentiostatLastDwellS_ = durationS;
    }
    if (potentiostatRunButton_   != nullptr) potentiostatRunButton_->setEnabled(false);
    if (potentiostatStopButton_  != nullptr) potentiostatStopButton_->setEnabled(true);
    if (potentiostatExportButton_ != nullptr) potentiostatExportButton_->setEnabled(false);
    if (potentiostatCurrentLabel_   != nullptr) potentiostatCurrentLabel_->setText("...");
    if (potentiostatMeasureStateLabel_ != nullptr) potentiostatMeasureStateLabel_->setText("Lancement...");
    if (potentiostatProgressLabel_ != nullptr) {
        potentiostatProgressLabel_->setText(simpleMeasurement ? "Mesure simple" : "En attente");
    }
    if (sequenceStatusLabel_ != nullptr) {
        sequenceStatusLabel_->setText(simpleMeasurement
            ? QString("Mesure simple - sans balayage")
            : QString("0 / %1 points").arg(nPoints));
    }
    if (tabWidget_ != nullptr) {
        tabWidget_->setCurrentIndex(2);
    }

    auto ctrl      = potentiostatController_;
    auto motorCtrl = motorController_;
    if (potentiostatThread_.joinable()) {
        potentiostatThread_.join();
    }

    if (simpleMeasurement) {
        appendLog(QString("%1 simple : sans zone, sans deplacement moteur | echantillonnage UI=%2 s")
            .arg(techniqueLabel)
            .arg(simpleMeasurementSamplePeriodS, 0, 'f', 3));
        appendLog(QString("[POTDBG] start simple=%1 technique=%2 potStop=%3 seqStop=%4 busy=%5")
            .arg(simpleMeasurement ? "oui" : "non")
            .arg(techniqueLabel)
            .arg(potentiostatStopRequested_.load() ? "oui" : "non")
            .arg(sequenceStopRequested_.load() ? "oui" : "non")
            .arg(potentiostatBusy_.load() ? "oui" : "non"));
    } else if (cfg.mode == ScanConfig::AcquisitionMode::PointByPoint) {
        appendLog(QString("%1 spatial : %2 pts | dwell=%3 s")
            .arg(techniqueLabel)
            .arg(nPoints)
            .arg(durationS, 0, 'f', 2));
    } else {
        appendLog(QString("%1 continu : %2 pts (%3 x %4) | pas acquisition=%5 mm | vitesse=%6 mm/s | pas inter-ligne=%7 mm")
            .arg(techniqueLabel)
            .arg(nPoints)
            .arg(cols)
            .arg(rows)
            .arg(continuousSamplePitchMm, 0, 'f', 3)
            .arg(cfg.scanSpeedMmPerS, 0, 'f', 3)
            .arg(rectangleMode ? continuousRowStepMm : 0.0, 0, 'f', 3));
        if (rectangleMode) {
            appendLog(QString("Zone continue effective : X [%1, %2] mm (origine=%3 mm, effective=%4 mm) | Y [%5, %6] mm (origine=%7 mm, effective=%8 mm)")
                .arg(continuousPlan.xMinMm, 0, 'f', 4)
                .arg(continuousPlan.xMaxMm, 0, 'f', 4)
                .arg(continuousPlan.originalWidthMm, 0, 'f', 4)
                .arg(continuousPlan.effectiveWidthMm, 0, 'f', 4)
                .arg(continuousPlan.yMinMm, 0, 'f', 4)
                .arg(continuousPlan.yMaxMm, 0, 'f', 4)
                .arg(continuousPlan.originalHeightMm, 0, 'f', 4)
                .arg(continuousPlan.effectiveHeightMm, 0, 'f', 4));
        } else {
            appendLog(QString("Ligne continue effective : origine=%1 mm | effective=%2 mm")
                .arg(continuousPlan.originalWidthMm, 0, 'f', 4)
                .arg(continuousPlan.effectiveWidthMm, 0, 'f', 4));
        }
    }
    if (rectangleMode) {
        appendLog(QString("Balayage rectangle : depart=%1 | axe principal=%2")
            .arg(rectangleStartCornerLabel(rectangleStartCorner))
            .arg(rectanglePrimaryAxisLabel(rectanglePrimaryAxis)));
    }
    appendLog(QString("Parametres %1 : %2").arg(techniqueLabel).arg(techniqueLogSummary));

    const QString runModeLabel = measurementModeLabel(simpleMeasurement, cfg.mode);
    const QString runModeSlug = measurementModeSlug(simpleMeasurement, cfg.mode);
    const QString zoneLabel = zoneShapeLabel(simpleMeasurement, rectangleMode);
    const QString initialMotorText = currentMotorPositionLogText(motorController_);
    const double zoneWidthMm = hasScanZone ? std::abs(endMm.x() - startMm.x()) : 0.0;
    const double zoneHeightMm = hasScanZone ? std::abs(endMm.y() - startMm.y()) : 0.0;
    const double zoneLengthMm = hasScanZone ? std::hypot(endMm.x() - startMm.x(), endMm.y() - startMm.y()) : 0.0;

    QString zonePxLine = "Zone px: non applicable.";
    if (hasScanZone) {
        if (sequenceRectStartFrame_.has_value() && sequenceRectEndFrame_.has_value()) {
            const QPoint startPx = *sequenceRectStartFrame_;
            const QPoint endPx = *sequenceRectEndFrame_;
            zonePxLine = QString("Zone px: depart=%1 | arrivee=%2 | taille=%3 x %4 px")
                .arg(formatPointPx(startPx))
                .arg(formatPointPx(endPx))
                .arg(std::abs(endPx.x() - startPx.x()))
                .arg(std::abs(endPx.y() - startPx.y()));
        } else {
            const auto gotoScale = currentGotoScale();
            if (std::abs(gotoScale.first) > kScanPlanningEpsilonMm && std::abs(gotoScale.second) > kScanPlanningEpsilonMm) {
                zonePxLine = QString("Zone px: taille approx=%1 x %2 px | base mm/px=(%3,%4)")
                    .arg(static_cast<int>(std::lround(zoneWidthMm / std::abs(gotoScale.first))))
                    .arg(static_cast<int>(std::lround(zoneHeightMm / std::abs(gotoScale.second))))
                    .arg(gotoScale.first, 0, 'f', 6)
                    .arg(gotoScale.second, 0, 'f', 6);
            } else {
                zonePxLine = "Zone px: indisponible (pas de selection image exploitable).";
            }
        }
    }

    QStringList measurementHeaderLines;
    measurementHeaderLines
        << QString("Technique: %1").arg(techniqueLabel)
        << QString("Mode: %1").arg(runModeLabel)
        << QString("Forme zone: %1").arg(zoneLabel)
        << QString("Canal: %1").arg(channel)
        << QString("Moteur lancement: %1").arg(initialMotorText)
        << zonePxLine;

    if (hasScanZone) {
        measurementHeaderLines
            << QString("Zone mm: depart=%1 | arrivee=%2 | taille=%3 x %4 mm | diagonale=%5 mm")
                .arg(formatPointMm(startMm))
                .arg(formatPointMm(endMm))
                .arg(zoneWidthMm, 0, 'f', 4)
                .arg(zoneHeightMm, 0, 'f', 4)
                .arg(zoneLengthMm, 0, 'f', 4)
            << QString("Bornes moteur: X=[%1,%2] mm | Y=[%3,%4] mm")
                .arg(scanXMinMm, 0, 'f', 4)
                .arg(scanXMaxMm, 0, 'f', 4)
                .arg(scanYMinMm, 0, 'f', 4)
                .arg(scanYMaxMm, 0, 'f', 4);
    } else {
        measurementHeaderLines << "Zone mm: non applicable.";
    }

    if (!simpleMeasurement) {
        measurementHeaderLines << QString("Plan scan: points=%1 | lignes=%2 | colonnes=%3")
            .arg(nPoints)
            .arg(rows)
            .arg(cols);
    }

    if (rectangleMode) {
        measurementHeaderLines << QString("Parcours rectangle: depart=%1 | axe principal=%2")
            .arg(rectangleStartCornerLabel(rectangleStartCorner))
            .arg(rectanglePrimaryAxisLabel(rectanglePrimaryAxis));
    }

    if (simpleMeasurement) {
        measurementHeaderLines << QString("Acquisition simple: periode UI=%1 s").arg(simpleMeasurementSamplePeriodS, 0, 'f', 3);
    } else if (cfg.mode == ScanConfig::AcquisitionMode::PointByPoint) {
        measurementHeaderLines << QString("Balayage pas a pas: pas=%1 mm | dwell=%2 s | mesures_par_point=%3 | vitesse reposition=%4 mm/s")
            .arg(stepMm, 0, 'f', 4)
            .arg(durationS, 0, 'f', 4)
            .arg(cfg.dwellSamples)
            .arg(stageSpeedMmPerS, 0, 'f', 4);
    } else {
        QString triggerLine = QString("Balayage continu: pas acquisition=%1 mm | vitesse=%2 mm/s | trigger=%3")
            .arg(continuousSamplePitchMm, 0, 'f', 4)
            .arg(cfg.scanSpeedMmPerS, 0, 'f', 4)
            .arg(continuousTriggerLabel(cfg.trigger));
        if (cfg.trigger == ScanConfig::ContinuousTrigger::Distance) {
            triggerLine += QString(" | pas trigger=%1 mm").arg(cfg.triggerDistanceMm, 0, 'f', 4);
        } else {
            triggerLine += QString(" | pas trigger=%1 s").arg(cfg.triggerTimeS, 0, 'f', 4);
        }
        if (rectangleMode) {
            triggerLine += QString(" | saut inter-ligne=%1 mm").arg(continuousRowStepMm, 0, 'f', 4);
            measurementHeaderLines << QString("Zone continue effective: largeur=%1 mm (origine=%2) | hauteur=%3 mm (origine=%4) | marge_acceleration=%5 mm")
                .arg(continuousPlan.effectiveWidthMm, 0, 'f', 4)
                .arg(continuousPlan.originalWidthMm, 0, 'f', 4)
                .arg(continuousPlan.effectiveHeightMm, 0, 'f', 4)
                .arg(continuousPlan.originalHeightMm, 0, 'f', 4)
                .arg(kContinuousRunUpMm, 0, 'f', 4);
        } else {
            measurementHeaderLines << QString("Ligne continue effective: longueur=%1 mm (origine=%2) | marge_acceleration=%3 mm")
                .arg(continuousPlan.effectiveWidthMm, 0, 'f', 4)
                .arg(continuousPlan.originalWidthMm, 0, 'f', 4)
                .arg(kContinuousRunUpMm, 0, 'f', 4);
        }
        measurementHeaderLines << triggerLine;
    }

    if (const auto* caParams = std::get_if<hardware::CaParams>(&techniqueParams)) {
        measurementHeaderLines << QString("Params CA: U=%1 V | duree=%2 s | record_dt=%3 s | vs_init=%4 | e_range=%5 | i_range=%6 | bandwidth=%7 | cycles=%8")
            .arg(caParams->voltage, 0, 'f', 4)
            .arg(caParams->duration, 0, 'f', 4)
            .arg(caParams->recordDt, 0, 'f', 4)
            .arg(yesNoText(caParams->vsInit))
            .arg(caParams->eRange)
            .arg(caParams->iRange)
            .arg(caParams->bandwidth)
            .arg(caParams->nCycles);
    } else if (const auto* ocvParams = std::get_if<hardware::OcvParams>(&techniqueParams)) {
        measurementHeaderLines << QString("Params OCV: repos=%1 s | record_dt=%2 s | record_dE=%3 V | e_range=%4")
            .arg(ocvParams->duration, 0, 'f', 4)
            .arg(ocvParams->recordDt, 0, 'f', 4)
            .arg(ocvParams->recordDE, 0, 'f', 6)
            .arg(ocvParams->eRange);
    } else if (const auto* cvaParams = std::get_if<hardware::CvaParams>(&techniqueParams)) {
        measurementHeaderLines
            << QString("Params CVA scan: Ei=%1 V | E1=%2 V | E2=%3 V | Ef=%4 V | rate=%5 mV/s | cycles=%6")
                .arg(cvaParams->voltageScan[0], 0, 'f', 4)
                .arg(cvaParams->voltageScan[1], 0, 'f', 4)
                .arg(cvaParams->voltageScan[2], 0, 'f', 4)
                .arg(cvaParams->voltageScan[3], 0, 'f', 4)
                .arg(cvaParams->scanRateMvPerS[0], 0, 'f', 4)
                .arg(cvaParams->nCycles)
            << QString("Params CVA acquisition: record_dE=%1 V | record_dt=%2 s | begin_I=%3 | end_I=%4 | i_range=%5 | e_range=%6 | bandwidth=%7")
                .arg(cvaParams->recordDE, 0, 'f', 6)
                .arg(cvaParams->recordDt, 0, 'f', 4)
                .arg(cvaParams->beginMeasuringI, 0, 'f', 4)
                .arg(cvaParams->endMeasuringI, 0, 'f', 4)
                .arg(cvaParams->iRange)
                .arg(cvaParams->eRange)
                .arg(cvaParams->bandwidth);
    }

    initializeMeasurementLog(QString("%1_%2").arg(techniqueLabel, runModeSlug), measurementHeaderLines);

    potentiostatThread_ = std::thread([this, ctrl, motorCtrl,
                                       technique, techniqueLabel,
                                       techniqueParams,
                                       waypoints = std::move(waypoints),
                                       nPoints, durationS, stageSpeedMmPerS,
                                       cfg, continuousPlan = std::move(continuousPlan),
                                       rows, cols, simpleMeasurement, simpleMeasurementSamplePeriodS]() mutable
    {
        using namespace std::chrono_literals;
        const auto finishUi = [this, simpleMeasurement](const QString& stateMsg) {
            QMetaObject::invokeMethod(this, [this, stateMsg, simpleMeasurement]() {
                potentiostatBusy_.store(false);
                if (potentiostatRunButton_   != nullptr) potentiostatRunButton_->setEnabled(true);
                if (potentiostatStopButton_  != nullptr) potentiostatStopButton_->setEnabled(false);
                if (potentiostatExportButton_ != nullptr)
                    potentiostatExportButton_->setEnabled(potentiostatSampleCount_ > 0);
                if (potentiostatMeasureStateLabel_ != nullptr) potentiostatMeasureStateLabel_->setText(stateMsg);
            }, Qt::QueuedConnection);
        };
        int acquiredSampleCount = 0;
        QString measurementOutcome = "completed";

        try {
            appendMeasurementLogEvent("STATE", QString("Thread mesure demarre | mode=%1 | moteur=%2")
                .arg(measurementModeLabel(simpleMeasurement, cfg.mode))
                .arg(currentMotorPositionLogText(motorCtrl)));

            if (!simpleMeasurement) {
                // ── Phase 0 : move to first waypoint ──
                QPointF firstPt = waypoints.front();
                // For continuous mode, go directly to the extended start (before first sample)
                if (cfg.mode == ScanConfig::AcquisitionMode::Continuous && !continuousPlan.linePlans.empty()) {
                    const auto& firstRow = continuousPlan.linePlans.front();
                    firstPt = firstRow.startPointMm - kContinuousRunUpMm * firstRow.unitDirection;
                }
                QMetaObject::invokeMethod(this, [this]() {
                    if (potentiostatMeasureStateLabel_ != nullptr)
                        potentiostatMeasureStateLabel_->setText("Mise en position...");
                }, Qt::QueuedConnection);
                appendMeasurementLogEvent("MOVE", QString("mise_en_position_debut | cible=%1 | moteur_avant=(%2) | vitesse=%3 mm/s")
                    .arg(formatPointMm(firstPt))
                    .arg(currentMotorPositionLogText(motorCtrl))
                    .arg(stageSpeedMmPerS, 0, 'f', 4));

                motorCtrl->setVelocity(hardware::AxisId::X, stageSpeedMmPerS);
                motorCtrl->setVelocity(hardware::AxisId::Y, stageSpeedMmPerS);

                {
                    const auto both = motorCtrl->snapshotBoth();
                    if (both.x.positionValid && both.y.positionValid) {
                        startPredictedMotorMotion(
                            both.x.positionMm, both.y.positionMm,
                            firstPt.x(), firstPt.y(),
                            stageSpeedMmPerS, stageSpeedMmPerS);
                    }
                }
                motorCtrl->moveAbsoluteNoWait(hardware::AxisId::X, firstPt.x());
                motorCtrl->moveAbsoluteNoWait(hardware::AxisId::Y, firstPt.y());
                motorCtrl->waitAxis(hardware::AxisId::X, kDefaultMotorTimeoutMs);
                motorCtrl->waitAxis(hardware::AxisId::Y, kDefaultMotorTimeoutMs);
                stopPredictedMotorMotion(firstPt.x(), firstPt.y());
                appendMeasurementLogEvent("MOVE", QString("mise_en_position_fin | cible=%1 | moteur_apres=(%2)")
                    .arg(formatPointMm(firstPt))
                    .arg(currentMotorPositionLogText(motorCtrl)));

                if (potentiostatStopRequested_.load()) {
                    measurementOutcome = "stopped_before_start";
                    appendMeasurementLogEvent("STOP", QString("Arret demande avant lancement instrument | moteur=(%1)")
                        .arg(currentMotorPositionLogText(motorCtrl)));
                    stopPredictedMotorMotion();
                    finalizeMeasurementLog(QString("status=%1 | acquisitions=%2 | moteur_final=(%3)")
                        .arg(measurementOutcome)
                        .arg(acquiredSampleCount)
                        .arg(currentMotorPositionLogText(motorCtrl)));
                    finishUi("Arrete.");
                    return;
                }
            }

            // ── Phase 1 : start selected technique ──
            bool startOk = false;
            int channel = 1;
            appendMeasurementLogEvent("STATE", QString("Lancement instrument %1...").arg(techniqueLabel));
            QMetaObject::invokeMethod(this, [this, techniqueLabel]() {
                appendLog(QString("Lancement %1...").arg(techniqueLabel));
            }, Qt::QueuedConnection);

            switch (technique) {
            case PotentiostatTechnique::OCV: {
                const auto& p = std::get<hardware::OcvParams>(techniqueParams);
                channel = p.channel;
                startOk = ctrl->startOcv(p);
                break;
            }
            case PotentiostatTechnique::CVA: {
                const auto& p = std::get<hardware::CvaParams>(techniqueParams);
                channel = p.channel;
                startOk = simpleMeasurement ? ctrl->startCvaSimple(p) : ctrl->startCva(p);
                break;
            }
            case PotentiostatTechnique::CA:
            default: {
                const auto& p = std::get<hardware::CaParams>(techniqueParams);
                channel = p.channel;
                startOk = ctrl->startCa(p);
                break;
            }
            }

            if (!startOk) {
                const QString err = ctrl->lastError();
                measurementOutcome = "start_failed";
                appendMeasurementLogEvent("ERROR", QString("Echec lancement %1 | error=%2").arg(techniqueLabel).arg(err));
                QMetaObject::invokeMethod(this, [this, techniqueLabel, err]() {
                    appendLog(QString("Erreur start%1 : %2").arg(techniqueLabel).arg(err));
                }, Qt::QueuedConnection);
                stopPredictedMotorMotion();
                finalizeMeasurementLog(QString("status=%1 | acquisitions=%2 | moteur_final=(%3)")
                    .arg(measurementOutcome)
                    .arg(acquiredSampleCount)
                    .arg(currentMotorPositionLogText(motorCtrl)));
                finishUi("Erreur : " + err);
                return;
            }
            appendMeasurementLogEvent("STATE", QString("%1 demarre | canal=%2").arg(techniqueLabel).arg(channel));
            QMetaObject::invokeMethod(this, [this, techniqueLabel]() {
                appendLog(QString("%1 demarre.").arg(techniqueLabel));
                if (potentiostatMeasureStateLabel_ != nullptr)
                    potentiostatMeasureStateLabel_->setText(QString("%1 en cours...").arg(techniqueLabel));
            }, Qt::QueuedConnection);
            if (!ctrl->lastStartDetails().isEmpty()) {
                const QString startDetails = ctrl->lastStartDetails();
                appendMeasurementLogEvent("STATE", QString("Details start: %1").arg(startDetails));
                QMetaObject::invokeMethod(this, [this, startDetails]() {
                    appendLog(startDetails);
                }, Qt::QueuedConnection);
            }

            if (simpleMeasurement) {
                QMetaObject::invokeMethod(this, [this, techniqueLabel]() {
                    if (potentiostatMeasureStateLabel_ != nullptr)
                        potentiostatMeasureStateLabel_->setText(QString("%1 simple...").arg(techniqueLabel));
                }, Qt::QueuedConnection);
            } else if (cfg.mode == ScanConfig::AcquisitionMode::Continuous) {
                QMetaObject::invokeMethod(this, [this, techniqueLabel]() {
                    if (potentiostatMeasureStateLabel_ != nullptr)
                        potentiostatMeasureStateLabel_->setText(QString("%1 continu...").arg(techniqueLabel));
                }, Qt::QueuedConnection);
            }
            const int totalEstimate = nPoints;
            if (simpleMeasurement) {
                int sampleIdx = 0;
                int pollCount = 0;
                int totalRowsSeen = 0;
                int buffersWithRows = 0;
                double lastAcceptedSampleStamp = -std::numeric_limits<double>::infinity();
                bool instrumentStopped = false;
                const double effectiveSamplePeriodS = std::max(simpleMeasurementSamplePeriodS, 0.05);
                const double simplePollPeriodS = std::clamp(effectiveSamplePeriodS * 0.25, 0.05, 0.20);
                const double stopGracePeriodS = std::max(0.75, effectiveSamplePeriodS * 6.0);
                auto lastSimpleProgress = std::chrono::steady_clock::now();
                const auto simpleLoopStartedAt = std::chrono::steady_clock::now();
                const bool useWallClockSampling = (technique == PotentiostatTechnique::CVA);
                bool sawInstrumentRunning = false;
                bool lastLoggedStopped = false;
                int lastLoggedTechniqueId = std::numeric_limits<int>::min();
                int lastLoggedProcessIndex = std::numeric_limits<int>::min();
                int lastLoggedNbRows = std::numeric_limits<int>::min();
                int lastLoggedNbCols = std::numeric_limits<int>::min();
                int lastLoggedCurrentState = std::numeric_limits<int>::min();
                QString stopReason = "natural_completion";

                QMetaObject::invokeMethod(this, [this, techniqueLabel, effectiveSamplePeriodS, simplePollPeriodS, stopGracePeriodS]() {
                    appendLog(QString("[POTDBG] %1 simple loop: samplePeriod=%2 s pollPeriod=%3 s stopGrace=%4 s")
                        .arg(techniqueLabel)
                        .arg(effectiveSamplePeriodS, 0, 'f', 3)
                        .arg(simplePollPeriodS, 0, 'f', 3)
                        .arg(stopGracePeriodS, 0, 'f', 3));
                }, Qt::QueuedConnection);

                while (!potentiostatStopRequested_.load() && !instrumentStopped) {
                    ++pollCount;
                    const auto data = ctrl->getData(channel);
                    if (!data.ok) {
                        throw std::runtime_error(QString("GetData a echoue : %1").arg(data.error).toStdString());
                    }
                    totalRowsSeen += data.nbRows;
                    if (data.nbRows > 0) {
                        ++buffersWithRows;
                    }

                    bool acceptedSample = false;
                    if (useWallClockSampling) {
                        if (!data.points.empty()) {
                            const double wallClockElapsedS = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - simpleLoopStartedAt).count();
                            if (wallClockElapsedS >= lastAcceptedSampleStamp + effectiveSamplePeriodS - kScanPlanningEpsilonMm) {
                                lastAcceptedSampleStamp = wallClockElapsedS;
                                acceptedSample = true;
                                const auto point = data.points.back();
                                const int idx = sampleIdx++;
                                acquiredSampleCount = sampleIdx;
                                appendMeasurementLogEvent("ACQ", QString("simple | t_abs=%1 | acq=%2 | t_pot=%3 s | moteur=(%4) | Ewe=%5 V | I=%6 A")
                                    .arg(currentLogTimestampText())
                                    .arg(idx + 1)
                                    .arg(point.t, 0, 'f', 6)
                                    .arg(currentMotorPositionLogText(motorCtrl))
                                    .arg(point.ewe, 0, 'e', 3)
                                    .arg(point.I, 0, 'e', 3));
                                QMetaObject::invokeMethod(this, [this, idx, point]() {
                                    currentWaypointIndex_ = idx + 1;
                                    appendPotentiostatVisualizationSample(
                                        idx, 0, -1, -1, QPointF(), point.t, point.ewe, point.I);
                                }, Qt::QueuedConnection);
                            }
                        }
                    } else {
                        for (const auto& point : data.points) {
                            if (point.t < lastAcceptedSampleStamp + effectiveSamplePeriodS - kScanPlanningEpsilonMm) {
                                continue;
                            }
                            lastAcceptedSampleStamp = point.t;
                            acceptedSample = true;
                            const int idx = sampleIdx++;
                            acquiredSampleCount = sampleIdx;
                            appendMeasurementLogEvent("ACQ", QString("simple | t_abs=%1 | acq=%2 | t_pot=%3 s | moteur=(%4) | Ewe=%5 V | I=%6 A")
                                .arg(currentLogTimestampText())
                                .arg(idx + 1)
                                .arg(point.t, 0, 'f', 6)
                                .arg(currentMotorPositionLogText(motorCtrl))
                                .arg(point.ewe, 0, 'e', 3)
                                .arg(point.I, 0, 'e', 3));
                            QMetaObject::invokeMethod(this, [this, idx, point]() {
                                currentWaypointIndex_ = idx + 1;
                                appendPotentiostatVisualizationSample(
                                    idx, 0, -1, -1, QPointF(), point.t, point.ewe, point.I);
                            }, Qt::QueuedConnection);
                        }
                    }

                    const bool shouldLogPoll =
                        pollCount <= 12
                        || data.nbRows > 0
                        || data.stopped != lastLoggedStopped
                        || data.techniqueId != lastLoggedTechniqueId
                        || data.processIndex != lastLoggedProcessIndex
                        || data.nbRows != lastLoggedNbRows
                        || data.nbCols != lastLoggedNbCols
                        || data.currentState != lastLoggedCurrentState
                        || acceptedSample;
                    if (shouldLogPoll) {
                        QString firstPointText = "-";
                        QString lastPointText = "-";
                        if (!data.points.empty()) {
                            const auto& firstPoint = data.points.front();
                            const auto& lastPoint = data.points.back();
                            firstPointText = QString("t=%1,E=%2,I=%3")
                                .arg(firstPoint.t, 0, 'f', 6)
                                .arg(firstPoint.ewe, 0, 'e', 3)
                                .arg(firstPoint.I, 0, 'e', 3);
                            lastPointText = QString("t=%1,E=%2,I=%3")
                                .arg(lastPoint.t, 0, 'f', 6)
                                .arg(lastPoint.ewe, 0, 'e', 3)
                                .arg(lastPoint.I, 0, 'e', 3);
                        }
                        QMetaObject::invokeMethod(this, [this, pollCount, data, acceptedSample, sampleIdx, firstPointText, lastPointText]() {
                            appendLog(QString("[POTDBG] simple poll=%1 tech=%2 proc=%3 rows=%4 cols=%5 state=%6 stopped=%7 accepted=%8 uiPts=%9 startT=%10 first={%11} last={%12}")
                                .arg(pollCount)
                                .arg(data.techniqueId)
                                .arg(data.processIndex)
                                .arg(data.nbRows)
                                .arg(data.nbCols)
                                .arg(data.currentState)
                                .arg(data.stopped ? "oui" : "non")
                                .arg(acceptedSample ? "oui" : "non")
                                .arg(sampleIdx)
                                .arg(data.startTime, 0, 'f', 6)
                                .arg(firstPointText)
                                .arg(lastPointText));
                        }, Qt::QueuedConnection);
                        lastLoggedStopped = data.stopped;
                        lastLoggedTechniqueId = data.techniqueId;
                        lastLoggedProcessIndex = data.processIndex;
                        lastLoggedNbRows = data.nbRows;
                        lastLoggedNbCols = data.nbCols;
                        lastLoggedCurrentState = data.currentState;
                    }

                    if (!data.stopped) {
                        sawInstrumentRunning = true;
                        lastSimpleProgress = std::chrono::steady_clock::now();
                    } else if (acceptedSample) {
                        lastSimpleProgress = std::chrono::steady_clock::now();
                    }

                    if (data.stopped && sawInstrumentRunning) {
                        const double stoppedForS = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - lastSimpleProgress).count();
                        instrumentStopped = stoppedForS >= stopGracePeriodS;
                        if (instrumentStopped) {
                            stopReason = QString("instrument_stop_stable_after_%1s").arg(stoppedForS, 0, 'f', 3);
                        }
                    } else {
                        instrumentStopped = false;
                    }

                    if (!instrumentStopped) {
                        std::this_thread::sleep_for(std::chrono::duration<double>(simplePollPeriodS));
                    }
                }

                if (potentiostatStopRequested_.load()) {
                    stopReason = "external_stop_request";
                } else if (instrumentStopped && stopReason == "natural_completion") {
                    stopReason = "instrument_stop_stable";
                }
                measurementOutcome = stopReason;
                appendMeasurementLogEvent("STATE", QString("Fin boucle simple | reason=%1 | acquisitions=%2 | moteur=(%3)")
                    .arg(stopReason)
                    .arg(acquiredSampleCount)
                    .arg(currentMotorPositionLogText(motorCtrl)));

                QMetaObject::invokeMethod(this, [this, pollCount, totalRowsSeen, buffersWithRows, sampleIdx, stopReason]() {
                    appendLog(QString("[POTDBG] simple loop exit: reason=%1 polls=%2 buffersWithRows=%3 totalRows=%4 uiPts=%5 potStop=%6")
                        .arg(stopReason)
                        .arg(pollCount)
                        .arg(buffersWithRows)
                        .arg(totalRowsSeen)
                        .arg(sampleIdx)
                        .arg(potentiostatStopRequested_.load() ? "oui" : "non"));
                }, Qt::QueuedConnection);
            } else if (cfg.mode == ScanConfig::AcquisitionMode::Continuous) {
                int globalSampleIdx = 0;
                double lastSampleElapsed = -std::numeric_limits<double>::infinity();
                bool instrumentStopped = false;

                const auto waitForFreshValue = [&](double timeoutS) -> std::optional<hardware::PotCurrentValues> {
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutS);
                    while (!potentiostatStopRequested_.load()) {
                        const auto cv = ctrl->getCurrentValues(channel);
                        if (cv.ok && cv.elapsedTime > lastSampleElapsed + kScanPlanningEpsilonMm) {
                            return cv;
                        }
                        if (std::chrono::steady_clock::now() >= deadline) {
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::duration<double>(kContinuousPollingPeriodS));
                    }
                    return std::nullopt;
                };

                const auto appendScheduledSample =
                    [&](int row, int col, const QPointF& plannedPoint, double timeoutS,
                        std::optional<QPointF> triggerMotorPos = std::nullopt) -> bool {
                    const auto maybeCv = waitForFreshValue(timeoutS);
                    if (!maybeCv.has_value()) {
                        return false;
                    }
                    const hardware::PotCurrentValues cv = *maybeCv;
                    const int idx = globalSampleIdx++;
                    acquiredSampleCount = globalSampleIdx;
                    lastSampleElapsed = cv.elapsedTime;
                    const QString acquisitionTimestamp = currentLogTimestampText();

                    QString actualMotorText = "X=--- Y=---";
                    if (triggerMotorPos.has_value()) {
                        // Use the motor position captured at trigger time (avoids
                        // systematic offset from calling snapshotBoth() after the
                        // potentiostat read while the motor keeps moving).
                        actualMotorText = QString("X=%1 Y=%2")
                            .arg(triggerMotorPos->x(), 0, 'f', 4)
                            .arg(triggerMotorPos->y(), 0, 'f', 4);
                    } else {
                        try {
                            const auto motorSnapshot = motorCtrl->snapshotBoth();
                            if (motorSnapshot.x.positionValid && motorSnapshot.y.positionValid) {
                                actualMotorText = QString("X=%1 Y=%2")
                                    .arg(motorSnapshot.x.positionMm, 0, 'f', 4)
                                    .arg(motorSnapshot.y.positionMm, 0, 'f', 4);
                            }
                        } catch (...) {
                        }
                    }

                    appendLog(
                        QString("[POTCONT] t_abs=%1 acq=%2 row=%3 col=%4 t_pot=%5 s cible=(%6,%7) mm moteur=(%8) Ewe=%9 V I=%10 A")
                            .arg(acquisitionTimestamp)
                            .arg(idx + 1)
                            .arg(row + 1)
                            .arg(col + 1)
                            .arg(cv.elapsedTime, 0, 'f', 6)
                            .arg(plannedPoint.x(), 0, 'f', 4)
                            .arg(plannedPoint.y(), 0, 'f', 4)
                            .arg(actualMotorText)
                            .arg(cv.ewe, 0, 'e', 3)
                            .arg(cv.I, 0, 'e', 3));
                    appendMeasurementLogEvent("ACQ", QString("continu | t_abs=%1 | acq=%2 | row=%3 | col=%4 | t_pot=%5 s | cible=%6 | moteur=(%7) | Ewe=%8 V | I=%9 A")
                        .arg(acquisitionTimestamp)
                        .arg(idx + 1)
                        .arg(row + 1)
                        .arg(col + 1)
                        .arg(cv.elapsedTime, 0, 'f', 6)
                        .arg(formatPointMm(plannedPoint))
                        .arg(actualMotorText)
                        .arg(cv.ewe, 0, 'e', 3)
                        .arg(cv.I, 0, 'e', 3));

                    QMetaObject::invokeMethod(this, [this, idx, totalEstimate, row, col, plannedPoint, cv]() {
                        currentWaypointIndex_ = idx + 1;
                        appendPotentiostatVisualizationSample(
                            idx, totalEstimate, row, col, plannedPoint, cv.elapsedTime, cv.ewe, cv.I);
                    }, Qt::QueuedConnection);

                    if (cv.stopped) {
                        const auto confirm = ctrl->getData(channel);
                        if (confirm.ok && confirm.stopped) {
                            instrumentStopped = true;
                            measurementOutcome = "instrument_stop_continu";
                            appendMeasurementLogEvent("STOP", QString("Instrument stop confirme pendant balayage continu | ligne=%1 | moteur=(%2)")
                                .arg(row + 1)
                                .arg(actualMotorText));
                            QMetaObject::invokeMethod(this, [this, techniqueLabel]() {
                                appendLog(QString("%1 arrete par l'instrument, fin du balayage.").arg(techniqueLabel));
                            }, Qt::QueuedConnection);
                        } else {
                            appendMeasurementLogEvent("STATE", QString("STOP transitoire ignore | t_pot=%1 s").arg(cv.elapsedTime, 0, 'f', 6));
                            QMetaObject::invokeMethod(this, [this, techniqueLabel]() {
                                appendLog(QString("Etat STOP transitoire ignore: %1 continue.").arg(techniqueLabel));
                            }, Qt::QueuedConnection);
                        }
                    }
                    return true;
                };

                const double regularSampleTimeoutS = 0.05;

                for (int lineIndex = 0; lineIndex < static_cast<int>(continuousPlan.linePlans.size()); ++lineIndex) {
                    const ContinuousRasterRow& rowPlan = continuousPlan.linePlans[static_cast<std::size_t>(lineIndex)];
                    if (potentiostatStopRequested_.load() || instrumentStopped) {
                        break;
                    }
                    if (rowPlan.samplePoints.empty()) {
                        continue;
                    }

                    const QPointF rowStart = rowPlan.startPointMm;
                    const QPointF rowEnd = rowPlan.endPointMm;
                    const bool moveLineX = std::abs(rowEnd.x() - rowStart.x()) > 0.0005;
                    const bool moveLineY = std::abs(rowEnd.y() - rowStart.y()) > 0.0005;

                    // ── Run-up / overrun margins for motor acceleration ──
                    const QPointF extendedStart = rowStart - kContinuousRunUpMm * rowPlan.unitDirection;
                    const QPointF extendedEnd   = rowEnd   + kContinuousRunUpMm * rowPlan.unitDirection;

                    appendMeasurementLogEvent("LINE", QString("ligne=%1/%2 | depart=%3 | arrivee=%4 | axe=%5 | longueur=%6 mm | longueur_mouvement=%7 mm | echantillons=%8 | marge=%9 mm")
                        .arg(lineIndex + 1)
                        .arg(static_cast<int>(continuousPlan.linePlans.size()))
                        .arg(formatPointMm(rowStart))
                        .arg(formatPointMm(rowEnd))
                        .arg(rowPlan.primaryAxisHorizontal ? "horizontal" : "vertical")
                        .arg(rowPlan.lengthMm, 0, 'f', 4)
                        .arg(rowPlan.lengthMm + 2.0 * kContinuousRunUpMm, 0, 'f', 4)
                        .arg(static_cast<int>(rowPlan.samplePoints.size()))
                        .arg(kContinuousRunUpMm, 0, 'f', 4));

                    // ── Reposition to extended start (before first sample) ──
                    {
                        const QPointF repoTarget = (lineIndex == 0) ? extendedStart : extendedStart;
                        appendMeasurementLogEvent("MOVE", QString("repositionnement_ligne_debut | ligne=%1/%2 | cible=%3 | moteur_avant=(%4) | vitesse=%5 mm/s")
                            .arg(lineIndex + 1)
                            .arg(static_cast<int>(continuousPlan.linePlans.size()))
                            .arg(formatPointMm(repoTarget))
                            .arg(currentMotorPositionLogText(motorCtrl))
                            .arg(stageSpeedMmPerS, 0, 'f', 4));
                        const auto both = motorCtrl->snapshotBoth();
                        if (both.x.positionValid && both.y.positionValid) {
                            startPredictedMotorMotion(
                                both.x.positionMm, both.y.positionMm,
                                repoTarget.x(), repoTarget.y(),
                                stageSpeedMmPerS, stageSpeedMmPerS);
                        }
                        motorCtrl->setVelocity(hardware::AxisId::X, stageSpeedMmPerS);
                        motorCtrl->setVelocity(hardware::AxisId::Y, stageSpeedMmPerS);
                        motorCtrl->moveAbsoluteNoWait(hardware::AxisId::X, repoTarget.x());
                        motorCtrl->moveAbsoluteNoWait(hardware::AxisId::Y, repoTarget.y());
                        motorCtrl->waitAxis(hardware::AxisId::X, kDefaultMotorTimeoutMs);
                        motorCtrl->waitAxis(hardware::AxisId::Y, kDefaultMotorTimeoutMs);
                        stopPredictedMotorMotion(repoTarget.x(), repoTarget.y());
                        appendMeasurementLogEvent("MOVE", QString("repositionnement_ligne_fin | ligne=%1/%2 | moteur_apres=(%3)")
                            .arg(lineIndex + 1)
                            .arg(static_cast<int>(continuousPlan.linePlans.size()))
                            .arg(currentMotorPositionLogText(motorCtrl)));
                    }

                    if (potentiostatStopRequested_.load() || instrumentStopped) {
                        break;
                    }
                    if (rowPlan.samplePoints.size() < 1) {
                        continue;
                    }

                    // ── Start sweep from extended start toward extended end ──
                    const double velocityX = std::abs(rowPlan.unitDirection.x()) * cfg.scanSpeedMmPerS;
                    const double velocityY = std::abs(rowPlan.unitDirection.y()) * cfg.scanSpeedMmPerS;
                    appendMeasurementLogEvent("MOVE", QString("balayage_ligne_debut | ligne=%1/%2 | depart=%3 | arrivee=%4 | vitesse_x=%5 mm/s | vitesse_y=%6 mm/s | moteur_avant=(%7)")
                        .arg(lineIndex + 1)
                        .arg(static_cast<int>(continuousPlan.linePlans.size()))
                        .arg(formatPointMm(extendedStart))
                        .arg(formatPointMm(extendedEnd))
                        .arg(velocityX, 0, 'f', 4)
                        .arg(velocityY, 0, 'f', 4)
                        .arg(currentMotorPositionLogText(motorCtrl)));
                    const auto both = motorCtrl->snapshotBoth();
                    if (both.x.positionValid && both.y.positionValid) {
                        startPredictedMotorMotion(
                            both.x.positionMm, both.y.positionMm,
                            extendedEnd.x(), extendedEnd.y(),
                            moveLineX ? std::optional<double>(velocityX) : std::nullopt,
                            moveLineY ? std::optional<double>(velocityY) : std::nullopt);
                    }
                    if (moveLineX) {
                        motorCtrl->setVelocity(hardware::AxisId::X, velocityX);
                        motorCtrl->moveAbsoluteNoWait(hardware::AxisId::X, extendedEnd.x());
                    }
                    if (moveLineY) {
                        motorCtrl->setVelocity(hardware::AxisId::Y, velocityY);
                        motorCtrl->moveAbsoluteNoWait(hardware::AxisId::Y, extendedEnd.y());
                    }

                    // ── Time-based calibration during run-up ──
                    // Take snapshots until the motor moves, then compute the
                    // exact wall-clock ↔ motor-position mapping.  One calibration
                    // snapshot is enough because the motor speed is constant.
                    //
                    // The position returned by snapshotBoth() was measured ~10 ms
                    // into the call (TS + TP on the scan axis only; Y and JSON
                    // overhead occur afterwards).  Using the *start* of the call
                    // as the position timestamp therefore introduces a systematic
                    // offset of only ~10 ms × speed = 0.5 µm at 0.05 mm/s.
                    // That is well below 1 µm, so we use the call start time.
                    auto calibSnapTime = std::chrono::steady_clock::now();
                    QPointF calibSnapPos = extendedStart;
                    {
                        constexpr double kMotionDetectThresholdMm = 0.001;  // 1 µm
                        const auto deadline = calibSnapTime + std::chrono::seconds(3);
                        while (!potentiostatStopRequested_.load()
                               && !instrumentStopped
                               && std::chrono::steady_clock::now() < deadline) {
                            const auto beforeSnap = std::chrono::steady_clock::now();
                            const auto pos = motorCtrl->snapshotBoth();
                            if (pos.x.positionValid && pos.y.positionValid) {
                                const QPointF p(pos.x.positionMm, pos.y.positionMm);
                                const QPointF delta = p - extendedStart;
                                const double travelled =
                                    delta.x() * rowPlan.unitDirection.x()
                                    + delta.y() * rowPlan.unitDirection.y();
                                if (travelled > kMotionDetectThresholdMm) {
                                    calibSnapTime = beforeSnap;
                                    calibSnapPos  = p;
                                    appendMeasurementLogEvent("SYNC",
                                        QString("ligne=%1/%2 | calibration_temps | deplacement=%3 mm | pos=%4")
                                            .arg(lineIndex + 1)
                                            .arg(static_cast<int>(continuousPlan.linePlans.size()))
                                            .arg(travelled, 0, 'f', 4)
                                            .arg(formatPointMm(p)));
                                    break;
                                }
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                    }

                    // ── Pure time-based trigger ──
                    // No snapshotBoth() in the trigger loop ⇒ sub-µm resolution.
                    // Trigger time for a target at distance D from rowStart:
                    //   T = calibSnapTime + (rowStart + D − calibSnapPos) / speed
                    // because at calibSnapTime the motor was at calibSnapPos
                    // and it advances at exactly cfg.scanSpeedMmPerS.
                    const auto waitForTargetDistanceMm = [&](double targetDistanceMm) -> std::optional<QPointF> {
                        const QPointF targetPos = rowStart + rowPlan.unitDirection * targetDistanceMm;
                        const double distFromCalib =
                            (targetPos.x() - calibSnapPos.x()) * rowPlan.unitDirection.x()
                            + (targetPos.y() - calibSnapPos.y()) * rowPlan.unitDirection.y();
                        const double dtS = distFromCalib / std::max(cfg.scanSpeedMmPerS, kScanPlanningEpsilonMm);
                        const auto expectedTime = calibSnapTime
                            + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<double>(dtS));

                        while (!potentiostatStopRequested_.load() && !instrumentStopped) {
                            const auto now = std::chrono::steady_clock::now();
                            if (now >= expectedTime) {
                                // Trigger fired at the right time.
                                // Take a real snapshot for logging/verification.
                                // This adds ~100ms but doesn't affect trigger precision.
                                try {
                                    const auto snap = motorCtrl->snapshotBoth();
                                    if (snap.x.positionValid && snap.y.positionValid) {
                                        return QPointF(snap.x.positionMm, snap.y.positionMm);
                                    }
                                } catch (...) {}
                                return targetPos; // fallback if snapshot fails
                            }
                            const double remainingS = std::chrono::duration<double>(expectedTime - now).count();
                            const double sleepS = std::clamp(remainingS * 0.5, 0.0005, kContinuousPollingPeriodS);
                            std::this_thread::sleep_for(std::chrono::duration<double>(sleepS));
                        }
                        return std::nullopt;
                    };

                    // All samples (including the first) are acquired while the motor is moving.
                    // The run-up margin ensures the motor is at full speed by the time it reaches
                    // the first sample point (distance 0.0 from rowStart).
                    for (int sampleOffset = 0; sampleOffset < static_cast<int>(rowPlan.samplePoints.size()); ++sampleOffset) {
                        if (potentiostatStopRequested_.load() || instrumentStopped) {
                            break;
                        }

                        const double targetDistanceMm = continuousPlan.samplePitchMm * static_cast<double>(sampleOffset);
                        std::optional<QPointF> triggerMotorPos;
                        if (cfg.trigger == ScanConfig::ContinuousTrigger::Distance) {
                            triggerMotorPos = waitForTargetDistanceMm(targetDistanceMm);
                        } else {
                            // For time-based trigger: use the calibrated time prediction
                            // rather than a fixed lineStartTime offset, for consistency.
                            triggerMotorPos = waitForTargetDistanceMm(targetDistanceMm);
                        }

                        if (potentiostatStopRequested_.load() || instrumentStopped) {
                            break;
                        }

                        const auto [row, col] = rowPlan.sampleCells[static_cast<std::size_t>(sampleOffset)];
                        const QPointF plannedPoint = rowPlan.samplePoints[static_cast<std::size_t>(sampleOffset)];
                        const double sampleTimeout = (sampleOffset == 0) ? kContinuousFreshValueTimeoutS : regularSampleTimeoutS;
                        if (!appendScheduledSample(row, col, plannedPoint, sampleTimeout, triggerMotorPos)) {
                            throw std::runtime_error(QString("Aucune nouvelle valeur au point %1 de la ligne %2.")
                                .arg(sampleOffset + 1).arg(lineIndex + 1).toStdString());
                        }
                    }

                    if (moveLineX) {
                        motorCtrl->waitAxis(hardware::AxisId::X, kDefaultMotorTimeoutMs);
                    }
                    if (moveLineY) {
                        motorCtrl->waitAxis(hardware::AxisId::Y, kDefaultMotorTimeoutMs);
                    }
                    stopPredictedMotorMotion(extendedEnd.x(), extendedEnd.y());
                    appendMeasurementLogEvent("MOVE", QString("balayage_ligne_fin | ligne=%1/%2 | moteur_apres=(%3)")
                        .arg(lineIndex + 1)
                        .arg(static_cast<int>(continuousPlan.linePlans.size()))
                        .arg(currentMotorPositionLogText(motorCtrl)));
                }
                if (potentiostatStopRequested_.load() && measurementOutcome == "completed") {
                    measurementOutcome = "external_stop_request";
                }
            } else {
            // ── Phase intervals (built from kbio ElapsedTime to align with graph x-axis) ──
            using MotorPhase = PotentiostatGraphWidget::MotorPhase;
            std::vector<MotorPhase> motorPhases;
            motorPhases.reserve(static_cast<std::size_t>(nPoints) * 2);
            double prevMeasureT = 0.0; // kbio elapsed time of previous measurement

            // ── Phase 2 : for each waypoint ──
            for (int idx = 0; idx < nPoints; ++idx) {
                if (potentiostatStopRequested_.load()) break;

                const QPointF wp = waypoints[static_cast<std::size_t>(idx)];

                // Move to waypoint (idx==0 already there)
                if (idx > 0) {
                    appendMeasurementLogEvent("MOVE", QString("redemarrage | pt=%1/%2 | cible=%3 | moteur_avant=(%4) | vitesse=%5 mm/s")
                        .arg(idx + 1)
                        .arg(nPoints)
                        .arg(formatPointMm(wp))
                        .arg(currentMotorPositionLogText(motorCtrl))
                        .arg(stageSpeedMmPerS, 0, 'f', 4));
                    const auto both = motorCtrl->snapshotBoth();
                    if (both.x.positionValid && both.y.positionValid) {
                        startPredictedMotorMotion(
                            both.x.positionMm, both.y.positionMm,
                            wp.x(), wp.y(),
                            stageSpeedMmPerS, stageSpeedMmPerS);
                    }
                    motorCtrl->moveAbsoluteNoWait(hardware::AxisId::X, wp.x());
                    motorCtrl->moveAbsoluteNoWait(hardware::AxisId::Y, wp.y());
                    motorCtrl->waitAxis(hardware::AxisId::X, kDefaultMotorTimeoutMs);
                    motorCtrl->waitAxis(hardware::AxisId::Y, kDefaultMotorTimeoutMs);
                    stopPredictedMotorMotion(wp.x(), wp.y());
                    appendMeasurementLogEvent("MOVE", QString("arret_sur_point | pt=%1/%2 | cible=%3 | moteur_apres=(%4)")
                        .arg(idx + 1)
                        .arg(nPoints)
                        .arg(formatPointMm(wp))
                        .arg(currentMotorPositionLogText(motorCtrl)));
                }

                if (potentiostatStopRequested_.load()) break;

                // ── Multi-sample dwell ──────────────────────────────────────
                const int nDwellSamples = std::max(1, cfg.dwellSamples);
                const double sampleIntervalS = durationS / nDwellSamples;
                const auto dwellStart = std::chrono::steady_clock::now();
                std::vector<double> dwellCurrents;
                std::vector<double> dwellEwes;
                dwellCurrents.reserve(static_cast<std::size_t>(nDwellSamples));
                dwellEwes.reserve(static_cast<std::size_t>(nDwellSamples));
                hardware::PotCurrentValues lastCv;
                appendMeasurementLogEvent("DWELL", QString("pt=%1/%2 | duree=%3 s | n_mesures=%4 | moteur=(%5)")
                    .arg(idx + 1)
                    .arg(nPoints)
                    .arg(durationS, 0, 'f', 4)
                    .arg(nDwellSamples)
                    .arg(currentMotorPositionLogText(motorCtrl)));
                for (int s = 0; s < nDwellSamples; ++s) {
                    const auto slotEnd = dwellStart
                        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>((s + 1) * sampleIntervalS));
                    while (std::chrono::steady_clock::now() < slotEnd) {
                        if (potentiostatStopRequested_.load()) break;
                        std::this_thread::sleep_for(20ms);
                    }
                    if (potentiostatStopRequested_.load()) break;
                    const auto cv_s = ctrl->getCurrentValues(channel);
                    if (!cv_s.ok) {
                        throw std::runtime_error(
                            QString("GetCurrentValues a echoue (sous-mesure %1/%2) : %3")
                                .arg(s + 1).arg(nDwellSamples).arg(cv_s.error).toStdString());
                    }
                    dwellCurrents.push_back(cv_s.I);
                    dwellEwes.push_back(cv_s.ewe);
                    lastCv = cv_s;
                    appendMeasurementLogEvent("SUBSAMPLE", QString("pt=%1/%2 | sample=%3/%4 | t_pot=%5 s | Ewe=%6 V | I=%7 A")
                        .arg(idx + 1).arg(nPoints)
                        .arg(s + 1).arg(nDwellSamples)
                        .arg(cv_s.elapsedTime, 0, 'f', 6)
                        .arg(cv_s.ewe, 0, 'e', 6)
                        .arg(cv_s.I, 0, 'e', 6));
                    if (cv_s.stopped) break;
                }

                if (potentiostatStopRequested_.load()) break;
                if (dwellCurrents.empty()) break;

                const double meanI = std::accumulate(
                    dwellCurrents.begin(), dwellCurrents.end(), 0.0)
                    / static_cast<double>(dwellCurrents.size());
                const double meanEwe = std::accumulate(
                    dwellEwes.begin(), dwellEwes.end(), 0.0)
                    / static_cast<double>(dwellEwes.size());
                const int nActualSamples = static_cast<int>(dwellCurrents.size());

                // Compute standard deviations
                double stdI = 0.0, stdEwe = 0.0;
                if (nActualSamples > 1) {
                    double sumSqI = 0.0, sumSqEwe = 0.0;
                    for (int si = 0; si < nActualSamples; ++si) {
                        const double dI = dwellCurrents[static_cast<std::size_t>(si)] - meanI;
                        const double dE = dwellEwes[static_cast<std::size_t>(si)] - meanEwe;
                        sumSqI  += dI * dI;
                        sumSqEwe += dE * dE;
                    }
                    stdI   = std::sqrt(sumSqI   / nActualSamples);
                    stdEwe = std::sqrt(sumSqEwe / nActualSamples);
                }

                // Serialize raw sample lists as comma-separated strings
                auto joinSamples = [](const std::vector<double>& vec) -> QString {
                    QStringList parts;
                    parts.reserve(static_cast<int>(vec.size()));
                    for (double v : vec)
                        parts << QString::number(v, 'e', 6);
                    return parts.join(",");
                };
                const QString rawISamples   = joinSamples(dwellCurrents);
                const QString rawEweSamples = joinSamples(dwellEwes);

                acquiredSampleCount = idx + 1;
                const QString acquisitionTimestamp = currentLogTimestampText();
                const QString actualMotorText = currentMotorPositionLogText(motorCtrl);
                int row = 0;
                int col = idx;
                if (idx >= 0 && idx < static_cast<int>(potentiostatScanOrder_.size())) {
                    row = potentiostatScanOrder_[static_cast<std::size_t>(idx)].first;
                    col = potentiostatScanOrder_[static_cast<std::size_t>(idx)].second;
                }

                // Record motor phases aligned on kbio ElapsedTime
                {
                    const double t = lastCv.elapsedTime;
                    const double tDwellStart = std::max(0.0, t - durationS);
                    if (idx > 0 && prevMeasureT < tDwellStart)
                        motorPhases.push_back({prevMeasureT, tDwellStart, true});  // MOVING
                    motorPhases.push_back({tDwellStart, t, false});                // STOPPED
                    prevMeasureT = t;
                }

                QMetaObject::invokeMethod(this, [this, idx, nPoints, row, col, wp,
                                                 lastCv, meanI, meanEwe, nActualSamples,
                                                 acquisitionTimestamp, actualMotorText,
                                                 phases = motorPhases,
                                                 cellSamples = dwellCurrents,
                                                 eweSamples = dwellEwes]() mutable {
                    currentWaypointIndex_ = idx + 1;
                    if (potentiostatGraphWidget_ != nullptr)
                        potentiostatGraphWidget_->setPhases(phases);
                    appendPotentiostatVisualizationSample(
                        idx, nPoints, row, col, wp,
                        lastCv.elapsedTime, meanEwe, meanI,
                        std::move(cellSamples), std::move(eweSamples));
                    appendLog(QString("[POTSTEP] t_abs=%1 pt=%2/%3 row=%4 col=%5 t_pot=%6 s cible=(%7,%8) mm moteur=(%9) Ewe=%10 V I=%11 A (moy %12 mesures) stopped=%13 ok=%14%15")
                        .arg(acquisitionTimestamp)
                        .arg(idx + 1).arg(nPoints)
                        .arg(row + 1)
                        .arg(col + 1)
                        .arg(lastCv.elapsedTime, 0, 'f', 6)
                        .arg(wp.x(), 0, 'f', 4)
                        .arg(wp.y(), 0, 'f', 4)
                        .arg(actualMotorText)
                        .arg(meanEwe, 0, 'e', 3).arg(meanI, 0, 'e', 3)
                        .arg(nActualSamples)
                        .arg(lastCv.stopped ? "oui" : "non")
                        .arg(lastCv.ok ? "oui" : "non")
                        .arg(lastCv.ok ? "" : " err=" + lastCv.error));
                }, Qt::QueuedConnection);
                appendMeasurementLogEvent("ACQ", QString("pas_a_pas | t_abs=%1 | pt=%2/%3 | row=%4 | col=%5 | t_pot=%6 s | cible=%7 | moteur=(%8) | Ewe_moy=%9 V | I_moy=%10 A | I_std=%11 A | Ewe_std=%12 V | n_mesures=%13 | I_samples=%14 | Ewe_samples=%15 | stopped=%16")
                    .arg(acquisitionTimestamp)
                    .arg(idx + 1)
                    .arg(nPoints)
                    .arg(row + 1)
                    .arg(col + 1)
                    .arg(lastCv.elapsedTime, 0, 'f', 6)
                    .arg(formatPointMm(wp))
                    .arg(actualMotorText)
                    .arg(meanEwe, 0, 'e', 3)
                    .arg(meanI, 0, 'e', 3)
                    .arg(stdI, 0, 'e', 3)
                    .arg(stdEwe, 0, 'e', 3)
                    .arg(nActualSamples)
                    .arg(rawISamples)
                    .arg(rawEweSamples)
                    .arg(yesNoText(lastCv.stopped)));

                if (lastCv.stopped) {
                    const auto confirm = ctrl->getData(channel);
                    if (confirm.ok && confirm.stopped) {
                        measurementOutcome = "instrument_stop_pas_a_pas";
                        appendMeasurementLogEvent("STOP", QString("Instrument stop confirme pendant pas a pas | pt=%1/%2 | moteur=(%3)")
                            .arg(idx + 1)
                            .arg(nPoints)
                            .arg(actualMotorText));
                        QMetaObject::invokeMethod(this, [this, techniqueLabel]() {
                            appendLog(QString("%1 arrete par l'instrument, fin du balayage.").arg(techniqueLabel));
                        }, Qt::QueuedConnection);
                        break;
                    }
                    appendMeasurementLogEvent("STATE", QString("STOP transitoire ignore | pt=%1/%2 | t_pot=%3 s")
                        .arg(idx + 1)
                        .arg(nPoints)
                        .arg(lastCv.elapsedTime, 0, 'f', 6));
                    QMetaObject::invokeMethod(this, [this, techniqueLabel]() {
                        appendLog(QString("Etat STOP transitoire ignore: %1 continue.").arg(techniqueLabel));
                    }, Qt::QueuedConnection);
                }
            }
            if (potentiostatStopRequested_.load() && measurementOutcome == "completed") {
                measurementOutcome = "external_stop_request";
            }
            } // end if (continuous) / else (point par point)

            // Stop channel
            appendMeasurementLogEvent("STATE", QString("stopChannel demande | channel=%1 | reason=%2 | moteur=(%3)")
                .arg(channel)
                .arg(measurementOutcome)
                .arg(currentMotorPositionLogText(motorCtrl)));
            QMetaObject::invokeMethod(this, [this, channel, techniqueLabel, simpleMeasurement]() {
                appendLog(QString("[POTDBG] stopChannel requested at thread end: technique=%1 simple=%2 channel=%3 potStop=%4")
                    .arg(techniqueLabel)
                    .arg(simpleMeasurement ? "oui" : "non")
                    .arg(channel)
                    .arg(potentiostatStopRequested_.load() ? "oui" : "non"));
            }, Qt::QueuedConnection);
            ctrl->stopChannel(channel);
            finalizeMeasurementLog(QString("status=%1 | acquisitions=%2 | moteur_final=(%3)")
                .arg(measurementOutcome)
                .arg(acquiredSampleCount)
                .arg(currentMotorPositionLogText(motorCtrl)));

            const bool stopped = potentiostatStopRequested_.load();
            QMetaObject::invokeMethod(this, [this, nPoints, stopped, simpleMeasurement]() {
                potentiostatBusy_.store(false);
                currentWaypointIndex_ = simpleMeasurement ? potentiostatSampleCount_ : nPoints;
                if (potentiostatRunButton_    != nullptr) potentiostatRunButton_->setEnabled(true);
                if (potentiostatStopButton_   != nullptr) potentiostatStopButton_->setEnabled(false);
                if (potentiostatExportButton_ != nullptr) {
                    potentiostatExportButton_->setEnabled(potentiostatSampleCount_ > 0);
                }
                if (potentiostatProgressLabel_ != nullptr) {
                    if (stopped) {
                        potentiostatProgressLabel_->setText("Arrete");
                    } else if (simpleMeasurement) {
                        potentiostatProgressLabel_->setText(QString("Termine (%1 pt(s))").arg(potentiostatSampleCount_));
                    } else {
                        potentiostatProgressLabel_->setText(QString("Termine (%1)").arg(potentiostatSampleCount_));
                    }
                }
                if (potentiostatMeasureStateLabel_ != nullptr)
                    potentiostatMeasureStateLabel_->setText(stopped ? "Arrete." : "Acquisition terminee.");
                if (!stopped && sequenceStatusLabel_ != nullptr) {
                    sequenceStatusLabel_->setText(simpleMeasurement
                        ? QString("Mesure simple terminee (%1 point(s)).").arg(potentiostatSampleCount_)
                        : QString("Termine (%1 points).").arg(nPoints));
                }
            }, Qt::QueuedConnection);

        } catch (const std::exception& ex) {
            measurementOutcome = "exception";
            appendMeasurementLogEvent("ERROR", QString("Exception acquisition | message=%1").arg(QString::fromUtf8(ex.what())));
            QMetaObject::invokeMethod(this, [this, message = QString::fromUtf8(ex.what())]() {
                appendLog(QString("[POTDBG] acquisition exception: %1").arg(message));
            }, Qt::QueuedConnection);
            stopPredictedMotorMotion();
            finalizeMeasurementLog(QString("status=%1 | acquisitions=%2 | moteur_final=(%3) | message=%4")
                .arg(measurementOutcome)
                .arg(acquiredSampleCount)
                .arg(currentMotorPositionLogText(motorCtrl))
                .arg(QString::fromUtf8(ex.what())));
            finishUi(QString("Erreur : ") + QString::fromUtf8(ex.what()));
        }
    });
}

void MainWindow::onExportPotentiostat()
{
    const bool hasGrid = potentiostatRows_ > 0 && potentiostatCols_ > 0 && !potentiostatMatrix_.empty();
    const bool hasTimeSeries = !potentiostatPlotTimes_.empty();
    if (!hasGrid && !hasTimeSeries) {
        QMessageBox::information(this, "Export", "Aucune donnée à exporter.");
        return;
    }

    // ── Dialog ────────────────────────────────────────────────────────────────
    QDialog dlg(this);
    dlg.setWindowTitle("Exporter les données");
    dlg.setMinimumWidth(500);
    auto* vl = new QVBoxLayout(&dlg);

    // Formats (checkboxes — export all selected at once)
    auto* fmtBox = new QGroupBox("Formats à exporter");
    auto* fmtLayout = new QVBoxLayout(fmtBox);
    auto* cbCsv  = new QCheckBox("Tableau CSV  (.csv)  —  données tabulaires pour Excel / Python");
    auto* cbGsf  = new QCheckBox("Gwyddion Simple Field  (.gsf)  —  données brutes float32");
    auto* cbHeat = new QCheckBox("Carte 2D  (.tiff)  —  image de la heatmap");
    auto* cb3D   = new QCheckBox("Surface 3D  (.tiff)  —  image de la vue 3D");
    cbCsv->setChecked(true);
    cbGsf->setChecked(hasGrid);
    cbHeat->setChecked(hasGrid);
    cb3D->setChecked(hasGrid);
    // Grid-only formats hidden for simple measurements
    cbGsf->setVisible(hasGrid);
    cbHeat->setVisible(hasGrid);
    cb3D->setVisible(hasGrid);
    fmtLayout->addWidget(cbCsv);
    fmtLayout->addWidget(cbGsf);
    fmtLayout->addWidget(cbHeat);
    fmtLayout->addWidget(cb3D);
    vl->addWidget(fmtBox);

    // Dossier de destination
    auto* dirBox = new QGroupBox("Dossier de destination");
    auto* dirHl  = new QHBoxLayout(dirBox);
    auto* dirEdit = new QLineEdit;
    dirEdit->setPlaceholderText("Dossier...");
    dirEdit->setText(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    auto* browseBtn = new QPushButton("Parcourir...");
    browseBtn->setFixedWidth(100);
    dirHl->addWidget(dirEdit);
    dirHl->addWidget(browseBtn);
    vl->addWidget(dirBox);

    // Nom de base
    auto* nameBox = new QGroupBox("Nom de base (les extensions seront ajoutées automatiquement)");
    auto* nameHl  = new QHBoxLayout(nameBox);
    auto* nameEdit = new QLineEdit;
    const QString baseName = hasGrid
        ? QString("carte_I_%1x%2").arg(potentiostatCols_).arg(potentiostatRows_)
        : QString("mesure_simple");
    nameEdit->setText(baseName);
    nameHl->addWidget(nameEdit);
    vl->addWidget(nameBox);

    // Parcourir
    QObject::connect(browseBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString dir = QFileDialog::getExistingDirectory(
            &dlg, "Choisir un dossier", dirEdit->text());
        if (!dir.isEmpty()) dirEdit->setText(dir);
    });

    // Boutons OK / Annuler
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    btnBox->button(QDialogButtonBox::Ok)->setText("Exporter");
    QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vl->addWidget(btnBox);

    if (dlg.exec() != QDialog::Accepted) return;

    // ── Build paths ──────────────────────────────────────────────────────────
    QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) name = baseName;
    // Strip any extension the user may have typed
    for (const auto& ext : {".csv", ".gsf", ".tiff"}) {
        if (name.endsWith(ext, Qt::CaseInsensitive))
            name.chop(static_cast<int>(strlen(ext)));
    }
    // Create a subfolder with the base name inside the selected directory
    QDir parentDir(dirEdit->text());
    if (!parentDir.mkpath(name)) {
        QMessageBox::critical(this, "Export",
            QString("Impossible de créer le dossier :\n%1").arg(parentDir.absoluteFilePath(name)));
        return;
    }
    const QDir outDir(parentDir.absoluteFilePath(name));
    int exportCount = 0;

    // ── Export CSV ─────────────────────────────────────────────────────────────
    if (cbCsv->isChecked()) {
        const QString csvPath = outDir.absoluteFilePath(name + ".csv");
        QFile csvFile(csvPath);
        if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Export",
                QString("Impossible d'ouvrir :\n%1").arg(csvFile.errorString()));
        } else {
            QTextStream ts(&csvFile);

            if (!hasGrid) {
                // ── Simple measurement: time-series ──
                ts << "# LaserBench - Export CSV - Mesure simple\n";
                ts << "# Date: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
                ts << QString("# Points: %1\n").arg(potentiostatPlotTimes_.size());
                ts << "#\n";
                ts << "t_s;I_A;Ewe_V\n";

                const std::size_t n = potentiostatPlotTimes_.size();
                for (std::size_t i = 0; i < n; ++i) {
                    const double t = potentiostatPlotTimes_[i];
                    const QString iStr = (i < potentiostatPlotCurrents_.size())
                        ? QString::number(potentiostatPlotCurrents_[i], 'e', 6) : QString();
                    const QString eweStr = (i < potentiostatPlotEwe_.size())
                        ? QString::number(potentiostatPlotEwe_[i], 'e', 6) : QString();
                    ts << QString::number(t, 'f', 6) << ";" << iStr << ";" << eweStr << "\n";
                }
            } else {
                // ── Grid measurement ──
                ts << "# LaserBench - Export CSV\n";
                ts << "# Date: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
                ts << QString("# Grille: %1 col x %2 row\n").arg(potentiostatCols_).arg(potentiostatRows_);
                ts << QString("# X: [%1 ; %2] mm\n")
                      .arg(potentiostatXMin_, 0, 'f', 4).arg(potentiostatXMax_, 0, 'f', 4);
                ts << QString("# Y: [%1 ; %2] mm\n")
                      .arg(potentiostatYMin_, 0, 'f', 4).arg(potentiostatYMax_, 0, 'f', 4);

                bool hasDwellSamples = false;
                for (const auto& v : potentiostatCellCurrentSamples_) {
                    if (v.size() > 1) { hasDwellSamples = true; break; }
                }
                ts << QString("# Mode: %1\n").arg(hasDwellSamples
                    ? "pas-a-pas (mesures multiples par point)" : "continu (1 mesure par point)");
                if (hasDwellSamples && potentiostatLastDwellS_ > 0.0) {
                    ts << QString("# Dwell: %1 s\n").arg(potentiostatLastDwellS_, 0, 'f', 6);
                }
                ts << "#\n";

                // Build index sorted by time
                const int nCells = potentiostatRows_ * potentiostatCols_;
                std::vector<int> sortedIdx(static_cast<std::size_t>(nCells));
                std::iota(sortedIdx.begin(), sortedIdx.end(), 0);
                std::sort(sortedIdx.begin(), sortedIdx.end(), [&](int a, int b) {
                    const double tA = (static_cast<std::size_t>(a) < potentiostatCellTimes_.size()) ? potentiostatCellTimes_[static_cast<std::size_t>(a)] : 0.0;
                    const double tB = (static_cast<std::size_t>(b) < potentiostatCellTimes_.size()) ? potentiostatCellTimes_[static_cast<std::size_t>(b)] : 0.0;
                    return tA < tB;
                });

                ts << "# ===========================================================================\n";
                ts << "# TABLEAU PRINCIPAL - Valeurs par cellule (tri par temps)\n";
                ts << "# ===========================================================================\n";
                ts << "Row;Col;X_mm;Y_mm;t_s;I_mean_A;Ewe_mean_V\n";

                for (int flatIdx : sortedIdx) {
                    const std::size_t idx = static_cast<std::size_t>(flatIdx);
                    const int r = flatIdx / potentiostatCols_;
                    const int c = flatIdx % potentiostatCols_;
                    double xMm = 0.0, yMm = 0.0, tS = 0.0;
                    if (idx < potentiostatCellPositions_.size()) {
                        xMm = potentiostatCellPositions_[idx].x();
                        yMm = potentiostatCellPositions_[idx].y();
                    }
                    if (idx < potentiostatCellTimes_.size()) {
                        tS = potentiostatCellTimes_[idx];
                    }
                    QString iStr, eweStr;
                    if (idx < potentiostatMatrix_.size() && potentiostatMatrix_[idx].has_value())
                        iStr = QString::number(*potentiostatMatrix_[idx], 'e', 6);
                    if (idx < potentiostatEweMatrix_.size() && potentiostatEweMatrix_[idx].has_value())
                        eweStr = QString::number(*potentiostatEweMatrix_[idx], 'e', 6);

                    ts << (r + 1) << ";" << (c + 1) << ";"
                       << QString::number(xMm, 'f', 4) << ";"
                       << QString::number(yMm, 'f', 4) << ";"
                       << QString::number(tS, 'f', 6) << ";"
                       << iStr << ";" << eweStr << "\n";
                }

                // ── Individual cell tables ──
                if (hasDwellSamples) {
                    for (int flatIdx : sortedIdx) {
                        const std::size_t idx = static_cast<std::size_t>(flatIdx);
                        const int r = flatIdx / potentiostatCols_;
                        const int c = flatIdx % potentiostatCols_;
                        const auto& iSamples = (idx < potentiostatCellCurrentSamples_.size())
                            ? potentiostatCellCurrentSamples_[idx] : std::vector<double>{};
                        const auto& eweSamples = (idx < potentiostatCellEweSamples_.size())
                            ? potentiostatCellEweSamples_[idx] : std::vector<double>{};
                        const std::size_t nSamples = std::max(iSamples.size(), eweSamples.size());
                        if (nSamples <= 1) continue;

                        double xMm = 0.0, yMm = 0.0;
                        if (idx < potentiostatCellPositions_.size()) {
                            xMm = potentiostatCellPositions_[idx].x();
                            yMm = potentiostatCellPositions_[idx].y();
                        }
                        ts << "#\n";
                        ts << "# ===========================================================================\n";
                        ts << QString("# CELLULE (%1,%2) - Position (%3 ; %4) mm - %5 mesures\n")
                              .arg(r + 1).arg(c + 1)
                              .arg(xMm, 0, 'f', 4).arg(yMm, 0, 'f', 4)
                              .arg(nSamples);
                        ts << "# ===========================================================================\n";
                        ts << "Sample;I_A;Ewe_V\n";

                        for (std::size_t s = 0; s < nSamples; ++s) {
                            const QString iVal = (s < iSamples.size())
                                ? QString::number(iSamples[s], 'e', 6) : QString();
                            const QString eweVal = (s < eweSamples.size())
                                ? QString::number(eweSamples[s], 'e', 6) : QString();
                            ts << (s + 1) << ";" << iVal << ";" << eweVal << "\n";
                        }
                    }
                }
            }
            ts.flush();
            csvFile.close();
            appendLog(QString("Export CSV : %1").arg(csvPath));
            ++exportCount;
        }
    }

    // ── Export GSF ─────────────────────────────────────────────────────────────
    if (cbGsf->isChecked() && hasGrid) {
        const QString gsfPath = outDir.absoluteFilePath(name + ".gsf");
        QFile file(gsfPath);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, "Export",
                QString("Impossible d'ouvrir :\n%1").arg(file.errorString()));
        } else {
            const double xReal = (potentiostatXMax_ - potentiostatXMin_) * 1e-3;
            const double yReal = (potentiostatYMax_ - potentiostatYMin_) * 1e-3;
            const double xOff  = potentiostatXMin_ * 1e-3;
            const double yOff  = potentiostatYMin_ * 1e-3;
            appendLog(QString("GSF dims: XMin=%1 XMax=%2 YMin=%3 YMax=%4 mm → xReal=%5 yReal=%6 m")
                .arg(potentiostatXMin_, 0, 'f', 4).arg(potentiostatXMax_, 0, 'f', 4)
                .arg(potentiostatYMin_, 0, 'f', 4).arg(potentiostatYMax_, 0, 'f', 4)
                .arg(xReal, 0, 'g', 10).arg(yReal, 0, 'g', 10));

            QByteArray header;
            header.append("Gwyddion Simple Field 1.0\n");
            header.append("XRes = ");
            header.append(QByteArray::number(potentiostatCols_));
            header.append('\n');
            header.append("YRes = ");
            header.append(QByteArray::number(potentiostatRows_));
            header.append('\n');
            header.append("XReal = ");
            header.append(QByteArray::number(xReal, 'g', 15));
            header.append('\n');
            header.append("YReal = ");
            header.append(QByteArray::number(yReal, 'g', 15));
            header.append('\n');
            header.append("XOffset = ");
            header.append(QByteArray::number(xOff, 'g', 15));
            header.append('\n');
            header.append("YOffset = ");
            header.append(QByteArray::number(yOff, 'g', 15));
            header.append('\n');
            header.append("XYUnits = m\n");
            header.append("ZUnits = A\n");
            header.append("Title = Courant I(x,y)\n");
            header += '\0';
            while (header.size() % 4 != 0) header += '\0';
            file.write(header);

            for (int r = potentiostatRows_ - 1; r >= 0; --r) {
                for (int c = 0; c < potentiostatCols_; ++c) {
                    const std::size_t matIdx = static_cast<std::size_t>(r * potentiostatCols_ + c);
                    float val = std::numeric_limits<float>::quiet_NaN();
                    if (matIdx < potentiostatMatrix_.size() && potentiostatMatrix_[matIdx].has_value())
                        val = static_cast<float>(*potentiostatMatrix_[matIdx]);
                    file.write(reinterpret_cast<const char*>(&val), sizeof(float));
                }
            }
            file.close();
            appendLog(QString("Export GSF : %1").arg(gsfPath));
            ++exportCount;
        }
    }

    // ── Export TIFF heatmap ────────────────────────────────────────────────────
    if (cbHeat->isChecked() && hasGrid) {
        const QString heatPath = outDir.absoluteFilePath(name + "_heatmap.tiff");
        if (potentiostatHeatmapWidget_ != nullptr) {
            const QSize sz = potentiostatHeatmapWidget_->size().isEmpty()
                ? QSize(1200, 800) : potentiostatHeatmapWidget_->size();
            QImage img(sz, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            { QPainter p(&img); potentiostatHeatmapWidget_->render(&p, {}, {}, QWidget::DrawChildren); }
            if (saveTiff(img, heatPath)) {
                appendLog(QString("Export TIFF heatmap : %1").arg(heatPath));
                ++exportCount;
            }
        }
    }

    // ── Export TIFF 3D ─────────────────────────────────────────────────────────
    if (cb3D->isChecked() && hasGrid) {
        const QString tdPath = outDir.absoluteFilePath(name + "_3D.tiff");
        if (potentiostat3DWidget_ != nullptr) {
            const QSize sz = potentiostat3DWidget_->size().isEmpty()
                ? QSize(1200, 900) : potentiostat3DWidget_->size();
            QImage img(sz, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            { QPainter p(&img); potentiostat3DWidget_->render(&p, {}, {}, QWidget::DrawChildren); }
            if (saveTiff(img, tdPath)) {
                appendLog(QString("Export TIFF 3D : %1").arg(tdPath));
                ++exportCount;
            }
        }
    }

    if (exportCount == 0) {
        QMessageBox::warning(this, "Export", "Aucun format sélectionné ou erreur d'écriture.");
    } else {
        appendLog(QString("Export terminé : %1 fichier(s) dans %2").arg(exportCount).arg(outDir.absolutePath()));
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  Import CSV → reconstructs visualization from previously exported data
// ══════════════════════════════════════════════════════════════════════════════
void MainWindow::onImportCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Importer un fichier CSV", QString(), "CSV (*.csv);;Tous (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Import",
            QString("Impossible d'ouvrir :\n%1").arg(file.errorString()));
        return;
    }

    QTextStream in(&file);
    enum class Format { Unknown, Grid, TimeSeries };
    Format fmt = Format::Unknown;

    int metaRows = 0, metaCols = 0;
    double metaXMin = 0, metaXMax = 0, metaYMin = 0, metaYMax = 0;
    bool hasMeta = false;
    double metaDwellS = 0.0;

    struct GridRow { int row; int col; double x; double y; double t; double iMean; double ewe; };
    std::vector<GridRow> gridData;

    struct TimeRow { double t; double i; double ewe; };
    std::vector<TimeRow> tsData;

    struct CellDetail { std::vector<double> iSamples; std::vector<double> eweSamples; };
    std::map<int, CellDetail> cellDetails;

    int currentCellKey = -1;
    bool inCellSection = false;
    int parsedNSamples = 0;  // track max samples per cell for dwell estimation

    while (!in.atEnd()) {
        const QString rawLine = in.readLine();
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith('#')) {
            static const QRegularExpression reGrid(
                R"(#\s*Grille\s*:\s*(\d+)\s*col\s*x\s*(\d+)\s*row)");
            auto mGrid = reGrid.match(line);
            if (mGrid.hasMatch()) {
                metaCols = mGrid.captured(1).toInt();
                metaRows = mGrid.captured(2).toInt();
            }
            static const QRegularExpression reX(
                R"(#\s*X\s*:\s*\[\s*([-+0-9.eE]+)\s*;\s*([-+0-9.eE]+)\s*\]\s*mm)");
            auto mX = reX.match(line);
            if (mX.hasMatch()) {
                metaXMin = mX.captured(1).toDouble();
                metaXMax = mX.captured(2).toDouble();
                hasMeta = true;
            }
            static const QRegularExpression reY(
                R"(#\s*Y\s*:\s*\[\s*([-+0-9.eE]+)\s*;\s*([-+0-9.eE]+)\s*\]\s*mm)");
            auto mY = reY.match(line);
            if (mY.hasMatch()) {
                metaYMin = mY.captured(1).toDouble();
                metaYMax = mY.captured(2).toDouble();
            }
            static const QRegularExpression reDwell(
                R"(#\s*Dwell\s*:\s*([-+0-9.eE]+)\s*s)");
            auto mDwell = reDwell.match(line);
            if (mDwell.hasMatch()) {
                metaDwellS = mDwell.captured(1).toDouble();
            }
            static const QRegularExpression reCell(
                R"(#\s*CELLULE\s*\(\s*(\d+)\s*,\s*(\d+)\s*\))");
            auto mCell = reCell.match(line);
            if (mCell.hasMatch() && metaCols > 0) {
                const int cr = mCell.captured(1).toInt() - 1;
                const int cc = mCell.captured(2).toInt() - 1;
                currentCellKey = cr * metaCols + cc;
                inCellSection = false;
            }
            continue;
        }

        if (line.startsWith("Row;Col;")) {
            fmt = Format::Grid;
            inCellSection = false;
            currentCellKey = -1;
            continue;
        }
        if (line.startsWith("t_s;I_A;Ewe_V")) {
            fmt = Format::TimeSeries;
            continue;
        }
        if (line.startsWith("Sample;I_A;Ewe_V")) {
            inCellSection = true;
            if (currentCellKey >= 0) cellDetails[currentCellKey] = {};
            continue;
        }

        if (inCellSection && currentCellKey >= 0) {
            const QStringList parts = line.split(';');
            if (parts.size() >= 3) {
                auto& cd = cellDetails[currentCellKey];
                cd.iSamples.push_back(parts[1].toDouble());
                cd.eweSamples.push_back(parts[2].toDouble());
                parsedNSamples = std::max(parsedNSamples, static_cast<int>(cd.iSamples.size()));
            }
            continue;
        }

        const QStringList parts = line.split(';');
        if (fmt == Format::Grid && parts.size() >= 7) {
            GridRow gr;
            gr.row  = parts[0].toInt();
            gr.col  = parts[1].toInt();
            gr.x    = parts[2].toDouble();
            gr.y    = parts[3].toDouble();
            gr.t    = parts[4].toDouble();
            gr.iMean = parts[5].isEmpty() ? 0.0 : parts[5].toDouble();
            gr.ewe   = parts[6].isEmpty() ? 0.0 : parts[6].toDouble();
            gridData.push_back(gr);
        } else if (fmt == Format::TimeSeries && parts.size() >= 3) {
            TimeRow tr;
            tr.t   = parts[0].toDouble();
            tr.i   = parts[1].isEmpty() ? 0.0 : parts[1].toDouble();
            tr.ewe = parts[2].isEmpty() ? 0.0 : parts[2].toDouble();
            tsData.push_back(tr);
        }
    }
    file.close();

    if (gridData.empty() && tsData.empty()) {
        QMessageBox::warning(this, "Import", "Aucune donnée valide trouvée dans le fichier CSV.");
        return;
    }

    // ── Clear import data ─────────────────────────────────────────────────────
    importPlotTimes_.clear();
    importPlotCurrents_.clear();
    importPlotEwe_.clear();
    importMatrix_.clear();
    importEweMatrix_.clear();
    importCellCurrentSamples_.clear();
    importCellEweSamples_.clear();
    importCellPositions_.clear();
    importCellTimes_.clear();
    importRows_ = 0;
    importCols_ = 0;
    importXMin_ = importXMax_ = importYMin_ = importYMax_ = 0.0;
    importLastDwellS_ = 0.0;

    // ── Populate import data ──────────────────────────────────────────────────
    if (!gridData.empty()) {
        if (metaRows == 0 || metaCols == 0) {
            int maxR = 0, maxC = 0;
            for (const auto& g : gridData) {
                maxR = std::max(maxR, g.row);
                maxC = std::max(maxC, g.col);
            }
            metaRows = maxR;
            metaCols = maxC;
        }

        importRows_ = metaRows;
        importCols_ = metaCols;
        const std::size_t nCells = static_cast<std::size_t>(metaRows * metaCols);
        importMatrix_.assign(nCells, std::nullopt);
        importEweMatrix_.assign(nCells, std::nullopt);
        importCellCurrentSamples_.assign(nCells, {});
        importCellEweSamples_.assign(nCells, {});
        importCellPositions_.assign(nCells, QPointF());
        importCellTimes_.assign(nCells, 0.0);

        if (hasMeta) {
            importXMin_ = metaXMin; importXMax_ = metaXMax;
            importYMin_ = metaYMin; importYMax_ = metaYMax;
        } else {
            double xMin = 1e18, xMax = -1e18, yMin = 1e18, yMax = -1e18;
            for (const auto& g : gridData) {
                xMin = std::min(xMin, g.x); xMax = std::max(xMax, g.x);
                yMin = std::min(yMin, g.y); yMax = std::max(yMax, g.y);
            }
            importXMin_ = xMin; importXMax_ = xMax;
            importYMin_ = yMin; importYMax_ = yMax;
        }

        // Use dwell time from CSV header if available, otherwise estimate
        if (metaDwellS > 0.0) {
            importLastDwellS_ = metaDwellS;
        } else {
            std::sort(gridData.begin(), gridData.end(),
                      [](const GridRow& a, const GridRow& b) { return a.t < b.t; });
            if (gridData.size() >= 2 && parsedNSamples > 1) {
                const double dt = gridData[1].t - gridData[0].t;
                importLastDwellS_ = std::max(0.1, dt);
            }
        }

        for (const auto& g : gridData) {
            const int r = g.row - 1;
            const int c = g.col - 1;
            if (r < 0 || r >= metaRows || c < 0 || c >= metaCols) continue;
            const std::size_t idx = static_cast<std::size_t>(r * metaCols + c);
            importMatrix_[idx] = g.iMean;
            importEweMatrix_[idx] = g.ewe;
            importCellPositions_[idx] = QPointF(g.x, g.y);
            importCellTimes_[idx] = g.t;

            importPlotTimes_.push_back(g.t);
            importPlotCurrents_.push_back(g.iMean);
            importPlotEwe_.push_back(g.ewe);

            auto it = cellDetails.find(r * metaCols + c);
            if (it != cellDetails.end()) {
                importCellCurrentSamples_[idx] = it->second.iSamples;
                importCellEweSamples_[idx] = it->second.eweSamples;
            }
        }

        if (importMapBox_ != nullptr) importMapBox_->setVisible(true);
        if (importView3DButton_ != nullptr) {
            importView3DButton_->setVisible(true);
            importView3DButton_->setEnabled(true);
        }
    } else {
        // Time-series
        importPlotTimes_.reserve(tsData.size());
        importPlotCurrents_.reserve(tsData.size());
        importPlotEwe_.reserve(tsData.size());
        for (const auto& tr : tsData) {
            importPlotTimes_.push_back(tr.t);
            importPlotCurrents_.push_back(tr.i);
            importPlotEwe_.push_back(tr.ewe);
        }
        if (importMapBox_ != nullptr) importMapBox_->setVisible(false);
        if (importView3DButton_ != nullptr) importView3DButton_->setVisible(false);
    }

    refreshImportVisualization();

    if (importInfoLabel_ != nullptr) {
        const QString info = gridData.empty()
            ? QString("Mesure simple : %1 points").arg(tsData.size())
            : QString("Grille %1×%2 : %3 cellules\n%4")
                .arg(metaCols).arg(metaRows)
                .arg(gridData.size())
                .arg(QFileInfo(path).fileName());
        importInfoLabel_->setText(info);
    }
    if (tabWidget_ != nullptr) tabWidget_->setCurrentIndex(3);  // Switch to "Import"
    appendLog(QString("Import CSV : %1  (%2 points)")
        .arg(path).arg(gridData.empty() ? tsData.size() : gridData.size()));
}

void MainWindow::onStopCaPotentiostat()
{
    appendLog(QString("[POTDBG] stop button pressed: set potStop=true | busy=%1 | seqStop=%2")
        .arg(potentiostatBusy_.load() ? "oui" : "non")
        .arg(sequenceStopRequested_.load() ? "oui" : "non"));
    appendMeasurementLogEvent("STOP_REQ", QString("Demande utilisateur | busy=%1 | seqStop=%2 | moteur=(%3)")
        .arg(yesNoText(potentiostatBusy_.load()))
        .arg(yesNoText(sequenceStopRequested_.load()))
        .arg(currentMotorPositionLogText(motorController_)));
    potentiostatStopRequested_.store(true);
    stopPredictedMotorMotion();

    // Arrêt immédiat des moteurs (débloque les waitAxis dans le thread de scan)
    if (motorController_ != nullptr) {
        try {
            motorController_->stopAxis(hardware::AxisId::X);
            motorController_->stopAxis(hardware::AxisId::Y);
        } catch (...) {}
    }

    // Arrêt du canal potentiostat
    if (potentiostatController_ == nullptr) { return; }
    const int channel = potentiostatChannelCombo_ != nullptr ? potentiostatChannelCombo_->currentIndex() + 1 : 1;
    auto ctrl = potentiostatController_;
    std::thread([ctrl, channel]() { ctrl->stopChannel(channel); }).detach();
}

// ─────────────────────────────────────────────────────────────────────────────
std::optional<std::pair<MainWindow::ScanConfig::RectangleStartCorner, MainWindow::ScanConfig::RectanglePrimaryAxis>>
MainWindow::promptRectangleTraversalSelection()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Sens du balayage rectangle");
    dlg.setMinimumWidth(420);

    auto* layout = new QVBoxLayout(&dlg);
    auto* intro = new QLabel("Cliquer sur une fleche pour choisir le point de depart et la direction initiale du balayage.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* preview = new RectangleTraversalPreviewWidget;
    preview->setSelection({effectiveRectangleStartCorner(), effectiveRectanglePrimaryAxis()});
    layout->addWidget(preview);

    auto* hint = new QLabel("Fleches horizontales : balayage gauche-droite. Fleches verticales : balayage haut-bas.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#5c6570; font-size:9pt;");
    layout->addWidget(hint);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btnBox);

    if (dlg.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    const RectangleTraversalSelection selection = preview->selection();
    return std::make_pair(selection.startCorner, selection.primaryAxis);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::showScanConfigDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Paramètres de balayage");
    dlg.setMinimumWidth(380);
    auto* vl = new QVBoxLayout(&dlg);

    // ── Mode ─────────────────────────────────────────────────────────────────
    auto* modeGrp = new QGroupBox("Mode d'acquisition");
    auto* modeHl  = new QHBoxLayout(modeGrp);
    auto* ppBtn   = new QRadioButton("Point par point");
    auto* cntBtn  = new QRadioButton("Balayage continu");
    ppBtn->setChecked(scanConfig_.mode == ScanConfig::AcquisitionMode::PointByPoint);
    cntBtn->setChecked(scanConfig_.mode == ScanConfig::AcquisitionMode::Continuous);
    modeHl->addWidget(ppBtn);
    modeHl->addWidget(cntBtn);
    vl->addWidget(modeGrp);

    // ── Stacked panels ───────────────────────────────────────────────────────
    auto* stack = new QStackedWidget;

    // Panel 0 — Point par point
    auto* ppWidget = new QWidget;
    auto* ppGrid   = new QGridLayout(ppWidget);
    ppGrid->addWidget(new QLabel("Pas :"), 0, 0);
    auto* stepSpin = new QDoubleSpinBox;
    stepSpin->setRange(0.001, 25.0); stepSpin->setDecimals(3); stepSpin->setSuffix(" mm");
    stepSpin->setValue(scanConfig_.stepMm);
    ppGrid->addWidget(stepSpin, 0, 1);
    ppGrid->addWidget(new QLabel("Durée pause avant mesure :"), 1, 0);
    auto* dwellSpin = new QDoubleSpinBox;
    dwellSpin->setRange(0.1, 3600.0); dwellSpin->setDecimals(2); dwellSpin->setSuffix(" s");
    dwellSpin->setValue(scanConfig_.dwellS);
    ppGrid->addWidget(dwellSpin, 1, 1);
    ppGrid->addWidget(new QLabel("Nb mesures / point :"), 2, 0);
    auto* dwellSamplesSpin = new QSpinBox;
    dwellSamplesSpin->setRange(1, 999); dwellSamplesSpin->setSuffix(" mesure(s)");
    dwellSamplesSpin->setValue(scanConfig_.dwellSamples);
    ppGrid->addWidget(dwellSamplesSpin, 2, 1);
    stack->addWidget(ppWidget);   // index 0

    // Panel 1 — Balayage continu
    auto* cntWidget = new QWidget;
    auto* cntGrid   = new QGridLayout(cntWidget);
    cntGrid->addWidget(new QLabel("Vitesse moteur :"), 0, 0);
    auto* speedSpin = new QDoubleSpinBox;
    speedSpin->setRange(0.001, 10.0); speedSpin->setDecimals(3); speedSpin->setSuffix(" mm/s");
    speedSpin->setValue(scanConfig_.scanSpeedMmPerS);
    cntGrid->addWidget(speedSpin, 0, 1);
    cntGrid->addWidget(new QLabel("Saut de ligne :"), 1, 0);
    auto* rowStepSpin = new QDoubleSpinBox;
    rowStepSpin->setRange(0.001, 25.0); rowStepSpin->setDecimals(3); rowStepSpin->setSuffix(" mm");
    const QString objNameForScan = objectiveCombo_ != nullptr
        ? objectiveCombo_->currentText().trimmed() : QString("4x");
    const double laserDiameterMm = laserRadiusPx_ * 2.0 * autoMmPerPxForObjective(objNameForScan);
    const double defaultRowStepMm = scanConfig_.rowStepMm > 0.0
        ? scanConfig_.rowStepMm
        : std::max(0.001, laserDiameterMm);
    rowStepSpin->setValue(defaultRowStepMm);
    cntGrid->addWidget(rowStepSpin, 1, 1);

    auto* intGrp  = new QGroupBox("Intervalle d'acquisition");
    auto* intGrid = new QGridLayout(intGrp);
    auto* distBtn = new QRadioButton("Toutes les");
    auto* timeBtn = new QRadioButton("Toutes les");
    distBtn->setChecked(scanConfig_.trigger == ScanConfig::ContinuousTrigger::Distance);
    timeBtn->setChecked(scanConfig_.trigger == ScanConfig::ContinuousTrigger::Time);
    auto* distSpin = new QDoubleSpinBox;
    distSpin->setRange(0.001, 25.0); distSpin->setDecimals(3); distSpin->setSuffix(" mm");
    distSpin->setValue(scanConfig_.triggerDistanceMm);
    auto* timeSpin = new QDoubleSpinBox;
    timeSpin->setRange(0.01, 3600.0); timeSpin->setDecimals(2); timeSpin->setSuffix(" s");
    timeSpin->setValue(scanConfig_.triggerTimeS);
    intGrid->addWidget(distBtn, 0, 0); intGrid->addWidget(distSpin, 0, 1);
    intGrid->addWidget(timeBtn, 1, 0); intGrid->addWidget(timeSpin, 1, 1);
    cntGrid->addWidget(intGrp, 2, 0, 1, 2);
    stack->addWidget(cntWidget);  // index 1

    vl->addWidget(stack);
    stack->setCurrentIndex(ppBtn->isChecked() ? 0 : 1);
    connect(ppBtn, &QRadioButton::toggled, [stack](bool chk) { stack->setCurrentIndex(chk ? 0 : 1); });

    // ── Buttons ──────────────────────────────────────────────────────────────
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vl->addWidget(btnBox);

    if (dlg.exec() != QDialog::Accepted) return;

    const bool rectangleScan = sequenceModeCombo_ != nullptr
        && sequenceModeCombo_->currentText().trimmed() == "Rectangle";
    std::optional<std::pair<ScanConfig::RectangleStartCorner, ScanConfig::RectanglePrimaryAxis>> selectedRectangleTraversal;
    if (rectangleScan) {
        selectedRectangleTraversal = promptRectangleTraversalSelection();
        if (!selectedRectangleTraversal.has_value()) {
            return;
        }
    }

    if (ppBtn->isChecked()) {
        scanConfig_.mode         = ScanConfig::AcquisitionMode::PointByPoint;
        scanConfig_.stepMm       = stepSpin->value();
        scanConfig_.dwellS       = dwellSpin->value();
        scanConfig_.dwellSamples = dwellSamplesSpin->value();
        if (sequenceStepMmEdit_)   sequenceStepMmEdit_->setText(QString::number(scanConfig_.stepMm, 'g', 4));
        if (sequenceDurationEdit_) sequenceDurationEdit_->setText(QString::number(scanConfig_.dwellS, 'g', 3));
        appendLog(QString("Balayage point par point : pas=%1 mm, dwell=%2 s, %3 mesure(s)/pt")
            .arg(scanConfig_.stepMm, 0, 'f', 3).arg(scanConfig_.dwellS, 0, 'f', 2).arg(scanConfig_.dwellSamples));
    } else {
        scanConfig_.mode              = ScanConfig::AcquisitionMode::Continuous;
        scanConfig_.scanSpeedMmPerS   = speedSpin->value();
        scanConfig_.trigger           = distBtn->isChecked()
                                        ? ScanConfig::ContinuousTrigger::Distance
                                        : ScanConfig::ContinuousTrigger::Time;
        scanConfig_.triggerDistanceMm = distSpin->value();
        scanConfig_.triggerTimeS      = timeSpin->value();
        scanConfig_.rowStepMm         = rowStepSpin->value();
        appendLog(distBtn->isChecked()
            ? QString("Balayage continu : vitesse=%1 mm/s, acquisition toutes les %2 mm, saut de ligne=%3 mm")
                .arg(scanConfig_.scanSpeedMmPerS, 0, 'f', 3)
                .arg(scanConfig_.triggerDistanceMm, 0, 'f', 3)
                .arg(scanConfig_.rowStepMm, 0, 'f', 3)
            : QString("Balayage continu : vitesse=%1 mm/s, acquisition toutes les %2 s, saut de ligne=%3 mm")
                .arg(scanConfig_.scanSpeedMmPerS, 0, 'f', 3)
                .arg(scanConfig_.triggerTimeS, 0, 'f', 2)
                .arg(scanConfig_.rowStepMm, 0, 'f', 3));
    }

    if (selectedRectangleTraversal.has_value()) {
        scanConfig_.rectangleStartCorner = selectedRectangleTraversal->first;
        scanConfig_.rectanglePrimaryAxis = selectedRectangleTraversal->second;
        scanConfig_.rectangleTraversalExplicit = true;
        appendLog(QString("Balayage rectangle selectionne : depart=%1 | axe principal=%2")
            .arg(rectangleStartCornerLabel(selectedRectangleTraversal->first))
            .arg(rectanglePrimaryAxisLabel(selectedRectangleTraversal->second)));
    }
}

}  // namespace laserbench::ui
