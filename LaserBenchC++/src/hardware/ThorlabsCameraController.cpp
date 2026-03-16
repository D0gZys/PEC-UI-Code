#include "hardware/ThorlabsCameraController.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

extern "C" {
#include "tl_camera_sdk.h"
#include "tl_camera_sdk_load.h"
#include "tl_color_enum.h"
#include "tl_mono_to_color_processing_load.h"
}

namespace laserbench::hardware {

namespace {

QString sdkErrorText()
{
    const char* error = tl_camera_get_last_error != nullptr ? tl_camera_get_last_error() : nullptr;
    return error == nullptr ? QString("Unknown camera SDK error") : QString::fromLocal8Bit(error);
}

QString colorErrorText()
{
    const char* error = tl_mono_to_color_get_last_error != nullptr ? tl_mono_to_color_get_last_error() : nullptr;
    return error == nullptr ? QString("Unknown mono-to-color SDK error") : QString::fromLocal8Bit(error);
}

QStringList parseCameraIds(const char* cameraIds)
{
    const QString raw = QString::fromLocal8Bit(cameraIds == nullptr ? "" : cameraIds).trimmed();
    return raw.isEmpty() ? QStringList {} : raw.split(' ', Qt::SkipEmptyParts);
}

}  // namespace

ThorlabsCameraController::ThorlabsCameraController() = default;

ThorlabsCameraController::~ThorlabsCameraController()
{
    try {
        disconnectCamera();
    } catch (...) {
    }

    if (sdkOpen_) {
        tl_camera_close_sdk();
        sdkOpen_ = false;
    }
    if (dllInitialized_) {
        tl_camera_sdk_dll_terminate();
        dllInitialized_ = false;
    }
    if (monoToColorSdkOpen_) {
        tl_mono_to_color_processing_terminate();
        monoToColorSdkOpen_ = false;
    }
}

QString ThorlabsCameraController::displayName() const
{
    return "Camera microscope";
}

core::DeviceState ThorlabsCameraController::state() const
{
    std::lock_guard<std::mutex> lock(cameraMutex_);
    return state_;
}

QString ThorlabsCameraController::backendSummary() const
{
    return "Thorlabs Native C SDK";
}

QString ThorlabsCameraController::cameraIdentifier() const
{
    std::lock_guard<std::mutex> lock(cameraMutex_);
    return cameraLabel_;
}

QString ThorlabsCameraController::previewSummary() const
{
    return "Preview live asynchrone avec zoom souris/trackpad";
}

QStringList ThorlabsCameraController::discoverAvailableCameras()
{
    std::lock_guard<std::mutex> lock(cameraMutex_);
    ensureSdkOpen();

    char cameraIds[4096] {};
    if (tl_camera_discover_available_cameras(cameraIds, static_cast<int>(sizeof(cameraIds))) != 0) {
        state_ = core::DeviceState::Error;
        throwLastError("Camera discovery failed");
    }

    discoveredCameras_ = parseCameraIds(cameraIds);
    if (cameraHandle_ == nullptr) {
        state_ = core::DeviceState::Disconnected;
    }
    return discoveredCameras_;
}

QString ThorlabsCameraController::selectedCamera() const
{
    std::lock_guard<std::mutex> lock(cameraMutex_);
    return selectedCameraId_;
}

bool ThorlabsCameraController::isConnected() const
{
    std::lock_guard<std::mutex> lock(cameraMutex_);
    return cameraHandle_ != nullptr;
}

bool ThorlabsCameraController::isLive() const
{
    return live_.load(std::memory_order_relaxed);
}

void ThorlabsCameraController::connectCamera(const QString& cameraId)
{
    disconnectCamera();

    QByteArray serial = cameraId.trimmed().toLocal8Bit();
    if (serial.isEmpty()) {
        throw std::runtime_error("Aucune camera selectionnee.");
    }

    try {
        std::lock_guard<std::mutex> lock(cameraMutex_);
        ensureSdkOpen();

        if (tl_camera_open_camera(serial.data(), &cameraHandle_) != 0) {
            state_ = core::DeviceState::Error;
            throwLastError("Camera connection failed");
        }

        selectedCameraId_ = QString::fromLocal8Bit(serial);
        discoveredCameras_.removeAll(selectedCameraId_);
        discoveredCameras_.prepend(selectedCameraId_);

        char model[256] {};
        if (tl_camera_get_model(cameraHandle_, model, static_cast<int>(sizeof(model))) == 0) {
            cameraLabel_ = QString("%1 (%2)").arg(QString::fromLocal8Bit(model), selectedCameraId_);
        } else {
            cameraLabel_ = QString("Thorlabs (%1)").arg(selectedCameraId_);
        }

        if (tl_camera_get_image_width(cameraHandle_, &imageWidth_) != 0
            || tl_camera_get_image_height(cameraHandle_, &imageHeight_) != 0
            || tl_camera_get_bit_depth(cameraHandle_, &bitDepth_) != 0) {
            state_ = core::DeviceState::Error;
            throwLastError("Camera metadata query failed");
        }

        long long exposureUs = 0;
        if (tl_camera_get_exposure_time(cameraHandle_, &exposureUs) == 0) {
            exposureUs_ = static_cast<double>(exposureUs);
        } else {
            tl_camera_set_exposure_time(cameraHandle_, static_cast<long long>(exposureUs_));
        }

        int gainIndex = 0;
        if (tl_camera_get_gain(cameraHandle_, &gainIndex) == 0) {
            double gainDb = 0.0;
            if (tl_camera_convert_gain_to_decibels(cameraHandle_, gainIndex, &gainDb) == 0) {
                gainDb_ = gainDb;
            }
        }

        configureColorPipelineLocked();
        allocateFrameBuffersLocked();
        activeFrameBufferIndex_ = 0;
        lastFrameCount_ = -1;
        live_ = false;
        state_ = core::DeviceState::Connected;
    } catch (...) {
        std::lock_guard<std::mutex> lock(cameraMutex_);
        releaseColorPipelineLocked();
        if (cameraHandle_ != nullptr) {
            tl_camera_close_camera(cameraHandle_);
            cameraHandle_ = nullptr;
        }
        selectedCameraId_.clear();
        cameraLabel_ = "Thorlabs";
        imageWidth_ = 0;
        imageHeight_ = 0;
        lastFrameCount_ = -1;
        bitDepth_ = 16;
        frameBuffers_ = {};
        activeFrameBufferIndex_ = 0;
        throw;
    }

    std::lock_guard<std::mutex> frameLock(frameMutex_);
    publishedFrame_ = QImage();
    previewFrame_ = QImage();
    pendingWorkerError_.clear();
    publishedFrameSerial_ = 0;
    consumedFrameSerial_ = 0;
}

void ThorlabsCameraController::disconnectCamera()
{
    stopLive();

    {
        std::lock_guard<std::mutex> lock(cameraMutex_);
        releaseColorPipelineLocked();

        if (cameraHandle_ != nullptr) {
            tl_camera_close_camera(cameraHandle_);
            cameraHandle_ = nullptr;
        }

        state_ = core::DeviceState::Disconnected;
        selectedCameraId_.clear();
        cameraLabel_ = "Thorlabs";
        imageWidth_ = 0;
        imageHeight_ = 0;
        lastFrameCount_ = -1;
        bitDepth_ = 16;
        frameBuffers_ = {};
        activeFrameBufferIndex_ = 0;
    }

    std::lock_guard<std::mutex> frameLock(frameMutex_);
    publishedFrame_ = QImage();
    previewFrame_ = QImage();
    pendingWorkerError_.clear();
    publishedFrameSerial_ = 0;
    consumedFrameSerial_ = 0;
}

void ThorlabsCameraController::startLive()
{
    {
        std::lock_guard<std::mutex> lock(cameraMutex_);
        ensureCameraOpen();
        if (live_) {
            return;
        }

        if (tl_camera_set_exposure_time(cameraHandle_, static_cast<long long>(exposureUs_)) != 0) {
            state_ = core::DeviceState::Error;
            throwLastError("Camera exposure update failed");
        }
        applyGainIfSupportedLocked();

        if (tl_camera_set_frames_per_trigger_zero_for_unlimited(cameraHandle_, 0) != 0
            || tl_camera_set_image_poll_timeout(cameraHandle_, 0) != 0
            || tl_camera_arm(cameraHandle_, 2) != 0
            || tl_camera_issue_software_trigger(cameraHandle_) != 0) {
            state_ = core::DeviceState::Error;
            throwLastError("Camera live start failed");
        }

        lastFrameCount_ = -1;
        live_.store(true, std::memory_order_release);
        state_ = core::DeviceState::Connected;
    }

    {
        std::lock_guard<std::mutex> frameLock(frameMutex_);
        pendingWorkerError_.clear();
        publishedFrameSerial_ = 0;
        consumedFrameSerial_ = 0;
        publishedFrame_ = QImage();
    }

    startAcquisitionLoop();
}

void ThorlabsCameraController::stopLive()
{
    stopAcquisitionLoop();

    std::lock_guard<std::mutex> lock(cameraMutex_);
    if (cameraHandle_ == nullptr || !live_.load(std::memory_order_acquire)) {
        live_.store(false, std::memory_order_release);
        return;
    }

    tl_camera_disarm(cameraHandle_);
    live_.store(false, std::memory_order_release);
    state_ = core::DeviceState::Connected;
}

bool ThorlabsCameraController::refreshFrame()
{
    QString workerError;
    {
        std::lock_guard<std::mutex> frameLock(frameMutex_);
        if (!pendingWorkerError_.isEmpty()) {
            workerError = pendingWorkerError_;
            pendingWorkerError_.clear();
        } else if (publishedFrameSerial_ != consumedFrameSerial_) {
            previewFrame_ = publishedFrame_;
            consumedFrameSerial_ = publishedFrameSerial_;
            return true;
        }
    }

    if (!workerError.isEmpty()) {
        stopAcquisitionLoop();

        std::lock_guard<std::mutex> cameraLock(cameraMutex_);
        if (cameraHandle_ != nullptr && live_) {
            tl_camera_disarm(cameraHandle_);
        }
        live_ = false;
        state_ = core::DeviceState::Error;
        throw std::runtime_error(workerError.toStdString());
    }

    return false;
}

double ThorlabsCameraController::exposureTimeUs() const
{
    std::lock_guard<std::mutex> lock(cameraMutex_);
    return exposureUs_;
}

double ThorlabsCameraController::gain() const
{
    std::lock_guard<std::mutex> lock(cameraMutex_);
    return gainDb_;
}

void ThorlabsCameraController::setExposureTimeUs(double exposureUs)
{
    std::lock_guard<std::mutex> lock(cameraMutex_);
    exposureUs_ = exposureUs;
    if (cameraHandle_ == nullptr) {
        return;
    }

    if (tl_camera_set_exposure_time(cameraHandle_, static_cast<long long>(exposureUs_)) != 0) {
        state_ = core::DeviceState::Error;
        throwLastError("Camera exposure update failed");
    }
}

void ThorlabsCameraController::setGain(double gain)
{
    std::lock_guard<std::mutex> lock(cameraMutex_);
    gainDb_ = gain;
    if (cameraHandle_ == nullptr) {
        return;
    }

    applyGainIfSupportedLocked();
}

QImage ThorlabsCameraController::previewFrame() const
{
    std::lock_guard<std::mutex> lock(frameMutex_);
    return previewFrame_;
}

void ThorlabsCameraController::ensureSdkLoaded()
{
    if (dllInitialized_) {
        return;
    }

    if (tl_camera_sdk_dll_initialize() != 0) {
        state_ = core::DeviceState::Error;
        throw std::runtime_error("Impossible de charger la DLL Thorlabs Camera SDK.");
    }

    dllInitialized_ = true;
}

void ThorlabsCameraController::ensureSdkOpen()
{
    ensureSdkLoaded();
    if (sdkOpen_) {
        return;
    }

    if (tl_camera_open_sdk() != 0) {
        state_ = core::DeviceState::Error;
        throwLastError("Camera SDK open failed");
    }

    sdkOpen_ = true;
}

void ThorlabsCameraController::ensureCameraOpen() const
{
    if (cameraHandle_ == nullptr) {
        throw std::runtime_error("Aucune camera connectee.");
    }
}

void ThorlabsCameraController::ensureMonoToColorSdkLoaded()
{
    if (monoToColorSdkOpen_) {
        return;
    }

    if (tl_mono_to_color_processing_initialize() != 0) {
        state_ = core::DeviceState::Error;
        throwColorError("Mono-to-color SDK init failed");
    }

    monoToColorSdkOpen_ = true;
}

void ThorlabsCameraController::allocateFrameBuffersLocked()
{
    frameBuffers_[0] = QImage(imageWidth_, imageHeight_, QImage::Format_RGB888);
    frameBuffers_[1] = QImage(imageWidth_, imageHeight_, QImage::Format_RGB888);
}

void ThorlabsCameraController::configureColorPipelineLocked()
{
    releaseColorPipelineLocked();

    enum TL_CAMERA_SENSOR_TYPE sensorType = TL_CAMERA_SENSOR_TYPE_MONOCHROME;
    if (tl_camera_get_camera_sensor_type(cameraHandle_, &sensorType) != 0) {
        state_ = core::DeviceState::Error;
        throwLastError("Camera sensor type query failed");
    }

    if (sensorType != TL_CAMERA_SENSOR_TYPE_BAYER) {
        colorFrameReady_ = false;
        return;
    }

    ensureMonoToColorSdkLoaded();

    enum TL_COLOR_FILTER_ARRAY_PHASE colorFilterPhase = TL_COLOR_FILTER_ARRAY_PHASE_BAYER_RED;
    float colorCorrectionMatrix[9] {};
    float whiteBalanceMatrix[9] {};

    if (tl_camera_get_color_filter_array_phase(cameraHandle_, &colorFilterPhase) != 0
        || tl_camera_get_color_correction_matrix(cameraHandle_, colorCorrectionMatrix) != 0
        || tl_camera_get_default_white_balance_matrix(cameraHandle_, whiteBalanceMatrix) != 0) {
        state_ = core::DeviceState::Error;
        throwLastError("Camera color metadata query failed");
    }

    if (tl_mono_to_color_create_mono_to_color_processor(
            sensorType,
            colorFilterPhase,
            colorCorrectionMatrix,
            whiteBalanceMatrix,
            bitDepth_,
            &monoToColorProcessor_) != 0) {
        state_ = core::DeviceState::Error;
        throwColorError("Mono-to-color processor creation failed");
    }

    if (tl_mono_to_color_set_output_format != nullptr
        && tl_mono_to_color_set_output_format(monoToColorProcessor_, TL_COLOR_FORMAT_RGB_PIXEL) != 0) {
        state_ = core::DeviceState::Error;
        throwColorError("Mono-to-color output format setup failed");
    }

    colorFrameReady_ = true;
}

void ThorlabsCameraController::releaseColorPipelineLocked()
{
    if (monoToColorProcessor_ != nullptr) {
        tl_mono_to_color_destroy_mono_to_color_processor(monoToColorProcessor_);
        monoToColorProcessor_ = nullptr;
    }

    colorFrameReady_ = false;
}

void ThorlabsCameraController::applyGainIfSupportedLocked()
{
    int gainMin = 0;
    int gainMax = 0;
    if (tl_camera_get_gain_range(cameraHandle_, &gainMin, &gainMax) != 0) {
        state_ = core::DeviceState::Error;
        throwLastError("Camera gain range query failed");
    }
    if (gainMax <= 0) {
        return;
    }

    int gainIndex = 0;
    if (tl_camera_convert_decibels_to_gain(cameraHandle_, gainDb_, &gainIndex) != 0
        || tl_camera_set_gain(cameraHandle_, gainIndex) != 0) {
        state_ = core::DeviceState::Error;
        throwLastError("Camera gain update failed");
    }
}

void ThorlabsCameraController::startAcquisitionLoop()
{
    stopAcquisitionLoop();
    acquisitionStopRequested_.store(false);
    acquisitionThread_ = std::thread(&ThorlabsCameraController::acquisitionLoop, this);
}

void ThorlabsCameraController::stopAcquisitionLoop()
{
    acquisitionStopRequested_.store(true);
    if (acquisitionThread_.joinable()) {
        acquisitionThread_.join();
    }
    acquisitionStopRequested_.store(false);
}

void ThorlabsCameraController::acquisitionLoop()
{
    while (!acquisitionStopRequested_.load()) {
        try {
            if (!captureFrame()) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> frameLock(frameMutex_);
            pendingWorkerError_ = QString::fromUtf8(ex.what());
            return;
        }
    }
}

bool ThorlabsCameraController::captureFrame()
{
    // ---- Step 1: hold cameraMutex_ only long enough to get the raw pixel pointer
    //              and memcpy it to our private buffer. --------------------------------
    int capturedWidth = 0;
    int capturedHeight = 0;
    int capturedBitDepth = 0;
    bool capturedColorReady = false;

    {
        std::lock_guard<std::mutex> lock(cameraMutex_);
        if (cameraHandle_ == nullptr || !live_.load(std::memory_order_relaxed)) {
            return false;
        }

        unsigned short* imageBuffer = nullptr;
        int frameCount = 0;
        unsigned char* metadata = nullptr;
        int metadataSize = 0;

        if (tl_camera_get_pending_frame_or_null(cameraHandle_, &imageBuffer, &frameCount, &metadata, &metadataSize) != 0) {
            state_ = core::DeviceState::Error;
            throwLastError("Camera frame polling failed");
        }

        Q_UNUSED(metadata);
        Q_UNUSED(metadataSize);

        if (imageBuffer == nullptr || imageWidth_ <= 0 || imageHeight_ <= 0) {
            return false;
        }
        if (frameCount == lastFrameCount_) {
            return false;
        }
        lastFrameCount_ = frameCount;

        // Copy raw pixels while holding the lock (fast memcpy, ~1 ms for a 5 MP sensor).
        // The conversion happens outside the lock so isLive() / setExposure() / etc.
        // are never blocked during the expensive colour-transform step.
        const std::size_t pixelCount = static_cast<std::size_t>(imageWidth_) * static_cast<std::size_t>(imageHeight_);
        acquisitionRawBuffer_.resize(pixelCount);
        std::memcpy(acquisitionRawBuffer_.data(), imageBuffer, pixelCount * sizeof(unsigned short));

        capturedWidth = imageWidth_;
        capturedHeight = imageHeight_;
        capturedBitDepth = bitDepth_;
        capturedColorReady = colorFrameReady_ && (monoToColorProcessor_ != nullptr);
    }

    // ---- Step 2: convert outside the lock. -----------------------------------------
    // monoToColorProcessor_, frameBuffers_, activeFrameBufferIndex_ are only touched
    // by this thread during live mode, so no lock needed here.
    const int nextBufferIndex = 1 - activeFrameBufferIndex_;
    if (frameBuffers_[nextBufferIndex].isNull()
        || frameBuffers_[nextBufferIndex].size() != QSize(capturedWidth, capturedHeight)
        || frameBuffers_[nextBufferIndex].format() != QImage::Format_RGB888) {
        frameBuffers_[nextBufferIndex] = QImage(capturedWidth, capturedHeight, QImage::Format_RGB888);
    }

    unsigned char* rgbBuffer = frameBuffers_[nextBufferIndex].bits();

    if (capturedColorReady) {
        if (tl_mono_to_color_transform_to_24(
                monoToColorProcessor_,
                acquisitionRawBuffer_.data(),
                capturedWidth,
                capturedHeight,
                rgbBuffer) != 0) {
            std::lock_guard<std::mutex> lock(cameraMutex_);
            state_ = core::DeviceState::Error;
            throwColorError("Camera color transform failed");
        }
    } else {
        const int shift = std::max(capturedBitDepth - 8, 0);
        for (int y = 0; y < capturedHeight; ++y) {
            auto* line = frameBuffers_[nextBufferIndex].scanLine(y);
            const auto* src = acquisitionRawBuffer_.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(capturedWidth));
            for (int x = 0; x < capturedWidth; ++x) {
                const unsigned char value = static_cast<unsigned char>(src[x] >> shift);
                line[(x * 3) + 0] = value;
                line[(x * 3) + 1] = value;
                line[(x * 3) + 2] = value;
            }
        }
    }

    activeFrameBufferIndex_ = nextBufferIndex;
    publishFrame(frameBuffers_[activeFrameBufferIndex_]);
    return true;
}

void ThorlabsCameraController::publishFrame(const QImage& frame)
{
    std::lock_guard<std::mutex> lock(frameMutex_);
    publishedFrame_ = frame;
    ++publishedFrameSerial_;
}

[[noreturn]] void ThorlabsCameraController::throwLastError(const QString& context) const
{
    const QString message = context + ": " + sdkErrorText();
    throw std::runtime_error(message.toStdString());
}

[[noreturn]] void ThorlabsCameraController::throwColorError(const QString& context) const
{
    const QString message = context + ": " + colorErrorText();
    throw std::runtime_error(message.toStdString());
}

}  // namespace laserbench::hardware
