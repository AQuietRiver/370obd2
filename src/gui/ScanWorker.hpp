#pragma once

#include "core/EnhancedPidProfile.hpp"
#include "core/ObdTypes.hpp"

#include <QObject>
#include <QString>

#include <memory>

namespace obd { class PosixElm327Transport; }

Q_DECLARE_METATYPE(obd::CoverageReport)
Q_DECLARE_METATYPE(obd::DtcReport)

namespace gui {

class ScanWorker : public QObject {
    Q_OBJECT

public:
    explicit ScanWorker(QObject* parent = nullptr);
    ~ScanWorker() override;

public slots:
    void scanSimulator();
    void clearSimulatorDtcs();
    void scanVehicle(const QString& devicePath);
    void clearVehicleDtcs(const QString& devicePath);
    void disconnectVehicle();

signals:
    void scanFinished(const obd::CoverageReport& report, const obd::DtcReport& dtcs,
        const QString& source);
    void operationFailed(const QString& title, const QString& message);

private:
    obd::PosixElm327Transport* connectedTransport(const QString& devicePath, std::string* error);
    void runVehicleScan(const QString& devicePath);

    obd::EnhancedPidProfile profile_;
    QString profileError_;
    QString connectedDevicePath_;
    std::unique_ptr<obd::PosixElm327Transport> transport_;
};

} // namespace gui
