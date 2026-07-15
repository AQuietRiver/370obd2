#pragma once

#include <QMainWindow>
#include <QFile>
#include <QJsonArray>
#include <QHash>
#include <QStringList>

#include <vector>

class QTableWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QProgressBar;
class QTextEdit;
class QThread;
class QStackedWidget;

namespace obd { struct CoverageReport; struct DtcReport; }

namespace gui {

class ScanWorker;
class LiveDataChart;

struct PlaybackRow {
    QString module;
    QString command;
    QString sensor;
    QString value;
    QString unit;
    QString status;
};

struct PlaybackSnapshot {
    qint64 timestampMs = 0;
    std::vector<PlaybackRow> rows;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void runSimulatorScan();
    void runSerialScan();
    void clearSimulatorDtcs();
    void clearVehicleDtcs();
    void askOpenRouter();
    void refreshOpenRouterModels();
    void populateModelList();
    void setAiEnabled(bool enabled);
    void loadSavedApiKey();
    void saveApiKeySecurely();
    void forgetSavedApiKey();
    void saveTechLogbookEntry();
    void viewTechLogbook();
    void displayReport(const obd::CoverageReport& report, const QString& source);
    void displayDtcs(const obd::DtcReport& report);
    void setScanBusy(bool busy, const QString& message = {});
    void toggleRecording();
    void startRecording();
    void stopRecording(const QString& statusMessage = {});
    void loadRecording();
    void togglePlayback();
    void advancePlayback();
    QString sensorContext() const;
    QString logbookContext(const QString& question) const;
    QString manualContext(const QStringList& paths, const QString& question) const;

    QTableWidget* table_ = nullptr;
    QTableWidget* dtcTable_ = nullptr;
    QStackedWidget* diagnosticPages_ = nullptr;
    LiveDataChart* liveChart_ = nullptr;
    QCheckBox* aiEnabled_ = nullptr;
    QCheckBox* aiFreeOnly_ = nullptr;
    QComboBox* aiModel_ = nullptr;
    QComboBox* aiManual_ = nullptr;
    QLineEdit* serialPath_ = nullptr;
    QLabel* aiStatus_ = nullptr;
    QLabel* aiPricing_ = nullptr;
    QTextEdit* aiQuestion_ = nullptr;
    QTextEdit* aiAnswer_ = nullptr;
    QTextEdit* techNotes_ = nullptr;
    QPushButton* aiAsk_ = nullptr;
    QPushButton* aiRefreshModels_ = nullptr;
    QPushButton* aiForgetKey_ = nullptr;
    QPushButton* saveLogbook_ = nullptr;
    QPushButton* simulatorScan_ = nullptr;
    QPushButton* simulatorClear_ = nullptr;
    QPushButton* vehicleScan_ = nullptr;
    QPushButton* vehicleClear_ = nullptr;
    QProgressBar* scanProgress_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
    QThread* scanThread_ = nullptr;
    ScanWorker* scanWorker_ = nullptr;
    QWidget* titleBar_ = nullptr;
    QPushButton* recordToggle_ = nullptr;
    QFile recordingFile_;
    bool recording_ = false;
    int recordedSampleCount_ = 0;

    QPushButton* scanVehicleButton_ = nullptr;
    QPushButton* loadRecordingButton_ = nullptr;
    QPushButton* playbackToggle_ = nullptr;
    std::vector<PlaybackSnapshot> playbackSnapshots_;
    int playbackIndex_ = 0;
    bool playbackPlaying_ = false;
    QString playbackSourceLabel_;
    QString aiApiKey_;
    QString scanSource_ = "No sensor scan has been run";
    QJsonArray availableModels_;
    mutable QHash<QString, QString> manualTextCache_;
};

} // namespace gui
