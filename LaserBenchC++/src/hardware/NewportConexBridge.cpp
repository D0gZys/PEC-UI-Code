#include "hardware/NewportConexBridgeApi.hpp"

#include <msclr/marshal_cppstd.h>
#include <vcclr.h>

#include <chrono>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#using <System.dll>
#using <Newport.CONEXCC.CommandInterface.dll>

using namespace CommandInterfaceConexCC;
using namespace System;
using namespace msclr::interop;

namespace {

constexpr int kBridgeException = -10000;

const std::unordered_set<std::string> kNotReferencedStates {
    "0A", "0B", "0C", "0D", "0E", "0F"
};

const std::unordered_set<std::string> kBusyStates {
    "1E", "1F", "28", "29", "2A", "2B", "46", "47"
};

const std::unordered_set<std::string> kDisabledStates {
    "3C", "3D"
};

void clearBuffer(char* buffer, int size)
{
    if (buffer != nullptr && size > 0) {
        buffer[0] = '\0';
    }
}

void writeBuffer(const std::string& text, char* buffer, int size)
{
    if (buffer == nullptr || size <= 0) {
        return;
    }

    std::strncpy(buffer, text.c_str(), static_cast<size_t>(size - 1));
    buffer[size - 1] = '\0';
}

std::string normalizeState(const std::string& value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
    }
    return normalized;
}

std::string toUtf8(String^ value)
{
    return value == nullptr ? std::string() : marshal_as<std::string>(value);
}

String^ toManaged(const char* value)
{
    return marshal_as<String^>(std::string(value == nullptr ? "" : value));
}

std::string diagnosticText(ConexCC^ api, int address)
{
    try {
        String^ lastError = nullptr;
        String^ teErr = nullptr;
        const int teRet = api->TE(address, lastError, teErr);
        if (teRet != 0) {
            return std::string("TE failed (") + std::to_string(teRet) + ")";
        }

        const std::string errorCode = normalizeState(toUtf8(lastError));
        if (errorCode.empty() || errorCode == "0") {
            return {};
        }

        String^ tbError = nullptr;
        String^ tbErr = nullptr;
        const int tbRet = api->TB(address, lastError, tbError, tbErr);
        if (tbRet != 0) {
            return std::string("TE=") + errorCode + "; TB failed (" + std::to_string(tbRet) + ")";
        }

        return std::string("TE=") + errorCode + "; TB=" + toUtf8(tbError);
    } catch (...) {
        return {};
    }
}

std::string buildFailure(const char* command, int retCode, String^ errString, ConexCC^ api, int address)
{
    std::string message = std::string(command) + " failed (" + std::to_string(retCode) + ")";
    const std::string err = toUtf8(errString);
    if (!err.empty()) {
        message += " - " + err;
    }

    const std::string diagnostic = diagnosticText(api, address);
    if (!diagnostic.empty()) {
        message += " | " + diagnostic;
    }
    return message;
}

struct lb_newport_axis_handle
{
    std::mutex mutex;
    gcroot<ConexCC^> api;
    gcroot<String^> port;
    int address {1};
    bool connected {false};
};

int readStateLocked(
    lb_newport_axis_handle* handle,
    std::string* outErrorCode,
    std::string* outStateCode,
    std::string* outError
)
{
    if (handle == nullptr || !handle->connected) {
        if (outError != nullptr) {
            *outError = "Axis is not connected";
        }
        return kBridgeException;
    }

    String^ errorCode = nullptr;
    String^ stateCode = nullptr;
    String^ err = nullptr;
    const int ret = handle->api->TS(handle->address, errorCode, stateCode, err);
    if (ret != 0) {
        if (outError != nullptr) {
            *outError = buildFailure("TS", ret, err, handle->api, handle->address);
        }
        return ret;
    }

    if (outErrorCode != nullptr) {
        *outErrorCode = normalizeState(toUtf8(errorCode));
    }
    if (outStateCode != nullptr) {
        *outStateCode = normalizeState(toUtf8(stateCode));
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return 0;
}

int readPositionLocked(lb_newport_axis_handle* handle, double* outPositionMm, std::string* outError)
{
    if (handle == nullptr || !handle->connected) {
        if (outError != nullptr) {
            *outError = "Axis is not connected";
        }
        return kBridgeException;
    }

    double position = 0.0;
    String^ err = nullptr;
    const int ret = handle->api->TP(handle->address, position, err);
    if (ret != 0) {
        if (outError != nullptr) {
            *outError = buildFailure("TP", ret, err, handle->api, handle->address);
        }
        return ret;
    }

    if (outPositionMm != nullptr) {
        *outPositionMm = position;
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return 0;
}

int waitDone(lb_newport_axis_handle* handle, int timeoutMs, std::string* outError)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string lastState;

    while (std::chrono::steady_clock::now() < deadline) {
        std::string errorCode;
        std::string stateCode;
        std::string error;

        {
            std::lock_guard<std::mutex> lock(handle->mutex);
            const int ret = readStateLocked(handle, &errorCode, &stateCode, &error);
            if (ret != 0) {
                if (outError != nullptr) {
                    *outError = error;
                }
                return ret;
            }
        }

        lastState = stateCode;
        if (kBusyStates.find(stateCode) != kBusyStates.end()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (kDisabledStates.find(stateCode) != kDisabledStates.end()) {
            if (outError != nullptr) {
                *outError = std::string("Axis disabled (") + stateCode + ")";
            }
            return kBridgeException;
        }
        if (outError != nullptr) {
            outError->clear();
        }
        return 0;
    }

    if (outError != nullptr) {
        *outError = std::string("Timeout while waiting for motion complete (last state=") + lastState + ")";
    }
    return kBridgeException;
}

}  // namespace

extern "C" {

int lb_newport_scan_devices(char* out_devices, int out_devices_size, char* out_error, int out_error_size)
{
    clearBuffer(out_devices, out_devices_size);
    clearBuffer(out_error, out_error_size);

    try {
        ConexCC^ api = gcnew ConexCC();
        array<String^>^ devices = api->GetDevices();
        std::string joined;
        if (devices != nullptr) {
            for each (String ^ device in devices) {
                const std::string current = toUtf8(device);
                if (current.empty()) {
                    continue;
                }
                if (!joined.empty()) {
                    joined += ';';
                }
                joined += current;
            }
        }

        writeBuffer(joined, out_devices, out_devices_size);
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while scanning devices", out_error, out_error_size);
        return kBridgeException;
    }
}

int lb_newport_axis_create(const char* port, int address, lb_newport_axis_handle** out_handle, char* out_error, int out_error_size)
{
    clearBuffer(out_error, out_error_size);
    if (out_handle == nullptr) {
        writeBuffer("Output handle pointer is null", out_error, out_error_size);
        return kBridgeException;
    }

    try {
        auto* handle = new lb_newport_axis_handle();
        handle->api = gcnew ConexCC();
        handle->port = toManaged(port);
        handle->address = address;
        handle->connected = false;
        *out_handle = handle;
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while creating axis handle", out_error, out_error_size);
        return kBridgeException;
    }
}

int lb_newport_axis_destroy(lb_newport_axis_handle* handle)
{
    if (handle != nullptr) {
        delete handle;
    }
    return 0;
}

int lb_newport_axis_open(lb_newport_axis_handle* handle, char* out_error, int out_error_size)
{
    clearBuffer(out_error, out_error_size);
    if (handle == nullptr) {
        writeBuffer("Axis handle is null", out_error, out_error_size);
        return kBridgeException;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->mutex);
        if (handle->connected) {
            return 0;
        }

        const int ret = handle->api->OpenInstrument(handle->port);
        if (ret != 0) {
            writeBuffer(std::string("OpenInstrument failed (") + std::to_string(ret) + ")", out_error, out_error_size);
            return ret;
        }

        handle->connected = true;
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while opening axis", out_error, out_error_size);
        return kBridgeException;
    }
}

int lb_newport_axis_close(lb_newport_axis_handle* handle, char* out_error, int out_error_size)
{
    clearBuffer(out_error, out_error_size);
    if (handle == nullptr) {
        return 0;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->mutex);
        if (!handle->connected) {
            return 0;
        }

        const int ret = handle->api->CloseInstrument();
        handle->connected = false;
        if (ret != 0) {
            writeBuffer(std::string("CloseInstrument failed (") + std::to_string(ret) + ")", out_error, out_error_size);
            return ret;
        }
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while closing axis", out_error, out_error_size);
        return kBridgeException;
    }
}

int lb_newport_axis_stop(lb_newport_axis_handle* handle, char* out_error, int out_error_size)
{
    clearBuffer(out_error, out_error_size);
    if (handle == nullptr) {
        writeBuffer("Axis handle is null", out_error, out_error_size);
        return kBridgeException;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->mutex);
        if (!handle->connected) {
            return 0;
        }

        String^ err = nullptr;
        const int ret = handle->api->ST(handle->address, err);
        if (ret != 0) {
            writeBuffer(buildFailure("ST", ret, err, handle->api, handle->address), out_error, out_error_size);
            return ret;
        }
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while stopping axis", out_error, out_error_size);
        return kBridgeException;
    }
}

int lb_newport_axis_home(lb_newport_axis_handle* handle, int timeout_ms, char* out_state_code, int out_state_code_size, char* out_error, int out_error_size)
{
    clearBuffer(out_state_code, out_state_code_size);
    clearBuffer(out_error, out_error_size);
    if (handle == nullptr) {
        writeBuffer("Axis handle is null", out_error, out_error_size);
        return kBridgeException;
    }

    try {
        {
            std::lock_guard<std::mutex> lock(handle->mutex);
            String^ err = nullptr;
            const int ret = handle->api->OR(handle->address, err);
            if (ret != 0) {
                writeBuffer(buildFailure("OR", ret, err, handle->api, handle->address), out_error, out_error_size);
                return ret;
            }
        }

        std::string waitError;
        const int waitRet = waitDone(handle, timeout_ms, &waitError);
        if (waitRet != 0) {
            writeBuffer(waitError, out_error, out_error_size);
            return waitRet;
        }

        std::string errorCode;
        std::string stateCode;
        std::string stateError;
        {
            std::lock_guard<std::mutex> lock(handle->mutex);
            const int ret = readStateLocked(handle, &errorCode, &stateCode, &stateError);
            if (ret != 0) {
                writeBuffer(stateError, out_error, out_error_size);
                return ret;
            }
        }

        if (kNotReferencedStates.find(stateCode) != kNotReferencedStates.end()) {
            writeBuffer(std::string("Home finished but axis is still not referenced (") + stateCode + ")", out_error, out_error_size);
            return kBridgeException;
        }

        writeBuffer(stateCode, out_state_code, out_state_code_size);
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while homing axis", out_error, out_error_size);
        return kBridgeException;
    }
}

int lb_newport_axis_move_absolute(lb_newport_axis_handle* handle, double position_mm, int timeout_ms, char* out_error, int out_error_size)
{
    clearBuffer(out_error, out_error_size);
    if (handle == nullptr) {
        writeBuffer("Axis handle is null", out_error, out_error_size);
        return kBridgeException;
    }

    try {
        {
            std::lock_guard<std::mutex> lock(handle->mutex);
            String^ err = nullptr;
            const int ret = handle->api->PA_Set(handle->address, position_mm, err);
            if (ret != 0) {
                writeBuffer(buildFailure("PA_Set", ret, err, handle->api, handle->address), out_error, out_error_size);
                return ret;
            }
        }

        std::string waitError;
        const int waitRet = waitDone(handle, timeout_ms, &waitError);
        if (waitRet != 0) {
            writeBuffer(waitError, out_error, out_error_size);
            return waitRet;
        }
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while moving axis absolutely", out_error, out_error_size);
        return kBridgeException;
    }
}

int lb_newport_axis_move_relative(lb_newport_axis_handle* handle, double delta_mm, int timeout_ms, char* out_error, int out_error_size)
{
    clearBuffer(out_error, out_error_size);
    if (handle == nullptr) {
        writeBuffer("Axis handle is null", out_error, out_error_size);
        return kBridgeException;
    }

    try {
        {
            std::lock_guard<std::mutex> lock(handle->mutex);
            String^ err = nullptr;
            const int ret = handle->api->PR_Set(handle->address, delta_mm, err);
            if (ret != 0) {
                writeBuffer(buildFailure("PR_Set", ret, err, handle->api, handle->address), out_error, out_error_size);
                return ret;
            }
        }

        std::string waitError;
        const int waitRet = waitDone(handle, timeout_ms, &waitError);
        if (waitRet != 0) {
            writeBuffer(waitError, out_error, out_error_size);
            return waitRet;
        }
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while moving axis relatively", out_error, out_error_size);
        return kBridgeException;
    }
}

int lb_newport_axis_read_state(lb_newport_axis_handle* handle, char* out_error_code, int out_error_code_size, char* out_state_code, int out_state_code_size, char* out_error, int out_error_size)
{
    clearBuffer(out_error_code, out_error_code_size);
    clearBuffer(out_state_code, out_state_code_size);
    clearBuffer(out_error, out_error_size);
    if (handle == nullptr) {
        writeBuffer("Axis handle is null", out_error, out_error_size);
        return kBridgeException;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->mutex);
        std::string errorCode;
        std::string stateCode;
        std::string error;
        const int ret = readStateLocked(handle, &errorCode, &stateCode, &error);
        if (ret != 0) {
            writeBuffer(error, out_error, out_error_size);
            return ret;
        }

        writeBuffer(errorCode, out_error_code, out_error_code_size);
        writeBuffer(stateCode, out_state_code, out_state_code_size);
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while reading axis state", out_error, out_error_size);
        return kBridgeException;
    }
}

int lb_newport_axis_read_position(lb_newport_axis_handle* handle, double* out_position_mm, char* out_error, int out_error_size)
{
    clearBuffer(out_error, out_error_size);
    if (handle == nullptr) {
        writeBuffer("Axis handle is null", out_error, out_error_size);
        return kBridgeException;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->mutex);
        std::string error;
        const int ret = readPositionLocked(handle, out_position_mm, &error);
        if (ret != 0) {
            writeBuffer(error, out_error, out_error_size);
            return ret;
        }
        return 0;
    } catch (Exception^ ex) {
        writeBuffer(toUtf8(ex->ToString()), out_error, out_error_size);
        return kBridgeException;
    } catch (...) {
        writeBuffer("Unknown bridge error while reading axis position", out_error, out_error_size);
        return kBridgeException;
    }
}

}  // extern "C"
