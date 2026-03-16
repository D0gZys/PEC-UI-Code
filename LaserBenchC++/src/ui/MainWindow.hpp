#pragma once

#include <QImage>
#include <QMainWindow>
#include <QPoint>
#include <QSize>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "core/AppState.hpp"
#include "hardware/NewportConexController.hpp"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QEvent;
class QLabel;
class QLineEdit;
class QObject;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;
QT_END_NAMESPACE

namespace laserbench::hardware {
class ICameraController;
class MockPotentiostatController;
}

namespace laserbench::ui {

class CameraPreviewWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct MotorTaskResult
    {
        bool success {true};
        QString message;
        std::function<void()> uiContinuation;
    };

    void buildMenus();
    void buildUi();
    QWidget* buildSetupTab();
    QWidget* buildMeasureTab();
    void openMotorConnectionDialog();
    void openCameraConnectionDialog();
    void openCameraSettingsDialog();
    void openCalibrationDialog();
    void applyObjectivePreset();
    void syncLaserOverlay(const QSize& frameSize = QSize());
    void updateLaserLabel();
    void updatePreviewCursor();
    void setGotoArmed(bool armed);
    void setSequenceSelectArmed(bool armed);
    void syncCalibrationUi();
    void clearSequencePreviewSelection();
    void updateSequenceLabels(const QPointF& startMm, const QPointF& endMm);
    void syncSequenceOverlay();
    void applyLaserCalibrationEdits();
    void nudgeLaserTarget(int dxPx, int dyPx, int dRadiusPx);
    void applyCameraSettings();
    void startCameraLive();
    void stopCameraLive();
    void refreshSummaries();
    void refreshMotorUi();
    void refreshCameraUi();
    void flushLatestCameraFrameToUi();
    void startCameraPolling();
    void stopCameraPolling();
    void cameraPollingLoop();
    void appendLog(const QString& message);
    void runMotorTask(const QString& label, std::function<MotorTaskResult()> worker);
    void startMotorPolling();
    void stopMotorPolling();
    void motorPollingLoop();
    void startContinuousJog(hardware::AxisId axis, int direction);
    void stopContinuousJog(hardware::AxisId axis);
    void updateHeldJogTimer();
    void triggerHeldJog();
    bool shouldHandleJogKeys() const;
    double readJogSpeedMmPerS() const;

    void onScanPorts();
    void onConnectAxes();
    void onHomeAxes();
    void onDisconnectAxes();
    void onJogAxis(hardware::AxisId axis, int direction);
    void onMoveAbsoluteBoth();
    void onArmGoto();
    void onSetSequenceStart();
    void onSetSequenceEnd();
    void onArmSequenceRectangle();
    void onRunSequence();
    void onStopSequence();
    void onPreviewFrameClicked(const QPoint& framePointPx);
    void onPreviewBackgroundClicked();

    double readJogStepMm() const;
    double readAbsoluteTargetMm(hardware::AxisId axis) const;
    double autoMmPerPxForObjective(const QString& objectiveName) const;
    std::pair<double, double> currentGotoScale() const;
    QPointF readMotorPositionsOrThrow() const;
    QPointF framePointToMotorTarget(const QPoint& framePointPx, const QPointF& baseMotorMm) const;
    QPoint motorTargetToFramePoint(const QPointF& motorTargetMm, const QPointF& currentMotorMm) const;
    std::vector<QPointF> buildWaypointsLinear(const QPointF& startMm, const QPointF& endMm, double stepMm) const;
    std::vector<QPointF> buildWaypointsRect(const QPointF& startMm, const QPointF& endMm, double stepMm) const;
    void setSequenceRunning(bool running);
    std::optional<QPointF> latestPolledMotorPosition();
    void startPredictedMotorMotion(
        std::optional<double> startXmm,
        std::optional<double> startYmm,
        std::optional<double> targetXmm,
        std::optional<double> targetYmm,
        std::optional<double> speedXmmPerS,
        std::optional<double> speedYmmPerS
    );
    void stopPredictedMotorMotion(std::optional<double> holdXmm = std::nullopt, std::optional<double> holdYmm = std::nullopt);
    void updatePredictedMotorMotion(const hardware::MotorAxisSnapshot& xSnapshot, const hardware::MotorAxisSnapshot& ySnapshot);
    std::optional<QPointF> estimatedMotorPositionForOverlay() const;
    std::optional<QPointF> overlayMotorPositionForDisplay() const;

    struct AxisMotionPrediction
    {
        bool active {false};
        double basePositionMm {0.0};
        std::optional<double> targetPositionMm;
        double velocityMmPerS {0.0};
        int direction {0};
        std::chrono::steady_clock::time_point baseTimestamp {};
    };

    core::RuntimeSnapshot snapshot_;
    std::shared_ptr<hardware::NewportConexController> motorController_;
    std::unique_ptr<hardware::ICameraController> cameraController_;
    std::unique_ptr<hardware::MockPotentiostatController> potentiostatController_;

    QTimer* motorPollTimer_ {nullptr};
    QTimer* cameraPollTimer_ {nullptr};
    QTimer* keyJogTimer_ {nullptr};
    std::thread motorPollThread_;
    std::thread cameraPollThread_;
    std::thread sequenceThread_;
    std::atomic_bool motorPollStopRequested_ {false};
    std::atomic_bool cameraPollStopRequested_ {false};
    std::atomic_bool motorUiUpdatePending_ {false};
    std::atomic_bool cameraUiUpdatePending_ {false};
    std::atomic_bool sequenceStopRequested_ {false};
    mutable std::mutex motorSnapshotMutex_;
    std::mutex cameraSnapshotMutex_;
    mutable std::mutex predictedMotionMutex_;
    hardware::MotorAxisSnapshot polledXSnapshot_ {hardware::AxisId::X};
    hardware::MotorAxisSnapshot polledYSnapshot_ {hardware::AxisId::Y};
    QImage polledCameraFrame_;
    bool motorSnapshotsReady_ {false};
    bool cameraFrameReady_ {false};
    QString pendingMotorPollError_;
    QString pendingCameraPollError_;
    bool motorTaskRunning_ {false};
    bool sequenceRunning_ {false};
    bool gotoArmed_ {false};
    bool sequenceSelectArmed_ {false};
    bool leftKeyHeld_ {false};
    bool rightKeyHeld_ {false};
    bool upKeyHeld_ {false};
    bool downKeyHeld_ {false};
    std::mutex xContinuousJogMutex_;
    std::mutex yContinuousJogMutex_;
    std::atomic_uint64_t xContinuousJogGeneration_ {0};
    std::atomic_uint64_t yContinuousJogGeneration_ {0};
    bool xContinuousJogActive_ {false};
    bool yContinuousJogActive_ {false};
    int xContinuousJogDirection_ {0};
    int yContinuousJogDirection_ {0};
    AxisMotionPrediction predictedXMotion_;
    AxisMotionPrediction predictedYMotion_;
    std::optional<QPointF> lastMeasuredMotorMm_;
    std::optional<std::chrono::steady_clock::time_point> lastMeasuredMotorTimestamp_;
    std::optional<QPointF> stableOverlayMotorMm_;
    std::optional<std::chrono::steady_clock::time_point> lastMotorUiRefresh_;
    std::optional<std::chrono::steady_clock::time_point> lastCameraUiRefresh_;
    std::optional<std::chrono::steady_clock::time_point> lastStatusRefresh_;

    QTabWidget* tabWidget_ {nullptr};
    QLabel* stageSummaryLabel_ {nullptr};
    QLabel* cameraSummaryLabel_ {nullptr};
    QLabel* potentiostatSummaryLabel_ {nullptr};
    QPlainTextEdit* logView_ {nullptr};
    QDialog* motorConnectionDialog_ {nullptr};
    QDialog* cameraConnectionDialog_ {nullptr};
    QDialog* cameraSettingsDialog_ {nullptr};
    QDialog* calibrationDialog_ {nullptr};

    QComboBox* xPortCombo_ {nullptr};
    QComboBox* yPortCombo_ {nullptr};
    QComboBox* cameraSerialCombo_ {nullptr};
    QComboBox* objectiveCombo_ {nullptr};
    QLineEdit* jogStepEdit_ {nullptr};
    QLineEdit* absXEdit_ {nullptr};
    QLineEdit* absYEdit_ {nullptr};
    QLineEdit* gotoCorrXpEdit_ {nullptr};
    QLineEdit* gotoCorrXmEdit_ {nullptr};
    QLineEdit* gotoCorrYpEdit_ {nullptr};
    QLineEdit* gotoCorrYmEdit_ {nullptr};
    QLineEdit* gotoVelocityEdit_ {nullptr};
    QLineEdit* laserMoveStepEdit_ {nullptr};
    QLineEdit* laserXEdit_ {nullptr};
    QLineEdit* laserYEdit_ {nullptr};
    QLineEdit* laserSizeEdit_ {nullptr};
    QLineEdit* cameraExposureEdit_ {nullptr};
    QLineEdit* cameraGainEdit_ {nullptr};
    QLabel* xPositionValueLabel_ {nullptr};
    QLabel* yPositionValueLabel_ {nullptr};
    QLabel* laserPointLabel_ {nullptr};
    QLabel* gotoStatusLabel_ {nullptr};
    QLabel* cameraZoomLabel_ {nullptr};
    CameraPreviewWidget* cameraPreviewWidget_ {nullptr};
    QPoint laserPointPx_ {2588, 1350};
    int laserRadiusPx_ {32};
    std::optional<QPointF> sequenceStartMotorMm_;
    std::optional<QPointF> sequenceEndMotorMm_;
    std::optional<QPoint> sequenceFirstFramePoint_;
    std::optional<QPoint> sequenceRectStartFrame_;
    std::optional<QPoint> sequenceRectEndFrame_;
    std::optional<QPointF> sequenceBaseMotorMm_;
    std::vector<QPointF> waypointsMm_;
    int currentWaypointIndex_ {-1};
    std::optional<QPointF> cachedMotorMm_;
    bool sequenceRectFollowSample_ {false};
    QCheckBox* gotoInvertXCheck_ {nullptr};
    QCheckBox* gotoInvertYCheck_ {nullptr};
    QComboBox* sequenceModeCombo_ {nullptr};
    QLineEdit* sequenceStepMmEdit_ {nullptr};
    QLineEdit* sequenceDurationEdit_ {nullptr};
    QLabel* sequenceStartLabel_ {nullptr};
    QLabel* sequenceEndLabel_ {nullptr};
    QLabel* sequenceStatusLabel_ {nullptr};
    QCheckBox* showZoneCheck_ {nullptr};
    QCheckBox* hideWaypointsCheck_ {nullptr};

    QPushButton* scanPortsButton_ {nullptr};
    QPushButton* connectAxesButton_ {nullptr};
    QPushButton* homeAxesButton_ {nullptr};
    QPushButton* disconnectAxesButton_ {nullptr};
    QPushButton* scanCameraButton_ {nullptr};
    QPushButton* connectCameraButton_ {nullptr};
    QPushButton* disconnectCameraButton_ {nullptr};
    QPushButton* applyCameraSettingsButton_ {nullptr};
    QPushButton* startCameraLiveButton_ {nullptr};
    QPushButton* stopCameraLiveButton_ {nullptr};
    QPushButton* moveAbsXYButton_ {nullptr};
    QPushButton* gotoButton_ {nullptr};
    QPushButton* sequenceSetStartButton_ {nullptr};
    QPushButton* sequenceSetEndButton_ {nullptr};
    QPushButton* sequencePickButton_ {nullptr};
    QPushButton* sequenceRunButton_ {nullptr};
    QPushButton* sequenceStopButton_ {nullptr};
};

}  // namespace laserbench::ui
