#include "hardware/BioLogicController.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace laserbench::hardware {

// Board type values returned by BL_GetChannelBoardType
static constexpr uint32_t BOARD_ESSENTIAL = 1;  // VMP3 series    -> ca.ecc  / ocv.ecc
static constexpr uint32_t BOARD_PREMIUM   = 2;  // VMP-300 series -> ca4.ecc / ocv4.ecc
static constexpr uint32_t BOARD_DIGICORE  = 3;  // SP-300 series  -> ca5.ecc / ocv5.ecc

static constexpr int ERR_OK = 0;

static float scanRateVoltsPerSecond(double scanRateMvPerS)
{
    return static_cast<float>(scanRateMvPerS * 1e-3);
}

static bool supportsNativeCvaForModel(const QString& model)
{
    const QString normalized = model.trimmed().toUpper();
    return normalized == "VMP"
        || normalized == "VMP2"
        || normalized == "MPG"
        || normalized == "BISTAT"
        || normalized == "MCS200"
        || normalized == "VMP3"
        || normalized == "VSP"
        || normalized == "HCP803"
        || normalized == "EPP400"
        || normalized == "EPP4000"
        || normalized == "BISTAT2"
        || normalized == "FCT150S"
        || normalized == "FCT50S";
}

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
QString BioLogicController::acquisitionSummary() const { return "CA / OCV / CVA"; }

bool BioLogicController::connect(const QString& dllPath, const QString& address, int channel)
{
    connected_ = false;
    connectedModel_.clear();
    lastError_.clear();
    lastStartDetails_.clear();

    std::lock_guard<std::mutex> lock(impl_.mutex);
    try {
        if (!impl_.load(dllPath)) {
            throwErr(QString("Impossible de charger EClib64.dll. "
                             "Verifiez que EClib64.dll est present dans le dossier de l'application "
                             "ou indiquez son chemin dans le champ DLL. "
                             "(Chemin essaye : %1)").arg(dllPath));
        }

        // BL_Connect peut ignorer son paramètre timeout sur certaines DLLs.
        // On l'exécute dans un thread dédié avec un timeout dur de 15 secondes.
        struct BlResult {
            std::atomic<bool> done {false};
            int rc {-1};
            int id {-1};
            TDeviceInfos_t infos {};
        };
        auto blRes = std::make_shared<BlResult>();
        {
            const std::string addr = address.toLatin1().toStdString();
            auto blConnect = impl_.blConnect;
            std::thread([blRes, addr, blConnect]() {
                blRes->rc = blConnect(addr.c_str(), 10, &blRes->id, &blRes->infos);
                blRes->done.store(true);
            }).detach();
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!blRes->done.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (!blRes->done.load())
            throwErr("Timeout (15s) : appareil non reachable. Verifiez IP et connexion reseau.");
        if (blRes->rc != ERR_OK)
            throwErr(QString("BL_Connect : %1").arg(impl_.errorMsg(blRes->rc)));

        TDeviceInfos_t infos = blRes->infos;
        impl_.connectionId = blRes->id;

        const uint8 ch = static_cast<uint8>(channel - 1);
        {
            const int rc = impl_.blGetChannelBoardType(impl_.connectionId, ch, &impl_.boardType);
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
    lastStartDetails_.clear();
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
        lastStartDetails_.clear();

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

        lastStartDetails_ = QString("[POTDBG] controller start: technique=CA path=direct model=%1 boardType=%2 tech=%3 voltage=%4 V duration=%5 s recordDt=%6 s")
            .arg(connectedModel_)
            .arg(impl_.boardType)
            .arg(tech)
            .arg(p.voltage, 0, 'f', 6)
            .arg(p.duration, 0, 'f', 3)
            .arg(p.recordDt, 0, 'f', 3);

        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        lastStartDetails_.clear();
        return false;
    }
}

bool BioLogicController::startOcv(const OcvParams& p)
{
    std::lock_guard<std::mutex> lock(impl_.mutex);
    try {
        if (!impl_.isConnected()) throwErr("Non connecte");
        lastStartDetails_.clear();

        constexpr int kNParams = 4;
        std::vector<TEccParam_t> params(kNParams);

        impl_.blDefineSgl("Rest_time_T",     static_cast<float>(p.duration),  0, &params[0]);
        impl_.blDefineSgl("Record_every_dE", static_cast<float>(p.recordDE),  0, &params[1]);
        impl_.blDefineSgl("Record_every_dT", static_cast<float>(p.recordDt),  0, &params[2]);
        impl_.blDefineInt("E_Range",         p.eRange,                        0, &params[3]);

        TEccParams_t eccParams;
        eccParams.len     = kNParams;
        eccParams.pParams = params.data();

        const QString tech = impl_.techFile("ocv4.ecc", "ocv5.ecc", "ocv.ecc");
        const uint8   ch   = static_cast<uint8>(p.channel - 1);

        int rc = impl_.blLoadTechnique(impl_.connectionId, ch, tech.toLatin1().constData(), eccParams, true, true, false);
        if (rc != ERR_OK) throwErr(QString("BL_LoadTechnique OCV : %1").arg(impl_.errorMsg(rc)));

        rc = impl_.blStartChannel(impl_.connectionId, ch);
        if (rc != ERR_OK) throwErr(QString("BL_StartChannel : %1").arg(impl_.errorMsg(rc)));

        lastStartDetails_ = QString("[POTDBG] controller start: technique=OCV path=direct model=%1 boardType=%2 tech=%3 rest=%4 s recordDE=%5 V recordDt=%6 s")
            .arg(connectedModel_)
            .arg(impl_.boardType)
            .arg(tech)
            .arg(p.duration, 0, 'f', 3)
            .arg(p.recordDE, 0, 'f', 6)
            .arg(p.recordDt, 0, 'f', 3);

        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        lastStartDetails_.clear();
        return false;
    }
}

bool BioLogicController::startCva(const CvaParams& p)
{
    std::lock_guard<std::mutex> lock(impl_.mutex);
    try {
        if (!impl_.isConnected()) throwErr("Non connecte");

        const uint8 ch = static_cast<uint8>(p.channel - 1);

        // The native CVA technique (biovscan.ecc) is only available on the
        // legacy VMP3 family. On newer boards such as SP-50/SP-150, EC-Lab
        // exposes the same workflow but the OEM SDK is more reliable when we
        // sequence an initial hold, a VSCAN segment, then an optional final hold.
        if (impl_.boardType != BOARD_ESSENTIAL) {
            const auto loadCaHold =
                [&](double voltage, bool vsInitial, double durationS, double recordDtS, bool first, bool last) {
                    constexpr int kCaParamCount = 9;
                    std::vector<TEccParam_t> params(kCaParamCount);

                    impl_.blDefineSgl ("Voltage_step",    static_cast<float>(voltage),                    0, &params[0]);
                    impl_.blDefineSgl ("Duration_step",   static_cast<float>(durationS),                  0, &params[1]);
                    impl_.blDefineBool("vs_initial",      vsInitial,                                      0, &params[2]);
                    impl_.blDefineInt ("Step_number",     0,                                              0, &params[3]);
                    impl_.blDefineSgl ("Record_every_dT", static_cast<float>(std::max(recordDtS, 1e-3)), 0, &params[4]);
                    impl_.blDefineInt ("I_Range",         p.iRange,                                       0, &params[5]);
                    impl_.blDefineInt ("E_Range",         p.eRange,                                       0, &params[6]);
                    impl_.blDefineInt ("Bandwidth",       p.bandwidth,                                    0, &params[7]);
                    impl_.blDefineInt ("N_Cycles",        0,                                              0, &params[8]);

                    TEccParams_t eccParams;
                    eccParams.len = kCaParamCount;
                    eccParams.pParams = params.data();

                    const QString tech = impl_.techFile("ca4.ecc", "ca5.ecc", "ca.ecc");
                    const int rc = impl_.blLoadTechnique(
                        impl_.connectionId, ch, tech.toLatin1().constData(), eccParams, first, last, false);
                    if (rc != ERR_OK) {
                        throwErr(QString("BL_LoadTechnique CA (CVA emulee) : %1").arg(impl_.errorMsg(rc)));
                    }
                };

            const auto loadVscan =
                [&](bool first, bool last) {
                    constexpr int kVertexCount = 4;
                    constexpr int kParamCount = 20;
                    std::vector<TEccParam_t> params(kParamCount);
                    int paramIndex = 0;

                    for (int i = 0; i < kVertexCount; ++i) {
                        impl_.blDefineSgl(
                            "Voltage_step",
                            static_cast<float>(p.voltageScan[static_cast<std::size_t>(i)]),
                            i,
                            &params[paramIndex++]);
                        impl_.blDefineBool(
                            "vs_initial",
                            p.vsInitialScan[static_cast<std::size_t>(i)],
                            i,
                            &params[paramIndex++]);
                        impl_.blDefineSgl(
                            "Scan_Rate",
                            static_cast<float>(p.scanRateMvPerS[static_cast<std::size_t>(i)] * 1e-3),
                            i,
                            &params[paramIndex++]);
                    }

                    impl_.blDefineInt ("Scan_number",       2,                                     0, &params[paramIndex++]);
                    impl_.blDefineInt ("N_Cycles",          p.nCycles,                             0, &params[paramIndex++]);
                    impl_.blDefineSgl ("Record_every_dE",   static_cast<float>(p.recordDE),       0, &params[paramIndex++]);
                    impl_.blDefineSgl ("Begin_measuring_I", static_cast<float>(p.beginMeasuringI),0, &params[paramIndex++]);
                    impl_.blDefineSgl ("End_measuring_I",   static_cast<float>(p.endMeasuringI),  0, &params[paramIndex++]);
                    impl_.blDefineInt ("I_Range",           p.iRange,                              0, &params[paramIndex++]);
                    impl_.blDefineInt ("E_Range",           p.eRange,                              0, &params[paramIndex++]);
                    impl_.blDefineInt ("Bandwidth",         p.bandwidth,                           0, &params[paramIndex++]);

                    TEccParams_t eccParams;
                    eccParams.len = paramIndex;
                    eccParams.pParams = params.data();

                    const QString tech = impl_.techFile("vscan4.ecc", "vscan5.ecc", "vscan.ecc");
                    const int rc = impl_.blLoadTechnique(
                        impl_.connectionId, ch, tech.toLatin1().constData(), eccParams, first, last, false);
                    if (rc != ERR_OK) {
                        throwErr(QString("BL_LoadTechnique VSCAN (CVA emulee) : %1").arg(impl_.errorMsg(rc)));
                    }
                };

            const bool hasInitialHold = p.durationStep[0] > 1e-9;
            const bool hasFinalHold = p.durationStep[1] > 1e-9;
            bool firstTechnique = true;

            if (hasInitialHold) {
                loadCaHold(
                    p.voltageStep[0],
                    p.vsInitialStep[0],
                    p.durationStep[0],
                    p.recordDtStep[0],
                    true,
                    false);
                firstTechnique = false;
            }

            loadVscan(firstTechnique, !hasFinalHold);

            if (hasFinalHold) {
                loadCaHold(
                    p.voltageStep[1],
                    p.vsInitialStep[1],
                    p.durationStep[1],
                    p.recordDtStep[1],
                    false,
                    true);
            }
        } else {
            constexpr int kNativeParamCount = 31;
            std::vector<TEccParam_t> params(kNativeParamCount);
            int paramIndex = 0;

            for (int i = 0; i < static_cast<int>(p.vsInitialScan.size()); ++i)
                impl_.blDefineBool("vs_initial_scan", p.vsInitialScan[static_cast<std::size_t>(i)], i, &params[paramIndex++]);
            for (int i = 0; i < static_cast<int>(p.voltageScan.size()); ++i)
                impl_.blDefineSgl("Voltage_scan", static_cast<float>(p.voltageScan[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);
            for (int i = 0; i < static_cast<int>(p.scanRateMvPerS.size()); ++i)
                impl_.blDefineSgl("Scan_Rate", static_cast<float>(p.scanRateMvPerS[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);

            impl_.blDefineInt ("Scan_number",       2,                                        0, &params[paramIndex++]);
            impl_.blDefineSgl ("Record_every_dE",   static_cast<float>(p.recordDE),           0, &params[paramIndex++]);
            impl_.blDefineBool("Average_over_dE",   p.averageOverDE,                          0, &params[paramIndex++]);
            impl_.blDefineInt ("N_Cycles",          p.nCycles,                                0, &params[paramIndex++]);
            impl_.blDefineSgl ("Begin_measuring_I", static_cast<float>(p.beginMeasuringI),    0, &params[paramIndex++]);
            impl_.blDefineSgl ("End_measuring_I",   static_cast<float>(p.endMeasuringI),      0, &params[paramIndex++]);

            for (int i = 0; i < static_cast<int>(p.vsInitialStep.size()); ++i)
                impl_.blDefineBool("vs_initial_step", p.vsInitialStep[static_cast<std::size_t>(i)], i, &params[paramIndex++]);
            for (int i = 0; i < static_cast<int>(p.voltageStep.size()); ++i)
                impl_.blDefineSgl("Voltage_step", static_cast<float>(p.voltageStep[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);
            for (int i = 0; i < static_cast<int>(p.durationStep.size()); ++i)
                impl_.blDefineSgl("Duration_step", static_cast<float>(p.durationStep[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);

            impl_.blDefineInt ("Step_number",      0,                                        0, &params[paramIndex++]);
            impl_.blDefineSgl ("Record_every_dT",  static_cast<float>(p.recordDt),           0, &params[paramIndex++]);
            impl_.blDefineSgl ("Record_every_dI",  static_cast<float>(p.recordDI),           0, &params[paramIndex++]);
            impl_.blDefineBool("Trig_on_off",      p.trigOnOff,                              0, &params[paramIndex++]);
            impl_.blDefineInt ("I_Range",          p.iRange,                                 0, &params[paramIndex++]);
            impl_.blDefineInt ("E_Range",          p.eRange,                                 0, &params[paramIndex++]);
            impl_.blDefineInt ("Bandwidth",        p.bandwidth,                              0, &params[paramIndex++]);

            TEccParams_t eccParams;
            eccParams.len = paramIndex;
            eccParams.pParams = params.data();

            QString tech = QDir(impl_.dllDir).filePath("biovscan.ecc");
            if (!QFileInfo::exists(tech))
                tech = "biovscan.ecc";

            const int rc = impl_.blLoadTechnique(
                impl_.connectionId, ch, tech.toLatin1().constData(), eccParams, true, true, false);
            if (rc != ERR_OK) throwErr(QString("BL_LoadTechnique CVA : %1").arg(impl_.errorMsg(rc)));
        }

        const int rc = impl_.blStartChannel(impl_.connectionId, ch);
        if (rc != ERR_OK) throwErr(QString("BL_StartChannel : %1").arg(impl_.errorMsg(rc)));

        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        return false;
    }
}

bool BioLogicController::startCvaSimple(const CvaParams& p)
{
    std::lock_guard<std::mutex> lock(impl_.mutex);
    try {
        if (!impl_.isConnected()) throwErr("Non connecte");

        const uint8 ch = static_cast<uint8>(p.channel - 1);
        const bool useNativeCva = supportsNativeCvaForModel(connectedModel_);
        lastStartDetails_.clear();

        if (useNativeCva) {
            constexpr int kNativeParamCount = 31;
            std::vector<TEccParam_t> params(kNativeParamCount);
            int paramIndex = 0;

            for (int i = 0; i < static_cast<int>(p.vsInitialScan.size()); ++i)
                impl_.blDefineBool("vs_initial_scan", p.vsInitialScan[static_cast<std::size_t>(i)], i, &params[paramIndex++]);
            for (int i = 0; i < static_cast<int>(p.voltageScan.size()); ++i)
                impl_.blDefineSgl("Voltage_scan", static_cast<float>(p.voltageScan[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);
            for (int i = 0; i < static_cast<int>(p.scanRateMvPerS.size()); ++i)
                impl_.blDefineSgl("Scan_Rate", scanRateVoltsPerSecond(p.scanRateMvPerS[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);

            impl_.blDefineInt ("Scan_number",       2,                                        0, &params[paramIndex++]);
            impl_.blDefineSgl ("Record_every_dE",   static_cast<float>(p.recordDE),           0, &params[paramIndex++]);
            impl_.blDefineBool("Average_over_dE",   p.averageOverDE,                          0, &params[paramIndex++]);
            impl_.blDefineInt ("N_Cycles",          p.nCycles,                                0, &params[paramIndex++]);
            impl_.blDefineSgl ("Begin_measuring_I", static_cast<float>(p.beginMeasuringI),    0, &params[paramIndex++]);
            impl_.blDefineSgl ("End_measuring_I",   static_cast<float>(p.endMeasuringI),      0, &params[paramIndex++]);

            for (int i = 0; i < static_cast<int>(p.vsInitialStep.size()); ++i)
                impl_.blDefineBool("vs_initial_step", p.vsInitialStep[static_cast<std::size_t>(i)], i, &params[paramIndex++]);
            for (int i = 0; i < static_cast<int>(p.voltageStep.size()); ++i)
                impl_.blDefineSgl("Voltage_step", static_cast<float>(p.voltageStep[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);
            for (int i = 0; i < static_cast<int>(p.durationStep.size()); ++i)
                impl_.blDefineSgl("Duration_step", static_cast<float>(p.durationStep[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);

            impl_.blDefineInt ("Step_number",      0,                                        0, &params[paramIndex++]);
            impl_.blDefineSgl ("Record_every_dT",  static_cast<float>(p.recordDt),           0, &params[paramIndex++]);
            impl_.blDefineSgl ("Record_every_dI",  static_cast<float>(p.recordDI),           0, &params[paramIndex++]);
            impl_.blDefineBool("Trig_on_off",      p.trigOnOff,                              0, &params[paramIndex++]);
            impl_.blDefineInt ("I_Range",          p.iRange,                                 0, &params[paramIndex++]);
            impl_.blDefineInt ("E_Range",          p.eRange,                                 0, &params[paramIndex++]);
            impl_.blDefineInt ("Bandwidth",        p.bandwidth,                              0, &params[paramIndex++]);

            TEccParams_t eccParams;
            eccParams.len = paramIndex;
            eccParams.pParams = params.data();

            QString tech = QDir(impl_.dllDir).filePath("biovscan.ecc");
            if (!QFileInfo::exists(tech))
                tech = "biovscan.ecc";

            int rc = impl_.blLoadTechnique(
                impl_.connectionId, ch, tech.toLatin1().constData(), eccParams, true, true, false);
            if (rc != ERR_OK) throwErr(QString("BL_LoadTechnique CVA : %1").arg(impl_.errorMsg(rc)));

            rc = impl_.blStartChannel(impl_.connectionId, ch);
            if (rc != ERR_OK) throwErr(QString("BL_StartChannel : %1").arg(impl_.errorMsg(rc)));

            lastStartDetails_ = QString("[POTDBG] controller start: technique=CVA simple path=native-biovscan model=%1 boardType=%2 tech=%3 scanRateSdk=%4 V/s")
                .arg(connectedModel_)
                .arg(impl_.boardType)
                .arg(tech)
                .arg(scanRateVoltsPerSecond(p.scanRateMvPerS[1]), 0, 'f', 6);

            return true;
        }

        const auto loadCaHold =
            [&](double voltage, bool vsInitial, double durationS, double recordDtS, bool first, bool last) {
                constexpr int kCaParamCount = 9;
                std::vector<TEccParam_t> params(kCaParamCount);

                impl_.blDefineSgl ("Voltage_step",    static_cast<float>(voltage),                    0, &params[0]);
                impl_.blDefineSgl ("Duration_step",   static_cast<float>(durationS),                  0, &params[1]);
                impl_.blDefineBool("vs_initial",      vsInitial,                                      0, &params[2]);
                impl_.blDefineInt ("Step_number",     0,                                              0, &params[3]);
                impl_.blDefineSgl ("Record_every_dT", static_cast<float>(std::max(recordDtS, 1e-3)), 0, &params[4]);
                impl_.blDefineInt ("I_Range",         p.iRange,                                       0, &params[5]);
                impl_.blDefineInt ("E_Range",         p.eRange,                                       0, &params[6]);
                impl_.blDefineInt ("Bandwidth",       p.bandwidth,                                    0, &params[7]);
                impl_.blDefineInt ("N_Cycles",        0,                                              0, &params[8]);

                TEccParams_t eccParams;
                eccParams.len = kCaParamCount;
                eccParams.pParams = params.data();

                const QString tech = impl_.techFile("ca4.ecc", "ca5.ecc", "ca.ecc");
                const int rc = impl_.blLoadTechnique(
                    impl_.connectionId, ch, tech.toLatin1().constData(), eccParams, first, last, false);
                if (rc != ERR_OK) {
                    throwErr(QString("BL_LoadTechnique CA (CVA simple) : %1").arg(impl_.errorMsg(rc)));
                }
            };

        const auto loadCvScan =
            [&](bool first, bool last) {
                constexpr int kVertexCount = 5;
                constexpr int kParamCount = 24;
                std::vector<TEccParam_t> params(kParamCount);
                int paramIndex = 0;

                const std::array<double, kVertexCount> voltages = {
                    p.voltageScan[0],
                    p.voltageScan[1],
                    p.voltageScan[2],
                    p.voltageScan[0],
                    p.voltageScan[3]
                };
                const std::array<bool, kVertexCount> vsInitial = {
                    p.vsInitialScan[0],
                    p.vsInitialScan[1],
                    p.vsInitialScan[2],
                    p.vsInitialScan[0],
                    p.vsInitialScan[3]
                };
                const std::array<double, kVertexCount> scanRatesMvPerS = {
                    0.0,
                    p.scanRateMvPerS[1],
                    p.scanRateMvPerS[2],
                    p.scanRateMvPerS[1],
                    p.scanRateMvPerS[3]
                };

                for (int i = 0; i < kVertexCount; ++i) {
                    impl_.blDefineBool("vs_initial", vsInitial[static_cast<std::size_t>(i)], i, &params[paramIndex++]);
                }
                for (int i = 0; i < kVertexCount; ++i) {
                    impl_.blDefineSgl("Voltage_step", static_cast<float>(voltages[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);
                }
                for (int i = 0; i < kVertexCount; ++i) {
                    impl_.blDefineSgl("Scan_Rate", scanRateVoltsPerSecond(scanRatesMvPerS[static_cast<std::size_t>(i)]), i, &params[paramIndex++]);
                }

                impl_.blDefineInt ("Scan_number",       2,                                     0, &params[paramIndex++]);
                impl_.blDefineSgl ("Record_every_dE",   static_cast<float>(p.recordDE),       0, &params[paramIndex++]);
                impl_.blDefineBool("Average_over_dE",   p.averageOverDE,                      0, &params[paramIndex++]);
                impl_.blDefineInt ("N_Cycles",          p.nCycles,                             0, &params[paramIndex++]);
                impl_.blDefineSgl ("Begin_measuring_I", static_cast<float>(p.beginMeasuringI),0, &params[paramIndex++]);
                impl_.blDefineSgl ("End_measuring_I",   static_cast<float>(p.endMeasuringI),  0, &params[paramIndex++]);
                impl_.blDefineInt ("I_Range",           p.iRange,                              0, &params[paramIndex++]);
                impl_.blDefineInt ("E_Range",           p.eRange,                              0, &params[paramIndex++]);
                impl_.blDefineInt ("Bandwidth",         p.bandwidth,                           0, &params[paramIndex++]);

                TEccParams_t eccParams;
                eccParams.len = paramIndex;
                eccParams.pParams = params.data();

                const QString tech = impl_.techFile("cv4.ecc", "cv5.ecc", "cv.ecc");
                const int rc = impl_.blLoadTechnique(
                    impl_.connectionId, ch, tech.toLatin1().constData(), eccParams, first, last, false);
                if (rc != ERR_OK) {
                    throwErr(QString("BL_LoadTechnique CV (CVA simple) : %1").arg(impl_.errorMsg(rc)));
                }
            };

        const bool hasInitialHold = p.durationStep[0] > 1e-9;
        const bool hasFinalHold = p.durationStep[1] > 1e-9;
        bool firstTechnique = true;

        if (hasInitialHold) {
            loadCaHold(
                p.voltageStep[0],
                p.vsInitialStep[0],
                p.durationStep[0],
                p.recordDtStep[0],
                true,
                false);
            firstTechnique = false;
        }

        loadCvScan(firstTechnique, !hasFinalHold);

        if (hasFinalHold) {
            loadCaHold(
                p.voltageStep[1],
                p.vsInitialStep[1],
                p.durationStep[1],
                p.recordDtStep[1],
                false,
                true);
        }

        const int rc = impl_.blStartChannel(impl_.connectionId, ch);
        if (rc != ERR_OK) throwErr(QString("BL_StartChannel : %1").arg(impl_.errorMsg(rc)));

        lastStartDetails_ = QString("[POTDBG] controller start: technique=CVA simple path=emulated-ca-cv-ca model=%1 boardType=%2 tech=cv scanRateSdk=%3 V/s holdEi=%4 s holdEf=%5 s")
            .arg(connectedModel_)
            .arg(impl_.boardType)
            .arg(scanRateVoltsPerSecond(p.scanRateMvPerS[1]), 0, 'f', 6)
            .arg(p.durationStep[0], 0, 'f', 3)
            .arg(p.durationStep[1], 0, 'f', 3);

        return true;
    } catch (const std::exception& ex) {
        lastError_ = QString::fromUtf8(ex.what());
        lastStartDetails_.clear();
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
        result.techniqueId = info.TechniqueID;
        result.processIndex = info.ProcessIndex;
        result.nbRows = info.NbRows;
        result.nbCols = info.NbCols;
        result.currentState = curr.State;
        result.startTime = info.StartTime;

        for (int i = 0; i < info.NbRows; ++i) {
            const int offset = i * info.NbCols;

            double t = info.StartTime;
            if (impl_.blConvertTimeSecs) {
                double dt = 0.0;
                impl_.blConvertTimeSecs(&buf.data[offset], &dt, curr.TimeBase, impl_.boardType);
                t += dt;
            }

            float ewe = 0.0f;
            float I = 0.0f;
            switch (info.TechniqueID) {
            case KBIO_TECHID_CV:
            case KBIO_TECHID_CVA:
            case KBIO_TECHID_PDYN:
                // CV/CVA/VSCAN scans return VMP3 rows as:
                //   t_high, t_low, Ec, <I>, <Ewe>, cycle
                // and VMP-300/SP rows as:
                //   t_high, t_low, <I>, <Ewe>, cycle
                if (impl_.blConvertNumSgl) {
                    if (info.NbCols >= 6) {
                        impl_.blConvertNumSgl(buf.data[offset + 3], &I, impl_.boardType);
                        impl_.blConvertNumSgl(buf.data[offset + 4], &ewe, impl_.boardType);
                    } else if (info.NbCols >= 5) {
                        impl_.blConvertNumSgl(buf.data[offset + 2], &I, impl_.boardType);
                        impl_.blConvertNumSgl(buf.data[offset + 3], &ewe, impl_.boardType);
                    } else if (info.NbCols >= 4) {
                        impl_.blConvertNumSgl(buf.data[offset + 2], &ewe, impl_.boardType);
                        impl_.blConvertNumSgl(buf.data[offset + 3], &I, impl_.boardType);
                    }
                }
                break;
            case KBIO_TECHID_OCV:
                // Official OCV format: t_high, t_low, Ewe, [Ece].
                if (info.NbCols >= 3 && impl_.blConvertNumSgl) {
                    impl_.blConvertNumSgl(buf.data[offset + 2], &ewe, impl_.boardType);
                }
                I = 0.0f;
                break;
            case KBIO_TECHID_CA:
            case KBIO_TECHID_CP:
            default:
                // CA/CP format: t_high, t_low, Ewe, I, cycle.
                if (info.NbCols >= 3 && impl_.blConvertNumSgl) {
                    impl_.blConvertNumSgl(buf.data[offset + 2], &ewe, impl_.boardType);
                }
                if (info.NbCols >= 4 && impl_.blConvertNumSgl) {
                    impl_.blConvertNumSgl(buf.data[offset + 3], &I, impl_.boardType);
                }
                break;
            }

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
        result.state       = curr.State;
        result.elapsedTime = static_cast<double>(curr.ElapsedTime);
        result.ewe         = static_cast<double>(curr.Ewe);
        result.I           = static_cast<double>(curr.I);
        result.ece         = static_cast<double>(curr.Ece);
        result.iRange      = curr.IRange;
        result.eOverflow   = curr.Eoverflow;
        result.iOverflow   = curr.Ioverflow;
    } catch (const std::exception& ex) {
        result.ok    = false;
        result.error = QString::fromUtf8(ex.what());
    }
    return result;
}

} // namespace laserbench::hardware
