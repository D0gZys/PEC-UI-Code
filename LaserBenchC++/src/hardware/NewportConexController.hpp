#pragma once

#include <QString>
#include <QStringList>

#include <memory>

namespace laserbench::hardware {

enum class AxisId
{
    X,
    Y
};

struct MotorAxisSnapshot
{
    AxisId axis {AxisId::X};
    bool connected {false};
    QString port;
    bool positionValid {false};
    double positionMm {0.0};
    QString errorCode;
    QString stateCode;
    QString issue;
};

QString axisIdLabel(AxisId axis);
QString conexStateLabel(const QString& code);
QString formatAxisState(const MotorAxisSnapshot& snapshot);

struct BothAxesSnapshot
{
    MotorAxisSnapshot x {AxisId::X};
    MotorAxisSnapshot y {AxisId::Y};
};

class NewportConexController final
{
public:
    NewportConexController();
    ~NewportConexController();

    QStringList scanAvailablePorts() const;

    void connectAxes(const QString& xPort, const QString& yPort, int address = 1);
    void disconnectAxes(bool stopBeforeClose);
    void homeAll(int timeoutMs = 90000);
    void setVelocity(AxisId axis, double speedMmPerS);
    void moveAbsoluteNoWait(AxisId axis, double positionMm);
    void waitAxis(AxisId axis, int timeoutMs = 30000);
    void stopAxis(AxisId axis);
    void moveRelative(AxisId axis, double deltaMm, int timeoutMs = 30000);
    void moveAbsolute(AxisId axis, double positionMm, int timeoutMs = 30000);

    MotorAxisSnapshot snapshot(AxisId axis) const;
    BothAxesSnapshot snapshotBoth() const;
    bool anyAxisConnected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace laserbench::hardware
