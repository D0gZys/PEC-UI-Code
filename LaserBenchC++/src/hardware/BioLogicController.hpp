#pragma once

#include "hardware/DeviceContracts.hpp"

#include <QString>

#include <mutex>
#include <vector>

#define NOMINMAX
#include <windows.h>

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
    int    channel  {1};
    double duration {1.0};
    double recordDt {0.1};
    int    eRange   {0};
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
    std::vector<PotDataPoint> points;
    QString                   error;
};

struct PotCurrentValues
{
    bool    ok      {false};
    bool    stopped {false};
    double  elapsedTime {0.0};
    double  ewe     {0.0};
    double  I       {0.0};
    QString error;
};

class BioLogicController final : public IPotentiostatController
{
public:
    explicit BioLogicController(const QString& helperScriptPath);
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
    void             stopChannel(int channel);
    PotDataResult    getData(int channel);
    PotCurrentValues getCurrentValues(int channel);

    bool    isConnected()    const { return connected_; }
    QString connectedModel() const { return connectedModel_; }
    QString lastError()      const { return lastError_; }

private:
    struct Impl
    {
        mutable std::mutex transportMutex;
        HANDLE processHandle {nullptr};
        HANDLE stdinWrite    {nullptr};
        HANDLE stdoutRead    {nullptr};
        bool   ready         {false};   // helper sent {"ready":true}

        ~Impl() { shutdown(); }

        bool isRunning() const
        {
            return processHandle != nullptr
                && WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT;
        }

        void shutdown();
        void ensureStarted(const QString& helperScriptPath);
    };

    QString sendRawCommand(const QString& jsonLine);

    QString helperScriptPath_;
    Impl    impl_;
    bool    connected_      {false};
    QString connectedModel_;
    QString lastError_;
};

} // namespace laserbench::hardware
