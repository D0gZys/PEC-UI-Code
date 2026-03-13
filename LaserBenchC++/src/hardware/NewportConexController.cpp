#include "hardware/NewportConexController.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#define NOMINMAX
#include <windows.h>

namespace laserbench::hardware {

namespace {

constexpr double kAbsMinMm = 0.0;
constexpr double kAbsMaxMm = 25.0;

[[noreturn]] void throwError(const QString& message)
{
    const QByteArray bytes = message.toUtf8();
    throw std::runtime_error(std::string(bytes.constData(), bytes.size()));
}

QString fromWide(const wchar_t* text)
{
    return QString::fromWCharArray(text == nullptr ? L"" : text);
}

QString win32Message(DWORD errorCode)
{
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size = FormatMessageW(
        flags,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    QString message = size > 0 ? fromWide(buffer).trimmed() : QString("Win32 error %1").arg(errorCode);
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return message;
}

QString axisIdToProtocol(AxisId axis)
{
    return axis == AxisId::X ? "X" : "Y";
}

QString readLineFromHandle(HANDLE handle)
{
    QByteArray data;
    char byte = 0;
    DWORD bytesRead = 0;

    while (true) {
        if (!ReadFile(handle, &byte, 1, &bytesRead, nullptr)) {
            throwError(QString("Failed to read helper output: %1").arg(win32Message(GetLastError())));
        }
        if (bytesRead == 0) {
            throwError("Helper closed its output stream unexpectedly.");
        }
        if (byte == '\n') {
            break;
        }
        if (byte != '\r') {
            data.append(byte);
        }
    }

    return QString::fromUtf8(data);
}

bool responseIsOk(const QJsonObject& response)
{
    return response.value("ok").toBool(false);
}

QString responseMessage(const QJsonObject& response)
{
    return response.value("message").toString();
}

}  // namespace

struct NewportConexController::Impl
{
    mutable std::mutex transportMutex;
    HANDLE processHandle {nullptr};
    HANDLE stdinWrite {nullptr};
    HANDLE stdoutRead {nullptr};
    bool connected {false};

    ~Impl()
    {
        shutdown();
    }

    bool isRunning() const
    {
        if (processHandle == nullptr) {
            return false;
        }
        return WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT;
    }

    void shutdown()
    {
        if (stdinWrite != nullptr) {
            CloseHandle(stdinWrite);
            stdinWrite = nullptr;
        }
        if (stdoutRead != nullptr) {
            CloseHandle(stdoutRead);
            stdoutRead = nullptr;
        }
        if (processHandle != nullptr) {
            if (WaitForSingleObject(processHandle, 500) == WAIT_TIMEOUT) {
                TerminateProcess(processHandle, 0);
                WaitForSingleObject(processHandle, 1000);
            }
            CloseHandle(processHandle);
            processHandle = nullptr;
        }
        connected = false;
    }

    void ensureStarted()
    {
        if (isRunning()) {
            return;
        }

        shutdown();

        const QString appDir = QCoreApplication::applicationDirPath();
        const QString helperPath = appDir + "/NewportConexHelper.exe";
        if (!QFileInfo::exists(helperPath)) {
            throwError(QString("NewportConexHelper.exe not found in %1").arg(appDir));
        }

        SECURITY_ATTRIBUTES saAttr {};
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = nullptr;

        HANDLE childStdoutRead = nullptr;
        HANDLE childStdoutWrite = nullptr;
        HANDLE childStdinRead = nullptr;
        HANDLE childStdinWrite = nullptr;

        if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &saAttr, 0)) {
            throwError(QString("CreatePipe stdout failed: %1").arg(win32Message(GetLastError())));
        }
        if (!SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(childStdoutRead);
            CloseHandle(childStdoutWrite);
            throwError(QString("SetHandleInformation stdout failed: %1").arg(win32Message(GetLastError())));
        }
        if (!CreatePipe(&childStdinRead, &childStdinWrite, &saAttr, 0)) {
            CloseHandle(childStdoutRead);
            CloseHandle(childStdoutWrite);
            throwError(QString("CreatePipe stdin failed: %1").arg(win32Message(GetLastError())));
        }
        if (!SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(childStdoutRead);
            CloseHandle(childStdoutWrite);
            CloseHandle(childStdinRead);
            CloseHandle(childStdinWrite);
            throwError(QString("SetHandleInformation stdin failed: %1").arg(win32Message(GetLastError())));
        }

        STARTUPINFOW startupInfo {};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = childStdinRead;
        startupInfo.hStdOutput = childStdoutWrite;
        startupInfo.hStdError = childStdoutWrite;

        PROCESS_INFORMATION processInfo {};
        std::wstring commandLine = QString("\"%1\"").arg(helperPath).toStdWString();
        const std::wstring workingDir = appDir.toStdWString();

        const BOOL created = CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDir.c_str(),
            &startupInfo,
            &processInfo
        );

        CloseHandle(childStdinRead);
        CloseHandle(childStdoutWrite);

        if (!created) {
            CloseHandle(childStdoutRead);
            CloseHandle(childStdinWrite);
            throwError(QString("CreateProcess helper failed: %1").arg(win32Message(GetLastError())));
        }

        CloseHandle(processInfo.hThread);
        processHandle = processInfo.hProcess;
        stdinWrite = childStdinWrite;
        stdoutRead = childStdoutRead;
    }

    QJsonObject sendCommand(const QJsonObject& request)
    {
        std::lock_guard<std::mutex> lock(transportMutex);
        ensureStarted();

        const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        DWORD bytesWritten = 0;
        if (!WriteFile(stdinWrite, payload.constData(), static_cast<DWORD>(payload.size()), &bytesWritten, nullptr)) {
            const QString message = QString("Failed to write helper input: %1").arg(win32Message(GetLastError()));
            shutdown();
            throwError(message);
        }
        if (bytesWritten != static_cast<DWORD>(payload.size())) {
            shutdown();
            throwError("Incomplete write to helper process.");
        }

        const QString responseLine = readLineFromHandle(stdoutRead);
        QJsonParseError parseError {};
        const QJsonDocument document = QJsonDocument::fromJson(responseLine.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            throwError(QString("Invalid helper response: %1").arg(responseLine));
        }
        return document.object();
    }
};

QString axisIdLabel(AxisId axis)
{
    return axis == AxisId::X ? "X" : "Y";
}

QString conexStateLabel(const QString& code)
{
    const QString upper = code.trimmed().toUpper();

    if (upper == "0A") return "Not referenced from reset";
    if (upper == "0B") return "Not referenced from homing";
    if (upper == "0C") return "Not referenced from moving";
    if (upper == "0D") return "Not referenced from disable";
    if (upper == "0E") return "Not referenced from jog";
    if (upper == "0F") return "Not referenced";
    if (upper == "1E") return "Homing";
    if (upper == "1F") return "Homing (2)";
    if (upper == "28") return "Moving";
    if (upper == "29") return "Stepping";
    if (upper == "2A") return "Jogging";
    if (upper == "2B") return "Tracking";
    if (upper == "32") return "Ready from reset";
    if (upper == "33") return "Ready from homing";
    if (upper == "34") return "Ready from moving";
    if (upper == "35") return "Ready from disable";
    if (upper == "36") return "Ready from jog";
    if (upper == "37") return "Ready from tracking";
    if (upper == "3C") return "Disable from ready";
    if (upper == "3D") return "Disable from moving";
    if (upper == "46") return "Motion process";
    if (upper == "47") return "Motion process";
    return upper.isEmpty() ? QString() : "Unknown state";
}

QString formatAxisState(const MotorAxisSnapshot& snapshot)
{
    if (!snapshot.connected) {
        return "Deconnecte";
    }
    if (!snapshot.issue.isEmpty()) {
        return snapshot.issue;
    }
    if (snapshot.stateCode.isEmpty()) {
        return "Connecte";
    }

    const QString label = conexStateLabel(snapshot.stateCode);
    if (label.isEmpty()) {
        return snapshot.stateCode;
    }
    return QString("%1 - %2").arg(snapshot.stateCode, label);
}

NewportConexController::NewportConexController()
    : impl_(std::make_unique<Impl>())
{
}

NewportConexController::~NewportConexController() = default;

QStringList NewportConexController::scanAvailablePorts() const
{
    const QJsonObject response = impl_->sendCommand(QJsonObject {{"cmd", "scan"}});
    if (!responseIsOk(response)) {
        throwError(responseMessage(response));
    }

    QStringList ports;
    const QJsonArray portArray = response.value("ports").toArray();
    for (const QJsonValue& value : portArray) {
        const QString port = value.toString().trimmed();
        if (!port.isEmpty()) {
            ports.append(port);
        }
    }
    ports.removeDuplicates();
    return ports;
}

void NewportConexController::connectAxes(const QString& xPort, const QString& yPort, int address)
{
    const QString cleanX = xPort.trimmed();
    const QString cleanY = yPort.trimmed();

    if (cleanX.isEmpty() || cleanY.isEmpty()) {
        throwError("Select COM ports for X and Y");
    }
    if (cleanX == cleanY) {
        throwError("X and Y must use different COM ports");
    }

    const QJsonObject response = impl_->sendCommand(QJsonObject {
        {"cmd", "connect"},
        {"xPort", cleanX},
        {"yPort", cleanY},
        {"address", address}
    });
    if (!responseIsOk(response)) {
        throwError(responseMessage(response));
    }
    impl_->connected = true;
}

void NewportConexController::disconnectAxes(bool stopBeforeClose)
{
    if (!impl_->isRunning()) {
        impl_->connected = false;
        return;
    }

    const QJsonObject response = impl_->sendCommand(QJsonObject {
        {"cmd", "disconnect"},
        {"stop", stopBeforeClose}
    });
    if (!responseIsOk(response)) {
        throwError(responseMessage(response));
    }
    impl_->connected = false;
}

void NewportConexController::homeAll(int timeoutMs)
{
    QJsonObject response = impl_->sendCommand(QJsonObject {{"cmd", "home"}});
    if (!responseIsOk(response)) {
        throwError(responseMessage(response));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        response = impl_->sendCommand(QJsonObject {{"cmd", "status"}});
        if (!responseIsOk(response)) {
            throwError(responseMessage(response));
        }
        if (!response.value("busy").toBool(false)) {
            const QString lastError = response.value("lastError").toString();
            if (!lastError.isEmpty()) {
                throwError(lastError);
            }
            return;
        }
    }
    throwError("Timeout while waiting for homing completion");
}

void NewportConexController::setVelocity(AxisId axis, double speedMmPerS)
{
    if (speedMmPerS <= 0.0) {
        throwError(QString("Velocity for axis %1 must be > 0 mm/s").arg(axisIdLabel(axis)));
    }

    const QJsonObject response = impl_->sendCommand(QJsonObject {
        {"cmd", "setVelocity"},
        {"axis", axisIdToProtocol(axis)},
        {"speed", speedMmPerS}
    });
    if (!responseIsOk(response)) {
        throwError(responseMessage(response));
    }
}

void NewportConexController::moveAbsoluteNoWait(AxisId axis, double positionMm)
{
    if (positionMm < kAbsMinMm || positionMm > kAbsMaxMm) {
        throwError(QString("Target %1 must be between %2 and %3 mm")
            .arg(axisIdLabel(axis))
            .arg(kAbsMinMm, 0, 'f', 1)
            .arg(kAbsMaxMm, 0, 'f', 1));
    }

    const QJsonObject response = impl_->sendCommand(QJsonObject {
        {"cmd", "moveAbsoluteNoWait"},
        {"axis", axisIdToProtocol(axis)},
        {"position", positionMm}
    });
    if (!responseIsOk(response)) {
        throwError(responseMessage(response));
    }
}

void NewportConexController::stopAxis(AxisId axis)
{
    const QJsonObject response = impl_->sendCommand(QJsonObject {
        {"cmd", "stopAxis"},
        {"axis", axisIdToProtocol(axis)}
    });
    if (!responseIsOk(response)) {
        throwError(responseMessage(response));
    }
}

void NewportConexController::moveRelative(AxisId axis, double deltaMm, int timeoutMs)
{
    QJsonObject response = impl_->sendCommand(QJsonObject {
        {"cmd", "moveRelative"},
        {"axis", axisIdToProtocol(axis)},
        {"delta", deltaMm}
    });
    if (!responseIsOk(response)) {
        throwError(responseMessage(response));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        response = impl_->sendCommand(QJsonObject {{"cmd", "status"}});
        if (!responseIsOk(response)) {
            throwError(responseMessage(response));
        }
        if (!response.value("busy").toBool(false)) {
            const QString lastError = response.value("lastError").toString();
            if (!lastError.isEmpty()) {
                throwError(lastError);
            }
            return;
        }
    }
    throwError(QString("Timeout while waiting for axis %1 relative move").arg(axisIdLabel(axis)));
}

void NewportConexController::moveAbsolute(AxisId axis, double positionMm, int timeoutMs)
{
    if (positionMm < kAbsMinMm || positionMm > kAbsMaxMm) {
        throwError(QString("Target %1 must be between %2 and %3 mm")
            .arg(axisIdLabel(axis))
            .arg(kAbsMinMm, 0, 'f', 1)
            .arg(kAbsMaxMm, 0, 'f', 1));
    }

    QJsonObject response = impl_->sendCommand(QJsonObject {
        {"cmd", "moveAbsolute"},
        {"axis", axisIdToProtocol(axis)},
        {"position", positionMm}
    });
    if (!responseIsOk(response)) {
        throwError(responseMessage(response));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        response = impl_->sendCommand(QJsonObject {{"cmd", "status"}});
        if (!responseIsOk(response)) {
            throwError(responseMessage(response));
        }
        if (!response.value("busy").toBool(false)) {
            const QString lastError = response.value("lastError").toString();
            if (!lastError.isEmpty()) {
                throwError(lastError);
            }
            return;
        }
    }
    throwError(QString("Timeout while waiting for axis %1 absolute move").arg(axisIdLabel(axis)));
}

MotorAxisSnapshot NewportConexController::snapshot(AxisId axis) const
{
    MotorAxisSnapshot snapshot;
    snapshot.axis = axis;

    if (!impl_->isRunning() && !impl_->connected) {
        return snapshot;
    }

    const QJsonObject response = impl_->sendCommand(QJsonObject {{"cmd", "snapshot"}});
    if (!responseIsOk(response)) {
        snapshot.issue = responseMessage(response);
        return snapshot;
    }

    const QJsonObject axisObject = response.value(axis == AxisId::X ? "x" : "y").toObject();
    snapshot.connected = axisObject.value("connected").toBool(false);
    snapshot.port = axisObject.value("port").toString();
    snapshot.positionValid = axisObject.contains("position") && axisObject.value("position").isDouble();
    snapshot.positionMm = axisObject.value("position").toDouble();
    snapshot.errorCode = axisObject.value("errorCode").toString();
    snapshot.stateCode = axisObject.value("stateCode").toString();
    snapshot.issue = axisObject.value("issue").toString();
    return snapshot;
}

bool NewportConexController::anyAxisConnected() const
{
    return impl_->connected;
}

}  // namespace laserbench::hardware
