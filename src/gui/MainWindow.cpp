#include "gui/MainWindow.hpp"

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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPdfDocument>
#include <QPdfView>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include <qt6keychain/keychain.h>

namespace gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    auto* central = new QSplitter(Qt::Vertical, this);
    table_ = new QTableWidget(central);
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({"Module", "Command", "Sensor", "Value", "Unit", "Status"});
    table_->horizontalHeader()->setStretchLastSection(true);
    central->addWidget(table_);

    dtcTable_ = new QTableWidget(central);
    dtcTable_->setColumnCount(3);
    dtcTable_->setHorizontalHeaderLabels({"Module", "Code", "Status"});
    dtcTable_->horizontalHeader()->setStretchLastSection(true);
    central->addWidget(dtcTable_);
    central->setStretchFactor(0, 3);
    central->setStretchFactor(1, 1);
    setCentralWidget(central);

    auto* toolbar = addToolBar("Connection");
    auto* scan = new QPushButton("Run simulator scan", this);
    toolbar->addWidget(scan);
    connect(scan, &QPushButton::clicked, this, &MainWindow::runSimulatorScan);
    auto* clearSimDtcs = new QPushButton("Clear DTCs (simulator)", this);
    toolbar->addWidget(clearSimDtcs);
    connect(clearSimDtcs, &QPushButton::clicked, this, &MainWindow::clearSimulatorDtcs);
    toolbar->addSeparator();
    serialPath_ = new QLineEdit("/dev/ttyUSB0", this);
    serialPath_->setPlaceholderText("Serial device, e.g. /dev/ttyUSB0");
    serialPath_->setMinimumWidth(190);
    toolbar->addWidget(serialPath_);
    auto* serialScan = new QPushButton("Scan vehicle", this);
    toolbar->addWidget(serialScan);
    connect(serialScan, &QPushButton::clicked, this, &MainWindow::runSerialScan);
    auto* clearVehicleDtcsButton = new QPushButton("Clear vehicle DTCs", this);
    toolbar->addWidget(clearVehicleDtcsButton);
    connect(clearVehicleDtcsButton, &QPushButton::clicked, this, &MainWindow::clearVehicleDtcs);

    auto* manualsMenu = menuBar()->addMenu("Service Manuals");
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
                layout->addWidget(viewer);
                viewerWindow->show();
                });
            });
        }
    }

    network_ = new QNetworkAccessManager(this);
    auto* aiDock = new QDockWidget("Manual AI Assistant", this);
    auto* aiPanel = new QWidget(aiDock);
    auto* aiLayout = new QVBoxLayout(aiPanel);
    auto* aiForm = new QFormLayout;

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
    for (const QFileInfo& manual : manuals) {
        aiManual_->addItem(manual.completeBaseName(), manual.absoluteFilePath());
    }

    aiForm->addRow(aiEnabled_);
    aiForm->addRow("Status", aiStatus_);
    aiForm->addRow(aiForgetKey_);
    aiForm->addRow("Model", modelRow);
    aiFreeOnly_ = new QCheckBox("Show free models only", aiPanel);
    aiFreeOnly_->setChecked(true);
    aiPricing_ = new QLabel("Pricing: refresh models to load current rates", aiPanel);
    aiPricing_->setWordWrap(true);
    aiForm->addRow(aiFreeOnly_);
    aiForm->addRow("Cost", aiPricing_);
    aiForm->addRow("Manual", aiManual_);
    aiLayout->addLayout(aiForm);
    aiLayout->addWidget(new QLabel("Question", aiPanel));
    aiQuestion_ = new QTextEdit(aiPanel);
    aiQuestion_->setPlaceholderText("Example: What should I check for code P0101?");
    aiQuestion_->setMaximumHeight(100);
    aiLayout->addWidget(aiQuestion_);
    aiAsk_ = new QPushButton("Ask using selected manual", aiPanel);
    aiAsk_->setEnabled(false);
    aiLayout->addWidget(aiAsk_);
    aiLayout->addWidget(new QLabel("Answer", aiPanel));
    aiAnswer_ = new QTextEdit(aiPanel);
    aiAnswer_->setReadOnly(true);
    aiAnswer_->setMinimumHeight(320);
    QFont aiFont = aiAnswer_->font();
    aiFont.setPointSize(11);
    aiQuestion_->setFont(aiFont);
    aiAnswer_->setFont(aiFont);
    aiLayout->addWidget(aiAnswer_);
    aiLayout->addWidget(new QLabel("Technician notes / symptoms", aiPanel));
    techNotes_ = new QTextEdit(aiPanel);
    techNotes_->setPlaceholderText(
        "Example: Intermittent rough idle after warm-up; P0101 returned twice this month.");
    techNotes_->setMaximumHeight(90);
    techNotes_->setFont(aiFont);
    aiLayout->addWidget(techNotes_);
    auto* logbookButtons = new QWidget(aiPanel);
    auto* logbookLayout = new QHBoxLayout(logbookButtons);
    logbookLayout->setContentsMargins(0, 0, 0, 0);
    saveLogbook_ = new QPushButton("Save case to Tech Logbook", logbookButtons);
    auto* viewLogbook = new QPushButton("View Logbook", logbookButtons);
    logbookLayout->addWidget(saveLogbook_);
    logbookLayout->addWidget(viewLogbook);
    aiLayout->addWidget(logbookButtons);
    aiPanel->setLayout(aiLayout);
    aiDock->setWidget(aiPanel);
    aiDock->setMinimumWidth(500);
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

    setWindowTitle("370 OBD-II Scanner");
    resize(1400, 850);
    loadSavedApiKey();
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
        QProcess extractor;
        extractor.start("pdftotext", {"-layout", path, "-"});
        if (!extractor.waitForFinished(15000) || extractor.exitCode() != 0) {
            continue;
        }
        const QStringList pages = QString::fromUtf8(extractor.readAllStandardOutput()).split('\f');
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
    if (!aiEnabled_->isChecked() || aiApiKey_.isEmpty()) {
        QMessageBox::information(this, "AI disabled",
            "Enable OpenRouter AI first. The app will then ask for your API key.");
        return;
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
    if (!manualPaths.isEmpty() && manual.isEmpty()) {
        QMessageBox::warning(this, "No manual context",
            "No relevant pages were found, or pdftotext could not read the manual.");
        return;
    }
    if (sensors.isEmpty() && manual.isEmpty()) {
        QMessageBox::information(this, "No diagnostic context",
            "Run a sensor scan or select a service manual before asking.");
        return;
    }

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
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + aiApiKey_.toUtf8());
    request.setRawHeader("X-Title", "370 OBD-II Scanner");

    aiAsk_->setEnabled(false);
    aiAnswer_->setPlainText("Asking OpenRouter...");
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
    context += "Module | Command | Sensor | Value | Unit | Status\n";
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
    layout->addWidget(viewer);
    dialog->show();
}

void MainWindow::displayReport(const obd::CoverageReport& report, const QString& source) {
    scanSource_ = source;
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
        table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(value.module)));
        table_->setItem(row, 1, new QTableWidgetItem(command));
        table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(value.name)));
        table_->setItem(row, 3, new QTableWidgetItem(displayValue));
        table_->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(value.unit)));
        table_->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(value.status)));
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
        dtcTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(dtc.module)));
        dtcTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(dtc.code)));
        dtcTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(dtc.status)));
    }
}

void MainWindow::runSimulatorScan() {
    obd::EnhancedPidProfile profile;
    std::string error;
    profile.loadCsv(OBD_PROJECT_SOURCE_DIR "/data/sample_nissan_z34_enhanced_pids.csv", &error);

    obd::SimulatorTransport transport;
    transport.connect(&error);
    obd::ScanSession session(transport, &profile);
    const obd::ScanOptions options;
    displayReport(session.run(options), "Simulator data (not from vehicle)");
    displayDtcs(session.readDtcs(options.modules));
}

void MainWindow::runSerialScan() {
    const QString path = serialPath_->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::information(this, "Missing serial device",
            "Enter an ELM327 serial device such as /dev/ttyUSB0.");
        return;
    }

    obd::EnhancedPidProfile profile;
    std::string error;
    profile.loadCsv(OBD_PROJECT_SOURCE_DIR "/data/sample_nissan_z34_enhanced_pids.csv", &error);
    obd::PosixElm327Transport transport(path.toStdString());
    if (!transport.connect(&error)) {
        QMessageBox::warning(this, "Vehicle connection failed", QString::fromStdString(error));
        return;
    }
    obd::ScanSession session(transport, &profile);
    const obd::ScanOptions options;
    displayReport(session.run(options), "Vehicle scan from " + path);
    displayDtcs(session.readDtcs(options.modules));
}

void MainWindow::clearSimulatorDtcs() {
    obd::SimulatorTransport transport;
    std::string error;
    transport.connect(&error);
    obd::ScanSession session(transport, nullptr);
    if (!session.clearDtcs(obd::ScanOptions{}.modules, &error)) {
        QMessageBox::warning(this, "Clear DTCs failed", QString::fromStdString(error));
        return;
    }
    runSimulatorScan();
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

    obd::PosixElm327Transport transport(path.toStdString());
    std::string error;
    if (!transport.connect(&error)) {
        QMessageBox::warning(this, "Vehicle connection failed", QString::fromStdString(error));
        return;
    }
    obd::ScanSession session(transport, nullptr);
    if (!session.clearDtcs(obd::ScanOptions{}.modules, &error)) {
        QMessageBox::warning(this, "Clear DTCs failed", QString::fromStdString(error));
        return;
    }
    runSerialScan();
}

} // namespace gui
