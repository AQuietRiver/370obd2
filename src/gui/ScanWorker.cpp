#include "gui/ScanWorker.hpp"

#include "core/PosixElm327Transport.hpp"
#include "core/ScanSession.hpp"
#include "core/SimulatorTransport.hpp"

namespace gui {

ScanWorker::ScanWorker(QObject* parent) : QObject(parent) {
    std::string error;
    if (!profile_.loadCsv(OBD_PROJECT_SOURCE_DIR "/data/sample_nissan_z34_enhanced_pids.csv", &error)) {
        profileError_ = QString::fromStdString(error);
    }
}

ScanWorker::~ScanWorker() = default;

void ScanWorker::scanSimulator() {
    obd::SimulatorTransport transport;
    std::string error;
    if (!transport.connect(&error)) {
        emit operationFailed("Simulator connection failed", QString::fromStdString(error));
        return;
    }
    obd::ScanSession session(transport, profileError_.isEmpty() ? &profile_ : nullptr);
    const obd::ScanOptions options;
    emit scanFinished(session.run(options), session.readDtcs(options.modules),
        "Simulator data (not from vehicle)");
}

void ScanWorker::clearSimulatorDtcs() {
    obd::SimulatorTransport transport;
    std::string error;
    if (!transport.connect(&error)) {
        emit operationFailed("Simulator connection failed", QString::fromStdString(error));
        return;
    }
    obd::ScanSession session(transport, nullptr);
    if (!session.clearDtcs(obd::ScanOptions{}.modules, &error)) {
        emit operationFailed("Clear DTCs failed", QString::fromStdString(error));
        return;
    }
    scanSimulator();
}

obd::PosixElm327Transport* ScanWorker::connectedTransport(
    const QString& devicePath, std::string* error) {
    if (transport_ && connectedDevicePath_ == devicePath) {
        return transport_.get();
    }

    disconnectVehicle();
    auto candidate = std::make_unique<obd::PosixElm327Transport>(devicePath.toStdString());
    if (!candidate->connect(error)) {
        return nullptr;
    }
    connectedDevicePath_ = devicePath;
    transport_ = std::move(candidate);
    return transport_.get();
}

void ScanWorker::scanVehicle(const QString& devicePath) {
    runVehicleScan(devicePath);
}

void ScanWorker::runVehicleScan(const QString& devicePath) {
    std::string error;
    auto* transport = connectedTransport(devicePath, &error);
    if (!transport) {
        emit operationFailed("Vehicle connection failed", QString::fromStdString(error));
        return;
    }

    obd::ScanSession session(*transport, profileError_.isEmpty() ? &profile_ : nullptr);
    const obd::ScanOptions options;
    emit scanFinished(session.run(options), session.readDtcs(options.modules),
        "Vehicle scan from " + devicePath);
}

void ScanWorker::clearVehicleDtcs(const QString& devicePath) {
    std::string error;
    auto* transport = connectedTransport(devicePath, &error);
    if (!transport) {
        emit operationFailed("Vehicle connection failed", QString::fromStdString(error));
        return;
    }

    obd::ScanSession session(*transport, nullptr);
    if (!session.clearDtcs(obd::ScanOptions{}.modules, &error)) {
        emit operationFailed("Clear DTCs failed", QString::fromStdString(error));
        return;
    }
    runVehicleScan(devicePath);
}

void ScanWorker::disconnectVehicle() {
    if (transport_) {
        transport_->disconnect();
        transport_.reset();
    }
    connectedDevicePath_.clear();
}

} // namespace gui
