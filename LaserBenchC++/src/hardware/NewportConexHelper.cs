using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Web.Script.Serialization;
using CommandInterfaceConexCC;

internal static class Program
{
    private static readonly JavaScriptSerializer Json = new JavaScriptSerializer();
    private static readonly object SyncRoot = new object();

    private static ConexAxis AxisX;
    private static ConexAxis AxisY;
    private static Thread WorkerThread;
    private static bool WorkerRunning;
    private static string WorkerError = string.Empty;
    private static string WorkerName = string.Empty;

    private static readonly HashSet<string> NotReferencedStates = new HashSet<string> { "0A", "0B", "0C", "0D", "0E", "0F" };
    private static readonly HashSet<string> BusyStates = new HashSet<string> { "1E", "1F", "28", "29", "2A", "2B", "46", "47" };
    private static readonly HashSet<string> DisabledStates = new HashSet<string> { "3C", "3D" };

    private static void Main()
    {
        Console.InputEncoding = System.Text.Encoding.UTF8;
        Console.OutputEncoding = System.Text.Encoding.UTF8;

        string line;
        while ((line = Console.ReadLine()) != null)
        {
            if (string.IsNullOrWhiteSpace(line))
            {
                continue;
            }

            Dictionary<string, object> response;
            try
            {
                var request = Json.Deserialize<Dictionary<string, object>>(line);
                response = Handle(request);
            }
            catch (Exception ex)
            {
                response = Error(ex.Message);
            }

            Console.WriteLine(Json.Serialize(response));
            Console.Out.Flush();
        }

        try { Disconnect(false); } catch { }
    }

    private static Dictionary<string, object> Handle(Dictionary<string, object> request)
    {
        string command = ReadString(request, "cmd");
        switch (command)
        {
            case "scan":
                return Scan();
            case "connect":
                return Connect(ReadString(request, "xPort"), ReadString(request, "yPort"), ReadInt(request, "address", 1));
            case "disconnect":
                return Disconnect(ReadBool(request, "stop", false));
            case "home":
                return StartOperation("home", HomeAll);
            case "setVelocity":
                return SetVelocity(ReadString(request, "axis"), ReadDouble(request, "speed"));
            case "moveAbsoluteNoWait":
                return MoveAbsoluteNoWait(ReadString(request, "axis"), ReadDouble(request, "position"));
            case "waitAxis":
                return WaitAxis(ReadString(request, "axis"), ReadInt(request, "timeoutMs", 30000));
            case "stopAxis":
                return StopAxis(ReadString(request, "axis"));
            case "moveRelative":
                return StartOperation("moveRelative", () => AxisOrThrow(ReadString(request, "axis")).MoveRelative(ReadDouble(request, "delta"), 30000));
            case "moveAbsolute":
                return StartOperation("moveAbsolute", () => AxisOrThrow(ReadString(request, "axis")).MoveAbsolute(ReadDouble(request, "position"), 30000));
            case "status":
                return Status();
            case "snapshot":
                return Snapshot();
            default:
                return Error("Unknown command: " + command);
        }
    }

    private static Dictionary<string, object> Scan()
    {
        var api = new ConexCC();
        var devices = (api.GetDevices() ?? new string[0])
            .Select(value => (value ?? string.Empty).Trim())
            .Where(value => !string.IsNullOrEmpty(value))
            .Distinct()
            .OrderBy(value => value, StringComparer.OrdinalIgnoreCase)
            .ToArray();

        var response = new Dictionary<string, object>();
        response["ok"] = true;
        response["ports"] = devices;
        return response;
    }

    private static Dictionary<string, object> Connect(string xPort, string yPort, int address)
    {
        lock (SyncRoot)
        {
            EnsureNotBusy();
        }

        Disconnect(false);

        var axisX = new ConexAxis("X", xPort, address);
        axisX.Open();
        try
        {
            var axisY = new ConexAxis("Y", yPort, address);
            axisY.Open();
            lock (SyncRoot)
            {
                AxisX = axisX;
                AxisY = axisY;
                WorkerError = string.Empty;
                WorkerName = string.Empty;
            }
        }
        catch
        {
            axisX.Close();
            throw;
        }

        return Ok("Connected X=" + xPort + ", Y=" + yPort);
    }

    private static Dictionary<string, object> Disconnect(bool stopBeforeClose)
    {
        ConexAxis axisX;
        ConexAxis axisY;
        lock (SyncRoot)
        {
            if (WorkerRunning)
            {
                return Error("A motor operation is still running");
            }

            axisX = AxisX;
            axisY = AxisY;
            AxisX = null;
            AxisY = null;
            WorkerError = string.Empty;
            WorkerName = string.Empty;
        }

        ShutdownAxis(axisX, stopBeforeClose);
        ShutdownAxis(axisY, stopBeforeClose);
        return Ok("Disconnected");
    }

    private static Dictionary<string, object> StartOperation(string name, Action action)
    {
        lock (SyncRoot)
        {
            EnsureNotBusy();
            if (AxisX == null || AxisY == null)
            {
                return Error("Both axes must be connected first");
            }

            WorkerRunning = true;
            WorkerError = string.Empty;
            WorkerName = name;
            WorkerThread = new Thread(delegate()
            {
                try
                {
                    action();
                }
                catch (Exception ex)
                {
                    lock (SyncRoot)
                    {
                        WorkerError = ex.Message;
                    }
                }
                finally
                {
                    lock (SyncRoot)
                    {
                        WorkerRunning = false;
                        WorkerThread = null;
                    }
                }
            });
            WorkerThread.IsBackground = true;
            WorkerThread.Name = "NewportOperation";
            WorkerThread.Start();
        }

        return Ok(name + " started");
    }

    private static void HomeAll()
    {
        AxisOrThrow("X").Home(90000);
        AxisOrThrow("Y").Home(90000);
    }

    private static Dictionary<string, object> SetVelocity(string axis, double speed)
    {
        lock (SyncRoot)
        {
            EnsureNotBusy();
        }
        AxisOrThrow(axis).SetVelocity(speed);
        return Ok("Velocity set for " + axis);
    }

    private static Dictionary<string, object> MoveAbsoluteNoWait(string axis, double position)
    {
        lock (SyncRoot)
        {
            EnsureNotBusy();
        }
        AxisOrThrow(axis).MoveAbsoluteNoWait(position);
        return Ok("MoveAbsoluteNoWait sent for " + axis);
    }

    private static Dictionary<string, object> WaitAxis(string axis, int timeoutMs)
    {
        AxisOrThrow(axis).WaitDone(timeoutMs);
        return Ok("Motion complete for " + axis);
    }

    private static Dictionary<string, object> StopAxis(string axis)
    {
        AxisOrThrow(axis).Stop();
        return Ok("Stop sent for " + axis);
    }

    private static Dictionary<string, object> Status()
    {
        lock (SyncRoot)
        {
            var response = new Dictionary<string, object>();
            response["ok"] = true;
            response["busy"] = WorkerRunning;
            response["lastError"] = WorkerError ?? string.Empty;
            response["lastOperation"] = WorkerName ?? string.Empty;
            return response;
        }
    }

    private static Dictionary<string, object> Snapshot()
    {
        ConexAxis axisX;
        ConexAxis axisY;
        bool busy;
        string lastError;
        lock (SyncRoot)
        {
            axisX = AxisX;
            axisY = AxisY;
            busy = WorkerRunning;
            lastError = WorkerError ?? string.Empty;
        }

        var response = new Dictionary<string, object>();
        response["ok"] = true;
        response["busy"] = busy;
        response["lastError"] = lastError;
        response["x"] = axisX != null ? axisX.ToSnapshot() : DisconnectedSnapshot(string.Empty);
        response["y"] = axisY != null ? axisY.ToSnapshot() : DisconnectedSnapshot(string.Empty);
        return response;
    }

    private static void ShutdownAxis(ConexAxis axis, bool stopBeforeClose)
    {
        if (axis == null) return;
        if (stopBeforeClose)
        {
            try { axis.Stop(); } catch { }
        }
        try { axis.Close(); } catch { }
    }

    private static ConexAxis AxisOrThrow(string axis)
    {
        lock (SyncRoot)
        {
            if (axis == "X" && AxisX != null) return AxisX;
            if (axis == "Y" && AxisY != null) return AxisY;
        }
        throw new InvalidOperationException("Axis " + axis + " is not connected");
    }

    private static void EnsureNotBusy()
    {
        if (WorkerRunning)
        {
            throw new InvalidOperationException("Another motor operation is already running");
        }
    }

    private static Dictionary<string, object> Ok(string message)
    {
        var response = new Dictionary<string, object>();
        response["ok"] = true;
        response["message"] = message;
        return response;
    }

    private static Dictionary<string, object> Error(string message)
    {
        var response = new Dictionary<string, object>();
        response["ok"] = false;
        response["message"] = message;
        return response;
    }

    private static Dictionary<string, object> DisconnectedSnapshot(string port)
    {
        var snapshot = new Dictionary<string, object>();
        snapshot["connected"] = false;
        snapshot["port"] = port ?? string.Empty;
        snapshot["position"] = null;
        snapshot["errorCode"] = string.Empty;
        snapshot["stateCode"] = string.Empty;
        snapshot["issue"] = string.Empty;
        return snapshot;
    }

    private static string ReadString(Dictionary<string, object> request, string key)
    {
        object value;
        return request.TryGetValue(key, out value) ? Convert.ToString(value, CultureInfo.InvariantCulture) ?? string.Empty : string.Empty;
    }

    private static int ReadInt(Dictionary<string, object> request, string key, int defaultValue)
    {
        object value;
        return request.TryGetValue(key, out value) ? Convert.ToInt32(value, CultureInfo.InvariantCulture) : defaultValue;
    }

    private static double ReadDouble(Dictionary<string, object> request, string key)
    {
        object value;
        return request.TryGetValue(key, out value) ? Convert.ToDouble(value, CultureInfo.InvariantCulture) : 0.0;
    }

    private static bool ReadBool(Dictionary<string, object> request, string key, bool defaultValue)
    {
        object value;
        return request.TryGetValue(key, out value) ? Convert.ToBoolean(value, CultureInfo.InvariantCulture) : defaultValue;
    }

    private sealed class ConexAxis
    {
        private sealed class AxisStateResult
        {
            public string ErrorCode = string.Empty;
            public string StateCode = string.Empty;
        }

        private readonly object _lock = new object();
        private readonly string _axisName;
        private readonly string _port;
        private readonly int _address;
        private readonly ConexCC _api;
        private bool _connected;

        public ConexAxis(string axisName, string port, int address)
        {
            _axisName = axisName;
            _port = port;
            _address = address;
            _api = new ConexCC();
        }

        public void Open()
        {
            lock (_lock)
            {
                if (_connected) return;
                int ret = _api.OpenInstrument(_port);
                if (ret != 0)
                {
                    throw new InvalidOperationException(_axisName + ": OpenInstrument(" + _port + ") failed (" + ret + ")");
                }
                _connected = true;
            }
        }

        public void Close()
        {
            lock (_lock)
            {
                if (!_connected) return;
                _api.CloseInstrument();
                _connected = false;
            }
        }

        public void Stop()
        {
            lock (_lock)
            {
                if (!_connected) return;
                string err = string.Empty;
                int ret = _api.ST(_address, out err);
                RaiseIfFailed("ST", ret, err);
            }
        }

        public void Home(int timeoutMs)
        {
            lock (_lock)
            {
                string err = string.Empty;
                int ret = _api.OR(_address, out err);
                RaiseIfFailed("OR", ret, err);
            }
            WaitDone(timeoutMs);
            string state = ReadState().StateCode;
            if (NotReferencedStates.Contains(state))
            {
                throw new InvalidOperationException(_axisName + ": home finished but axis is still not referenced (" + state + ")");
            }
        }

        public void MoveRelative(double deltaMm, int timeoutMs)
        {
            lock (_lock)
            {
                string err = string.Empty;
                int ret = _api.PR_Set(_address, deltaMm, out err);
                RaiseIfFailed("PR_Set", ret, err);
            }
            WaitDone(timeoutMs);
        }

        public void MoveAbsolute(double positionMm, int timeoutMs)
        {
            lock (_lock)
            {
                string err = string.Empty;
                int ret = _api.PA_Set(_address, positionMm, out err);
                RaiseIfFailed("PA_Set", ret, err);
            }
            WaitDone(timeoutMs);
        }

        public void MoveAbsoluteNoWait(double positionMm)
        {
            lock (_lock)
            {
                string err = string.Empty;
                int ret = _api.PA_Set(_address, positionMm, out err);
                RaiseIfFailed("PA_Set", ret, err);
            }
        }

        public void SetVelocity(double velocityMmS)
        {
            lock (_lock)
            {
                string err = string.Empty;
                int ret = _api.VA_Set(_address, velocityMmS, out err);
                RaiseIfFailed("VA_Set", ret, err);
            }
        }

        public Dictionary<string, object> ToSnapshot()
        {
            if (!_connected)
            {
                return DisconnectedSnapshot(_port);
            }

            if (!Monitor.TryEnter(_lock))
            {
                var busySnapshot = new Dictionary<string, object>();
                busySnapshot["connected"] = true;
                busySnapshot["port"] = _port;
                busySnapshot["position"] = null;
                busySnapshot["errorCode"] = string.Empty;
                busySnapshot["stateCode"] = string.Empty;
                busySnapshot["issue"] = "busy";
                return busySnapshot;
            }

            try
            {
                var state = ReadStateInternal();
                double position = ReadPositionInternal();
                var snapshot = new Dictionary<string, object>();
                snapshot["connected"] = true;
                snapshot["port"] = _port;
                snapshot["position"] = position;
                snapshot["errorCode"] = state.ErrorCode;
                snapshot["stateCode"] = state.StateCode;
                snapshot["issue"] = string.Empty;
                return snapshot;
            }
            catch (Exception ex)
            {
                var errorSnapshot = new Dictionary<string, object>();
                errorSnapshot["connected"] = true;
                errorSnapshot["port"] = _port;
                errorSnapshot["position"] = null;
                errorSnapshot["errorCode"] = string.Empty;
                errorSnapshot["stateCode"] = string.Empty;
                errorSnapshot["issue"] = ex.Message;
                return errorSnapshot;
            }
            finally
            {
                Monitor.Exit(_lock);
            }
        }

        private AxisStateResult ReadState()
        {
            lock (_lock)
            {
                return ReadStateInternal();
            }
        }

        private AxisStateResult ReadStateInternal()
        {
            string errorCode = string.Empty;
            string stateCode = string.Empty;
            string err = string.Empty;
            int ret = _api.TS(_address, out errorCode, out stateCode, out err);
            RaiseIfFailed("TS", ret, err);

            var result = new AxisStateResult();
            result.ErrorCode = NormalizeState(errorCode);
            result.StateCode = NormalizeState(stateCode);
            return result;
        }

        private double ReadPositionInternal()
        {
            double position = 0.0;
            string err = string.Empty;
            int ret = _api.TP(_address, out position, out err);
            RaiseIfFailed("TP", ret, err);
            return position;
        }

        public void WaitDone(int timeoutMs)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            string lastState = string.Empty;
            while (DateTime.UtcNow < deadline)
            {
                string state = ReadState().StateCode;
                lastState = state;
                if (BusyStates.Contains(state))
                {
                    Thread.Sleep(100);
                    continue;
                }
                if (DisabledStates.Contains(state))
                {
                    throw new InvalidOperationException(_axisName + ": axis disabled (" + state + ")");
                }
                return;
            }

            throw new TimeoutException(_axisName + ": timeout while waiting for motion complete (last state=" + lastState + ")");
        }

        private void RaiseIfFailed(string command, int retCode, string errString)
        {
            if (retCode == 0) return;
            string detail = _axisName + ": " + command + " failed (" + retCode + ")";
            if (!string.IsNullOrWhiteSpace(errString))
            {
                detail += " - " + errString.Trim();
            }
            throw new InvalidOperationException(detail);
        }

        private static string NormalizeState(string code)
        {
            return string.IsNullOrWhiteSpace(code) ? string.Empty : code.Trim().ToUpperInvariant();
        }
    }
}

