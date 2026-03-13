#pragma once

#include <QImage>
#include <QMainWindow>
#include <QPoint>
#include <QSize>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "core/AppState.hpp"
#include "hardware/NewportConexController.hpp"

QT_BEGIN_NAMESPACE
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
    void applyObjectivePreset();
    void syncLaserOverlay(const QSize& frameSize = QSize());
    void updateLaserLabel();
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
    void onMoveAbsolute(hardware::AxisId axis);

    double readJogStepMm() const;
    double readAbsoluteTargetMm(hardware::AxisId axis) const;

    core::RuntimeSnapshot snapshot_;
    std::shared_ptr<hardware::NewportConexController> motorController_;
    std::unique_ptr<hardware::ICameraController> cameraController_;
    std::unique_ptr<hardware::MockPotentiostatController> potentiostatController_;

    QTimer* motorPollTimer_ {nullptr};
    QTimer* cameraPollTimer_ {nullptr};
    QTimer* keyJogTimer_ {nullptr};
    std::thread motorPollThread_;
    std::thread cameraPollThread_;
    std::atomic_bool motorPollStopRequested_ {false};
    std::atomic_bool cameraPollStopRequested_ {false};
    std::atomic_bool cameraUiUpdatePending_ {false};
    std::mutex motorSnapshotMutex_;
    std::mutex cameraSnapshotMutex_;
    hardware::MotorAxisSnapshot polledXSnapshot_ {hardware::AxisId::X};
    hardware::MotorAxisSnapshot polledYSnapshot_ {hardware::AxisId::Y};
    QImage polledCameraFrame_;
    bool motorSnapshotsReady_ {false};
    bool cameraFrameReady_ {false};
    QString pendingMotorPollError_;
    QString pendingCameraPollError_;
    bool motorTaskRunning_ {false};
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

    QTabWidget* tabWidget_ {nullptr};
    QLabel* stageSummaryLabel_ {nullptr};
    QLabel* cameraSummaryLabel_ {nullptr};
    QLabel* potentiostatSummaryLabel_ {nullptr};
    QPlainTextEdit* logView_ {nullptr};
    QDialog* motorConnectionDialog_ {nullptr};
    QDialog* cameraConnectionDialog_ {nullptr};

    QComboBox* xPortCombo_ {nullptr};
    QComboBox* yPortCombo_ {nullptr};
    QComboBox* cameraSerialCombo_ {nullptr};
    QComboBox* objectiveCombo_ {nullptr};
    QLineEdit* jogStepEdit_ {nullptr};
    QLineEdit* absXEdit_ {nullptr};
    QLineEdit* absYEdit_ {nullptr};
    QLineEdit* cameraExposureEdit_ {nullptr};
    QLineEdit* cameraGainEdit_ {nullptr};
    QLabel* xPositionValueLabel_ {nullptr};
    QLabel* yPositionValueLabel_ {nullptr};
    QLabel* laserPointLabel_ {nullptr};
    QLabel* cameraZoomLabel_ {nullptr};
    CameraPreviewWidget* cameraPreviewWidget_ {nullptr};
    QPoint laserPointPx_ {2588, 1350};
    int laserRadiusPx_ {32};

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
    QPushButton* moveAbsXButton_ {nullptr};
    QPushButton* moveAbsYButton_ {nullptr};
};

}  // namespace laserbench::ui
