#include "gui/MainWindow.hpp"
#include "gui/LiveDataChart.hpp"
#include "gui/ScanWorker.hpp"

#include "core/EnhancedPidProfile.hpp"
#include "core/PosixElm327Transport.hpp"
#include "core/ScanSession.hpp"
#include "core/SimulatorTransport.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPdfDocument>
#include <QPdfView>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QTextStream>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <utility>

#include <qt6keychain/keychain.h>

namespace gui {

namespace {
// Sensor names come from the user-editable enhanced-PID CSV profile, so quote
// any field that could otherwise break CSV column alignment.
QString csvField(const QString& value) {
    if (!value.contains(',') && !value.contains('"') && !value.contains('\n')) {
        return value;
    }
    QString escaped = value;
    escaped.replace('"', "\"\"");
    return "\"" + escaped + "\"";
}

// Reverses csvField()'s quoting for a single recorded CSV line.
QStringList parseCsvLine(const QString& line) {
    QStringList fields;
    QString field;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (inQuotes) {
            if (ch == '"') {
                if (i + 1 < line.size() && line.at(i + 1) == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += ch;
            }
        } else if (ch == '"') {
            inQuotes = true;
        } else if (ch == ',') {
            fields << field;
            field.clear();
        } else {
            field += ch;
        }
    }
    fields << field;
    return fields;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowFlag(Qt::FramelessWindowHint);
    qApp->installEventFilter(this);
    auto* applicationShell = new QWidget(this);
    applicationShell->setObjectName("applicationShell");
    auto* shellLayout = new QHBoxLayout(applicationShell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);

    auto* sidebar = new QWidget(applicationShell);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(196);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(8, 14, 8, 12);
    sidebarLayout->setSpacing(5);
    auto* connectionCaption = new QLabel("VEHICLE CONNECTION", sidebar);
    connectionCaption->setObjectName("eyebrow");
    auto* connectionState = new QLabel("●  Ready to connect", sidebar);
    connectionState->setObjectName("connectionState");
    auto* connectionDevice = new QLabel("/dev/ttyUSB0", sidebar);
    connectionDevice->setObjectName("deviceLabel");
    sidebarLayout->addWidget(connectionCaption);
    sidebarLayout->addWidget(connectionState);
    sidebarLayout->addWidget(connectionDevice);
    sidebarLayout->addSpacing(14);

    auto* liveNavigation = new QPushButton("⌁   Live Data", sidebar);
    auto* dtcNavigation = new QPushButton("△   Trouble Codes", sidebar);
    auto* manualsNavigation = new QPushButton("▤   Service Manuals", sidebar);
    auto* assistantNavigation = new QPushButton("?   Assistant", sidebar);
    auto* logbookNavigation = new QPushButton("□   Tech Logbook", sidebar);
    liveNavigation->setObjectName("navigationButton");
    dtcNavigation->setObjectName("navigationButton");
    manualsNavigation->setObjectName("navigationButton");
    assistantNavigation->setObjectName("navigationButton");
    logbookNavigation->setObjectName("navigationButton");
    liveNavigation->setCheckable(true);
    dtcNavigation->setCheckable(true);
    liveNavigation->setChecked(true);
    sidebarLayout->addWidget(liveNavigation);
    sidebarLayout->addWidget(dtcNavigation);
    sidebarLayout->addWidget(manualsNavigation);
    sidebarLayout->addWidget(logbookNavigation);
    sidebarLayout->addWidget(assistantNavigation);
    sidebarLayout->addStretch();
    auto* protocol = new QLabel("ISO 15765-4 CAN\nELM327 · 500 kbit/s", sidebar);
    protocol->setObjectName("protocolLabel");
    sidebarLayout->addWidget(protocol);
    shellLayout->addWidget(sidebar);

    diagnosticPages_ = new QStackedWidget(applicationShell);
    auto* livePage = new QWidget(diagnosticPages_);
    auto* liveLayout = new QVBoxLayout(livePage);
    liveLayout->setContentsMargins(0, 0, 0, 0);
    liveLayout->setSpacing(0);
    auto* liveSplitter = new QSplitter(Qt::Horizontal, livePage);
    auto* dataPane = new QWidget(liveSplitter);
    dataPane->setObjectName("dataPane");
    auto* dataLayout = new QVBoxLayout(dataPane);
    dataLayout->setContentsMargins(0, 0, 0, 0);
    dataLayout->setSpacing(0);
    auto* filters = new QWidget(dataPane);
    filters->setObjectName("filters");
    auto* filterLayout = new QHBoxLayout(filters);
    filterLayout->setContentsMargins(12, 9, 12, 9);
    filterLayout->setSpacing(8);
    auto* sensorCategory = new QPushButton("All sensors ▾", filters);
    auto* sensorFilter = new QLineEdit(filters);
    sensorFilter->setPlaceholderText("Filter sensors");
    filterLayout->addWidget(sensorCategory);
    filterLayout->addWidget(sensorFilter, 1);
    dataLayout->addWidget(filters);
    table_ = new QTableWidget(dataPane);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"Sensor", "Module", "Value", "Unit", "Status"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    dataLayout->addWidget(table_, 1);
    liveSplitter->addWidget(dataPane);

    auto* inspector = new QWidget(liveSplitter);
    inspector->setObjectName("inspector");
    inspector->setFixedWidth(300);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(17, 17, 17, 17);
    inspectorLayout->setSpacing(8);
    auto* readingName = new QLabel("Select a sensor", inspector);
    readingName->setObjectName("readingName");
    auto* readingValue = new QLabel("—", inspector);
    readingValue->setObjectName("readingValue");
    auto* readingStatus = new QLabel("WAITING FOR SCAN", inspector);
    readingStatus->setObjectName("healthyText");
    inspectorLayout->addWidget(readingName);
    inspectorLayout->addWidget(readingValue);
    inspectorLayout->addWidget(readingStatus);
    inspectorLayout->addSpacing(8);
    liveChart_ = new LiveDataChart(inspector);
    liveChart_->setFixedHeight(188);
    inspectorLayout->addWidget(liveChart_);
    auto* graphMeta = new QLabel(
        "WINDOW                 SAMPLE RATE          DROPPED\n"
        "60 seconds             snapshot             0 samples", inspector);
    graphMeta->setObjectName("graphMeta");
    inspectorLayout->addWidget(graphMeta);
    auto* readingDetails = new QLabel(
        "Command        —\nSource             OBD-II ECM\nLast update      —", inspector);
    readingDetails->setObjectName("readingDetails");
    inspectorLayout->addWidget(readingDetails);
    inspectorLayout->addStretch();
    liveSplitter->addWidget(inspector);
    liveSplitter->setStretchFactor(0, 1);
    liveSplitter->setStretchFactor(1, 0);
    liveLayout->addWidget(liveSplitter);
    diagnosticPages_->addWidget(livePage);

    dtcTable_ = new QTableWidget(diagnosticPages_);
    dtcTable_->setColumnCount(4);
    dtcTable_->setHorizontalHeaderLabels({"Code", "Module", "State", "Description"});
    dtcTable_->horizontalHeader()->setStretchLastSection(true);
    dtcTable_->setAlternatingRowColors(true);
    diagnosticPages_->addWidget(dtcTable_);
    shellLayout->addWidget(diagnosticPages_, 1);
    setCentralWidget(applicationShell);
    connect(liveNavigation, &QPushButton::clicked, this,
        [this, liveNavigation, dtcNavigation, assistantNavigation] {
            diagnosticPages_->setCurrentIndex(0);
            liveNavigation->setChecked(true);
            dtcNavigation->setChecked(false);
            assistantNavigation->setChecked(false);
        });
    connect(dtcNavigation, &QPushButton::clicked, this,
        [this, liveNavigation, dtcNavigation, assistantNavigation] {
            diagnosticPages_->setCurrentIndex(1);
            liveNavigation->setChecked(false);
            dtcNavigation->setChecked(true);
            assistantNavigation->setChecked(false);
        });
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (const auto* item = table_->item(row, 0)) {
            liveChart_->selectSeries(item->text());
        }
    });
    connect(table_, &QTableWidget::cellClicked, this,
        [this, readingName, readingValue, readingStatus, readingDetails](int row, int) {
            readingName->setText(table_->item(row, 0)->text());
            readingValue->setText(table_->item(row, 2)->text() + "  " + table_->item(row, 3)->text());
            readingStatus->setText(table_->item(row, 4)->text().toUpper());
            readingDetails->setText("Command        " + table_->item(row, 0)->data(Qt::UserRole).toString()
                + "\nSource             OBD-II " + table_->item(row, 1)->text()
                + "\nLast update      " + QTime::currentTime().toString("HH:mm:ss.zzz"));
        });
    connect(sensorFilter, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int row = 0; row < table_->rowCount(); ++row) {
            table_->setRowHidden(row, !table_->item(row, 0)->text().contains(text, Qt::CaseInsensitive));
        }
    });

    auto* toolbar = addToolBar("Connection");
    toolbar->setMovable(false);
    auto* brand = new QLabel("  370 OBD-II   ·   2010 Nissan 370Z · Z34  ", this);
    brand->setObjectName("brandLabel");
    toolbar->addWidget(brand);
    toolbar->addSeparator();
    auto* viewTitle = new QLabel("Live Data", this);
    viewTitle->setObjectName("viewTitle");
    toolbar->addWidget(viewTitle);
    auto* toolbarSpacer = new QWidget(toolbar);
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(toolbarSpacer);
    auto* pauseScan = new QPushButton("Pause", toolbar);
    pauseScan->setEnabled(false);
    toolbar->addWidget(pauseScan);
    connect(liveNavigation, &QPushButton::clicked, viewTitle,
        [viewTitle] { viewTitle->setText("Live Data"); });
    connect(dtcNavigation, &QPushButton::clicked, viewTitle,
        [viewTitle] { viewTitle->setText("Trouble Codes"); });
    simulatorScan_ = new QPushButton("Run simulator scan", this);
    toolbar->addWidget(simulatorScan_);
    connect(simulatorScan_, &QPushButton::clicked, this, &MainWindow::runSimulatorScan);
    simulatorClear_ = new QPushButton("Clear DTCs (simulator)", this);
    toolbar->addWidget(simulatorClear_);
    connect(simulatorClear_, &QPushButton::clicked, this, &MainWindow::clearSimulatorDtcs);
    toolbar->addSeparator();
    serialPath_ = new QLineEdit("/dev/ttyUSB0", this);
    serialPath_->setPlaceholderText("Serial device, e.g. /dev/ttyUSB0");
    serialPath_->setMinimumWidth(190);
    toolbar->addWidget(serialPath_);
    vehicleScan_ = new QPushButton("Scan vehicle", this);
    vehicleScan_->setText("Scan Vehicle");
    vehicleScan_->setObjectName("primaryButton");
    toolbar->addWidget(vehicleScan_);
    connect(vehicleScan_, &QPushButton::clicked, this, &MainWindow::runSerialScan);
    vehicleClear_ = new QPushButton("Clear vehicle DTCs", this);
    toolbar->addWidget(vehicleClear_);
    connect(vehicleClear_, &QPushButton::clicked, this, &MainWindow::clearVehicleDtcs);
    scanProgress_ = new QProgressBar(this);
    scanProgress_->setRange(0, 0);
    scanProgress_->setMaximumWidth(120);
    scanProgress_->setTextVisible(false);
    scanProgress_->hide();
    toolbar->addWidget(scanProgress_);
    simulatorScan_->hide();
    simulatorClear_->hide();
    serialPath_->hide();
    vehicleClear_->hide();

    auto* manualsMenu = menuBar()->addMenu("Service Manuals");
    connect(manualsNavigation, &QPushButton::clicked, this, [manualsMenu, manualsNavigation] {
        manualsMenu->popup(manualsNavigation->mapToGlobal(QPoint(manualsNavigation->width(), 0)));
    });
    const QDir manualsDir(QStringLiteral(OBD_PROJECT_SOURCE_DIR "/manuals"));
    const QFileInfoList manuals = manualsDir.entryInfoList(
        {"*.pdf"}, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    if (manuals.isEmpty()) {
        auto* unavailable = manualsMenu->addAction("No PDF manuals found");
        unavailable->setEnabled(false);
    } else {
        for (const QFileInfo& manual : manuals) {
            auto* action = manualsMenu->addAction(manual.completeBaseName());
            action->setToolTip(manual.fileName());
            connect(action, &QAction::triggered, this, [this, manualsMenu, path = manual.absoluteFilePath()] {
                manualsMenu->close();
                QTimer::singleShot(0, this, [this, path] {
                auto* viewerWindow = new QDialog(this);
                viewerWindow->setAttribute(Qt::WA_DeleteOnClose);
                viewerWindow->setWindowTitle(QFileInfo(path).completeBaseName() + " Service Manual");
                viewerWindow->resize(1000, 750);

                auto* document = new QPdfDocument(viewerWindow);
                if (document->load(path) != QPdfDocument::Error::None) {
                    QMessageBox::warning(this, "Unable to open manual",
                        "Could not load the PDF:\n" + path);
                    viewerWindow->deleteLater();
                    return;
                }

                auto* viewer = new QPdfView(viewerWindow);
                viewer->setDocument(document);
                viewer->setPageMode(QPdfView::PageMode::MultiPage);
                viewer->setZoomMode(QPdfView::ZoomMode::FitToWidth);

                auto* layout = new QVBoxLayout(viewerWindow);
                layout->setContentsMargins(0, 0, 0, 0);
                auto* dialogHead = new QWidget(viewerWindow);
                dialogHead->setObjectName("dialogHead");
                auto* dialogHeadLayout = new QHBoxLayout(dialogHead);
                dialogHeadLayout->setContentsMargins(14, 6, 8, 6);
                auto* dialogTitle = new QLabel(viewerWindow->windowTitle(), dialogHead);
                dialogTitle->setObjectName("dialogTitle");
                auto* dialogClose = new QPushButton("×", dialogHead);
                dialogClose->setObjectName("closeWindowButton");
                dialogClose->setFixedSize(34, 30);
                dialogHeadLayout->addWidget(dialogTitle);
                dialogHeadLayout->addStretch();
                dialogHeadLayout->addWidget(dialogClose);
                connect(dialogClose, &QPushButton::clicked, viewerWindow, &QDialog::close);
                layout->addWidget(dialogHead);
                layout->addWidget(viewer);
                viewerWindow->show();
                });
            });
        }
    }

    network_ = new QNetworkAccessManager(this);
    qRegisterMetaType<obd::CoverageReport>("obd::CoverageReport");
    qRegisterMetaType<obd::DtcReport>("obd::DtcReport");
    scanThread_ = new QThread(this);
    scanWorker_ = new ScanWorker;
    scanWorker_->moveToThread(scanThread_);
    connect(scanThread_, &QThread::finished, scanWorker_, &QObject::deleteLater);
    connect(scanWorker_, &ScanWorker::scanFinished, this,
        [this](const obd::CoverageReport& report, const obd::DtcReport& dtcs, const QString& source) {
            displayReport(report, source);
            displayDtcs(dtcs);
            setScanBusy(false);
            if (recording_) {
                // Continuous drive log: pace repeat scans a couple of seconds
                // apart rather than firing on a fixed-interval QTimer, so the
                // next request never overlaps a still-in-flight serial scan.
                constexpr int kRecordingIntervalMs = 2000;
                QTimer::singleShot(kRecordingIntervalMs, this, &MainWindow::runSerialScan);
            }
        });
    connect(scanWorker_, &ScanWorker::operationFailed, this,
        [this](const QString& title, const QString& message) {
            setScanBusy(false);
            if (recording_) {
                stopRecording("Recording stopped: " + message);
            }
            QMessageBox::warning(this, title, message);
        });
    scanThread_->start();
    auto* aiDock = new QDockWidget("DIAGNOSTIC ASSISTANT", this);
    auto* aiPanel = new QWidget(aiDock);
    auto* aiLayout = new QVBoxLayout(aiPanel);
    aiLayout->setContentsMargins(0, 0, 0, 0);
    aiLayout->setSpacing(0);

    aiEnabled_ = new QCheckBox("Enable OpenRouter AI", aiPanel);
    aiStatus_ = new QLabel("Disabled — no API key loaded", aiPanel);
    aiForgetKey_ = new QPushButton("Forget saved key", aiPanel);
    aiForgetKey_->setEnabled(false);
    aiModel_ = new QComboBox(aiPanel);
    aiModel_->setEditable(true);
    aiModel_->addItem("openrouter/free");
    aiRefreshModels_ = new QPushButton("Refresh", aiPanel);
    aiRefreshModels_->setEnabled(false);
    auto* modelRow = new QWidget(aiPanel);
    auto* modelLayout = new QHBoxLayout(modelRow);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelLayout->addWidget(aiModel_, 1);
    modelLayout->addWidget(aiRefreshModels_);
    aiManual_ = new QComboBox(aiPanel);
    aiManual_->addItem("Auto-search all manuals", "__all__");
    aiManual_->addItem("None — use sensor data only", QString());
    aiManual_->setCurrentIndex(1);
    for (const QFileInfo& manual : manuals) {
        aiManual_->addItem(manual.completeBaseName(), manual.absoluteFilePath());
    }

    aiFreeOnly_ = new QCheckBox("Show free models only", aiPanel);
    aiFreeOnly_->setChecked(true);
    aiPricing_ = new QLabel("Pricing: refresh models to load current rates", aiPanel);
    aiPricing_->setWordWrap(true);
    auto* assistantHead = new QWidget(aiPanel);
    assistantHead->setObjectName("assistantHead");
    auto* assistantHeadLayout = new QVBoxLayout(assistantHead);
    assistantHeadLayout->setContentsMargins(14, 11, 14, 10);
    assistantHeadLayout->setSpacing(4);
    auto* assistantTitle = new QLabel("●  Diagnostic Assistant", assistantHead);
    assistantTitle->setObjectName("assistantTitle");
    auto* assistantSubtitle = new QLabel("Available throughout the application", assistantHead);
    assistantSubtitle->setObjectName("mutedText");
    assistantHeadLayout->addWidget(assistantTitle);
    assistantHeadLayout->addWidget(assistantSubtitle);
    aiLayout->addWidget(assistantHead);
    auto* assistantContext = new QLabel(
        "Current context\n  Live scan    0 DTCs    Service manual", aiPanel);
    assistantContext->setObjectName("assistantContext");
    aiLayout->addWidget(assistantContext);
    auto* messageArea = new QWidget(aiPanel);
    messageArea->setObjectName("messageArea");
    auto* messageLayout = new QVBoxLayout(messageArea);
    messageLayout->setContentsMargins(13, 13, 13, 13);
    messageLayout->setSpacing(10);
    auto* welcome = new QLabel(
        "ASSISTANT\n\nI can reference the current sensor stream, stored codes, service manual, "
        "and your logbook. What would you like to investigate?", messageArea);
    welcome->setObjectName("assistantMessage");
    welcome->setWordWrap(true);
    messageLayout->addWidget(welcome);
    messageLayout->addStretch();
    aiLayout->addWidget(messageArea, 1);

    auto* answerCaption = new QLabel("ASSISTANT RESPONSE", aiPanel);
    answerCaption->setObjectName("answerCaption");
    answerCaption->hide();
    aiLayout->addWidget(answerCaption);
    aiAnswer_ = new QTextEdit(aiPanel);
    aiAnswer_->setObjectName("assistantAnswer");
    aiAnswer_->setReadOnly(true);
    aiAnswer_->setMaximumHeight(170);
    aiAnswer_->hide();
    aiLayout->addWidget(aiAnswer_);
    connect(aiAnswer_, &QTextEdit::textChanged, this, [this, answerCaption] {
        const bool hasAnswer = !aiAnswer_->toPlainText().trimmed().isEmpty();
        answerCaption->setVisible(hasAnswer);
        aiAnswer_->setVisible(hasAnswer);
    });

    auto* assistantInput = new QWidget(aiPanel);
    assistantInput->setObjectName("assistantInput");
    auto* inputLayout = new QVBoxLayout(assistantInput);
    inputLayout->setContentsMargins(11, 11, 11, 11);
    inputLayout->setSpacing(7);
    aiQuestion_ = new QTextEdit(aiPanel);
    aiQuestion_->setPlaceholderText("Ask about the current scan, a code, or the service manual");
    aiQuestion_->setFixedHeight(72);
    inputLayout->addWidget(aiQuestion_);
    auto* askRow = new QHBoxLayout;
    auto* contextHint = new QLabel("Uses visible diagnostic context", assistantInput);
    contextHint->setObjectName("mutedText");
    aiAsk_ = new QPushButton("Ask", assistantInput);
    aiAsk_->setObjectName("primaryButton");
    askRow->addWidget(contextHint);
    askRow->addStretch();
    askRow->addWidget(aiAsk_);
    inputLayout->addLayout(askRow);
    aiLayout->addWidget(assistantInput);

    QFont aiFont = aiAnswer_->font();
    aiFont.setPointSize(11);
    aiQuestion_->setFont(aiFont);
    aiAnswer_->setFont(aiFont);
    techNotes_ = new QTextEdit(aiPanel);
    techNotes_->setPlaceholderText(
        "Example: Intermittent rough idle after warm-up; P0101 returned twice this month.");
    techNotes_->setMaximumHeight(90);
    techNotes_->setFont(aiFont);
    auto* logbookButtons = new QWidget(aiPanel);
    auto* logbookLayout = new QHBoxLayout(logbookButtons);
    logbookLayout->setContentsMargins(0, 0, 0, 0);
    saveLogbook_ = new QPushButton("Save case to Tech Logbook", logbookButtons);
    auto* viewLogbook = new QPushButton("View Logbook", logbookButtons);
    logbookLayout->addWidget(saveLogbook_);
    logbookLayout->addWidget(viewLogbook);
    aiEnabled_->hide();
    aiStatus_->hide();
    aiForgetKey_->hide();
    modelRow->hide();
    aiFreeOnly_->hide();
    aiPricing_->hide();
    aiManual_->hide();
    techNotes_->hide();
    logbookButtons->hide();

    auto* assistantConfigPage = new QWidget(diagnosticPages_);
    assistantConfigPage->setObjectName("assistantConfigPage");
    auto* configOuter = new QVBoxLayout(assistantConfigPage);
    configOuter->setContentsMargins(28, 24, 28, 24);
    configOuter->setSpacing(12);
    auto* configTitle = new QLabel("Diagnostic Assistant Setup", assistantConfigPage);
    configTitle->setObjectName("configTitle");
    auto* configIntro = new QLabel(
        "Connect OpenRouter and choose the model used for diagnostic questions. "
        "Free models are preferred by default.", assistantConfigPage);
    configIntro->setObjectName("mutedText");
    configIntro->setWordWrap(true);
    configOuter->addWidget(configTitle);
    configOuter->addWidget(configIntro);
    configOuter->addSpacing(8);

    auto* configCard = new QWidget(assistantConfigPage);
    configCard->setObjectName("configCard");
    configCard->setMaximumWidth(720);
    auto* configForm = new QFormLayout(configCard);
    configForm->setContentsMargins(18, 18, 18, 18);
    configForm->setHorizontalSpacing(18);
    configForm->setVerticalSpacing(12);
    auto* apiKeyInput = new QLineEdit(configCard);
    apiKeyInput->setEchoMode(QLineEdit::Password);
    apiKeyInput->setPlaceholderText("Paste your OpenRouter API key");
    auto* apiKeyRow = new QWidget(configCard);
    auto* apiKeyLayout = new QHBoxLayout(apiKeyRow);
    apiKeyLayout->setContentsMargins(0, 0, 0, 0);
    apiKeyLayout->setSpacing(8);
    auto* applyApiKey = new QPushButton("Use API key", apiKeyRow);
    applyApiKey->setObjectName("primaryButton");
    auto* rememberApiKey = new QPushButton("Save securely", apiKeyRow);
    apiKeyLayout->addWidget(apiKeyInput, 1);
    apiKeyLayout->addWidget(applyApiKey);
    apiKeyLayout->addWidget(rememberApiKey);
    auto* activeKeyLabel = new QLabel("OpenRouter API key is securely stored and in use", configCard);
    activeKeyLabel->setObjectName("activeKeyLabel");
    activeKeyLabel->hide();
    configForm->addRow("API key", activeKeyLabel);
    configForm->addRow("API key", apiKeyRow);
    aiStatus_->setParent(configCard);
    aiStatus_->show();
    configForm->addRow("Status", aiStatus_);
    aiForgetKey_->setParent(configCard);
    aiForgetKey_->show();
    configForm->addRow("", aiForgetKey_);
    modelRow->setParent(configCard);
    modelRow->show();
    configForm->addRow("Model", modelRow);
    aiFreeOnly_->setParent(configCard);
    aiFreeOnly_->show();
    configForm->addRow("", aiFreeOnly_);
    aiPricing_->setParent(configCard);
    aiPricing_->show();
    configForm->addRow("Pricing", aiPricing_);
    aiManual_->setParent(configCard);
    aiManual_->show();
    configForm->addRow("Manual context", aiManual_);
    configOuter->addWidget(configCard);
    configOuter->addStretch();
    diagnosticPages_->addWidget(assistantConfigPage);

    connect(applyApiKey, &QPushButton::clicked, this, [this, apiKeyInput] {
        const QString key = apiKeyInput->text().trimmed();
        if (key.isEmpty()) {
            QMessageBox::information(this, "Missing API key", "Paste an OpenRouter API key first.");
            return;
        }
        aiApiKey_ = key;
        apiKeyInput->clear();
        aiEnabled_->blockSignals(true);
        aiEnabled_->setChecked(true);
        aiEnabled_->blockSignals(false);
        aiStatus_->setText("Enabled — key held in memory");
        aiAsk_->setEnabled(true);
        aiRefreshModels_->setEnabled(true);
        aiForgetKey_->setEnabled(true);
    });
    connect(rememberApiKey, &QPushButton::clicked, this, [this, apiKeyInput, applyApiKey] {
        if (!apiKeyInput->text().trimmed().isEmpty()) applyApiKey->click();
        if (aiApiKey_.isEmpty()) {
            QMessageBox::information(this, "Missing API key", "Enter an API key before saving it.");
            return;
        }
        saveApiKeySecurely();
    });
    auto* keyStateTimer = new QTimer(assistantConfigPage);
    connect(keyStateTimer, &QTimer::timeout, this,
        [this, apiKeyRow, activeKeyLabel] {
            const QString status = aiStatus_->text();
            const bool active = !aiApiKey_.isEmpty();
            apiKeyRow->setVisible(!active);
            activeKeyLabel->setVisible(active);
            activeKeyLabel->setText(status.contains("secure", Qt::CaseInsensitive)
                ? "OpenRouter API key is securely stored and in use"
                : "OpenRouter API key is in use for this session");
        });
    keyStateTimer->start(250);
    aiPanel->setLayout(aiLayout);
    aiDock->setWidget(aiPanel);
    aiDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    aiDock->setMinimumWidth(350);
    aiDock->setMaximumWidth(420);
    addDockWidget(Qt::RightDockWidgetArea, aiDock);
    connect(aiEnabled_, &QCheckBox::toggled, this, &MainWindow::setAiEnabled);
    connect(aiForgetKey_, &QPushButton::clicked, this, &MainWindow::forgetSavedApiKey);
    connect(aiRefreshModels_, &QPushButton::clicked, this, &MainWindow::refreshOpenRouterModels);
    connect(aiFreeOnly_, &QCheckBox::toggled, this, &MainWindow::populateModelList);
    connect(aiModel_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        const double prompt = aiModel_->itemData(index, Qt::UserRole + 1).toDouble() * 1000000.0;
        const double completion = aiModel_->itemData(index, Qt::UserRole + 2).toDouble() * 1000000.0;
        aiPricing_->setText(QString("Prompt $%1 / 1M tokens · Completion $%2 / 1M tokens")
            .arg(prompt, 0, 'g', 6).arg(completion, 0, 'g', 6));
    });
    connect(aiAsk_, &QPushButton::clicked, this, &MainWindow::askOpenRouter);
    connect(saveLogbook_, &QPushButton::clicked, this, &MainWindow::saveTechLogbookEntry);
    connect(viewLogbook, &QPushButton::clicked, this, &MainWindow::viewTechLogbook);
    assistantNavigation->setCheckable(true);
    connect(assistantNavigation, &QPushButton::clicked, this,
        [this, liveNavigation, dtcNavigation, assistantNavigation] {
            diagnosticPages_->setCurrentIndex(2);
            liveNavigation->setChecked(false);
            dtcNavigation->setChecked(false);
            assistantNavigation->setChecked(true);
        });
    connect(logbookNavigation, &QPushButton::clicked, this, &MainWindow::viewTechLogbook);

    // Compose the visible application as one frame, matching the browser prototype.
    // QMainWindow's native toolbar/dock chrome otherwise changes the proportions
    // significantly across desktop environments.
    takeCentralWidget();
    removeToolBar(toolbar);
    removeDockWidget(aiDock);
    aiDock->setWidget(new QWidget(aiDock));
    aiPanel->setParent(applicationShell);
    aiPanel->hide();
    toolbar->hide();
    brand->hide();

    shellLayout->removeWidget(diagnosticPages_);
    auto* centerColumn = new QWidget(applicationShell);
    centerColumn->setObjectName("centerColumn");
    auto* centerLayout = new QVBoxLayout(centerColumn);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    auto* centerToolbar = new QWidget(centerColumn);
    centerToolbar->setObjectName("centerToolbar");
    centerToolbar->setFixedHeight(62);
    auto* centerToolbarLayout = new QHBoxLayout(centerToolbar);
    centerToolbarLayout->setContentsMargins(14, 10, 14, 10);
    centerToolbarLayout->setSpacing(9);
    auto* visibleViewTitle = new QLabel("Live Data", centerToolbar);
    visibleViewTitle->setObjectName("viewTitle");
    playbackToggle_ = new QPushButton("Play", centerToolbar);
    playbackToggle_->setEnabled(false);
    connect(playbackToggle_, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    loadRecordingButton_ = new QPushButton("Load Recording…", centerToolbar);
    connect(loadRecordingButton_, &QPushButton::clicked, this, &MainWindow::loadRecording);
    recordToggle_ = new QPushButton("Record", centerToolbar);
    connect(recordToggle_, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    scanVehicleButton_ = new QPushButton("Scan Vehicle", centerToolbar);
    scanVehicleButton_->setObjectName("primaryButton");
    connect(scanVehicleButton_, &QPushButton::clicked, this, &MainWindow::runSerialScan);
    connect(liveNavigation, &QPushButton::clicked, visibleViewTitle,
        [visibleViewTitle] { visibleViewTitle->setText("Live Data"); });
    connect(dtcNavigation, &QPushButton::clicked, visibleViewTitle,
        [visibleViewTitle] { visibleViewTitle->setText("Trouble Codes"); });
    connect(assistantNavigation, &QPushButton::clicked, visibleViewTitle,
        [visibleViewTitle] { visibleViewTitle->setText("Assistant Configuration"); });
    centerToolbarLayout->addWidget(visibleViewTitle);
    centerToolbarLayout->addStretch();
    centerToolbarLayout->addWidget(playbackToggle_);
    centerToolbarLayout->addWidget(loadRecordingButton_);
    centerToolbarLayout->addWidget(recordToggle_);
    centerToolbarLayout->addWidget(scanVehicleButton_);
    centerLayout->addWidget(centerToolbar);
    centerLayout->addWidget(diagnosticPages_, 1);
    shellLayout->addWidget(centerColumn, 1);

    auto* root = new QWidget(this);
    root->setObjectName("rootFrame");
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);

    titleBar_ = new QWidget(root);
    titleBar_->setObjectName("titleBar");
    titleBar_->setFixedHeight(48);
    titleBar_->installEventFilter(this);
    auto* titleLayout = new QHBoxLayout(titleBar_);
    titleLayout->setContentsMargins(14, 0, 14, 0);
    titleLayout->setSpacing(9);
    auto* mark = new QLabel("Z", titleBar_);
    mark->setObjectName("vehicleMark");
    mark->setAlignment(Qt::AlignCenter);
    mark->setFixedSize(24, 24);
    auto* title = new QLabel("370 OBD-II", titleBar_);
    title->setObjectName("applicationTitle");
    auto* vehicle = new QLabel("2010 Nissan 370Z · Z34", titleBar_);
    vehicle->setObjectName("vehicleTitle");
    titleLayout->addWidget(mark);
    titleLayout->addWidget(title);
    titleLayout->addWidget(vehicle);
    titleLayout->addStretch();
    auto* minimizeWindow = new QPushButton("—", titleBar_);
    auto* maximizeWindow = new QPushButton("□", titleBar_);
    auto* closeWindow = new QPushButton("×", titleBar_);
    minimizeWindow->setObjectName("windowButton");
    maximizeWindow->setObjectName("windowButton");
    closeWindow->setObjectName("closeWindowButton");
    minimizeWindow->setFixedSize(34, 30);
    maximizeWindow->setFixedSize(34, 30);
    closeWindow->setFixedSize(34, 30);
    titleLayout->addWidget(minimizeWindow);
    titleLayout->addWidget(maximizeWindow);
    titleLayout->addWidget(closeWindow);
    connect(minimizeWindow, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(maximizeWindow, &QPushButton::clicked, this, [this] {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(closeWindow, &QPushButton::clicked, this, &QWidget::close);
    rootLayout->addWidget(titleBar_);

    auto* content = new QWidget(root);
    content->setObjectName("contentFrame");
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(applicationShell, 1);
    auto* visibleAssistant = new QWidget(content);
    visibleAssistant->setObjectName("assistantPanel");
    visibleAssistant->setFixedWidth(350);
    auto* visibleAssistantLayout = new QVBoxLayout(visibleAssistant);
    visibleAssistantLayout->setContentsMargins(0, 0, 0, 0);
    visibleAssistantLayout->setSpacing(0);
    auto* visibleAssistantHead = new QWidget(visibleAssistant);
    visibleAssistantHead->setObjectName("assistantHead");
    auto* visibleHeadLayout = new QVBoxLayout(visibleAssistantHead);
    visibleHeadLayout->setContentsMargins(14, 11, 14, 10);
    visibleHeadLayout->setSpacing(4);
    auto* visibleAssistantTitle = new QLabel("●  Diagnostic Assistant", visibleAssistantHead);
    visibleAssistantTitle->setObjectName("assistantTitle");
    auto* visibleAssistantSubtitle = new QLabel(
        "Available throughout the application", visibleAssistantHead);
    visibleAssistantSubtitle->setObjectName("mutedText");
    visibleHeadLayout->addWidget(visibleAssistantTitle);
    visibleHeadLayout->addWidget(visibleAssistantSubtitle);
    visibleAssistantLayout->addWidget(visibleAssistantHead);
    auto* visibleContext = new QLabel(
        "Current context\n  Live scan    0 DTCs    Service manual", visibleAssistant);
    visibleContext->setObjectName("assistantContext");
    visibleAssistantLayout->addWidget(visibleContext);
    auto* visibleMessages = new QWidget(visibleAssistant);
    visibleMessages->setObjectName("messageArea");
    auto* visibleMessagesLayout = new QVBoxLayout(visibleMessages);
    visibleMessagesLayout->setContentsMargins(13, 13, 13, 13);
    auto* visibleWelcome = new QLabel(
        "ASSISTANT\n\nI can reference the current sensor stream, stored codes, service manual, "
        "and your logbook. What would you like to investigate?", visibleMessages);
    visibleWelcome->setObjectName("assistantMessage");
    visibleWelcome->setWordWrap(true);
    visibleMessagesLayout->addWidget(visibleWelcome);
    visibleMessagesLayout->addStretch();
    visibleAssistantLayout->addWidget(visibleMessages, 1);
    QObject::disconnect(aiAnswer_, nullptr, this, nullptr);
    aiAnswer_->setParent(visibleAssistant);
    aiAnswer_->setObjectName("assistantAnswer");
    aiAnswer_->setMaximumHeight(170);
    aiAnswer_->hide();
    visibleAssistantLayout->addWidget(aiAnswer_);
    connect(aiAnswer_, &QTextEdit::textChanged, this, [this] {
        aiAnswer_->setVisible(!aiAnswer_->toPlainText().trimmed().isEmpty());
    });
    auto* visibleInput = new QWidget(visibleAssistant);
    visibleInput->setObjectName("assistantInput");
    auto* visibleInputLayout = new QVBoxLayout(visibleInput);
    visibleInputLayout->setContentsMargins(11, 11, 11, 11);
    visibleInputLayout->setSpacing(7);
    aiQuestion_->setParent(visibleInput);
    aiQuestion_->setFixedHeight(72);
    visibleInputLayout->addWidget(aiQuestion_);
    auto* visibleAskRow = new QHBoxLayout;
    auto* visibleHint = new QLabel("Uses visible diagnostic context", visibleInput);
    visibleHint->setObjectName("mutedText");
    aiAsk_->setParent(visibleInput);
    aiAsk_->setObjectName("primaryButton");
    aiAsk_->show();
    visibleAskRow->addWidget(visibleHint);
    visibleAskRow->addStretch();
    visibleAskRow->addWidget(aiAsk_);
    visibleInputLayout->addLayout(visibleAskRow);
    visibleAssistantLayout->addWidget(visibleInput);
    contentLayout->addWidget(visibleAssistant);
    rootLayout->addWidget(content, 1);

    auto* footer = new QWidget(root);
    footer->setObjectName("footer");
    footer->setFixedHeight(30);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 0, 12, 0);
    footerLayout->setSpacing(18);
    footerLayout->addWidget(new QLabel("● Ready to connect", footer));
    footerLayout->addWidget(new QLabel("ISO 15765-4 CAN", footer));
    footerLayout->addStretch();
    footerLayout->addWidget(new QLabel("No scan yet", footer));
    rootLayout->addWidget(footer);
    setCentralWidget(root);
    menuBar()->hide();
    statusBar()->hide();
    aiDock->hide();

    setWindowTitle("370 OBD-II Scanner");
    resize(1400, 850);
    setStyleSheet(R"(
        * { font-family: "Inter", "Segoe UI", sans-serif; font-size: 14px; }
        QMainWindow, QDialog { background: #171a1b; color: #e4e7e5; }
        QDialog { border: 1px solid #4c5354; }
        #dialogHead { background: #202425; border-bottom: 1px solid #383e3f; }
        #dialogTitle { color: #e4e7e5; font-size: 15px; font-weight: 650; }
        #rootFrame { background: #171a1b; border: 1px solid #4c5354; }
        #contentFrame, #applicationShell, #centerColumn { background: #202425; }
        #titleBar { background: #202425; border: 1px solid #4c5354; border-bottom: 1px solid #383e3f; }
        #vehicleMark { color: #d08a3c; border: 2px solid #d08a3c; border-radius: 12px; font-size: 10px; font-weight: 800; }
        #applicationTitle { color: #e4e7e5; font-size: 16px; font-weight: 650; }
        #vehicleTitle { color: #9da4a1; font-size: 13px; }
        #windowButton, #closeWindowButton { background: transparent; color: #9da4a1; border: 0; padding: 0; font-size: 15px; }
        #windowButton:hover { background: #303738; color: #e4e7e5; }
        #closeWindowButton:hover { background: #9e3f3f; color: white; }
        #contentFrame { border-left: 1px solid #4c5354; border-right: 1px solid #4c5354; }
        #footer { background: #282d2e; color: #9da4a1; border: 1px solid #4c5354; border-top: 1px solid #383e3f; }
        #footer QLabel { color: #9da4a1; font-size: 12px; }
        #assistantPanel { background: #282d2e; border-left: 1px solid #4c5354; }
        #assistantConfigPage { background: #202425; }
        #configTitle { color: #e4e7e5; font-size: 20px; font-weight: 650; }
        #configCard { background: #282d2e; border: 1px solid #383e3f; }
        #configCard QLabel { color: #c9cdcb; }
        #centerToolbar { background: #202425; border-bottom: 1px solid #383e3f; }
        #assistantHead { background: #282d2e; border-bottom: 1px solid #383e3f; }
        #assistantTitle { color: #e4e7e5; font-size: 15px; font-weight: 650; }
        #mutedText { color: #9da4a1; font-size: 12px; }
        #assistantContext { color: #9da4a1; font-size: 12px; padding: 9px 12px; border-bottom: 1px solid #383e3f; }
        #messageArea { background: #282d2e; }
        #assistantMessage { background: #202425; color: #dfe3e1; border-left: 2px solid #d08a3c; padding: 10px; font-size: 13px; }
        #answerCaption { color: #9da4a1; padding: 4px 13px; font-size: 11px; }
        #assistantAnswer { margin: 0 13px 10px 13px; background: #202425; border: 0; border-left: 2px solid #d08a3c; padding: 8px; font-size: 13px; }
        #assistantInput { background: #282d2e; border-top: 1px solid #383e3f; }
        QMenuBar, QMenu, QToolBar, QStatusBar { background: #282d2e; color: #d8dcda; }
        QMenuBar { border-bottom: 1px solid #383e3f; }
        QToolBar { border: 0; border-bottom: 1px solid #383e3f; spacing: 7px; padding: 7px; min-height: 46px; }
        #brandLabel { color: #e4e7e5; font-size: 15px; font-weight: 650; }
        #viewTitle { color: #e4e7e5; font-size: 18px; font-weight: 650; padding-left: 7px; }
        #sidebar { background: #282d2e; border-right: 1px solid #383e3f; }
        #eyebrow { color: #9da4a1; font-size: 11px; letter-spacing: 1px; }
        #connectionState { color: #e4e7e5; font-weight: 650; padding-top: 2px; }
        #deviceLabel, #protocolLabel { color: #9da4a1; font-family: monospace; font-size: 12px; }
        #deviceLabel { padding-left: 14px; }
        #protocolLabel { border-top: 1px solid #383e3f; padding: 12px 5px 0 5px; }
        #navigationButton { background: transparent; color: #c9cdcb; border: 0; border-left: 3px solid transparent; padding: 10px; text-align: left; }
        #navigationButton:hover { background: #292e2f; }
        #navigationButton:checked { background: #303738; color: #e4e7e5; border-left: 3px solid #d08a3c; font-weight: 650; }
        QStatusBar { border-top: 1px solid #383e3f; }
        QPushButton { background: #282d2e; color: #e4e7e5; border: 1px solid #4c5354; padding: 6px 10px; }
        QPushButton:hover { background: #303637; }
        #primaryButton { background: #d08a3c; color: #171a1b; border-color: #d08a3c; font-weight: 650; }
        QPushButton:disabled { color: #747b79; border-color: #383e3f; }
        QLineEdit, QTextEdit, QComboBox { background: #202425; color: #e4e7e5; border: 1px solid #4c5354; padding: 5px; selection-background-color: #72502d; }
        #filters { background: #202425; border-bottom: 1px solid #383e3f; }
        #dataPane { background: #202425; border-right: 1px solid #383e3f; }
        #inspector { background: #202425; }
        #readingName { color: #e4e7e5; font-size: 16px; font-weight: 650; }
        #readingValue { color: #e4e7e5; font-family: monospace; font-size: 32px; font-weight: 700; padding-top: 8px; }
        #healthyText { color: #55a77c; font-size: 12px; font-weight: 700; }
        #graphMeta { color: #9da4a1; border: 1px solid #383e3f; padding: 6px 8px; font-family: monospace; font-size: 10px; }
        #readingDetails { color: #9da4a1; border-top: 1px solid #383e3f; padding-top: 14px; font-size: 12px; }
        QTableWidget { background: #202425; alternate-background-color: #24292a; color: #e4e7e5; gridline-color: #383e3f; border: 0; }
        QTableWidget::item { padding: 7px 9px; }
        QTableWidget::item:selected { background: #303738; border-left: 2px solid #d08a3c; }
        QHeaderView::section { background: #282d2e; color: #9da4a1; border: 0; border-right: 1px solid #383e3f; border-bottom: 1px solid #4c5354; padding: 6px; }
        QDockWidget { color: #e4e7e5; font-weight: 600; }
        QDockWidget::title { background: #282d2e; border-bottom: 1px solid #4c5354; padding: 9px; }
        QSplitter::handle { background: #383e3f; width: 1px; }
        QProgressBar { border: 1px solid #4c5354; background: #202425; }
        QProgressBar::chunk { background: #d08a3c; }
    )");
    loadSavedApiKey();
}

MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    if (scanThread_) {
        scanThread_->quit();
        scanThread_->wait();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Polish) {
        if (auto* dialog = qobject_cast<QDialog*>(watched)) {
            dialog->setWindowFlag(Qt::FramelessWindowHint, true);
            dialog->setAttribute(Qt::WA_StyledBackground, true);
        }
    }
    if (watched == titleBar_) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                isMaximized() ? showNormal() : showMaximized();
                return true;
            }
        }
    }
    // The main title bar and every dialog's custom "dialogHead" header
    // (service manual viewer, tech logbook viewer) are both drag handles for
    // their respective top-level window -- neither has real OS chrome since
    // both the main window and its dialogs are frameless.
    auto* header = qobject_cast<QWidget*>(watched);
    const bool isDragHandle = header
        && (watched == titleBar_ || header->objectName() == "dialogHead");
    if (isDragHandle && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (auto* topWindow = header->window()->windowHandle()) {
                topWindow->startSystemMove();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setAiEnabled(bool enabled) {
    if (!enabled) {
        aiApiKey_.fill(QChar('\0'));
        aiApiKey_.clear();
        aiStatus_->setText("Disabled — API key cleared from memory");
        aiAsk_->setEnabled(false);
        aiRefreshModels_->setEnabled(false);
        return;
    }

    bool accepted = false;
    QString key = QInputDialog::getText(this, "Enable OpenRouter AI",
        "Enter your OpenRouter API key. It will be kept only for this run and will not be saved:",
        QLineEdit::Password, {}, &accepted).trimmed();
    if (!accepted || key.isEmpty()) {
        aiEnabled_->setChecked(false);
        return;
    }

    aiApiKey_ = std::move(key);
    aiStatus_->setText("Enabled — key held in memory only");
    aiAsk_->setEnabled(true);
    aiRefreshModels_->setEnabled(true);
    if (QMessageBox::question(this, "Remember API key securely",
            "Save this key in the operating system keyring for future launches?\n\n"
            "It will not be written to the project or a plain-text settings file.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
        saveApiKeySecurely();
    }
}

void MainWindow::loadSavedApiKey() {
    auto* job = new QKeychain::ReadPasswordJob("370obd2", this);
    job->setKey("openrouter-api-key");
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job*) {
        if (job->error() == QKeychain::NoError && !job->textData().isEmpty()) {
            aiApiKey_ = job->textData();
            aiEnabled_->blockSignals(true);
            aiEnabled_->setChecked(true);
            aiEnabled_->blockSignals(false);
            aiAsk_->setEnabled(true);
            aiRefreshModels_->setEnabled(true);
            aiForgetKey_->setEnabled(true);
            aiStatus_->setText("Enabled — key loaded securely from system keyring");
        }
    });
    job->start();
}

void MainWindow::saveApiKeySecurely() {
    auto* job = new QKeychain::WritePasswordJob("370obd2", this);
    job->setKey("openrouter-api-key");
    job->setTextData(aiApiKey_);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job*) {
        if (job->error() == QKeychain::NoError) {
            aiForgetKey_->setEnabled(true);
            aiStatus_->setText("Enabled — key saved securely in system keyring");
        } else {
            aiStatus_->setText("Enabled in memory; keyring save failed: " + job->errorString());
        }
    });
    job->start();
}

void MainWindow::forgetSavedApiKey() {
    auto* job = new QKeychain::DeletePasswordJob("370obd2", this);
    job->setKey("openrouter-api-key");
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job*) {
        if (job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound) {
            aiForgetKey_->setEnabled(false);
            aiEnabled_->setChecked(false);
            aiStatus_->setText("Saved key removed; AI disabled");
        } else {
            QMessageBox::warning(this, "Could not remove saved key", job->errorString());
        }
    });
    job->start();
}

void MainWindow::refreshOpenRouterModels() {
    if (aiApiKey_.isEmpty()) {
        return;
    }
    aiRefreshModels_->setEnabled(false);
    aiStatus_->setText("Loading available models...");
    QNetworkRequest request(QUrl("https://openrouter.ai/api/v1/models"));
    request.setTransferTimeout(30000);
    request.setRawHeader("Authorization", "Bearer " + aiApiKey_.toUtf8());
    QNetworkReply* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        aiRefreshModels_->setEnabled(true);
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            aiStatus_->setText("Model refresh failed: " + reply->errorString());
            reply->deleteLater();
            return;
        }
        availableModels_ = QJsonDocument::fromJson(body).object().value("data").toArray();
        populateModelList();
        aiStatus_->setText(QString("Enabled — loaded %1 models").arg(availableModels_.size()));
        reply->deleteLater();
    });
}

void MainWindow::populateModelList() {
    const QString previousId = aiModel_->currentData().toString();
    aiModel_->clear();
    aiModel_->addItem("OpenRouter Free Router", "openrouter/free");
    aiModel_->setItemData(0, 0.0, Qt::UserRole + 1);
    aiModel_->setItemData(0, 0.0, Qt::UserRole + 2);

    struct ModelRow { QString label; QString id; double prompt; double completion; };
    std::vector<ModelRow> rows;
    for (const QJsonValue& value : availableModels_) {
        const QJsonObject model = value.toObject();
        const QString id = model.value("id").toString();
        const QJsonObject pricing = model.value("pricing").toObject();
        bool promptOk = false;
        bool completionOk = false;
        const double prompt = pricing.value("prompt").toString().toDouble(&promptOk);
        const double completion = pricing.value("completion").toString().toDouble(&completionOk);
        if (id.isEmpty() || !promptOk || !completionOk) continue;
        const bool free = prompt == 0.0 && completion == 0.0;
        if (aiFreeOnly_->isChecked() && !free) continue;
        const QString name = model.value("name").toString(id);
        rows.push_back({name + (free ? " — FREE" : ""), id, prompt, completion});
    }
    std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
        return left.label.compare(right.label, Qt::CaseInsensitive) < 0;
    });
    for (const auto& row : rows) {
        aiModel_->addItem(row.label, row.id);
        const int index = aiModel_->count() - 1;
        aiModel_->setItemData(index, row.prompt, Qt::UserRole + 1);
        aiModel_->setItemData(index, row.completion, Qt::UserRole + 2);
        aiModel_->setItemData(index, row.id, Qt::ToolTipRole);
    }
    const int previous = aiModel_->findData(previousId);
    aiModel_->setCurrentIndex(previous >= 0 ? previous : 0);
}

QString MainWindow::manualContext(const QStringList& paths, const QString& question) const {
    QStringList terms = question.toLower().split(
        QRegularExpression("[^a-z0-9]+"), Qt::SkipEmptyParts);
    terms.erase(std::remove_if(terms.begin(), terms.end(),
        [](const QString& term) { return term.size() < 4; }), terms.end());

    struct RankedPage { int score; QString path; int page; QString text; };
    std::vector<RankedPage> ranked;
    for (const QString& path : paths) {
        QString extractedText;
        const auto cached = manualTextCache_.constFind(path);
        if (cached != manualTextCache_.cend()) {
            extractedText = cached.value();
        } else {
            QProcess extractor;
            extractor.start("pdftotext", {"-layout", path, "-"});
            if (!extractor.waitForFinished(15000) || extractor.exitCode() != 0) {
                continue;
            }
            extractedText = QString::fromUtf8(extractor.readAllStandardOutput());
            manualTextCache_.insert(path, extractedText);
        }
        const QStringList pages = extractedText.split('\f');
        for (int page = 0; page < pages.size(); ++page) {
            const QString lower = pages[page].toLower();
            int score = 0;
            for (const QString& term : terms) {
                score += lower.count(term);
            }
            if (score > 0) {
                ranked.push_back({score, path, page, pages[page]});
            }
        }
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.score > right.score;
    });

    QString context;
    const int selectedPages = std::min(4, static_cast<int>(ranked.size()));
    for (int i = 0; i < selectedPages; ++i) {
        const RankedPage& page = ranked[static_cast<size_t>(i)];
        context += QString("\n--- %1, PDF page %2 ---\n%3\n")
            .arg(QFileInfo(page.path).fileName())
            .arg(page.page + 1)
            .arg(page.text.left(3500));
    }
    return context;
}

void MainWindow::askOpenRouter() {
    const QString question = aiQuestion_->toPlainText().trimmed();
    const QString manualChoice = aiManual_->currentData().toString();
    if (aiApiKey_.isEmpty()) {
        setAiEnabled(true);
        if (aiApiKey_.isEmpty()) return;
    }
    if (question.isEmpty()) {
        QMessageBox::information(this, "Missing information",
            "Type a question first.");
        return;
    }

    const QString sensors = sensorContext();
    const QString history = logbookContext(question + "\n" + sensors);
    QStringList manualPaths;
    if (manualChoice == "__all__") {
        const QDir directory(QStringLiteral(OBD_PROJECT_SOURCE_DIR "/manuals"));
        for (const QFileInfo& file : directory.entryInfoList({"*.pdf"}, QDir::Files | QDir::Readable)) {
            manualPaths.push_back(file.absoluteFilePath());
        }
        aiAnswer_->setPlainText("Searching all service manuals for relevant pages...");
        QApplication::processEvents();
    } else if (!manualChoice.isEmpty()) {
        manualPaths.push_back(manualChoice);
    }
    const QString manual = manualPaths.isEmpty()
        ? QString() : manualContext(manualPaths, question + "\n" + sensors);
    QJsonObject payload;
    const QString selectedModel = aiModel_->currentData().toString().isEmpty()
        ? aiModel_->currentText().trimmed() : aiModel_->currentData().toString();
    payload["model"] = selectedModel;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "system"}, {"content",
            "You are a cautious 2009 Nissan 370Z diagnostic assistant. Analyze the supplied "
            "sensor snapshot, relevant technician logbook history, and optional service-manual "
            "excerpts. Look for recurring codes, symptoms, and sensor trends, but do not claim a "
            "trend unless the dated history supports it. Clearly distinguish simulated "
            "data from vehicle data. Cite manual filename and PDF page when using a manual. "
            "If the supplied evidence does not establish an answer, say so. Never invent PID "
            "addresses, decoding formulas, torque values, or safety procedures."}},
        QJsonObject{{"role", "user"}, {"content",
            "Question: " + question + "\n\nSensor snapshot:\n" + sensors
            + "\n\nRelevant technician logbook history:\n" + history
            + "\n\nService manual excerpts:\n" + manual}}
    };

    QNetworkRequest request(QUrl("https://openrouter.ai/api/v1/chat/completions"));
    request.setTransferTimeout(60000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + aiApiKey_.toUtf8());
    request.setRawHeader("X-Title", "370 OBD-II Scanner");

    aiAsk_->setEnabled(false);
    aiAnswer_->setPlainText("Asking OpenRouter...");
    aiQuestion_->clear();
    QNetworkReply* reply = network_->post(request, QJsonDocument(payload).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        aiAsk_->setEnabled(true);
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            aiAnswer_->setPlainText("OpenRouter error: " + reply->errorString()
                + "\n\n" + QString::fromUtf8(body));
            reply->deleteLater();
            return;
        }

        const QJsonObject response = QJsonDocument::fromJson(body).object();
        const QJsonArray choices = response.value("choices").toArray();
        if (choices.isEmpty()) {
            aiAnswer_->setPlainText("OpenRouter returned no answer.\n\n" + QString::fromUtf8(body));
        } else {
            QString answer = choices[0].toObject().value("message")
                .toObject().value("content").toString();
            const QJsonObject usage = response.value("usage").toObject();
            const int promptTokens = usage.value("prompt_tokens").toInt();
            const int completionTokens = usage.value("completion_tokens").toInt();
            const int modelIndex = aiModel_->currentIndex();
            const double promptRate = aiModel_->itemData(modelIndex, Qt::UserRole + 1).toDouble();
            const double completionRate = aiModel_->itemData(modelIndex, Qt::UserRole + 2).toDouble();
            const double estimatedCost = promptTokens * promptRate + completionTokens * completionRate;
            answer += QString("\n\n---\nTokens: %1 input + %2 output · Estimated cost: $%3")
                .arg(promptTokens).arg(completionTokens).arg(estimatedCost, 0, 'f', 6);
            aiAnswer_->setPlainText(answer);
        }
        reply->deleteLater();
    });
}

QString MainWindow::sensorContext() const {
    if (table_->rowCount() == 0) {
        return {};
    }
    QString context = "Source: " + scanSource_ + "\n";
    context += "Sensor | Module | Value | Unit | Status\n";
    for (int row = 0; row < table_->rowCount(); ++row) {
        QStringList cells;
        for (int column = 0; column < table_->columnCount(); ++column) {
            const QTableWidgetItem* item = table_->item(row, column);
            cells.push_back(item ? item->text() : QString());
        }
        context += cells.join(" | ") + "\n";
    }
    return context;
}

QString MainWindow::logbookContext(const QString& question) const {
    QFile file(QStringLiteral(OBD_PROJECT_SOURCE_DIR "/logbook/TECH_LOGBOOK.md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString contents = QString::fromUtf8(file.readAll());
    QStringList entries;
    const QStringList sections = contents.split("\n## ", Qt::SkipEmptyParts);
    for (const QString& section : sections) {
        if (!section.startsWith("# 370Z Technician Logbook")) {
            entries.push_back("## " + section);
        }
    }
    QStringList terms = question.toLower().split(
        QRegularExpression("[^a-z0-9]+"), Qt::SkipEmptyParts);
    terms.erase(std::remove_if(terms.begin(), terms.end(),
        [](const QString& term) { return term.size() < 4; }), terms.end());

    struct RankedEntry { int score; int index; QString text; };
    std::vector<RankedEntry> ranked;
    for (int index = 0; index < entries.size(); ++index) {
        const QString lower = entries[index].toLower();
        int score = 0;
        for (const QString& term : terms) score += lower.count(term);
        if (index >= entries.size() - 2) score += 1;
        ranked.push_back({score, index, entries[index]});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        if (left.score != right.score) return left.score > right.score;
        return left.index > right.index;
    });

    QString result;
    for (const RankedEntry& entry : ranked) {
        if (entry.score <= 0 || result.size() >= 12000) break;
        result += entry.text.left(5000) + "\n";
        if (result.size() >= 12000) break;
    }
    return result.left(12000);
}

void MainWindow::saveTechLogbookEntry() {
    const QString notes = techNotes_->toPlainText().trimmed();
    const QString sensors = sensorContext();
    const QString question = aiQuestion_->toPlainText().trimmed();
    const QString answer = aiAnswer_->toPlainText().trimmed();
    if (notes.isEmpty() && sensors.isEmpty() && question.isEmpty()) {
        QMessageBox::information(this, "Nothing to save",
            "Add technician notes, run a scan, or enter a diagnostic question first.");
        return;
    }

    const QString directory = QStringLiteral(OBD_PROJECT_SOURCE_DIR "/logbook");
    if (!QDir().mkpath(directory)) {
        QMessageBox::warning(this, "Logbook error", "Could not create the logbook directory.");
        return;
    }
    const QString path = directory + "/TECH_LOGBOOK.md";
    const bool newFile = !QFileInfo::exists(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QMessageBox::warning(this, "Logbook error", "Could not write to:\n" + path);
        return;
    }
    QTextStream out(&file);
    if (newFile) {
        out << "# 370Z Technician Logbook\n\n"
               "Local diagnostic history for technician and AI-assisted review. "
               "Verify all AI suggestions against service information.\n\n";
    }
    out << "## " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";
    out << "- Data source: " << scanSource_ << "\n";
    if (!notes.isEmpty()) out << "- Technician notes: " << notes << "\n";
    if (!question.isEmpty()) out << "- Diagnostic question: " << question << "\n";
    if (!sensors.isEmpty()) out << "\n### Sensor snapshot\n\n~~~text\n" << sensors << "~~~\n";
    if (!answer.isEmpty() && answer != "Asking OpenRouter...") {
        out << "\n### AI analysis (unverified)\n\n" << answer << "\n";
    }
    out << "\n---\n\n";
    file.close();
    techNotes_->clear();
    statusBar()->showMessage("Case saved to Tech Logbook", 5000);
}

void MainWindow::viewTechLogbook() {
    const QString path = QStringLiteral(OBD_PROJECT_SOURCE_DIR "/logbook/TECH_LOGBOOK.md");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::information(this, "Tech Logbook", "No logbook entries have been saved yet.");
        return;
    }
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("370Z Technician Logbook");
    dialog->resize(950, 700);
    auto* viewer = new QTextEdit(dialog);
    viewer->setReadOnly(true);
    viewer->setMarkdown(QString::fromUtf8(file.readAll()));
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* dialogHead = new QWidget(dialog);
    dialogHead->setObjectName("dialogHead");
    auto* dialogHeadLayout = new QHBoxLayout(dialogHead);
    dialogHeadLayout->setContentsMargins(14, 6, 8, 6);
    auto* dialogTitle = new QLabel("370Z Technician Logbook", dialogHead);
    dialogTitle->setObjectName("dialogTitle");
    auto* dialogClose = new QPushButton("×", dialogHead);
    dialogClose->setObjectName("closeWindowButton");
    dialogClose->setFixedSize(34, 30);
    dialogHeadLayout->addWidget(dialogTitle);
    dialogHeadLayout->addStretch();
    dialogHeadLayout->addWidget(dialogClose);
    connect(dialogClose, &QPushButton::clicked, dialog, &QDialog::close);
    layout->addWidget(dialogHead);
    layout->addWidget(viewer);
    dialog->show();
}

void MainWindow::displayReport(const obd::CoverageReport& report, const QString& source) {
    scanSource_ = source;
    const qint64 sampleTime = QDateTime::currentMSecsSinceEpoch();
    std::vector<obd::SensorValue> rows;
    rows.insert(rows.end(), report.decodedSensors.begin(), report.decodedSensors.end());
    rows.insert(rows.end(), report.rawOnlySensors.begin(), report.rawOnlySensors.end());
    rows.insert(rows.end(), report.unavailableSensors.begin(), report.unavailableSensors.end());

    table_->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const auto& value = rows[static_cast<size_t>(row)];
        const QString command = QString::fromStdString(obd::byteToHex(value.mode)
            + obd::byteToHex(value.pid, value.pid > 0xFF ? 4 : 2));
        const QString displayValue = value.numericValue
            ? QString::number(*value.numericValue, 'f', 2)
            : QString::fromStdString(value.raw);
        auto* sensorItem = new QTableWidgetItem(QString::fromStdString(value.name));
        sensorItem->setData(Qt::UserRole, command);
        table_->setItem(row, 0, sensorItem);
        table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(value.module)));
        table_->setItem(row, 2, new QTableWidgetItem(displayValue));
        table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(value.unit)));
        table_->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(value.status)));
        if (value.numericValue) {
            liveChart_->addSample(QString::fromStdString(value.name), *value.numericValue,
                QString::fromStdString(value.unit), sampleTime);
        }
        if (recording_) {
            const QString csvRow = QDateTime::fromMSecsSinceEpoch(sampleTime).toString(Qt::ISODateWithMs)
                + "," + csvField(QString::fromStdString(value.module))
                + "," + csvField(command)
                + "," + csvField(QString::fromStdString(value.name))
                + "," + csvField(displayValue)
                + "," + csvField(QString::fromStdString(value.unit))
                + "," + csvField(QString::fromStdString(value.status)) + "\n";
            recordingFile_.write(csvRow.toUtf8());
            ++recordedSampleCount_;
        }
    }
    if (recording_) {
        recordingFile_.flush();
    }
    if (!rows.empty()) {
        table_->selectRow(0);
        liveChart_->selectSeries(QString::fromStdString(rows.front().name));
    }
    statusBar()->showMessage(QString("%1 — %2 sensor rows").arg(source).arg(rows.size()));
}

void MainWindow::displayDtcs(const obd::DtcReport& report) {
    std::vector<obd::DiagnosticTroubleCode> rows;
    rows.insert(rows.end(), report.stored.begin(), report.stored.end());
    rows.insert(rows.end(), report.pending.begin(), report.pending.end());
    rows.insert(rows.end(), report.permanent.begin(), report.permanent.end());

    dtcTable_->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const auto& dtc = rows[static_cast<size_t>(row)];
        dtcTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(dtc.code)));
        dtcTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(dtc.module)));
        dtcTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(dtc.status)));
        dtcTable_->setItem(row, 3, new QTableWidgetItem("Description unavailable"));
    }
}

void MainWindow::runSimulatorScan() {
    setScanBusy(true, "Scanning simulator...");
    QMetaObject::invokeMethod(scanWorker_, "scanSimulator", Qt::QueuedConnection);
}

void MainWindow::runSerialScan() {
    const QString path = serialPath_->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::information(this, "Missing serial device",
            "Enter an ELM327 serial device such as /dev/ttyUSB0.");
        return;
    }

    setScanBusy(true, "Scanning vehicle...");
    QMetaObject::invokeMethod(scanWorker_, "scanVehicle", Qt::QueuedConnection,
        Q_ARG(QString, path));
}

void MainWindow::clearSimulatorDtcs() {
    setScanBusy(true, "Clearing simulator DTCs...");
    QMetaObject::invokeMethod(scanWorker_, "clearSimulatorDtcs", Qt::QueuedConnection);
}

void MainWindow::clearVehicleDtcs() {
    const QString path = serialPath_->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::information(this, "Missing serial device",
            "Enter an ELM327 serial device such as /dev/ttyUSB0.");
        return;
    }
    if (QMessageBox::question(this, "Clear vehicle DTCs",
            "This clears stored and pending trouble codes and turns off the MIL on the "
            "connected vehicle. Permanent codes are not affected. Continue?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    setScanBusy(true, "Clearing vehicle DTCs...");
    QMetaObject::invokeMethod(scanWorker_, "clearVehicleDtcs", Qt::QueuedConnection,
        Q_ARG(QString, path));
}

void MainWindow::setScanBusy(bool busy, const QString& message) {
    simulatorScan_->setEnabled(!busy);
    simulatorClear_->setEnabled(!busy);
    vehicleScan_->setEnabled(!busy);
    vehicleClear_->setEnabled(!busy);
    serialPath_->setEnabled(!busy);
    scanProgress_->setVisible(busy);
    loadRecordingButton_->setEnabled(!busy);
    playbackToggle_->setEnabled(!busy && !playbackSnapshots_.empty());
    if (!message.isEmpty()) {
        statusBar()->showMessage(message);
    } else if (!busy) {
        statusBar()->clearMessage();
    }
}

void MainWindow::toggleRecording() {
    if (recording_) {
        stopRecording("Recording stopped.");
        return;
    }
    startRecording();
}

void MainWindow::startRecording() {
    const QString path = serialPath_->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::information(this, "Missing serial device",
            "Enter an ELM327 serial device such as /dev/ttyUSB0 before recording.");
        return;
    }

    const QString dirPath = QStringLiteral(OBD_PROJECT_SOURCE_DIR "/logbook/recordings");
    QDir().mkpath(dirPath);
    const QString fileName = "drive_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv";
    recordingFile_.setFileName(dirPath + "/" + fileName);
    if (!recordingFile_.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Recording failed",
            "Could not create " + recordingFile_.fileName());
        return;
    }
    recordingFile_.write("timestamp,module,command,sensor,value,unit,status\n");

    recording_ = true;
    recordedSampleCount_ = 0;
    recordToggle_->setText("Stop Recording");
    serialPath_->setEnabled(false);
    statusBar()->showMessage("Recording live data to " + recordingFile_.fileName());
    runSerialScan();
}

void MainWindow::stopRecording(const QString& statusMessage) {
    if (!recording_) {
        return;
    }
    recording_ = false;
    recordToggle_->setText("Record");
    serialPath_->setEnabled(!scanProgress_->isVisible());
    const QString path = recordingFile_.fileName();
    recordingFile_.close();
    if (!statusMessage.isEmpty()) {
        statusBar()->showMessage(statusMessage + " Saved " + QString::number(recordedSampleCount_)
            + " samples to " + path);
    }
}

void MainWindow::loadRecording() {
    if (recording_ || scanProgress_->isVisible()) {
        QMessageBox::information(this, "Cannot load recording",
            "Stop the current recording or scan before loading a saved drive log.");
        return;
    }

    const QString dirPath = QStringLiteral(OBD_PROJECT_SOURCE_DIR "/logbook/recordings");
    const QString path = QFileDialog::getOpenFileName(this, "Load Recording", dirPath, "CSV files (*.csv)");
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Load failed", "Could not open " + path);
        return;
    }

    std::vector<PlaybackSnapshot> snapshots;
    QString currentTimestamp;
    QTextStream in(&file);
    bool firstLine = true;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (firstLine) {
            firstLine = false;
            continue;
        }
        if (line.isEmpty()) {
            continue;
        }
        const QStringList fields = parseCsvLine(line);
        if (fields.size() < 7) {
            continue;
        }
        const QString& timestamp = fields[0];
        if (snapshots.empty() || timestamp != currentTimestamp) {
            PlaybackSnapshot snapshot;
            snapshot.timestampMs = QDateTime::fromString(timestamp, Qt::ISODateWithMs).toMSecsSinceEpoch();
            snapshots.push_back(snapshot);
            currentTimestamp = timestamp;
        }
        PlaybackRow row;
        row.module = fields[1];
        row.command = fields[2];
        row.sensor = fields[3];
        row.value = fields[4];
        row.unit = fields[5];
        row.status = fields[6];
        snapshots.back().rows.push_back(row);
    }
    file.close();

    if (snapshots.empty()) {
        QMessageBox::information(this, "Empty recording", "No sensor rows found in " + path);
        return;
    }

    playbackSnapshots_ = std::move(snapshots);
    playbackIndex_ = 0;
    playbackPlaying_ = false;
    playbackSourceLabel_ = QFileInfo(path).fileName();
    playbackToggle_->setEnabled(true);
    playbackToggle_->setText("Play");
    statusBar()->showMessage(QString("Loaded %1 snapshots from %2")
        .arg(playbackSnapshots_.size())
        .arg(playbackSourceLabel_));
}

void MainWindow::togglePlayback() {
    if (playbackSnapshots_.empty()) {
        return;
    }
    if (playbackPlaying_) {
        playbackPlaying_ = false;
        playbackToggle_->setText("Play");
        statusBar()->showMessage("Playback paused.");
        return;
    }

    if (playbackIndex_ >= static_cast<int>(playbackSnapshots_.size())) {
        playbackIndex_ = 0;
    }
    playbackPlaying_ = true;
    playbackToggle_->setText("Pause");
    scanVehicleButton_->setEnabled(false);
    recordToggle_->setEnabled(false);
    loadRecordingButton_->setEnabled(false);
    advancePlayback();
}

void MainWindow::advancePlayback() {
    if (!playbackPlaying_) {
        // Paused: a previously scheduled tick may still fire, but
        // togglePlayback() already restored button state and the status
        // message, so there is nothing left to do here.
        return;
    }
    if (playbackIndex_ >= static_cast<int>(playbackSnapshots_.size())) {
        playbackPlaying_ = false;
        playbackToggle_->setText("Play");
        scanVehicleButton_->setEnabled(true);
        recordToggle_->setEnabled(true);
        loadRecordingButton_->setEnabled(true);
        statusBar()->showMessage("Playback finished: " + playbackSourceLabel_);
        return;
    }

    const auto& snapshot = playbackSnapshots_[static_cast<size_t>(playbackIndex_)];
    table_->setRowCount(static_cast<int>(snapshot.rows.size()));
    for (int row = 0; row < static_cast<int>(snapshot.rows.size()); ++row) {
        const auto& sample = snapshot.rows[static_cast<size_t>(row)];
        auto* sensorItem = new QTableWidgetItem(sample.sensor);
        sensorItem->setData(Qt::UserRole, sample.command);
        table_->setItem(row, 0, sensorItem);
        table_->setItem(row, 1, new QTableWidgetItem(sample.module));
        table_->setItem(row, 2, new QTableWidgetItem(sample.value));
        table_->setItem(row, 3, new QTableWidgetItem(sample.unit));
        table_->setItem(row, 4, new QTableWidgetItem(sample.status));

        bool numericOk = false;
        const double numericValue = sample.value.toDouble(&numericOk);
        if (numericOk) {
            liveChart_->addSample(sample.sensor, numericValue, sample.unit, snapshot.timestampMs);
        }
    }
    if (!snapshot.rows.empty()) {
        table_->selectRow(0);
        liveChart_->selectSeries(snapshot.rows.front().sensor);
    }

    const int position = playbackIndex_ + 1;
    statusBar()->showMessage(QString("Playback %1/%2 — %3")
        .arg(position)
        .arg(playbackSnapshots_.size())
        .arg(playbackSourceLabel_));

    constexpr int kMinStepMs = 200;
    constexpr int kMaxStepMs = 5000;
    int delayMs = 1000;
    if (playbackIndex_ + 1 < static_cast<int>(playbackSnapshots_.size())) {
        const qint64 delta = playbackSnapshots_[static_cast<size_t>(playbackIndex_ + 1)].timestampMs
            - snapshot.timestampMs;
        delayMs = std::clamp(static_cast<int>(delta), kMinStepMs, kMaxStepMs);
    }

    ++playbackIndex_;
    QTimer::singleShot(delayMs, this, &MainWindow::advancePlayback);
}

} // namespace gui
