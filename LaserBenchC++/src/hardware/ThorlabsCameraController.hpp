#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "hardware/DeviceContracts.hpp"

namespace laserbench::hardware {

class ThorlabsCameraController final : public ICameraController
{
public:
    ThorlabsCameraController();
    ~ThorlabsCameraController() override;

    QString displayName() const override;
    core::DeviceState state() const override;
    QString backendSummary() const override;
    QString cameraIdentifier() const override;
    QString previewSummary() const override;
    QStringList discoverAvailableCameras() override;
    QString selectedCamera() const override;
    bool isConnected() const override;
    bool isLive() const override;
    void connectCamera(const QString& cameraId) override;
    void disconnectCamera() override;
    void startLive() override;
    void stopLive() override;
    bool refreshFrame() override;
    double exposureTimeUs() const override;
    double gain() const override;
    void setExposureTimeUs(double exposureUs) override;
    void setGain(double gain) override;
    QImage previewFrame() const override;

private:
    void ensureSdkLoaded();
    void ensureSdkOpen();
    void ensureCameraOpen() const;
    void ensureMonoToColorSdkLoaded();
    void allocateFrameBuffersLocked();
    void configureColorPipelineLocked();
    void releaseColorPipelineLocked();
    void applyGainIfSupportedLocked();

    void startAcquisitionLoop();
    void stopAcquisitionLoop();
    void acquisitionLoop();
    bool captureFrame();
    void publishFrame(const QImage& frame);

    [[noreturn]] void throwLastError(const QString& context) const;
    [[noreturn]] void throwColorError(const QString& context) const;

    mutable std::mutex cameraMutex_;
    mutable std::mutex frameMutex_;
    std::thread acquisitionThread_;
    std::atomic_bool acquisitionStopRequested_ {false};

    QStringList discoveredCameras_;
    QString selectedCameraId_;
    QString cameraLabel_ {"Thorlabs"};
    core::DeviceState state_ {core::DeviceState::Disconnected};
    void* cameraHandle_ {nullptr};
    bool dllInitialized_ {false};
    bool sdkOpen_ {false};
    bool monoToColorSdkOpen_ {false};
    std::atomic<bool> live_ {false};
    int imageWidth_ {0};
    int imageHeight_ {0};
    int lastFrameCount_ {-1};
    int bitDepth_ {16};
    bool colorFrameReady_ {false};
    void* monoToColorProcessor_ {nullptr};
    double exposureUs_ {10000.0};
    double gainDb_ {0.0};
    std::array<QImage, 2> frameBuffers_;
    std::vector<unsigned short> acquisitionRawBuffer_;
    int activeFrameBufferIndex_ {0};

    std::uint64_t publishedFrameSerial_ {0};
    std::uint64_t consumedFrameSerial_ {0};
    QImage publishedFrame_;
    QImage previewFrame_;
    QString pendingWorkerError_;
};

}  // namespace laserbench::hardware
