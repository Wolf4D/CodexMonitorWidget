QT += core gui widgets

CONFIG += c++17 release

TARGET = codex_widget
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

LIBS += -lpsapi -luser32

SOURCES += \
    main.cpp \
    widget.cpp \
    codex_monitor.cpp

HEADERS += \
    widget.h \
    codex_monitor.h \
    styles.h

RESOURCES += \
    resources.qrc

TRANSLATIONS += \
    translations/cmw_ru.ts \
    translations/cmw_en.ts

RC_FILE = app.rc
