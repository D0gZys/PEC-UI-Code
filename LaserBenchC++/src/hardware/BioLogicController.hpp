#pragma once

#include "hardware/DeviceContracts.hpp"

#include <QString>

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

#define NOMINMAX
#include <windows.h>

#include "BLStructs.h"

namespace laserbench::hardware {

struct CaParams
{
    int    channel   {1};
    double voltage   {0.5};
    double duration  {1.0};
    bool   vsInit    {false};
    double recordDt  {0.1};
    int    iRange    {12};    // I_RANGE_AUTO
    int    eRange    {0};     // E_RANGE_2_5V
    int    bandwidth {8};
    int    nCycles   {0};
};

struct OcvParams
{
    int    channel   {1};
    double duration  {1.0};
    double recordDt  {0.1};
    double recordDE  {0.01};
    int    eRange    {0};
};

struct CvaParams
{
    int                      channel             {1};
    std::array<bool, 4>      vsInitialScan      {false, false, false, false};
    std::array<double, 4>    voltageScan        {0.0, 2.5, -0.2, 0.0};
    std::array<double, 4>    scanRateMvPerS     {80.0, 80.0, 80.0, 80.0};
    double                   recordDE           {0.001};
    bool                     averageOverDE      {true};
    int                      nCycles            {1};
    double                   beginMeasuringI    {0.5};
    double                   endMeasuringI      {1.0};
    std::array<bool, 2>      vsInitialStep      {false, false};
    std::array<double, 2>    voltageStep        {0.0, 0.0};
    std::array<double, 2>    durationStep       {5.0, 5.0};
    std::array<double, 2>    recordDtStep       {1.0, 0.1};
    double                   recordDt           {0.1};
    double                   recordDI           {1.0};
    bool                     trigOnOff          {false};
    int                      iRange             {12};
    int                      eRange             {0};
    int                      bandwidth          {5};
};

struct PotDataPoint
{
    double t   {0.0};
    double ewe {0.0};
    double I   {0.0};
};

struct PotDataResult
{
    bool                      ok      {false};
    bool                      stopped {false};
    int                       techniqueId {-1};
    int                       processIndex {-1};
    int                       nbRows {0};
    int                       nbCols {0};
    int                       currentState {-1};
    double                    startTime {0.0};
    std::vector<PotDataPoint> points;
    QString                   error;
};

struct PotCurrentValues
{
    bool    ok          {false};
    bool    stopped     {false};
    int     state       {-1};   // TChannelState_e: 0=STOP, 1=RUN, 2=PAUSE
    double  elapsedTime {0.0};
    double  ewe         {0.0};
    double  I           {0.0};
    double  ece         {0.0};
    int     iRange      {-1};   // TIntensityRange_e
    int     eOverflow   {0};
    int     iOverflow   {0};
    QString error;
};

class BioLogicController final : public IPotentiostatController
{
public:
    BioLogicController();
    ~BioLogicController() override;

    QString           displayName()        const override;
    core::DeviceState state()              const override;
    QString           backendSummary()     const override;
    QString           channelSummary()     const override;
    QString           acquisitionSummary() const override;

    bool             connect(const QString& dllPath, const QString& address, int channel);
    void             disconnect();
    void             loadFirmware(int channel);
    bool             startCa(const CaParams& p);
    bool             startOcv(const OcvParams& p);
    bool             startCva(const CvaParams& p);
    bool             startCvaSimple(const CvaParams& p);
    void             stopChannel(int channel);
    PotDataResult    getData(int channel);
    PotCurrentValues getCurrentValues(int channel);

    bool    isConnected()    const { return connected_; }
    QString connectedModel() const { return connectedModel_; }
    QString lastError()      const { return lastError_; }
    QString lastStartDetails() const { return lastStartDetails_; }

private:
    // Function pointer types (all __stdcall as per BioLogic API)
    using FnConnect              = int(__stdcall*)(const char*, uint8, int*, TDeviceInfos_t*);
    using FnDisconnect           = int(__stdcall*)(int);
    using FnLoadFirmware         = int(__stdcall*)(int, uint8*, int*, uint8, bool, bool, const char*, const char*);
    using FnLoadTechnique        = int(__stdcall*)(int, uint8, const char*, TEccParams_t, bool, bool, bool);
    using FnStartChannel         = int(__stdcall*)(int, uint8);
    using FnStopChannel          = int(__stdcall*)(int, uint8);
    using FnGetData              = int(__stdcall*)(int, uint8, TDataBuffer_t*, TDataInfos_t*, TCurrentValues_t*);
    using FnGetCurrentValues     = int(__stdcall*)(int, uint8, TCurrentValues_t*);
    using FnGetChannelBoardType  = int(__stdcall*)(int32_t, uint8, uint32_t*);
    using FnDefineSgl            = int(__stdcall*)(const char*, float, int, TEccParam_t*);
    using FnDefineBool           = int(__stdcall*)(const char*, bool, int, TEccParam_t*);
    using FnDefineInt            = int(__stdcall*)(const char*, int, int, TEccParam_t*);
    using FnConvertNumSgl        = int(__stdcall*)(uint32_t, float*, uint32_t);
    using FnConvertTimeSecs      = int(__stdcall*)(uint32_t*, double*, float, uint32_t);
    using FnGetErrorMsg          = int(__stdcall*)(int, char*, unsigned int*);

    struct Impl
    {
        HMODULE hDll             {nullptr};
        int     connectionId     {-1};
        uint32_t boardType       {0};
        QString  dllDir;
        mutable std::mutex mutex;

        FnConnect             blConnect             {nullptr};
        FnDisconnect          blDisconnect          {nullptr};
        FnLoadFirmware        blLoadFirmware        {nullptr};
        FnLoadTechnique       blLoadTechnique       {nullptr};
        FnStartChannel        blStartChannel        {nullptr};
        FnStopChannel         blStopChannel         {nullptr};
        FnGetData             blGetData             {nullptr};
        FnGetCurrentValues    blGetCurrentValues    {nullptr};
        FnGetChannelBoardType blGetChannelBoardType {nullptr};
        FnDefineSgl           blDefineSgl           {nullptr};
        FnDefineBool          blDefineBool          {nullptr};
        FnDefineInt           blDefineInt           {nullptr};
        FnConvertNumSgl       blConvertNumSgl       {nullptr};
        FnConvertTimeSecs     blConvertTimeSecs     {nullptr};
        FnGetErrorMsg         blGetErrorMsg         {nullptr};

        ~Impl() { unload(); }

        bool    load(const QString& dllPath);
        void    unload();
        bool    isLoaded()    const { return hDll != nullptr; }
        bool    isConnected() const { return connectionId >= 0; }
        QString errorMsg(int code) const;

        QString techFile(const char* base4, const char* base5, const char* base) const;
    };

    Impl    impl_;
    bool    connected_      {false};
    QString connectedModel_;
    QString lastError_;
    QString lastStartDetails_;
};

} // namespace laserbench::hardware
