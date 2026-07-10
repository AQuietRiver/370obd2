#pragma once

#include <QMainWindow>
#include <QJsonArray>
#include <QStringList>

class QTableWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QTextEdit;

namespace obd { struct CoverageReport; struct DtcReport; }

namespace gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

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
    QString sensorContext() const;
    QString logbookContext(const QString& question) const;
    QString manualContext(const QStringList& paths, const QString& question) const;

    QTableWidget* table_ = nullptr;
    QTableWidget* dtcTable_ = nullptr;
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
    QNetworkAccessManager* network_ = nullptr;
    QString aiApiKey_;
    QString scanSource_ = "No sensor scan has been run";
    QJsonArray availableModels_;
};

} // namespace gui
