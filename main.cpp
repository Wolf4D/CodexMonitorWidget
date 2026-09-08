#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QGuiApplication>
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
    widget.show();

    return app.exec();
}
