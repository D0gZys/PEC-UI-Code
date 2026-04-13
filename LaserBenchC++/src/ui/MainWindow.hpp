#pragma once

#include <QImage>
#include <QMainWindow>
#include <QPoint>
#include <QSize>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
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
class QGroupBox;
class QLabel;
class QLineEdit;
class QObject;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTimer;
QT_END_NAMESPACE

namespace laserbench::hardware {
class ICameraController;
class BioLogicController;
}

namespace laserbench::ui {

class CameraPreviewWidget;
class PotentiostatGraphWidget;
class PotentiostatHeatmapWidget;
class Potentiostat3DWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    // ── Scan configuration (set via dialog when zone is drawn) ────────────────
    struct ScanConfig {
        enum class AcquisitionMode { PointByPoint, Continuous };
        enum class ContinuousTrigger { Distance, Time };
        enum class RectangleStartCorner { TopLeft, TopRight, BottomLeft, BottomRight };
        enum class RectanglePrimaryAxis { Horizontal, Vertical };

        AcquisitionMode   mode             {AcquisitionMode::PointByPoint};
        // Point par point
        double            stepMm           {0.05};
        double            dwellS           {0.5};
        int               dwellSamples     {1};
        // Balayage continu
        double            scanSpeedMmPerS  {1.0};
        ContinuousTrigger trigger          {ContinuousTrigger::Distance};
        double            triggerDistanceMm{0.1};
        double            triggerTimeS     {1.0};
        double            rowStepMm        {0.05};
        RectangleStartCorner rectangleStartCorner {RectangleStartCorner::TopLeft};
        RectanglePrimaryAxis rectanglePrimaryAxis {RectanglePrimaryAxis::Horizontal};
        bool              rectangleTraversalExplicit {false};
    };

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
    QWidget* buildPotentiostatTab();
    QWidget* buildMeasureTab();
    QWidget* buildImportTab();
    void refreshImportVisualization();
    void showImportCellDetailDialog(int row, int col);
    void openStartupConnectionDialog();
    void openMotorConnectionDialog();
    void openCameraConnectionDialog();
    void openCameraSettingsDialog();
    void openCalibrationDialog();
    void initializeSessionLog();
    void scheduleRuntimeDependencyCheck();
    void runRuntimeDependencyCheck();
    [[nodiscard]] QStringList collectRuntimeDependencyIssues() const;
    void applyObjectivePreset();
    void syncLaserOverlay(const QSize& frameSize = QSize());
    void updateLaserLabel();
    void updatePreviewCursor();
    void setGotoArmed(bool armed);
    void setSequenceSelectArmed(bool armed);
    void loadCalibrationPresets();
    void saveCalibrationPresets();
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
    void initializeMeasurementLog(const QString& fileStem, const QStringList& headerLines);
    void appendMeasurementLog(const QString& message);
    void appendMeasurementLogEvent(const QString& category, const QString& message);
    void finalizeMeasurementLog(const QString& outcome);
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

    void onConnectPotentiostat();
    void onStartCaPotentiostat();
    void onStopCaPotentiostat();
    void onExportPotentiostat();
    void onImportCsv();
    void onDisconnectPotentiostat();
    void onLoadFirmware();
    void syncPotentiostatTechniqueUi();

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
    void resetPotentiostatVisualization(int rows, int cols, const std::vector<std::pair<int, int>>& order);
    void appendPotentiostatVisualizationSample(
        int index,
        int total,
        int row,
        int col,
        const QPointF& waypointMm,
        double elapsedTime,
        double ewe,
        double current,
        std::vector<double> cellCurrentSamples = {},
        std::vector<double> cellEweSamples = {}
    );
    void clearPotentiostatVisualization();
    void refreshPotentiostatVisualization();
    void showCellDetailDialog(int row, int col);

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
    void recordMotorPositionSample(
        const QPointF& positionMm,
        std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());
    std::optional<QPointF> estimateMotorPositionAt(std::chrono::steady_clock::time_point timestamp) const;
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

    struct TimedMotorPositionSample
    {
        std::chrono::steady_clock::time_point timestamp {};
        QPointF positionMm;
    };

    core::RuntimeSnapshot snapshot_;
    std::shared_ptr<hardware::NewportConexController> motorController_;
    std::unique_ptr<hardware::ICameraController> cameraController_;
    std::shared_ptr<hardware::BioLogicController> potentiostatController_;

    QTimer* motorPollTimer_ {nullptr};
    QTimer* cameraPollTimer_ {nullptr};
    QTimer* keyJogTimer_ {nullptr};
    std::thread motorPollThread_;
    std::thread cameraPollThread_;
    std::thread sequenceThread_;
    std::thread potentiostatThread_;
    std::atomic_bool potentiostatBusy_ {false};
    std::atomic_bool potentiostatStopRequested_ {false};
    std::atomic_bool motorPollStopRequested_ {false};
    std::atomic_bool cameraPollStopRequested_ {false};
    std::atomic_bool motorUiUpdatePending_ {false};
    std::atomic_bool cameraUiUpdatePending_ {false};
    std::atomic_bool sequenceStopRequested_ {false};
    mutable std::mutex motorSnapshotMutex_;
    std::mutex cameraSnapshotMutex_;
    std::mutex logMutex_;
    mutable std::mutex predictedMotionMutex_;
    hardware::MotorAxisSnapshot polledXSnapshot_ {hardware::AxisId::X};
    hardware::MotorAxisSnapshot polledYSnapshot_ {hardware::AxisId::Y};
    QImage polledCameraFrame_;
    bool motorSnapshotsReady_ {false};
    bool cameraFrameReady_ {false};
    QString pendingMotorPollError_;
    QString pendingCameraPollError_;
    QString sessionLogPath_;
    QString measurementLogPath_;
    bool measurementLogActive_ {false};
    bool motorTaskRunning_ {false};
    bool sequenceRunning_ {false};
    bool gotoArmed_ {false};
    bool sequenceSelectArmed_ {false};
    bool rulerArmed_ {false};
    bool rulerHasP1_ {false};
    bool rulerHasP2_ {false};
    QPointF rulerP1Px_;
    QPointF rulerP2Px_;
    bool circleArmed_ {false};
    bool circleHasCenter_ {false};
    bool circleHasEdge_ {false};
    QPointF circleCenterPx_;
    QPointF circleEdgePx_;
    bool rectArmed_ {false};
    bool rectHasP1_ {false};
    bool rectHasP2_ {false};
    QPointF rectP1Px_;
    QPointF rectP2Px_;
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
    QLabel* mouseCoordsLabel_ {nullptr};
    QLabel* cameraPageStatusLabel_ {nullptr};
    QComboBox*   potentiostatTechniqueCombo_    {nullptr};
    QStackedWidget* potentiostatTechniqueStack_ {nullptr};
    QLineEdit*   potentiostatDllPathEdit_      {nullptr};
    QLineEdit*   potentiostatAddressEdit_      {nullptr};
    QComboBox*   potentiostatChannelCombo_     {nullptr};
    QLineEdit*   potentiostatVoltageEdit_      {nullptr};
    QComboBox*   potentiostatCurrentRangeCombo_ {nullptr};
    QComboBox*   potentiostatVsCombo_          {nullptr};
    QComboBox*   potentiostatErangeCombo_      {nullptr};
    QComboBox*   potentiostatBandwidthCombo_   {nullptr};
    QLabel*      potentiostatMeasureStateLabel_ {nullptr};
    QLabel*      potentiostatCurrentLabel_     {nullptr};
    QLabel*      potentiostatPointCountLabel_  {nullptr};
    QLabel*      potentiostatProgressLabel_    {nullptr};
    QLineEdit*   potentiostatNbCyclesEdit_     {nullptr};
    QLineEdit*   potentiostatOcvRestHoursEdit_   {nullptr};
    QLineEdit*   potentiostatOcvRestMinutesEdit_ {nullptr};
    QLineEdit*   potentiostatOcvRestSecondsEdit_ {nullptr};
    QLineEdit*   potentiostatOcvRecordDEEdit_    {nullptr};
    QLineEdit*   potentiostatOcvRecordDtEdit_    {nullptr};
    QComboBox*   potentiostatOcvErangeCombo_     {nullptr};
    QLineEdit*   potentiostatCvaEiEdit_          {nullptr};
    QComboBox*   potentiostatCvaEiVsCombo_       {nullptr};
    QLineEdit*   potentiostatCvaTiHoursEdit_     {nullptr};
    QLineEdit*   potentiostatCvaTiMinutesEdit_   {nullptr};
    QLineEdit*   potentiostatCvaTiSecondsEdit_   {nullptr};
    QLineEdit*   potentiostatCvaDtiEdit_         {nullptr};
    QLineEdit*   potentiostatCvaScanRateEdit_    {nullptr};
    QComboBox*   potentiostatCvaScanRateUnitCombo_ {nullptr};
    QLineEdit*   potentiostatCvaE1Edit_          {nullptr};
    QComboBox*   potentiostatCvaE1VsCombo_       {nullptr};
    QLineEdit*   potentiostatCvaT1HoursEdit_     {nullptr};
    QLineEdit*   potentiostatCvaT1MinutesEdit_   {nullptr};
    QLineEdit*   potentiostatCvaT1SecondsEdit_   {nullptr};
    QLineEdit*   potentiostatCvaDt1Edit_         {nullptr};
    QLineEdit*   potentiostatCvaE2Edit_          {nullptr};
    QComboBox*   potentiostatCvaE2VsCombo_       {nullptr};
    QLineEdit*   potentiostatCvaT2HoursEdit_     {nullptr};
    QLineEdit*   potentiostatCvaT2MinutesEdit_   {nullptr};
    QLineEdit*   potentiostatCvaT2SecondsEdit_   {nullptr};
    QLineEdit*   potentiostatCvaDt2Edit_         {nullptr};
    QLineEdit*   potentiostatCvaMeasurePercentEdit_ {nullptr};
    QLineEdit*   potentiostatCvaAverageNStepsEdit_ {nullptr};
    QLineEdit*   potentiostatCvaRepeatCyclesEdit_ {nullptr};
    QComboBox*   potentiostatCvaErangeCombo_     {nullptr};
    QComboBox*   potentiostatCvaCurrentRangeCombo_ {nullptr};
    QComboBox*   potentiostatCvaBandwidthCombo_  {nullptr};
    QCheckBox*   potentiostatCvaEndScanCheck_    {nullptr};
    QLineEdit*   potentiostatCvaEfEdit_          {nullptr};
    QComboBox*   potentiostatCvaEfVsCombo_       {nullptr};
    QLineEdit*   potentiostatCvaTfHoursEdit_     {nullptr};
    QLineEdit*   potentiostatCvaTfMinutesEdit_   {nullptr};
    QLineEdit*   potentiostatCvaTfSecondsEdit_   {nullptr};
    QLineEdit*   potentiostatCvaDtfEdit_         {nullptr};
    QComboBox*   potentiostatGraphTypeCombo_   {nullptr};
    QPushButton* colorGraphButton_             {nullptr};
    QPushButton* potentiostatConnectButton_    {nullptr};
    QPushButton* potentiostatDisconnectButton_ {nullptr};
    QPushButton* potentiostatFirmwareButton_   {nullptr};
    QLabel*      potentiostatStatusLabel_      {nullptr};
    QPushButton* potentiostatRunButton_        {nullptr};
    QPushButton* potentiostatStopButton_       {nullptr};
    QPushButton* potentiostatExportButton_     {nullptr};
    QGroupBox*   potentiostatGraphBox_         {nullptr};
    QGroupBox*   potentiostatMapBox_           {nullptr};
    PotentiostatGraphWidget*  potentiostatGraphWidget_  {nullptr};
    PotentiostatHeatmapWidget* potentiostatHeatmapWidget_ {nullptr};
    Potentiostat3DWidget*     potentiostat3DWidget_     {nullptr};
    QStackedWidget*           measureRightStack_        {nullptr};
    QPushButton*              view3DButton_             {nullptr};

    // Import tab widgets
    QPushButton*              importButton_             {nullptr};
    QComboBox*                importGraphTypeCombo_     {nullptr};
    QGroupBox*                importGraphBox_           {nullptr};
    QGroupBox*                importMapBox_             {nullptr};
    PotentiostatGraphWidget*  importGraphWidget_        {nullptr};
    PotentiostatHeatmapWidget* importHeatmapWidget_     {nullptr};
    Potentiostat3DWidget*     import3DWidget_           {nullptr};
    QStackedWidget*           importRightStack_         {nullptr};
    QPushButton*              importView3DButton_       {nullptr};
    QLabel*                   importInfoLabel_          {nullptr};
    QPlainTextEdit* logView_ {nullptr};
    QDialog* startupConnectionDialog_ {nullptr};
    QDialog* motorConnectionDialog_ {nullptr};
    QDialog* cameraConnectionDialog_ {nullptr};
    QDialog* cameraSettingsDialog_ {nullptr};
    QDialog*   calibrationDialog_     {nullptr};
    QComboBox* calibObjectiveCombo_   {nullptr};

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
    std::vector<double> potentiostatPlotTimes_;
    std::vector<double> potentiostatPlotCurrents_;
    std::vector<double> potentiostatPlotEwe_;
    std::vector<std::optional<double>> potentiostatMatrix_;
    std::vector<std::vector<double>> potentiostatCellCurrentSamples_;
    std::vector<std::vector<double>> potentiostatCellEweSamples_;
    std::vector<std::optional<double>> potentiostatEweMatrix_;
    std::vector<QPointF> potentiostatCellPositions_;
    std::vector<double> potentiostatCellTimes_;
    double potentiostatLastDwellS_ {0.0};
    std::vector<std::pair<int, int>> potentiostatScanOrder_;
    int potentiostatRows_ {0};
    int potentiostatCols_ {0};
    int potentiostatSampleCount_ {0};
    std::pair<int,int> potentiostatLastSampledCell_ {0, 0};
    double potentiostatXMin_ {0.0};
    double potentiostatXMax_ {0.0};
    double potentiostatYMin_ {0.0};
    double potentiostatYMax_ {0.0};

    // Import tab data
    std::vector<double> importPlotTimes_;
    std::vector<double> importPlotCurrents_;
    std::vector<double> importPlotEwe_;
    std::vector<std::optional<double>> importMatrix_;
    std::vector<std::optional<double>> importEweMatrix_;
    std::vector<std::vector<double>> importCellCurrentSamples_;
    std::vector<std::vector<double>> importCellEweSamples_;
    std::vector<QPointF> importCellPositions_;
    std::vector<double> importCellTimes_;
    int importRows_ {0};
    int importCols_ {0};
    double importXMin_ {0.0};
    double importXMax_ {0.0};
    double importYMin_ {0.0};
    double importYMax_ {0.0};
    double importLastDwellS_ {0.0};
    int currentWaypointIndex_ {-1};
    std::optional<QPointF> cachedMotorMm_;
    std::deque<TimedMotorPositionSample> motorPositionHistory_;
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
    QPushButton* cameraPageLiveButton_ {nullptr};
    QPushButton* cameraPageStopButton_ {nullptr};
    QPushButton* applyCameraSettingsButton_ {nullptr};
    QPushButton* startCameraLiveButton_ {nullptr};
    QPushButton* stopCameraLiveButton_ {nullptr};
    QPushButton* moveAbsXYButton_ {nullptr};
    QPushButton* gotoButton_ {nullptr};
    QPushButton* rulerButton_ {nullptr};
    QLabel*      rulerDistanceLabel_ {nullptr};
    QPushButton* circleButton_ {nullptr};
    QLabel*      circleDiameterLabel_ {nullptr};
    QPushButton* rectButton_ {nullptr};
    QLabel*      rectSizeLabel_ {nullptr};
    QPushButton* sequenceSetStartButton_ {nullptr};
    QPushButton* sequenceSetEndButton_ {nullptr};
    QPushButton* sequencePickButton_ {nullptr};
    QPushButton* sequenceRunButton_ {nullptr};
    QPushButton* sequenceStopButton_ {nullptr};
    QPushButton* captureButton_      {nullptr};
    QLabel*      captureDeltaLabel_  {nullptr};
    std::optional<QPointF> capturedMotorPos_;

    void onToggleRuler();
    void updateRulerOverlay();
    [[nodiscard]] QString computeRulerDistanceText() const;
    void onToggleCircle();
    void updateCircleOverlay();
    [[nodiscard]] QString computeCircleDiameterText() const;
    void onToggleRect();
    void updateRectOverlay();
    [[nodiscard]] QString computeRectSizeText() const;
    void disarmAllMeasureTools();
    void onPreviewFrameDoubleClicked(const QPoint& framePointPx);
    void onCapturePosition();
    void showScanConfigDialog();
    [[nodiscard]] std::optional<std::pair<ScanConfig::RectangleStartCorner, ScanConfig::RectanglePrimaryAxis>> promptRectangleTraversalSelection();
    [[nodiscard]] ScanConfig::RectangleStartCorner effectiveRectangleStartCorner() const;
    [[nodiscard]] ScanConfig::RectanglePrimaryAxis effectiveRectanglePrimaryAxis() const;
    [[nodiscard]] std::vector<std::pair<int, int>> buildRectangleTraversalOrder(int rows, int cols) const;
    [[nodiscard]] QString selectedPotentiostatTechniqueLabel() const;

    enum class PotentiostatTechnique
    {
        CA,
        OCV,
        CVA
    };

    [[nodiscard]] PotentiostatTechnique selectedPotentiostatTechnique() const;

    ScanConfig scanConfig_;
};

}  // namespace laserbench::ui
