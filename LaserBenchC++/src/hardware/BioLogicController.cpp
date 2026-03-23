#include "hardware/BioLogicController.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <stdexcept>
#include <vector>

namespace laserbench::hardware {

// Board type values returned by BL_GetChannelBoardType
static constexpr uint32_t BOARD_ESSENTIAL = 1;  // VMP3 series    -> ca.ecc  / ocv.ecc
static constexpr uint32_t BOARD_PREMIUM   = 2;  // VMP-300 series -> ca4.ecc / ocv4.ecc
static constexpr uint32_t BOARD_DIGICORE  = 3;  // SP-300 series  -> ca5.ecc / ocv5.ecc

static constexpr int ERR_OK = 0;

[[noreturn]] static void throwErr(const QString& msg)
{
    throw std::runtime_error(msg.toStdString());
}

// ── Impl ──────────────────────────────────────────────────────────────────────

bool BioLogicController::Impl::load(const QString& dllPath)
{
    unload();

    // Resolve the DLL path — priority: provided path > next to exe > system install
    // If the provided path is a directory, append the DLL filename automatically.
    QString path = dllPath;
    if (!path.isEmpty() && QFileInfo(path).isDir())
        path = QDir(path).filePath("EClib64.dll");

    if (!QFileInfo(path).isFile()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            QDir(appDir).filePath("EClib64.dll"),
            "C:/EC-Lab Development Package/lib/EClib64.dll",
            "C:/Program Files/Bio-Logic Science Instruments/EC-Lab Development Package/lib/EClib64.dll",
        };
        for (const QString& c : candidates) {
            if (QFileInfo(c).isFile()) { path = c; break; }
        }
    }

    hDll = LoadLibraryW(path.toStdWString().c_str());
    if (!hDll) return false;

    dllDir = QFileInfo(path).absolutePath();

    // Bind all function pointers
    auto get = [this](const char* name) {
        return GetProcAddress(hDll, name);
    };

    blConnect             = reinterpret_cast<FnConnect>            (get("BL_Connect"));
    blDisconnect          = reinterpret_cast<FnDisconnect>         (get("BL_Disconnect"));
    blLoadFirmware        = reinterpret_cast<FnLoadFirmware>       (get("BL_LoadFirmware"));
    blLoadTechnique       = reinterpret_cast<FnLoadTechnique>      (get("BL_LoadTechnique"));
    blStartChannel        = reinterpret_cast<FnStartChannel>       (get("BL_StartChannel"));
    blStopChannel         = reinterpret_cast<FnStopChannel>        (get("BL_StopChannel"));
    blGetData             = reinterpret_cast<FnGetData>            (get("BL_GetData"));
    blGetCurrentValues    = reinterpret_cast<FnGetCurrentValues>   (get("BL_GetCurrentValues"));
    blGetChannelBoardType = reinterpret_cast<FnGetChannelBoardType>(get("BL_GetChannelBoardType"));
    blDefineSgl           = reinterpret_cast<FnDefineSgl>          (get("BL_DefineSglParameter"));
    blDefineBool          = reinterpret_cast<FnDefineBool>         (get("BL_DefineBoolParameter"));
    blDefineInt           = reinterpret_cast<FnDefineInt>          (get("BL_DefineIntParameter"));
    blConvertNumSgl       = reinterpret_cast<FnConvertNumSgl>      (get("BL_ConvertChannelNumericIntoSingle"));
    blConvertTimeSecs     = reinterpret_cast<FnConvertTimeSecs>    (get("BL_ConvertTimeChannelNumericIntoSeconds"));
    blGetErrorMsg         = reinterpret_cast<FnGetErrorMsg>        (get("BL_GetErrorMsg"));

    const bool ok = blConnect && blDisconnect && blLoadTechnique
        && blStartChannel && blStopChannel
        && blGetData && blGetCurrentValues && blGetChannelBoardType
        && blDefineSgl && blDefineBool && blDefineInt
        && blConvertNumSgl && blConvertTimeSecs;

    if (!ok) { unload(); }
    return ok;
}

void BioLogicController::Impl::unload()
{
    if (connectionId >= 0 && blDisconnect) {
        blDisconnect(connectionId);
        connectionId = -1;
    }
    if (hDll) {
        FreeLibrary(hDll);
        hDll = nullptr;
    }
    blConnect = nullptr; blDisconnect = nullptr; blLoadFirmware = nullptr;
    blLoadTechnique = nullptr; blStartChannel = nullptr; blStopChannel = nullptr;
    blGetData = nullptr; blGetCurrentValues = nullptr; blGetChannelBoardType = nullptr;
    blDefineSgl = nullptr; blDefineBool = nullptr; blDefineInt = nullptr;
    blConvertNumSgl = nullptr; blConvertTimeSecs = nullptr; blGetErrorMsg = nullptr;
    boardType = 0;
    dllDir.clear();
}

QString BioLogicController::Impl::errorMsg(int code) const
{
    if (blGetErrorMsg) {
        char buf[256] = {};
        unsigned int sz = static_cast<unsigned int>(sizeof(buf));
        blGetErrorMsg(code, buf, &sz);
        return QString::fromLatin1(buf).trimmed();
    }
    return QString("Erreur EClib %1").arg(code);
}

QString BioLogicController::Impl::techFile(const char* base4, const char* base5, const char* base) const
{
    const char* name = (boardType == BOARD_PREMIUM)  ? base4 :
                       (boardType == BOARD_DIGICORE) ? base5 : base;

    const QString full = QDir(dllDir).filePath(QString::fromLatin1(name));
    return QFileInfo::exists(full) ? full : QString::fromLatin1(name);
}

// ── BioLogicController ────────────────────────────────────────────────────────

BioLogicController::BioLogicController() = default;
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

bool BioLogicController::connect(const QString& dllPath, const QString& address, int channel)
{
    connected_ = false;
    connectedModel_.clear();
    lastError_.clear();

    std::lock_guard<std::mutex> lock(impl_.mutex);
    try {
        if (!impl_.load(dllPath)) {
            throwErr(QString("Impossible de charger EClib64.dll. "
                             "Verifiez que EClib64.dll est present dans le dossier de l'application "
                             "ou indiquez son chemin dans le champ DLL. "
                             "(Chemin essaye : %1)").arg(dllPath));
        }

        TDeviceInfos_t infos {};
        const int id = [&] {
            int tmp = -1;
            const int rc = impl_.blConnect(address.toLatin1().constData(), 5, &tmp, &infos);
            if (rc != ERR_OK) throwErr(QString("BL_Connect : %1").arg(impl_.errorMsg(rc)));
            return tmp;
        }();
        impl_.connectionId = id;

        const uint8 ch = static_cast<uint8>(channel - 1);
        {
            const int rc = impl_.blGetChannelBoardType(id, ch, &impl_.boardType);
            if (rc != ERR_OK) throwErr(QString("BL_GetChannelBoardType : %1").arg(impl_.errorMsg(rc)));
        }

        // Map DeviceCode to human-readable model name
        static const char* const kDeviceNames[] = {
            "VMP","VMP2","MPG","BiStat","MCS200","VMP3","VSP","HCP803",
            "EPP400","EPP4000","BiStat2","FCT150S","VMP300","SP50","SP150",
            "FCT50S","SP300","CLB500","HCP1005","CLB2000","VSP300","SP200",
            "MPG2","SP100","MOSLED","Kinetic","Nikita","SP240",
            "MPG205","MPG210","MPG220","MPG240","BP300","VMP3e","VSP3e","SP50e","SP150e"
        };
        const int code = infos.DeviceCode;
        connectedModel_ = (code >= 0 && code < static_cast<int>(std::size(kDeviceNames)))
                          ? QString::fromLatin1(kDeviceNames[code])
                          : QString("Device%1").arg(code);
        connected_ = true;
        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        impl_.unload();
        return false;
    }
}

void BioLogicController::disconnect()
{
    connected_ = false;
    connectedModel_.clear();
    std::lock_guard<std::mutex> lock(impl_.mutex);
    impl_.unload();
}

void BioLogicController::loadFirmware(int channel)
{
    std::lock_guard<std::mutex> lock(impl_.mutex);
    if (!impl_.isConnected()) throwErr("Non connecte");
    if (!impl_.blLoadFirmware)  throwErr("BL_LoadFirmware non disponible");

    const char* fw   = nullptr;
    const char* fpga = nullptr;
    if (impl_.boardType == BOARD_ESSENTIAL) { fw = "kernel.bin";  fpga = "Vmp_ii_0437_a6.xlx"; }
    else if (impl_.boardType == BOARD_PREMIUM)   { fw = "kernel4.bin"; fpga = "vmp_iv_0395_aa.xlx"; }
    else if (impl_.boardType == BOARD_DIGICORE)  { fw = "kernel5.bin"; fpga = ""; }
    else throwErr(QString("Type de carte inconnu : %1").arg(impl_.boardType));

    constexpr uint8 kMaxCh = 16;
    uint8 channels[kMaxCh] = {};
    int   results[kMaxCh]  = {};
    channels[static_cast<uint8>(channel - 1)] = 1;

    const QString fwFull   = impl_.dllDir.isEmpty() ? QString::fromLatin1(fw)   : QDir(impl_.dllDir).filePath(QString::fromLatin1(fw));
    const QString fpgaFull = (fpga && *fpga) ? (impl_.dllDir.isEmpty() ? QString::fromLatin1(fpga) : QDir(impl_.dllDir).filePath(QString::fromLatin1(fpga))) : QString();

    const int rc = impl_.blLoadFirmware(
        impl_.connectionId, channels, results, kMaxCh,
        false, true,
        fwFull.toLatin1().constData(),
        fpgaFull.isEmpty() ? nullptr : fpgaFull.toLatin1().constData()
    );
    if (rc != ERR_OK) throwErr(QString("BL_LoadFirmware : %1").arg(impl_.errorMsg(rc)));
}

bool BioLogicController::startCa(const CaParams& p)
{
    std::lock_guard<std::mutex> lock(impl_.mutex);
    try {
        if (!impl_.isConnected()) throwErr("Non connecte");

        constexpr int kNParams = 9;
        std::vector<TEccParam_t> params(kNParams);

        impl_.blDefineSgl ("Voltage_step",    static_cast<float>(p.voltage),   0, &params[0]);
        impl_.blDefineSgl ("Duration_step",   static_cast<float>(p.duration),  0, &params[1]);
        impl_.blDefineBool("vs_initial",      p.vsInit,                        0, &params[2]);
        impl_.blDefineInt ("Step_number",     0,                               0, &params[3]);
        impl_.blDefineSgl ("Record_every_dT", static_cast<float>(p.recordDt),  0, &params[4]);
        impl_.blDefineInt ("I_Range",         p.iRange,                        0, &params[5]);
        impl_.blDefineInt ("E_Range",         p.eRange,                        0, &params[6]);
        impl_.blDefineInt ("Bandwidth",       p.bandwidth,                     0, &params[7]);
        impl_.blDefineInt ("N_Cycles",        p.nCycles,                       0, &params[8]);

        TEccParams_t eccParams;
        eccParams.len     = kNParams;
        eccParams.pParams = params.data();

        const QString tech  = impl_.techFile("ca4.ecc", "ca5.ecc", "ca.ecc");
        const uint8   ch    = static_cast<uint8>(p.channel - 1);

        int rc = impl_.blLoadTechnique(impl_.connectionId, ch, tech.toLatin1().constData(), eccParams, true, true, false);
        if (rc != ERR_OK) throwErr(QString("BL_LoadTechnique CA : %1").arg(impl_.errorMsg(rc)));

        rc = impl_.blStartChannel(impl_.connectionId, ch);
        if (rc != ERR_OK) throwErr(QString("BL_StartChannel : %1").arg(impl_.errorMsg(rc)));

        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        return false;
    }
}

bool BioLogicController::startOcv(const OcvParams& p)
{
    std::lock_guard<std::mutex> lock(impl_.mutex);
    try {
        if (!impl_.isConnected()) throwErr("Non connecte");

        constexpr int kNParams = 3;
        std::vector<TEccParam_t> params(kNParams);

        impl_.blDefineSgl("Rest_time_T",     static_cast<float>(p.duration),  0, &params[0]);
        impl_.blDefineSgl("Record_every_dT", static_cast<float>(p.recordDt),  0, &params[1]);
        impl_.blDefineInt("E_Range",         p.eRange,                        0, &params[2]);

        TEccParams_t eccParams;
        eccParams.len     = kNParams;
        eccParams.pParams = params.data();

        const QString tech = impl_.techFile("ocv4.ecc", "ocv5.ecc", "ocv.ecc");
        const uint8   ch   = static_cast<uint8>(p.channel - 1);

        int rc = impl_.blLoadTechnique(impl_.connectionId, ch, tech.toLatin1().constData(), eccParams, true, true, false);
        if (rc != ERR_OK) throwErr(QString("BL_LoadTechnique OCV : %1").arg(impl_.errorMsg(rc)));

        rc = impl_.blStartChannel(impl_.connectionId, ch);
        if (rc != ERR_OK) throwErr(QString("BL_StartChannel : %1").arg(impl_.errorMsg(rc)));

        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        return false;
    }
}

void BioLogicController::stopChannel(int channel)
{
    std::lock_guard<std::mutex> lock(impl_.mutex);
    if (!impl_.isConnected() || !impl_.blStopChannel) return;
    impl_.blStopChannel(impl_.connectionId, static_cast<uint8>(channel - 1));
}

PotDataResult BioLogicController::getData(int channel)
{
    PotDataResult result;
    std::lock_guard<std::mutex> lock(impl_.mutex);
    try {
        if (!impl_.isConnected()) throwErr("Non connecte");

        TDataBuffer_t    buf {};
        TDataInfos_t     info {};
        TCurrentValues_t curr {};
        const uint8      ch = static_cast<uint8>(channel - 1);

        const int rc = impl_.blGetData(impl_.connectionId, ch, &buf, &info, &curr);
        if (rc != ERR_OK) throwErr(QString("BL_GetData : %1").arg(impl_.errorMsg(rc)));

        result.ok      = true;
        result.stopped = (curr.State == KBIO_STATE_STOP);

        // Data layout per row: [t_lo, t_hi, Ewe, I?, ...] (NbCols columns)
        // Column 0-1 : time encoding -> BL_ConvertTimeChannelNumericIntoSeconds
        // Column 2   : Ewe (V)
        // Column 3   : I (A)  -- only for CA (NbCols >= 4)
        for (int i = 0; i < info.NbRows; ++i) {
            const int offset = i * info.NbCols;

            double t = info.StartTime;
            if (impl_.blConvertTimeSecs) {
                double dt = 0.0;
                impl_.blConvertTimeSecs(&buf.data[offset], &dt, curr.TimeBase, impl_.boardType);
                t += dt;
            }

            float ewe = 0.0f;
            if (info.NbCols >= 3 && impl_.blConvertNumSgl)
                impl_.blConvertNumSgl(buf.data[offset + 2], &ewe, impl_.boardType);

            float I = 0.0f;
            if (info.NbCols >= 4 && impl_.blConvertNumSgl)
                impl_.blConvertNumSgl(buf.data[offset + 3], &I, impl_.boardType);

            result.points.push_back({t, static_cast<double>(ewe), static_cast<double>(I)});
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
    std::lock_guard<std::mutex> lock(impl_.mutex);
    try {
        if (!impl_.isConnected()) throwErr("Non connecte");

        TCurrentValues_t curr {};
        const uint8      ch = static_cast<uint8>(channel - 1);

        const int rc = impl_.blGetCurrentValues(impl_.connectionId, ch, &curr);
        if (rc != ERR_OK) throwErr(QString("BL_GetCurrentValues : %1").arg(impl_.errorMsg(rc)));

        result.ok          = true;
        result.stopped     = (curr.State == KBIO_STATE_STOP);
        result.elapsedTime = static_cast<double>(curr.ElapsedTime);
        result.ewe         = static_cast<double>(curr.Ewe);
        result.I           = static_cast<double>(curr.I);
    } catch (const std::exception& ex) {
        result.ok    = false;
        result.error = QString::fromUtf8(ex.what());
    }
    return result;
}

} // namespace laserbench::hardware
