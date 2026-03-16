#include "ui/MainWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <thread>

#include "ui/CameraPreviewWidget.hpp"
#include "hardware/MockHardware.hpp"
#include "hardware/ThorlabsCameraController.hpp"

namespace laserbench::ui {

namespace {

constexpr double kCameraPixelPitchUm = 3.45;
constexpr double kDefaultGotoMmPerPx = 0.001;
constexpr int kDefaultMotorTimeoutMs = 30000;
constexpr double kOverlayPredictionEpsilonMm = 0.002;
constexpr double kOverlayStabilityDeadbandMm = 0.00075;

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

QString formatMm(double value)
{
    return QString::number(value, 'f', 3) + " mm";
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

constexpr std::array<ObjectivePreset, 3> kObjectivePresets {{
    {"4x", 4.0, 2588, 1350, 32},
    {"10x", 10.0, 2588, 1350, 32},
    {"50x", 50.0, 2588, 1350, 32},
}};

const ObjectivePreset* findObjectivePreset(const QString& name)
{
    for (const ObjectivePreset& preset : kObjectivePresets) {
        if (name == QLatin1String(preset.name)) {
            return &preset;
        }
    }
    return nullptr;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , snapshot_(core::makeDefaultSnapshot())
    , motorController_(std::make_shared<hardware::NewportConexController>())
    , cameraController_(std::make_unique<hardware::ThorlabsCameraController>())
    , potentiostatController_(std::make_unique<hardware::MockPotentiostatController>())
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
        "QPushButton[accent='true'] { background:#1f6feb; border-color:#1f6feb; color:#ffffff; }"
        "QPushButton[accent='true']:hover { background:#1b63d3; border-color:#1b63d3; }"
        "QPushButton[accent='true']:pressed { background:#195cc4; }"
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
}

MainWindow::~MainWindow()
{
    sequenceStopRequested_.store(true);
    if (sequenceThread_.joinable()) {
        sequenceThread_.join();
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

        if (key == Qt::Key_Left) startContinuousJog(hardware::AxisId::X, -1);
        if (key == Qt::Key_Right) startContinuousJog(hardware::AxisId::X, +1);
        if (key == Qt::Key_Up) startContinuousJog(hardware::AxisId::Y, +1);
        if (key == Qt::Key_Down) startContinuousJog(hardware::AxisId::Y, -1);
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
    auto* aboutAction = helpMenu->addAction("A propos");
    connect(calibrationAction, &QAction::triggered, this, &MainWindow::openCalibrationDialog);
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

    auto* verticalSplitter = new QSplitter(Qt::Vertical);
    verticalSplitter->setChildrenCollapsible(false);

    tabWidget_ = new QTabWidget;
    tabWidget_->addTab(buildSetupTab(), "Parametrage");
    tabWidget_->addTab(buildMeasureTab(), "Mesure");
    verticalSplitter->addWidget(tabWidget_);

    auto* journalBox = createGroupBox("Journal");
    auto* journalLayout = new QVBoxLayout(journalBox);
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(600);
    journalLayout->addWidget(logView_);
    verticalSplitter->addWidget(journalBox);
    verticalSplitter->setStretchFactor(0, 1);
    verticalSplitter->setStretchFactor(1, 0);
    verticalSplitter->setSizes({760, 190});

    mainLayout->addWidget(verticalSplitter, 1);

    setCentralWidget(central);

    stageSummaryLabel_ = new QLabel;
    cameraSummaryLabel_ = new QLabel;
    potentiostatSummaryLabel_ = new QLabel;

    statusBar()->addPermanentWidget(stageSummaryLabel_, 1);
    statusBar()->addPermanentWidget(cameraSummaryLabel_, 1);
    statusBar()->addPermanentWidget(potentiostatSummaryLabel_, 1);
    statusBar()->showMessage("Pret");
}

QWidget* MainWindow::buildSetupTab()
{
    auto* page = new QWidget;
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto* splitter = new QSplitter(Qt::Horizontal);
    layout->addWidget(splitter, 1);

    auto* leftPanel = new QWidget;
    leftPanel->setMinimumWidth(430);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    auto* motorsBox = createGroupBox("Controle moteur");
    auto* motorsLayout = new QGridLayout(motorsBox);

    jogStepEdit_ = new QLineEdit("0.500");
    motorsLayout->addWidget(new QLabel("Vitesse jog (mm/s)"), 0, 0);
    motorsLayout->addWidget(jogStepEdit_, 0, 1);
    motorsLayout->addWidget(new QLabel("Position X"), 1, 0);
    xPositionValueLabel_ = new QLabel("-");
    xPositionValueLabel_->setStyleSheet("font-size:18px; font-weight:600; color:#111927;");
    motorsLayout->addWidget(xPositionValueLabel_, 1, 1);
    motorsLayout->addWidget(new QLabel("Position Y"), 1, 2);
    yPositionValueLabel_ = new QLabel("-");
    yPositionValueLabel_->setStyleSheet("font-size:18px; font-weight:600; color:#111927;");
    motorsLayout->addWidget(yPositionValueLabel_, 1, 3);

    motorsLayout->addWidget(new QLabel("Absolu X (mm)"), 2, 0);
    absXEdit_ = new QLineEdit("0.000");
    motorsLayout->addWidget(absXEdit_, 2, 1);
    motorsLayout->addWidget(new QLabel("Absolu Y (mm)"), 2, 2);
    absYEdit_ = new QLineEdit("0.000");
    motorsLayout->addWidget(absYEdit_, 2, 3);
    moveAbsXYButton_ = createActionButton("Aller XY");
    motorsLayout->addWidget(moveAbsXYButton_, 3, 0, 1, 4);

    connect(moveAbsXYButton_, &QPushButton::clicked, this, &MainWindow::onMoveAbsoluteBoth);

    leftLayout->addWidget(motorsBox);

    auto* laserBox = createGroupBox("Objectif / Laser");
    auto* laserLayout = new QGridLayout(laserBox);
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

    connect(objectiveCombo_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        applyObjectivePreset();
    });
    connect(applyObjectiveButton, &QPushButton::clicked, this, &MainWindow::applyObjectivePreset);
    connect(gotoButton_, &QPushButton::clicked, this, &MainWindow::onArmGoto);

    leftLayout->addWidget(laserBox);

    auto* sequenceBox = createGroupBox("Sequence balayage");
    auto* sequenceLayout = new QGridLayout(sequenceBox);
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

    sequenceSetStartButton_ = createActionButton("Set Depart");
    sequenceSetEndButton_ = createActionButton("Set Arrivee");
    sequenceLayout->addWidget(sequenceSetStartButton_, 2, 0, 1, 2);
    sequenceLayout->addWidget(sequenceSetEndButton_, 2, 2, 1, 2);

    sequenceRunButton_ = createActionButton("Lancer");
    sequenceRunButton_->setProperty("accent", true);
    sequenceStopButton_ = createActionButton("Stop");
    sequenceLayout->addWidget(sequenceRunButton_, 3, 0, 1, 2);
    sequenceLayout->addWidget(sequenceStopButton_, 3, 2, 1, 2);

    sequencePickButton_ = createActionButton("Zone image");
    sequencePickButton_->setProperty("accent", true);
    sequenceLayout->addWidget(sequencePickButton_, 4, 0, 1, 2);
    auto* zoneCheckWidget = new QWidget;
    auto* zoneCheckLayout = new QVBoxLayout(zoneCheckWidget);
    zoneCheckLayout->setContentsMargins(0, 0, 0, 0);
    zoneCheckLayout->setSpacing(2);
    showZoneCheck_ = new QCheckBox("Zone visible");
    showZoneCheck_->setChecked(true);
    showZoneCheck_->setStyleSheet("font-size:9pt;");
    hideWaypointsCheck_ = new QCheckBox("Masquer points");
    hideWaypointsCheck_->setStyleSheet("font-size:9pt;");
    zoneCheckLayout->addWidget(showZoneCheck_);
    zoneCheckLayout->addWidget(hideWaypointsCheck_);
    sequenceLayout->addWidget(zoneCheckWidget, 4, 2, 1, 2);

    sequenceStatusLabel_ = new QLabel;
    sequenceStatusLabel_->setStyleSheet("color:#1f6feb; font-size:9pt;");
    sequenceLayout->addWidget(sequenceStatusLabel_, 5, 0, 1, 4);

    connect(sequenceSetStartButton_, &QPushButton::clicked, this, &MainWindow::onSetSequenceStart);
    connect(sequenceSetEndButton_, &QPushButton::clicked, this, &MainWindow::onSetSequenceEnd);
    connect(sequenceRunButton_, &QPushButton::clicked, this, &MainWindow::onRunSequence);
    connect(sequenceStopButton_, &QPushButton::clicked, this, &MainWindow::onStopSequence);
    connect(sequencePickButton_, &QPushButton::clicked, this, &MainWindow::onArmSequenceRectangle);

    leftLayout->addWidget(sequenceBox);
    leftLayout->addStretch();

    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

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
    applyObjectivePreset();

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({430, 1020});

    return page;
}

void MainWindow::openMotorConnectionDialog()
{
    if (motorConnectionDialog_ == nullptr) {
        motorConnectionDialog_ = new QDialog(this);
        motorConnectionDialog_->setObjectName("motorConnectionDialog");
        motorConnectionDialog_->setWindowTitle("Connexion moteurs");
        motorConnectionDialog_->setModal(true);
        motorConnectionDialog_->setMinimumWidth(620);

        auto* layout = new QVBoxLayout(motorConnectionDialog_);
        layout->setContentsMargins(18, 18, 18, 18);
        layout->setSpacing(10);

        auto* box = createGroupBox("Connexion Newport CONEX-CC");
        auto* grid = new QGridLayout(box);
        grid->addWidget(new QLabel("Port X"), 0, 0);
        xPortCombo_ = new QComboBox;
        xPortCombo_->setEditable(true);
        grid->addWidget(xPortCombo_, 0, 1);
        grid->addWidget(new QLabel("Port Y"), 0, 2);
        yPortCombo_ = new QComboBox;
        yPortCombo_->setEditable(true);
        grid->addWidget(yPortCombo_, 0, 3);

        scanPortsButton_ = createActionButton("Chercher COM");
        connectAxesButton_ = createActionButton("Connecter");
        homeAxesButton_ = createActionButton("Initialiser / Home");
        disconnectAxesButton_ = createActionButton("Deconnecter");
        scanPortsButton_->setProperty("accent", true);
        connectAxesButton_->setProperty("accent", true);

        grid->addWidget(scanPortsButton_, 1, 0);
        grid->addWidget(connectAxesButton_, 1, 1);
        grid->addWidget(homeAxesButton_, 1, 2);
        grid->addWidget(disconnectAxesButton_, 1, 3);

        auto* dependencyInfo = new QLabel(
            "La dependance Newport est chargee automatiquement depuis le depot. Aucun chemin DLL manuel n'est requis."
        );
        dependencyInfo->setWordWrap(true);
        dependencyInfo->setStyleSheet("color:#5c6570; font-size:9pt;");
        grid->addWidget(dependencyInfo, 2, 0, 1, 4);

        layout->addWidget(box);

        connect(scanPortsButton_, &QPushButton::clicked, this, &MainWindow::onScanPorts);
        connect(connectAxesButton_, &QPushButton::clicked, this, &MainWindow::onConnectAxes);
        connect(homeAxesButton_, &QPushButton::clicked, this, &MainWindow::onHomeAxes);
        connect(disconnectAxesButton_, &QPushButton::clicked, this, &MainWindow::onDisconnectAxes);
    }

    refreshMotorUi();
    motorConnectionDialog_->adjustSize();
    motorConnectionDialog_->show();
    motorConnectionDialog_->raise();
    motorConnectionDialog_->activateWindow();
}

void MainWindow::openCameraConnectionDialog()
{
    if (cameraConnectionDialog_ == nullptr) {
        cameraConnectionDialog_ = new QDialog(this);
        cameraConnectionDialog_->setWindowTitle("Connexion camera");
        cameraConnectionDialog_->setModal(true);
        cameraConnectionDialog_->setMinimumWidth(520);

        auto* layout = new QVBoxLayout(cameraConnectionDialog_);
        layout->setContentsMargins(18, 18, 18, 18);
        layout->setSpacing(10);

        auto* box = createGroupBox("Camera Thorlabs");
        auto* grid = new QGridLayout(box);
        grid->addWidget(new QLabel("Camera"), 0, 0);
        cameraSerialCombo_ = new QComboBox;
        cameraSerialCombo_->setEditable(false);
        grid->addWidget(cameraSerialCombo_, 0, 1, 1, 3);

        scanCameraButton_ = createActionButton("Chercher");
        connectCameraButton_ = createActionButton("Connecter");
        disconnectCameraButton_ = createActionButton("Deconnecter");
        scanCameraButton_->setProperty("accent", true);
        connectCameraButton_->setProperty("accent", true);
        grid->addWidget(scanCameraButton_, 1, 0);
        grid->addWidget(connectCameraButton_, 1, 1);
        grid->addWidget(disconnectCameraButton_, 1, 2);

        layout->addWidget(box);

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
    }

    if (scanCameraButton_ != nullptr) {
        scanCameraButton_->click();
    }
    refreshCameraUi();
    cameraConnectionDialog_->adjustSize();
    cameraConnectionDialog_->show();
    cameraConnectionDialog_->raise();
    cameraConnectionDialog_->activateWindow();
}

void MainWindow::openCameraSettingsDialog()
{
    if (cameraSettingsDialog_ == nullptr) {
        cameraSettingsDialog_ = new QDialog(this);
        cameraSettingsDialog_->setWindowTitle("Parametres camera");
        cameraSettingsDialog_->setModal(false);
        cameraSettingsDialog_->setMinimumWidth(420);

        auto* layout = new QVBoxLayout(cameraSettingsDialog_);
        layout->setContentsMargins(18, 18, 18, 18);
        layout->setSpacing(10);

        auto* box = createGroupBox("Camera");
        auto* grid = new QGridLayout(box);
        grid->addWidget(new QLabel("Exposure Time (us)"), 0, 0);
        cameraExposureEdit_ = new QLineEdit(QString::number(cameraController_->exposureTimeUs(), 'f', 0));
        grid->addWidget(cameraExposureEdit_, 0, 1);
        grid->addWidget(new QLabel("Gain"), 1, 0);
        cameraGainEdit_ = new QLineEdit(QString::number(cameraController_->gain(), 'f', 1));
        grid->addWidget(cameraGainEdit_, 1, 1);

        applyCameraSettingsButton_ = createActionButton("Appliquer");
        applyCameraSettingsButton_->setProperty("accent", true);
        grid->addWidget(applyCameraSettingsButton_, 2, 0, 1, 2);
        layout->addWidget(box);

        connect(applyCameraSettingsButton_, &QPushButton::clicked, this, &MainWindow::applyCameraSettings);
        connect(cameraExposureEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyCameraSettings);
        connect(cameraGainEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyCameraSettings);
    }

    if (cameraExposureEdit_ != nullptr && !cameraExposureEdit_->hasFocus()) {
        cameraExposureEdit_->setText(QString::number(cameraController_->exposureTimeUs(), 'f', 0));
    }
    if (cameraGainEdit_ != nullptr && !cameraGainEdit_->hasFocus()) {
        cameraGainEdit_->setText(QString::number(cameraController_->gain(), 'f', 1));
    }

    cameraSettingsDialog_->adjustSize();
    cameraSettingsDialog_->show();
    cameraSettingsDialog_->raise();
    cameraSettingsDialog_->activateWindow();
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
        laserLayout->addWidget(new QLabel("Pas (px)"), 0, 0);
        laserMoveStepEdit_ = new QLineEdit("10");
        laserLayout->addWidget(laserMoveStepEdit_, 0, 1);

        auto* moveXMinusButton = createActionButton("X -");
        auto* moveXPlusButton = createActionButton("X +");
        auto* moveYMinusButton = createActionButton("Y -");
        auto* moveYPlusButton = createActionButton("Y +");
        auto* sizeMinusButton = createActionButton("Taille -");
        auto* sizePlusButton = createActionButton("Taille +");
        laserLayout->addWidget(moveXMinusButton, 1, 0);
        laserLayout->addWidget(moveXPlusButton, 1, 1);
        laserLayout->addWidget(moveYMinusButton, 1, 2);
        laserLayout->addWidget(moveYPlusButton, 1, 3);
        laserLayout->addWidget(sizeMinusButton, 2, 0, 1, 2);
        laserLayout->addWidget(sizePlusButton, 2, 2, 1, 2);

        laserLayout->addWidget(new QLabel("X cible"), 3, 0);
        laserXEdit_ = new QLineEdit(QString::number(laserPointPx_.x()));
        laserLayout->addWidget(laserXEdit_, 3, 1);
        laserLayout->addWidget(new QLabel("Y cible"), 3, 2);
        laserYEdit_ = new QLineEdit(QString::number(laserPointPx_.y()));
        laserLayout->addWidget(laserYEdit_, 3, 3);

        laserLayout->addWidget(new QLabel("Taille"), 4, 0);
        laserSizeEdit_ = new QLineEdit(QString::number(laserRadiusPx_));
        laserLayout->addWidget(laserSizeEdit_, 4, 1);
        auto* applyLaserButton = createActionButton("Appliquer cible");
        applyLaserButton->setProperty("accent", true);
        laserLayout->addWidget(applyLaserButton, 4, 2, 1, 2);

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

    cameraPreviewWidget_->setCursor((gotoArmed_ || sequenceSelectArmed_) ? Qt::CrossCursor : Qt::ArrowCursor);
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

    laserPointPx_.setX(targetX);
    laserPointPx_.setY(targetY);
    laserRadiusPx_ = std::max(targetSize, 1);
    syncLaserOverlay();
    appendLog(QString("Cible laser calibree: X=%1 Y=%2 Taille=%3").arg(laserPointPx_.x()).arg(laserPointPx_.y()).arg(laserRadiusPx_));
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

    const int cols = std::max(1, static_cast<int>(std::lround((xMax - xMin) / stepMm))) + 1;
    const int rows = std::max(1, static_cast<int>(std::lround((yMax - yMin) / stepMm))) + 1;

    std::vector<double> xRange;
    xRange.reserve(static_cast<std::size_t>(cols));
    for (int i = 0; i < cols; ++i) {
        const double t = cols <= 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(cols - 1);
        xRange.push_back(xMin + ((xMax - xMin) * t));
    }

    std::vector<double> yRange;
    yRange.reserve(static_cast<std::size_t>(rows));
    for (int j = 0; j < rows; ++j) {
        const double t = rows <= 1 ? 0.0 : static_cast<double>(j) / static_cast<double>(rows - 1);
        yRange.push_back(yMin + ((yMax - yMin) * t));
    }

    std::vector<QPointF> waypoints;
    waypoints.reserve(static_cast<std::size_t>(rows * cols));
    for (int row = 0; row < rows; ++row) {
        const double y = yRange[static_cast<std::size_t>(row)];
        if ((row % 2) == 0) {
            for (double x : xRange) {
                waypoints.emplace_back(x, y);
            }
        } else {
            for (auto it = xRange.rbegin(); it != xRange.rend(); ++it) {
                waypoints.emplace_back(*it, y);
            }
        }
    }

    return waypoints;
}

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
        QString("Sequence %1 : (%2,%3) -> (%4,%5) | %6 points | pas=%7 mm | %8 s/pt")
            .arg(mode == "Rectangle" ? "rectangle (serpentin)" : "lineaire")
            .arg(startMm.x(), 0, 'f', 4)
            .arg(startMm.y(), 0, 'f', 4)
            .arg(endMm.x(), 0, 'f', 4)
            .arg(endMm.y(), 0, 'f', 4)
            .arg(total)
            .arg(stepMm, 0, 'f', 4)
            .arg(durationS, 0, 'f', 3)
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
            QMetaObject::invokeMethod(this, [this, firstPoint]() {
                if (sequenceStatusLabel_ != nullptr) {
                    sequenceStatusLabel_->setText("Mise en position...");
                }
                appendLog(QString("Mise en position vers depart (%1, %2) mm...").arg(firstPoint.x(), 0, 'f', 4).arg(firstPoint.y(), 0, 'f', 4));
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

            QMetaObject::invokeMethod(this, [this]() {
                appendLog("Point de depart atteint. Debut du balayage.");
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
                if (stepBoth.x.positionValid && stepBoth.y.positionValid) {
                    startPredictedMotorMotion(
                        stepBoth.x.positionMm,
                        stepBoth.y.positionMm,
                        waypoint.x(),
                        waypoint.y(),
                        stageSpeedMmPerS,
                        stageSpeedMmPerS
                    );
                }

                controller->moveAbsoluteNoWait(hardware::AxisId::X, waypoint.x());
                controller->moveAbsoluteNoWait(hardware::AxisId::Y, waypoint.y());
                controller->waitAxis(hardware::AxisId::X, kDefaultMotorTimeoutMs);
                controller->waitAxis(hardware::AxisId::Y, kDefaultMotorTimeoutMs);
                stopPredictedMotorMotion(waypoint.x(), waypoint.y());

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

            QMetaObject::invokeMethod(this, [this, total]() {
                currentWaypointIndex_ = total;
                if (sequenceStatusLabel_ != nullptr) {
                    sequenceStatusLabel_->setText(QString("Termine (%1 points).").arg(total));
                }
                appendLog("Sequence terminee.");
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

QWidget* MainWindow::buildMeasureTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto* placeholder = createPlaceholderPanel(
        "Mesure et cartographie",
        "Cet onglet recevra ensuite l'acquisition potentiostat, la grille de balayage, le graphe temps / courant et la carte 2D de l'echantillon."
    );
    layout->addWidget(placeholder, 1);

    return page;
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
    if (live) {
        flushLatestCameraFrameToUi();
    } else if (cameraPreviewWidget_ != nullptr) {
        const QImage frame = cameraController_->previewFrame();
        cameraPreviewWidget_->setFrame(frame);
        syncLaserOverlay(frame.size());
        syncSequenceOverlay();
    }

    // In live mode this method is called at ~60 Hz; avoid property churn on widgets.
    if (live) {
        return;
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

    if (cameraExposureEdit_ != nullptr && !cameraExposureEdit_->hasFocus()) {
        setLineEditTextIfChanged(cameraExposureEdit_, QString::number(cameraController_->exposureTimeUs(), 'f', 0));
    }
    if (cameraGainEdit_ != nullptr && !cameraGainEdit_->hasFocus()) {
        setLineEditTextIfChanged(cameraGainEdit_, QString::number(cameraController_->gain(), 'f', 1));
    }
    setWidgetEnabledIfChanged(scanCameraButton_, true);
    setWidgetEnabledIfChanged(connectCameraButton_, !cameraController_->isConnected());
    setWidgetEnabledIfChanged(disconnectCameraButton_, cameraController_->isConnected());
    setWidgetEnabledIfChanged(startCameraLiveButton_, cameraController_->isConnected() && !live);
    setWidgetEnabledIfChanged(stopCameraLiveButton_, live);
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

    cameraController_->setExposureTimeUs(exposureUs);
    cameraController_->setGain(gain);
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
    if (logView_ != nullptr) {
        logView_->appendPlainText(message);
    }
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

}  // namespace laserbench::ui
