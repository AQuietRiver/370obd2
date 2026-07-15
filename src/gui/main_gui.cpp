#include "gui/MainWindow.hpp"

#include <QApplication>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    gui::MainWindow window;
    window.show();
    const QString screenshotPath = qEnvironmentVariable("OBD_GUI_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(500, &window, [&app, &window, screenshotPath] {
            window.grab().save(screenshotPath);
            app.quit();
        });
    }
    return app.exec();
}
