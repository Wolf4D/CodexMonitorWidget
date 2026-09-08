#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QGuiApplication>
#include <QThread>
#include <QDir>
#include <QDateTime>
#include "widget.h"

int main(int argc, char *argv[])
{
    // Enable High DPI scaling and seamless multi-monitor transitions
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    QApplication app(argc, argv);
    app.setApplicationName("Codex Monitor Widget (CMW)");
    app.setApplicationDisplayName("Codex Monitor Widget (CMW)");
    app.setOrganizationName("Madness Studio");
    app.setWindowIcon(QIcon(":/app.ico"));
    app.setQuitOnLastWindowClosed(false); // Keeps running in tray

    QFont defaultFont("Segoe UI", 9);
    app.setFont(defaultFont);

    CodexWidget widget;

    if (app.arguments().contains("--render-screenshots")) {
        widget.pauseMonitor();
        widget.show();

        auto renderShot = [&](const QString &lang, bool expanded, const QString &outputPath, bool isWorking = false) {
            widget.setLanguage(lang);

            CodexSnapshot snap;
            snap.state = isWorking ? CodexState::Working : CodexState::Idle;
            snap.isProcessRunning = true;
            snap.isRunnerRunning = isWorking;
            snap.primaryUsedPercent = 22.0; // 78% remaining
            snap.primaryResetsAt = QDateTime::currentDateTime().toSecsSinceEpoch() + 3 * 3600 + 45 * 60;
            snap.secondaryUsedPercent = 18.0;
            snap.lastMessageAuthor = "Codex";

            if (lang == "ru") {
                snap.stateDescription = isWorking ? "Выполняется команда" : "Ожидает команды";
                snap.lastMessageText = isWorking ? "Запуск набора тестов для проверки изменений в коде..."
                                                 : "Оптимизирована сетевая подсистема и добавлены юнит-тесты для обработки таймаутов. Все 42 проверки успешно пройдены.";
                snap.lastCommand.command = isWorking ? "pytest tests/test_core.py -v" : "npm test -- --coverage";
                snap.lastCommand.status = isWorking ? "running" : "completed";
                snap.lastCommand.statusText = isWorking ? "Выполняется" : "Выполнена";
                snap.lastCommand.wallTime = isWorking ? "4.2 с" : "1.8 с";
            } else {
                snap.stateDescription = isWorking ? "Executing terminal command" : "Ready for input";
                snap.lastMessageText = isWorking ? "Running test suite to verify code changes..."
                                                 : "Refactored the network polling engine and added unit tests for timeout handling. All 42 test suites passed successfully.";
                snap.lastCommand.command = isWorking ? "pytest tests/test_core.py -v" : "npm test -- --coverage";
                snap.lastCommand.status = isWorking ? "running" : "completed";
                snap.lastCommand.statusText = isWorking ? "Running" : "Completed";
                snap.lastCommand.wallTime = isWorking ? "4.2 s" : "1.8 s";
            }

            snap.lastMessageTime = QDateTime::currentDateTime().addSecs(isWorking ? -6 : -45);
            snap.isTurnActive = isWorking;
            snap.turnDurationMs = isWorking ? 6400 : 14200;
            snap.lastCommand.timestamp = QDateTime::currentDateTime().addSecs(isWorking ? -4 : -55);
            snap.lastCommand.isValid = true;

            widget.applySnapshot(snap);
            widget.setMsgTextExpanded(expanded);
            widget.adjustSize();

            for (int i = 0; i < 25; ++i) {
                app.processEvents();
                QThread::msleep(20);
            }

            QPixmap shot = widget.grab();
            shot.save(outputPath, "PNG");
        };

        QDir().mkpath("assets");
        renderShot("en", false, "assets/widget_en.png", false);
        renderShot("en", true, "assets/widget_en_expanded.png", false);
        renderShot("en", false, "assets/widget_en_working.png", true);
        renderShot("ru", false, "assets/widget_ru.png", false);
        renderShot("ru", true, "assets/widget_ru_expanded.png", false);

        return 0;
    }

    widget.show();

    return app.exec();
}
