#include "ui/MainWindow.hpp"

#include <QAction>
#include <QApplication>
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

struct ObjectivePreset
{
    const char* name;
    int laserX;
    int laserY;
    int laserRadiusPx;
};

constexpr std::array<ObjectivePreset, 3> kObjectivePresets {{
    {"4x", 2588, 1350, 32},
    {"10x", 2588, 1350, 32},
    {"50x", 2588, 1350, 32},
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
    motorPollTimer_->setInterval(100);
    connect(motorPollTimer_, &QTimer::timeout, this, &MainWindow::refreshSummaries);
    motorPollTimer_->start();

    cameraPollTimer_ = new QTimer(this);
    cameraPollTimer_->setTimerType(Qt::PreciseTimer);
    cameraPollTimer_->setInterval(16);
    connect(cameraPollTimer_, &QTimer::timeout, this, &MainWindow::refreshCameraUi);

    appendLog("Interface moteurs Qt6 initialisee.");
    appendLog("La DLL Newport est chargee automatiquement depuis MotorController/lib via le pont .NET.");
    appendLog("Camera Thorlabs: menu Camera pour recherche, connexion et live.");
}

MainWindow::~MainWindow()
{
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
    if (motorTaskRunning_) {
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
    connect(cameraConnectionAction, &QAction::triggered, this, &MainWindow::openCameraConnectionDialog);
    connect(cameraDiscoverAction, &QAction::triggered, this, &MainWindow::openCameraConnectionDialog);
    connect(cameraConnectAction, &QAction::triggered, this, &MainWindow::openCameraConnectionDialog);
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
    auto* aboutAction = helpMenu->addAction("A propos");
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
    auto* keyboardHint = new QLabel("Fleches clavier maintenues: gauche/droite = X, haut/bas = Y");
    keyboardHint->setStyleSheet("color:#5c6570; font-size:9pt;");
    motorsLayout->addWidget(keyboardHint, 0, 2, 1, 2);

    motorsLayout->addWidget(new QLabel("Moteur X"), 1, 0);
    motorsLayout->addWidget(new QLabel("Position live"), 1, 1);
    xPositionValueLabel_ = new QLabel("-");
    xPositionValueLabel_->setStyleSheet("font-size:18px; font-weight:600; color:#111927;");
    motorsLayout->addWidget(xPositionValueLabel_, 1, 2, 1, 2);

    motorsLayout->addWidget(new QLabel("Absolu X (mm)"), 2, 0);
    auto* absXWrap = new QWidget;
    auto* absXLayout = new QHBoxLayout(absXWrap);
    absXLayout->setContentsMargins(0, 0, 0, 0);
    absXLayout->setSpacing(6);
    absXEdit_ = new QLineEdit("0.000");
    moveAbsXButton_ = createActionButton("Aller X");
    absXLayout->addWidget(absXEdit_, 1);
    absXLayout->addWidget(moveAbsXButton_);
    motorsLayout->addWidget(absXWrap, 2, 1, 1, 3);

    motorsLayout->addWidget(new QLabel("Moteur Y"), 3, 0);
    motorsLayout->addWidget(new QLabel("Position live"), 3, 1);
    yPositionValueLabel_ = new QLabel("-");
    yPositionValueLabel_->setStyleSheet("font-size:18px; font-weight:600; color:#111927;");
    motorsLayout->addWidget(yPositionValueLabel_, 3, 2, 1, 2);

    motorsLayout->addWidget(new QLabel("Absolu Y (mm)"), 4, 0);
    auto* absYWrap = new QWidget;
    auto* absYLayout = new QHBoxLayout(absYWrap);
    absYLayout->setContentsMargins(0, 0, 0, 0);
    absYLayout->setSpacing(6);
    absYEdit_ = new QLineEdit("0.000");
    moveAbsYButton_ = createActionButton("Aller Y");
    absYLayout->addWidget(absYEdit_, 1);
    absYLayout->addWidget(moveAbsYButton_);
    motorsLayout->addWidget(absYWrap, 4, 1, 1, 3);

    auto* rangeLabel = new QLabel("Plage logicielle actuelle: 0.0 mm a 25.0 mm.");
    rangeLabel->setStyleSheet("color:#5c6570; font-size:9pt;");
    motorsLayout->addWidget(rangeLabel, 5, 0, 1, 4);

    connect(moveAbsXButton_, &QPushButton::clicked, this, [this]() { onMoveAbsolute(hardware::AxisId::X); });
    connect(moveAbsYButton_, &QPushButton::clicked, this, [this]() { onMoveAbsolute(hardware::AxisId::Y); });

    leftLayout->addWidget(motorsBox);

    auto* laserBox = createGroupBox("Objectif / Laser");
    auto* laserLayout = new QGridLayout(laserBox);
    laserLayout->addWidget(new QLabel("Objectif"), 0, 0);
    objectiveCombo_ = new QComboBox;
    for (const ObjectivePreset& preset : kObjectivePresets) {
        objectiveCombo_->addItem(QLatin1String(preset.name));
    }
    objectiveCombo_->setCurrentText("4x");
    laserLayout->addWidget(objectiveCombo_, 0, 1);

    laserLayout->addWidget(new QLabel("Point laser"), 1, 0);
    laserPointLabel_ = new QLabel("-");
    laserPointLabel_->setStyleSheet("font-weight:600; color:#111927;");
    laserLayout->addWidget(laserPointLabel_, 1, 1);

    connect(objectiveCombo_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        applyObjectivePreset();
    });

    leftLayout->addWidget(laserBox);

    auto* cameraBox = createGroupBox("Camera");
    auto* cameraLayout = new QGridLayout(cameraBox);
    cameraLayout->addWidget(new QLabel("Exposure Time (us)"), 0, 0);
    cameraExposureEdit_ = new QLineEdit(QString::number(cameraController_->exposureTimeUs(), 'f', 0));
    cameraLayout->addWidget(cameraExposureEdit_, 0, 1);
    cameraLayout->addWidget(new QLabel("Gain"), 1, 0);
    cameraGainEdit_ = new QLineEdit(QString::number(cameraController_->gain(), 'f', 1));
    cameraLayout->addWidget(cameraGainEdit_, 1, 1);

    applyCameraSettingsButton_ = createActionButton("Appliquer");
    applyCameraSettingsButton_->setProperty("accent", true);
    startCameraLiveButton_ = createActionButton("Live");
    stopCameraLiveButton_ = createActionButton("Stop");
    cameraZoomLabel_ = new QLabel("Zoom: 1.00x");
    cameraZoomLabel_->setStyleSheet("font-weight:600; color:#44515d;");

    cameraLayout->addWidget(applyCameraSettingsButton_, 2, 0, 1, 2);
    cameraLayout->addWidget(startCameraLiveButton_, 3, 0, 1, 2);
    cameraLayout->addWidget(stopCameraLiveButton_, 4, 0, 1, 2);
    cameraLayout->addWidget(cameraZoomLabel_, 5, 0, 1, 2);

    connect(applyCameraSettingsButton_, &QPushButton::clicked, this, &MainWindow::applyCameraSettings);
    connect(cameraExposureEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyCameraSettings);
    connect(cameraGainEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyCameraSettings);
    connect(startCameraLiveButton_, &QPushButton::clicked, this, &MainWindow::startCameraLive);
    connect(stopCameraLiveButton_, &QPushButton::clicked, this, &MainWindow::stopCameraLive);
    leftLayout->addWidget(cameraBox);
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
    updateLaserLabel();
    syncLaserOverlay();
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
    refreshMotorUi();

    if (!cameraController_->isLive()) {
        refreshCameraUi();
    }

    cameraSummaryLabel_->setText(
        QString("Camera: %1 | %2 | Exp %3 us | Gain %4")
            .arg(core::deviceStateLabel(cameraController_->state()))
            .arg(cameraController_->cameraIdentifier())
            .arg(cameraController_->exposureTimeUs(), 0, 'f', 0)
            .arg(cameraController_->gain(), 0, 'f', 1)
    );
    potentiostatSummaryLabel_->setText(
        QString("Potentiostat: %1 | %2")
            .arg(core::deviceStateLabel(potentiostatController_->state()))
            .arg(potentiostatController_->channelSummary())
    );

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

    if (xPositionValueLabel_ != nullptr) xPositionValueLabel_->setText(snapshotPositionText(xSnapshot));
    if (yPositionValueLabel_ != nullptr) yPositionValueLabel_->setText(snapshotPositionText(ySnapshot));
    if (stageSummaryLabel_ != nullptr) stageSummaryLabel_->setText(axisConnectionSummary(xSnapshot, ySnapshot));

    const bool anyConnected = xSnapshot.connected || ySnapshot.connected;
    const bool bothConnected = xSnapshot.connected && ySnapshot.connected;
    const bool enableCommands = bothConnected && !motorTaskRunning_;

    if (scanPortsButton_ != nullptr) scanPortsButton_->setEnabled(!motorTaskRunning_);
    if (connectAxesButton_ != nullptr) connectAxesButton_->setEnabled(!motorTaskRunning_);
    if (homeAxesButton_ != nullptr) homeAxesButton_->setEnabled(bothConnected && !motorTaskRunning_);
    if (disconnectAxesButton_ != nullptr) disconnectAxesButton_->setEnabled(anyConnected && !motorTaskRunning_);
    if (moveAbsXButton_ != nullptr) moveAbsXButton_->setEnabled(enableCommands);
    if (moveAbsYButton_ != nullptr) moveAbsYButton_->setEnabled(enableCommands);
    if (jogStepEdit_ != nullptr) jogStepEdit_->setEnabled(!motorTaskRunning_);
    if (absXEdit_ != nullptr) absXEdit_->setEnabled(enableCommands);
    if (absYEdit_ != nullptr) absYEdit_->setEnabled(enableCommands);
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
    }

    // In live mode this method is called at ~60 Hz; avoid property churn on widgets.
    if (live) {
        return;
    }

    if (cameraExposureEdit_ != nullptr && !cameraExposureEdit_->hasFocus()) {
        cameraExposureEdit_->setText(QString::number(cameraController_->exposureTimeUs(), 'f', 0));
    }
    if (cameraGainEdit_ != nullptr && !cameraGainEdit_->hasFocus()) {
        cameraGainEdit_->setText(QString::number(cameraController_->gain(), 'f', 1));
    }
    if (scanCameraButton_ != nullptr) scanCameraButton_->setEnabled(true);
    if (connectCameraButton_ != nullptr) connectCameraButton_->setEnabled(!cameraController_->isConnected());
    if (disconnectCameraButton_ != nullptr) disconnectCameraButton_->setEnabled(cameraController_->isConnected());
    if (startCameraLiveButton_ != nullptr) startCameraLiveButton_->setEnabled(cameraController_->isConnected() && !live);
    if (stopCameraLiveButton_ != nullptr) stopCameraLiveButton_->setEnabled(live);
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
    }
}

void MainWindow::applyCameraSettings()
{
    bool exposureOk = false;
    bool gainOk = false;
    const double exposureUs = cameraExposureEdit_ != nullptr ? cameraExposureEdit_->text().trimmed().toDouble(&exposureOk) : 0.0;
    const double gain = cameraGainEdit_ != nullptr ? cameraGainEdit_->text().trimmed().toDouble(&gainOk) : 0.0;

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
    motorPollThread_ = std::thread(&MainWindow::motorPollingLoop, this);
}

void MainWindow::stopMotorPolling()
{
    motorPollStopRequested_.store(true);
    if (motorPollThread_.joinable()) {
        motorPollThread_.join();
    }
    motorPollStopRequested_.store(false);
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
            xSnapshot = motorController_->snapshot(hardware::AxisId::X);
            ySnapshot = motorController_->snapshot(hardware::AxisId::Y);
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

        std::this_thread::sleep_for(100ms);
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

    auto controller = motorController_;
    runMotorTask("Initialisation / homing X+Y", [controller]() {
        controller->homeAll(90000);
        return MotorTaskResult {true, "Homing X+Y termine.", {}};
    });
}

void MainWindow::onDisconnectAxes()
{
    leftKeyHeld_ = false;
    rightKeyHeld_ = false;
    upKeyHeld_ = false;
    downKeyHeld_ = false;
    stopContinuousJog(hardware::AxisId::X);
    stopContinuousJog(hardware::AxisId::Y);

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

void MainWindow::onMoveAbsolute(hardware::AxisId axis)
{
    stopContinuousJog(axis);
    const double targetMm = readAbsoluteTargetMm(axis);
    const QString axisLabel = hardware::axisIdLabel(axis);
    auto controller = motorController_;

    runMotorTask(QString("Mouvement absolu %1").arg(axisLabel), [controller, axis, axisLabel, targetMm]() {
        controller->moveAbsolute(axis, targetMm, 30000);
        return MotorTaskResult {true, QString("Mouvement absolu %1 -> %2 mm").arg(axisLabel).arg(targetMm, 0, 'f', 3), {}};
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
