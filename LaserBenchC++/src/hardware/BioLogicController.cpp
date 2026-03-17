#include "hardware/BioLogicController.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>
#include <string>

#define NOMINMAX
#include <windows.h>

namespace laserbench::hardware {

namespace {

[[noreturn]] void throwErr(const QString& msg)
{
    throw std::runtime_error(msg.toStdString());
}

QString fromWide(const wchar_t* text)
{
    return QString::fromWCharArray(text == nullptr ? L"" : text);
}

QString win32Msg(DWORD code)
{
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr
    );
    QString msg = n > 0 ? fromWide(buf).trimmed() : QString("Win32 error %1").arg(code);
    if (buf) LocalFree(buf);
    return msg;
}

QString readLine(HANDLE h)
{
    QByteArray data;
    char byte = 0;
    DWORD bytesRead = 0;
    while (true) {
        if (!ReadFile(h, &byte, 1, &bytesRead, nullptr)) {
            throwErr(QString("Lecture helper echouee : %1").arg(win32Msg(GetLastError())));
        }
        if (bytesRead == 0) {
            throwErr("Le helper a ferme son flux de sortie.");
        }
        if (byte == '\n') { break; }
        if (byte != '\r') { data.append(byte); }
    }
    return QString::fromUtf8(data);
}

void writeLine(HANDLE h, const QByteArray& data)
{
    QByteArray line = data + "\n";
    DWORD written = 0;
    if (!WriteFile(h, line.constData(), static_cast<DWORD>(line.size()), &written, nullptr)) {
        throwErr(QString("Ecriture vers helper echouee : %1").arg(win32Msg(GetLastError())));
    }
}

}  // namespace

// ── Impl ──────────────────────────────────────────────────────────────────────

void BioLogicController::Impl::shutdown()
{
    ready = false;
    if (stdinWrite  != nullptr) { CloseHandle(stdinWrite);    stdinWrite  = nullptr; }
    if (stdoutRead  != nullptr) { CloseHandle(stdoutRead);    stdoutRead  = nullptr; }
    if (processHandle != nullptr) {
        if (WaitForSingleObject(processHandle, 500) == WAIT_TIMEOUT) {
            TerminateProcess(processHandle, 0);
            WaitForSingleObject(processHandle, 1000);
        }
        CloseHandle(processHandle);
        processHandle = nullptr;
    }
}

void BioLogicController::Impl::ensureStarted(const QString& helperScriptPath)
{
    if (isRunning() && ready) {
        return;
    }
    shutdown();

    if (!QFileInfo::exists(helperScriptPath)) {
        throwErr(QString("Script introuvable : %1").arg(helperScriptPath));
    }

    SECURITY_ATTRIBUTES sa {};
    sa.nLength        = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE childStdoutRead = nullptr, childStdoutWrite = nullptr;
    HANDLE childStdinRead  = nullptr, childStdinWrite  = nullptr;

    if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0)) {
        throwErr(QString("CreatePipe stdout : %1").arg(win32Msg(GetLastError())));
    }
    SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0)) {
        CloseHandle(childStdoutRead); CloseHandle(childStdoutWrite);
        throwErr(QString("CreatePipe stdin : %1").arg(win32Msg(GetLastError())));
    }
    SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si {};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError  = childStdoutWrite;

    PROCESS_INFORMATION pi {};
    std::wstring cmdLine = QString("python \"%1\"").arg(helperScriptPath).toStdWString();

    const BOOL created = CreateProcessW(
        nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi
    );

    CloseHandle(childStdoutWrite);
    CloseHandle(childStdinRead);

    if (!created) {
        CloseHandle(childStdoutRead); CloseHandle(childStdinWrite);
        throwErr(QString("Impossible de lancer python : %1").arg(win32Msg(GetLastError())));
    }

    CloseHandle(pi.hThread);
    processHandle = pi.hProcess;
    stdoutRead    = childStdoutRead;
    stdinWrite    = childStdinWrite;

    // Wait for the ready message
    const QString line = readLine(stdoutRead);
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (doc.isNull()) {
        shutdown();
        throwErr(QString("Reponse initiale invalide : %1").arg(line));
    }
    const QJsonObject obj = doc.object();
    if (!obj.value("kbio_available").toBool(true)) {
        shutdown();
        throwErr(QString("kbio non disponible : %1").arg(obj.value("error").toString()));
    }
    ready = true;
}

// ── BioLogicController ────────────────────────────────────────────────────────

BioLogicController::BioLogicController(const QString& helperScriptPath)
    : helperScriptPath_(helperScriptPath)
{
}

BioLogicController::~BioLogicController() = default;

QString           BioLogicController::displayName()    const { return "Potentiostat BioLogic"; }
core::DeviceState BioLogicController::state()          const
{
    return connected_ ? core::DeviceState::Connected : core::DeviceState::Disconnected;
}
QString BioLogicController::backendSummary()     const
{
    return connected_ ? QString("Connecte - %1").arg(connectedModel_) : "Deconnecte";
}
QString BioLogicController::channelSummary()     const { return connected_ ? connectedModel_ : "---"; }
QString BioLogicController::acquisitionSummary() const { return "CA / OCV par point"; }

QString BioLogicController::sendRawCommand(const QString& jsonLine)
{
    // caller must hold transportMutex
    writeLine(impl_.stdinWrite, jsonLine.toUtf8());
    return readLine(impl_.stdoutRead);
}

bool BioLogicController::connect(const QString& dllPath, const QString& address, int channel)
{
    connected_ = false;
    connectedModel_.clear();
    lastError_.clear();

    std::lock_guard<std::mutex> lock(impl_.transportMutex);
    try {
        impl_.ensureStarted(helperScriptPath_);

        QJsonObject cmd;
        cmd["cmd"]      = "connect";
        cmd["dll_path"] = dllPath;
        cmd["address"]  = address;
        cmd["channel"]  = channel;

        const QJsonObject resp = QJsonDocument::fromJson(
            sendRawCommand(QJsonDocument(cmd).toJson(QJsonDocument::Compact)).toUtf8()
        ).object();

        if (!resp.value("ok").toBool()) {
            lastError_ = resp.value("error").toString("Erreur inconnue");
            return false;
        }
        connectedModel_ = resp.value("model").toString("inconnu");
        connected_ = true;
        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        return false;
    }
}

void BioLogicController::disconnect()
{
    connected_ = false;
    connectedModel_.clear();
    std::lock_guard<std::mutex> lock(impl_.transportMutex);
    if (impl_.isRunning()) {
        try {
            QJsonObject cmd;
            cmd["cmd"] = "disconnect";
            sendRawCommand(QJsonDocument(cmd).toJson(QJsonDocument::Compact));
        } catch (...) {}
    }
    impl_.shutdown();
}

void BioLogicController::loadFirmware(int channel)
{
    std::lock_guard<std::mutex> lock(impl_.transportMutex);
    QJsonObject cmd;
    cmd["cmd"]     = "load_firmware";
    cmd["channel"] = channel;
    const QJsonObject resp = QJsonDocument::fromJson(
        sendRawCommand(QJsonDocument(cmd).toJson(QJsonDocument::Compact)).toUtf8()
    ).object();
    if (!resp.value("ok").toBool()) {
        throwErr(resp.value("error").toString("Erreur firmware"));
    }
}

bool BioLogicController::startCa(const CaParams& p)
{
    std::lock_guard<std::mutex> lock(impl_.transportMutex);
    try {
        QJsonObject cmd;
        cmd["cmd"]       = "start_ca";
        cmd["channel"]   = p.channel;
        cmd["voltage"]   = p.voltage;
        cmd["duration"]  = p.duration;
        cmd["vs_init"]   = p.vsInit;
        cmd["record_dt"] = p.recordDt;
        cmd["i_range"]   = p.iRange;
        cmd["e_range"]   = p.eRange;
        cmd["bandwidth"] = p.bandwidth;
        cmd["n_cycles"]  = p.nCycles;
        const QJsonObject resp = QJsonDocument::fromJson(
            sendRawCommand(QJsonDocument(cmd).toJson(QJsonDocument::Compact)).toUtf8()
        ).object();
        if (!resp.value("ok").toBool()) {
            lastError_ = resp.value("error").toString("Erreur CA");
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        return false;
    }
}

bool BioLogicController::startOcv(const OcvParams& p)
{
    std::lock_guard<std::mutex> lock(impl_.transportMutex);
    try {
        QJsonObject cmd;
        cmd["cmd"]       = "start_ocv";
        cmd["channel"]   = p.channel;
        cmd["duration"]  = p.duration;
        cmd["record_dt"] = p.recordDt;
        cmd["e_range"]   = p.eRange;
        const QJsonObject resp = QJsonDocument::fromJson(
            sendRawCommand(QJsonDocument(cmd).toJson(QJsonDocument::Compact)).toUtf8()
        ).object();
        if (!resp.value("ok").toBool()) {
            lastError_ = resp.value("error").toString("Erreur OCV");
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        return false;
    }
}

void BioLogicController::stopChannel(int channel)
{
    std::lock_guard<std::mutex> lock(impl_.transportMutex);
    QJsonObject cmd;
    cmd["cmd"]     = "stop_channel";
    cmd["channel"] = channel;
    try {
        sendRawCommand(QJsonDocument(cmd).toJson(QJsonDocument::Compact));
    } catch (...) {}
}

PotDataResult BioLogicController::getData(int channel)
{
    PotDataResult result;
    std::lock_guard<std::mutex> lock(impl_.transportMutex);
    try {
        QJsonObject cmd;
        cmd["cmd"]     = "get_data";
        cmd["channel"] = channel;
        const QJsonObject resp = QJsonDocument::fromJson(
            sendRawCommand(QJsonDocument(cmd).toJson(QJsonDocument::Compact)).toUtf8()
        ).object();
        result.ok      = resp.value("ok").toBool();
        result.stopped = resp.value("status").toString() == "STOP";
        result.error   = resp.value("error").toString();
        const QJsonArray data = resp.value("data").toArray();
        result.points.reserve(static_cast<std::size_t>(data.size()));
        for (const QJsonValue& v : data) {
            const QJsonObject pt = v.toObject();
            result.points.push_back({
                pt.value("t").toDouble(),
                pt.value("Ewe").toDouble(),
                pt.value("I").toDouble()
            });
        }
    } catch (const std::exception& ex) {
        result.ok    = false;
        result.error = QString::fromUtf8(ex.what());
    }
    return result;
}

PotCurrentValues BioLogicController::getCurrentValues(int channel)
{
    PotCurrentValues result;
    std::lock_guard<std::mutex> lock(impl_.transportMutex);
    try {
        QJsonObject cmd;
        cmd["cmd"]     = "get_current_values";
        cmd["channel"] = channel;
        const QJsonObject resp = QJsonDocument::fromJson(
            sendRawCommand(QJsonDocument(cmd).toJson(QJsonDocument::Compact)).toUtf8()
        ).object();
        result.ok      = resp.value("ok").toBool();
        result.stopped = resp.value("stopped").toBool();
        result.elapsedTime = resp.value("ElapsedTime").toDouble();
        result.ewe     = resp.value("Ewe").toDouble();
        result.I       = resp.value("I").toDouble();
        result.error   = resp.value("error").toString();
    } catch (const std::exception& ex) {
        result.ok    = false;
        result.error = QString::fromUtf8(ex.what());
    }
    return result;
}

} // namespace laserbench::hardware
