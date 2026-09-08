#pragma once

#include <QWidget>
#include <QPoint>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QFrame>
#include <QTranslator>
#include "codex_monitor.h"

class CodexWidget : public QWidget {
    Q_OBJECT

public:
    explicit CodexWidget(QWidget *parent = nullptr);
    ~CodexWidget() override;
    void setLanguage(const QString &langCode);
    QMenu *trayMenu() const { return m_trayMenu; }

public slots:
    void toggleCompactMode();
    void toggleMsgTextExpanded();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;

private slots:
    void onSnapshotUpdated(const CodexSnapshot &snapshot);
    void toggleAlwaysOnTop();
    void updateLiveTimers();
    void copyMessageToClipboard();
    void copyCommandToClipboard();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void manualRefresh();

private:
    void setupUi();
    void setupTray();
    void loadSettings();
    void saveSettings();
    void updateTrayIcon(CodexState state);
    QString formatTimeRemaining(qint64 resetsAt);
    QString formatDuration(double elapsedSec, bool isRunning);
    void ensureVisibleOnScreen();
    void retranslateUi();

    // Backend
    CodexMonitor *m_monitor = nullptr;
    CodexSnapshot m_currentSnapshot;
    QTimer m_liveTimer;
    QTranslator m_translator;
    QString m_currentLanguage = "auto";

    // Window dragging
    bool m_isDragging = false;
    QPoint m_dragPosition;
    bool m_isAlwaysOnTop = true;
    bool m_isCompact = false;

    // UI Widgets
    QWidget *m_centralWidget = nullptr;
    QFrame *m_statusBadge = nullptr;
    QLabel *m_statusDot = nullptr;
    QLabel *m_statusText = nullptr;
    QPushButton *m_pinBtn = nullptr;
    QPushButton *m_compactBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    // Limit Card
    QFrame *m_limitCard = nullptr;
    QLabel *m_limitPercent = nullptr;
    QLabel *m_resetCountdown = nullptr;
    QLabel *m_secondaryLimitLabel = nullptr;
    QProgressBar *m_limitBar = nullptr;
    bool m_showRemainingLimit = true; // true = show % remaining (like Codex Switcher), false = % used

    // Message Card (collapsible assistant text, collapsed by default)
    QFrame *m_messageCard = nullptr;
    QPushButton *m_msgToggleBtn = nullptr;
    QLabel *m_msgAuthor = nullptr;
    QLabel *m_msgDurationBadge = nullptr;
    QLabel *m_msgTime = nullptr;
    QLabel *m_msgTextLabel = nullptr;
    QPushButton *m_copyMsgBtn = nullptr;
    bool m_isMsgTextExpanded = false; // Collapsed by default

    // Replica timing state
    QString m_lastReplicaText;
    QDateTime m_replicaStartTime;
    qint64 m_replicaDurationMs = 0;
    bool m_replicaCompleted = false;
    CodexState m_lastState = CodexState::Stopped;

    // Command Card (status badge with live running timer + duration + command text)
    QFrame *m_cmdCard = nullptr;
    QLabel *m_cmdStatusBadge = nullptr;
    QLabel *m_cmdTime = nullptr;
    QLabel *m_cmdTextLabel = nullptr;
    QPushButton *m_copyCmdBtn = nullptr;

    // System tray
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_alwaysOnTopAction = nullptr;
    QAction *m_refreshAction = nullptr;
    QAction *m_resetPosAction = nullptr;
    QMenu *m_langMenu = nullptr;
    QAction *m_langAutoAction = nullptr;
    QAction *m_langEnAction = nullptr;
    QAction *m_langRuAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QAction *m_quitAction = nullptr;
};
