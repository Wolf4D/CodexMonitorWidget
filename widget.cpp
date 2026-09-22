#include "widget.h"
#include "styles.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
#include <QSettings>
#include <QPainter>
#include <QDateTime>
#include <QStyle>
#include <QScreen>
#include <QWindow>
#include <QGuiApplication>
#include <QTimer>
#include <QMessageBox>

CodexWidget::CodexWidget(QWidget *parent)
    : QWidget(parent)
{
    loadSettings();
    setupUi();
    setupTray();
    setLanguage(m_currentLanguage);

    connect(&m_liveTimer, &QTimer::timeout, this, &CodexWidget::updateLiveTimers);

    // Start monitor with 200ms polling for instant real-time updates
    m_monitor = new CodexMonitor(this);
    connect(m_monitor, &CodexMonitor::snapshotUpdated, this, &CodexWidget::onSnapshotUpdated);
    m_monitor->start(200);
}

CodexWidget::~CodexWidget()
{
    saveSettings();
}

void CodexWidget::pauseMonitor()
{
    if (m_monitor) {
        m_monitor->stop();
    }
}

void CodexWidget::applySnapshot(const CodexSnapshot &snapshot)
{
    onSnapshotUpdated(snapshot);
}

void CodexWidget::setupUi()
{
    setWindowTitle(tr("Codex Monitor Widget (CMW)"));
    setWindowIcon(QIcon(":/app.ico"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window | (m_isAlwaysOnTop ? Qt::WindowStaysOnTopHint : Qt::Widget));
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(Styles::getAppStyle());

    // Root layout without pixel-based drop shadows that glitch on DPI change
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->setSizeConstraint(QLayout::SetMinimumSize);

    m_centralWidget = new QWidget(this);
    m_centralWidget->setObjectName("centralWidget");
    m_centralWidget->setFixedWidth(270);

    auto *mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(9, 7, 9, 8);
    mainLayout->setSpacing(5);

    // ==========================================
    // 1. Header Bar (Status + Pin + Compact + Close)
    // ==========================================
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    // Status Badge (Pill)
    m_statusBadge = new QFrame(m_centralWidget);
    m_statusBadge->setObjectName("statusBadge");
    auto *badgeLayout = new QHBoxLayout(m_statusBadge);
    badgeLayout->setContentsMargins(6, 2, 7, 2);
    badgeLayout->setSpacing(5);

    m_statusDot = new QLabel(m_statusBadge);
    m_statusDot->setObjectName("statusDot");
    m_statusDot->setStyleSheet("background-color: #ef4444; border-radius: 4px;");

    m_statusText = new QLabel(tr("Idle"), m_statusBadge);
    m_statusText->setObjectName("statusText");
    m_statusText->setStyleSheet("color: #f8fafc; font-weight: 700;");

    badgeLayout->addWidget(m_statusDot);
    badgeLayout->addWidget(m_statusText);

    headerLayout->addWidget(m_statusBadge);
    headerLayout->addStretch();

    // Pin Button (Always on Top)
    m_pinBtn = new QPushButton("📌", m_centralWidget);
    m_pinBtn->setObjectName("pinBtn");
    m_pinBtn->setToolTip(tr("Always on Top"));
    m_pinBtn->setFixedSize(20, 20);
    m_pinBtn->setProperty("pinned", m_isAlwaysOnTop);
    connect(m_pinBtn, &QPushButton::clicked, this, &CodexWidget::toggleAlwaysOnTop);
    headerLayout->addWidget(m_pinBtn);

    // Compact Button (Collapse / Expand)
    m_compactBtn = new QPushButton(m_isCompact ? "▼" : "▲", m_centralWidget);
    m_compactBtn->setObjectName("compactBtn");
    m_compactBtn->setToolTip(tr("Collapse / Expand"));
    m_compactBtn->setFixedSize(20, 20);
    connect(m_compactBtn, &QPushButton::clicked, this, &CodexWidget::toggleCompactMode);
    headerLayout->addWidget(m_compactBtn);

    // Close Button (quits application directly!)
    m_closeBtn = new QPushButton("✕", m_centralWidget);
    m_closeBtn->setObjectName("closeBtn");
    m_closeBtn->setToolTip(tr("Close application"));
    m_closeBtn->setFixedSize(20, 20);
    connect(m_closeBtn, &QPushButton::clicked, qApp, &QApplication::quit);
    headerLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(headerLayout);

    // ==========================================
    // 2. Card: Rate Limit (clean numbers, no text noise)
    // ==========================================
    m_limitCard = new QFrame(m_centralWidget);
    m_limitCard->setObjectName("card");
    auto *limitLayout = new QVBoxLayout(m_limitCard);
    limitLayout->setContentsMargins(8, 5, 8, 6);
    limitLayout->setSpacing(4);

    auto *limitHeader = new QHBoxLayout();
    limitHeader->setContentsMargins(0, 0, 0, 0);
    limitHeader->setSpacing(4);

    m_limitPercent = new QLabel("100%", m_limitCard);
    m_limitPercent->setObjectName("limitPercent");

    m_resetCountdown = new QLabel("--", m_limitCard);
    m_resetCountdown->setObjectName("resetCountdown");

    m_secondaryLimitLabel = new QLabel("", m_limitCard);
    m_secondaryLimitLabel->setObjectName("resetCountdown");
    m_secondaryLimitLabel->setStyleSheet("color: #cbd5e1; font-size: 11px; font-weight: 700;");

    limitHeader->addWidget(m_limitPercent);
    limitHeader->addSpacing(3);
    limitHeader->addWidget(m_resetCountdown);
    limitHeader->addStretch();
    limitHeader->addWidget(m_secondaryLimitLabel);
    limitLayout->addLayout(limitHeader);

    // Progress Bar (ultra sleek 4px)
    m_limitBar = new QProgressBar(m_limitCard);
    m_limitBar->setObjectName("limitBar");
    m_limitBar->setRange(0, 100);
    m_limitBar->setValue(100);
    m_limitBar->setTextVisible(false);
    limitLayout->addWidget(m_limitBar);

    m_limitCard->setCursor(Qt::PointingHandCursor);
    m_limitCard->installEventFilter(this);
    mainLayout->addWidget(m_limitCard);

    // ==========================================
    // 3. Card: Last Message (Compact section with collapsible text)
    // ==========================================
    m_messageCard = new QFrame(m_centralWidget);
    m_messageCard->setObjectName("card");
    auto *msgLayout = new QVBoxLayout(m_messageCard);
    msgLayout->setContentsMargins(6, 2, 6, 2);
    msgLayout->setSpacing(2);

    auto *msgHeader = new QHBoxLayout();
    msgHeader->setContentsMargins(0, 0, 0, 0);
    msgHeader->setSpacing(3);

    m_msgToggleBtn = new QPushButton(m_isMsgTextExpanded ? "▼" : "▶", m_messageCard);
    m_msgToggleBtn->setObjectName("msgToggleBtn");
    m_msgToggleBtn->setToolTip(tr("Collapse / Expand"));
    m_msgToggleBtn->setFixedSize(14, 14);
    connect(m_msgToggleBtn, &QPushButton::clicked, this, &CodexWidget::toggleMsgTextExpanded);

    m_msgAuthor = new QLabel(tr("🤖 Assistant"), m_messageCard);
    m_msgAuthor->setObjectName("msgAuthor");
    m_msgAuthor->setCursor(Qt::PointingHandCursor);
    m_msgAuthor->setToolTip(tr("Click to expand / collapse text"));
    m_msgAuthor->installEventFilter(this);

    m_msgDurationBadge = new QLabel(m_messageCard);
    m_msgDurationBadge->setObjectName("msgDurationBadge");
    m_msgDurationBadge->setCursor(Qt::PointingHandCursor);
    m_msgDurationBadge->installEventFilter(this);
    m_msgDurationBadge->setVisible(false);

    m_msgTime = new QLabel("", m_messageCard);
    m_msgTime->setObjectName("msgTime");

    m_copyMsgBtn = new QPushButton("📋", m_messageCard);
    m_copyMsgBtn->setToolTip(tr("Copy message"));
    m_copyMsgBtn->setFixedSize(16, 16);
    connect(m_copyMsgBtn, &QPushButton::clicked, this, &CodexWidget::copyMessageToClipboard);

    msgHeader->addWidget(m_msgToggleBtn);
    msgHeader->addWidget(m_msgAuthor);
    msgHeader->addStretch();
    msgHeader->addWidget(m_msgDurationBadge);
    msgHeader->addWidget(m_msgTime);
    msgHeader->addWidget(m_copyMsgBtn);
    msgLayout->addLayout(msgHeader);

    m_msgTextLabel = new QLabel(tr("No messages yet..."), m_messageCard);
    m_msgTextLabel->setObjectName("msgTextLabel");
    m_msgTextLabel->setWordWrap(true);
    m_msgTextLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_msgTextLabel->setVisible(m_isMsgTextExpanded); // Collapsed by default!
    msgLayout->addWidget(m_msgTextLabel);

    mainLayout->addWidget(m_messageCard);

    // ==========================================
    // 4. Card: Last Command (no redundant label)
    // ==========================================
    m_cmdCard = new QFrame(m_centralWidget);
    m_cmdCard->setObjectName("card");
    auto *cmdLayout = new QVBoxLayout(m_cmdCard);
    cmdLayout->setContentsMargins(8, 5, 8, 6);
    cmdLayout->setSpacing(2);

    auto *cmdHeader = new QHBoxLayout();
    cmdHeader->setContentsMargins(0, 0, 0, 0);
    cmdHeader->setSpacing(4);

    m_cmdStatusBadge = new QLabel(tr("Command"), m_cmdCard);
    m_cmdStatusBadge->setObjectName("cmdStatusBadge");
    m_cmdStatusBadge->setStyleSheet("background-color: rgba(255, 255, 255, 0.08); color: #a1a1aa;");

    m_cmdTime = new QLabel("", m_cmdCard);
    m_cmdTime->setObjectName("cmdTime");

    m_copyCmdBtn = new QPushButton("📋", m_cmdCard);
    m_copyCmdBtn->setToolTip(tr("Copy command"));
    m_copyCmdBtn->setFixedSize(18, 18);
    connect(m_copyCmdBtn, &QPushButton::clicked, this, &CodexWidget::copyCommandToClipboard);

    cmdHeader->addWidget(m_cmdStatusBadge);
    cmdHeader->addWidget(m_cmdTime);
    cmdHeader->addStretch();
    cmdHeader->addWidget(m_copyCmdBtn);
    cmdLayout->addLayout(cmdHeader);

    m_cmdTextLabel = new QLabel(tr("No commands yet"), m_cmdCard);
    m_cmdTextLabel->setObjectName("cmdTextLabel");
    m_cmdTextLabel->setWordWrap(true);
    m_cmdTextLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cmdLayout->addWidget(m_cmdTextLabel);

    mainLayout->addWidget(m_cmdCard);

    rootLayout->addWidget(m_centralWidget);

    if (m_isCompact) {
        m_messageCard->hide();
        m_cmdCard->hide();
    }
}

void CodexWidget::setupTray()
{
    m_trayIcon = new QSystemTrayIcon(this);
    updateTrayIcon(CodexState::Stopped);

    m_trayMenu = new QMenu(this);
    m_trayMenu->setStyleSheet(
        "QMenu { background-color: #1e1f24; color: #e4e4e7; border: 1px solid #3f3f46; border-radius: 8px; padding: 4px; }"
        "QMenu::item { padding: 6px 16px; border-radius: 4px; }"
        "QMenu::item:selected { background-color: #38bdf8; color: #000000; }"
    );

    m_showAction = m_trayMenu->addAction(tr("Show / Hide"));
    connect(m_showAction, &QAction::triggered, this, [this]() {
        if (isVisible()) hide();
        else { show(); raise(); activateWindow(); }
    });

    m_alwaysOnTopAction = m_trayMenu->addAction(tr("Always on Top"));
    m_alwaysOnTopAction->setCheckable(true);
    m_alwaysOnTopAction->setChecked(m_isAlwaysOnTop);
    connect(m_alwaysOnTopAction, &QAction::triggered, this, &CodexWidget::toggleAlwaysOnTop);

    m_refreshAction = m_trayMenu->addAction(tr("Refresh Now"));
    connect(m_refreshAction, &QAction::triggered, this, &CodexWidget::manualRefresh);

    m_resetPosAction = m_trayMenu->addAction(tr("Reset Position"));
    connect(m_resetPosAction, &QAction::triggered, this, [this]() {
        QScreen *primary = QGuiApplication::primaryScreen();
        if (primary) {
            QRect avail = primary->availableGeometry();
            move(avail.right() - 290, avail.top() + 60);
            show();
            raise();
            activateWindow();
            saveSettings();
        }
    });

    m_trayMenu->addSeparator();

    // Language Submenu
    m_langMenu = m_trayMenu->addMenu(tr("Language"));
    auto *langGroup = new QActionGroup(this);

    m_langAutoAction = m_langMenu->addAction(tr("Auto (System)"));
    m_langAutoAction->setCheckable(true);
    langGroup->addAction(m_langAutoAction);
    connect(m_langAutoAction, &QAction::triggered, this, [this]() { setLanguage("auto"); });

    m_langEnAction = m_langMenu->addAction("English");
    m_langEnAction->setCheckable(true);
    langGroup->addAction(m_langEnAction);
    connect(m_langEnAction, &QAction::triggered, this, [this]() { setLanguage("en"); });

    m_langRuAction = m_langMenu->addAction("Русский");
    m_langRuAction->setCheckable(true);
    langGroup->addAction(m_langRuAction);
    connect(m_langRuAction, &QAction::triggered, this, [this]() { setLanguage("ru"); });

    if (m_currentLanguage == "en") {
        m_langEnAction->setChecked(true);
    } else if (m_currentLanguage == "ru") {
        m_langRuAction->setChecked(true);
    } else {
        m_langAutoAction->setChecked(true);
    }

    m_trayMenu->addSeparator();

    m_aboutAction = m_trayMenu->addAction(tr("About..."));
    connect(m_aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("About"));
        msgBox.setIconPixmap(QIcon(":/app.png").pixmap(64, 64));
        msgBox.setTextFormat(Qt::RichText);
        QString versionText = tr("Version 1.0 • HUD Monitoring");
        QString devLabel = tr("Developer:");
        QString compLabel = tr("Company:");
        msgBox.setText(QString(
            "<h3 style='margin:0 0 6px 0; color:#38bdf8;'>Codex Monitor Widget (CMW)</h3>"
            "<p style='margin:0 0 8px 0; color:#94a3b8; font-size:11px;'>%1</p>"
            "<div style='border-top: 1px solid #334155; padding-top: 8px; font-size:12px;'>"
            "%2 <b style='color:#f8fafc;'>Ivan Klenov</b><br>"
            "%3 <b style='color:#38bdf8;'>Madness Studio</b>"
            "</div>"
        ).arg(versionText, devLabel, compLabel));
        msgBox.setStyleSheet(
            "QMessageBox { background-color: #171a22; color: #f1f5f9; border: 1px solid #3b4252; border-radius: 8px; }"
            "QLabel { color: #f1f5f9; font-family: 'Segoe UI', sans-serif; }"
            "QPushButton { background-color: #212530; border: 1px solid #3b4252; border-radius: 4px; color: #e2e8f0; font-weight: bold; padding: 5px 20px; min-width: 60px; }"
            "QPushButton:hover { background-color: #2d3444; border-color: #38bdf8; color: #ffffff; }"
        );
        msgBox.exec();
    });

    m_quitAction = m_trayMenu->addAction(tr("Exit"));
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &CodexWidget::onTrayActivated);
    m_trayIcon->show();
}

void CodexWidget::updateTrayIcon(CodexState state)
{
    QPixmap base(":/app.png");
    QPixmap pixmap = base.isNull() ? QPixmap(32, 32) : base.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (base.isNull()) {
        pixmap.fill(Qt::transparent);
    }

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    // Status dot at bottom-right corner
    QColor dotColor;
    switch (state) {
        case CodexState::Working: dotColor = QColor(16, 185, 129); break;
        case CodexState::Idle:    dotColor = QColor(245, 158, 11); break;
        case CodexState::Stopped:
        default:                  dotColor = QColor(239, 68, 68);  break;
    }

    // Outer dark ring for contrast
    p.setBrush(QColor(13, 15, 20));
    p.setPen(Qt::NoPen);
    p.drawEllipse(18, 18, 14, 14);

    // Vibrant status dot
    p.setBrush(dotColor);
    p.drawEllipse(20, 20, 10, 10);
    p.end();

    m_trayIcon->setIcon(QIcon(pixmap));
}

void CodexWidget::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible()) {
            hide();
        } else {
            show();
            raise();
            activateWindow();
        }
    }
}

void CodexWidget::onSnapshotUpdated(const CodexSnapshot &snap)
{
    m_currentSnapshot = snap;

    // 1. Status Indicator
    QString dotStyle;
    QString textStyle;
    QString statusString;

    switch (snap.state) {
        case CodexState::Working:
            dotStyle = "background-color: #10b981; border-radius: 4px;";
            textStyle = "color: #10b981; font-weight: bold; font-size: 11px;";
            statusString = tr("Working");
            break;
        case CodexState::Idle:
            dotStyle = "background-color: #f59e0b; border-radius: 4px;";
            textStyle = "color: #f59e0b; font-weight: bold; font-size: 11px;";
            statusString = tr("Idle");
            break;
        case CodexState::Stopped:
        default:
            dotStyle = "background-color: #ef4444; border-radius: 4px;";
            textStyle = "color: #ef4444; font-weight: bold; font-size: 11px;";
            statusString = tr("Not running");
            break;
    }

    m_statusDot->setStyleSheet(dotStyle);
    m_statusText->setStyleSheet(textStyle);
    m_statusText->setText(statusString);
    m_statusBadge->setToolTip(snap.stateDescription.isEmpty() ? statusString : snap.stateDescription);
    updateTrayIcon(snap.state);

    // 2. Limit Section
    qint64 nowSec = QDateTime::currentDateTime().toSecsSinceEpoch();
    double primaryRemaining = qMax(0.0, 100.0 - snap.primaryUsedPercent);
    double secondaryRemaining = qMax(0.0, 100.0 - snap.secondaryUsedPercent);

    if (snap.primaryResetsAt > 0 && nowSec >= snap.primaryResetsAt) {
        primaryRemaining = 100.0;
    }
    if (snap.secondaryResetsAt > 0 && nowSec >= snap.secondaryResetsAt) {
        secondaryRemaining = 100.0;
    }

    int displayPercent = m_showRemainingLimit ?
        qBound(0, static_cast<int>(qRound(primaryRemaining)), 100) :
        qBound(0, static_cast<int>(qRound(snap.primaryUsedPercent)), 100);

    QString countdownStr = formatTimeRemaining(snap.primaryResetsAt);

    if (snap.primaryResetsAt == 0) {
        m_limitPercent->setText("--%");
        m_limitBar->setValue(100);
        m_limitBar->setStyleSheet("QProgressBar#limitBar::chunk { background: #38bdf8; border-radius: 2px; }");
        m_limitPercent->setStyleSheet("color: #94a3b8; font-size: 14px; font-weight: 800;");
        m_resetCountdown->setText("--");
        m_secondaryLimitLabel->setText("");
    } else {
        m_limitPercent->setText(m_showRemainingLimit ?
            QString("%1%").arg(displayPercent) :
            tr("used %1%").arg(displayPercent));
        m_limitBar->setValue(displayPercent);

        if (m_showRemainingLimit) {
            QString chunkStyle;
            if (displayPercent <= 15) {
                chunkStyle = "QProgressBar#limitBar::chunk { background: #ef4444; border-radius: 2px; }";
                m_limitPercent->setStyleSheet("color: #ef4444; font-size: 14px; font-weight: 800;");
            } else if (displayPercent <= 30) {
                chunkStyle = "QProgressBar#limitBar::chunk { background: #f59e0b; border-radius: 2px; }";
                m_limitPercent->setStyleSheet("color: #f59e0b; font-size: 14px; font-weight: 800;");
            } else {
                chunkStyle = "QProgressBar#limitBar::chunk { background: #10b981; border-radius: 2px; }";
                m_limitPercent->setStyleSheet("color: #10b981; font-size: 14px; font-weight: 800;");
            }
            m_limitBar->setStyleSheet(chunkStyle);
        } else {
            QString chunkStyle;
            if (displayPercent >= 85) {
                chunkStyle = "QProgressBar#limitBar::chunk { background: #ef4444; border-radius: 2px; }";
                m_limitPercent->setStyleSheet("color: #ef4444; font-size: 14px; font-weight: 800;");
            } else if (displayPercent >= 70) {
                chunkStyle = "QProgressBar#limitBar::chunk { background: #f59e0b; border-radius: 2px; }";
                m_limitPercent->setStyleSheet("color: #f59e0b; font-size: 14px; font-weight: 800;");
            } else {
                chunkStyle = "QProgressBar#limitBar::chunk { background: #10b981; border-radius: 2px; }";
                m_limitPercent->setStyleSheet("color: #ffffff; font-size: 14px; font-weight: 800;");
            }
            m_limitBar->setStyleSheet(chunkStyle);
        }

        // Countdown and 7-day limit
        QString countdownStr = formatTimeRemaining(snap.primaryResetsAt);
        m_resetCountdown->setText(countdownStr);

        if (snap.secondaryResetsAt > 0) {
            int secRemaining = qBound(0, static_cast<int>(qRound(secondaryRemaining)), 100);
            m_secondaryLimitLabel->setText(tr("7d: %1%").arg(secRemaining));
        } else {
            m_secondaryLimitLabel->setText("");
        }
    }

    QString limitTooltip = tr(
        "5-hour limit:\n"
        "  • Remaining: %1%\n"
        "  • Used: %2%\n"
        "  • %3\n\n"
        "7-day limit:\n"
        "  • Remaining: %4%\n"
        "  • Used: %5%\n\n"
        "(Click card to toggle Remaining / Used)"
    )
    .arg(primaryRemaining, 0, 'f', 1)
    .arg(snap.primaryUsedPercent, 0, 'f', 1)
    .arg(countdownStr)
    .arg(secondaryRemaining, 0, 'f', 1)
    .arg(snap.secondaryUsedPercent, 0, 'f', 1);

    m_limitCard->setToolTip(limitTooltip);

    // 3. Message Section (1-2 lines with ellipsis)
    if (!snap.lastMessageText.isEmpty()) {
        QString authorIcon = (snap.lastMessageAuthor == "Codex" || snap.lastMessageAuthor.isEmpty()) ? tr("🤖 Assistant") : tr("👤 You");
        m_msgAuthor->setText(authorIcon);

        if (snap.lastMessageTime.isValid()) {
            m_msgTime->setText(snap.lastMessageTime.toLocalTime().toString("HH:mm:ss"));
        } else {
            m_msgTime->setText("");
        }

        // Clean & truncate to 1-2 lines (~130 chars) with ellipsis
        QString simplified = snap.lastMessageText.simplified();
        QString elided = simplified;
        if (elided.length() > 130) {
            elided = elided.left(127).trimmed() + "...";
        }
        m_msgTextLabel->setText(elided);
        m_msgTextLabel->setToolTip(snap.lastMessageText); // Full text on hover
    }

    // Replica Timing Lifecycle
    bool enteredWorking = (m_lastState != CodexState::Working && snap.state == CodexState::Working);
    bool enteredIdle = (m_lastState == CodexState::Working && snap.state != CodexState::Working);
    bool msgChanged = (!snap.lastMessageText.isEmpty() && snap.lastMessageText != m_lastReplicaText);

    if (msgChanged) {
        m_lastReplicaText = snap.lastMessageText;
        // The assistant line has changed: RESET REPLICA TIMER!
        m_replicaStartTime = snap.lastMessageTime.isValid() ? snap.lastMessageTime : QDateTime::currentDateTimeUtc();
        m_replicaDurationMs = 0;
        m_replicaCompleted = (snap.state != CodexState::Working);
    } else if (enteredWorking) {
        // Just started working on a new action: RESET REPLICA TIMER!
        m_replicaStartTime = QDateTime::currentDateTimeUtc();
        m_replicaDurationMs = 0;
        m_replicaCompleted = false;
    } else if (enteredIdle) {
        // Finished working: freeze replica duration
        m_replicaCompleted = true;
        if (m_replicaStartTime.isValid()) {
            qint64 dur = m_replicaStartTime.msecsTo(QDateTime::currentDateTimeUtc());
            if (dur > 0) {
                m_replicaDurationMs = dur;
            }
        }
    }

    bool isReplicaRunning = (snap.state == CodexState::Working && !m_replicaCompleted);
    if (!isReplicaRunning) {
        qint64 displayMs = m_replicaDurationMs;
        if (displayMs <= 0 && snap.turnDurationMs > 0) {
            displayMs = snap.turnDurationMs;
        }
        if (displayMs > 0) {
            double durSec = displayMs / 1000.0;
            QString doneStr = formatDuration(durSec, false);
            m_msgDurationBadge->setText(doneStr);
            m_msgDurationBadge->setToolTip(tr("Replica duration: %1").arg(doneStr));
            m_msgDurationBadge->setStyleSheet(
                "background-color: #064e3b; color: #34d399; border: 1px solid #10b981; "
                "border-radius: 3px; padding: 1px 5px; font-size: 10.5px; font-weight: 700;"
            );
            m_msgDurationBadge->setVisible(true);
        } else {
            m_msgDurationBadge->setVisible(false);
        }
    }

    // 4. Command Section
    const CommandInfo &cmd = snap.lastCommand;
    if (cmd.isValid && !cmd.command.isEmpty()) {
        if (!m_isCompact) {
            m_cmdCard->show();
        }

        if (cmd.status == "completed") {
            QString badgeText = "✓ " + (cmd.wallTime.isEmpty() ? "OK" : cmd.wallTime);
            m_cmdStatusBadge->setText(badgeText);
            m_cmdStatusBadge->setStyleSheet("background-color: #064e3b; color: #34d399; border: 1px solid #10b981; border-radius: 4px; padding: 2px 8px; font-size: 13px; font-weight: 800;");
        } else if (cmd.status == "failed") {
            QString badgeText = "✗ " + (cmd.wallTime.isEmpty() ? tr("Error") : cmd.wallTime);
            m_cmdStatusBadge->setText(badgeText);
            m_cmdStatusBadge->setStyleSheet("background-color: #450a0a; color: #f87171; border: 1px solid #ef4444; border-radius: 4px; padding: 2px 8px; font-size: 13px; font-weight: 800;");
        }

        if (cmd.timestamp.isValid()) {
            m_cmdTime->setText(cmd.timestamp.toLocalTime().toString("HH:mm"));
        } else {
            m_cmdTime->setText("");
        }

        QString cmdSimplified = cmd.command.simplified();
        QString cmdElided = cmdSimplified;
        if (cmdElided.length() > 80) {
            cmdElided = cmdElided.left(77).trimmed() + "...";
        }
        m_cmdTextLabel->setText(cmdElided);
        m_cmdTextLabel->setToolTip(cmd.command); // Full command on hover
    }

    // Manage live timer for command and/or replica
    bool isCmdRunning = (cmd.isValid && cmd.status == "running");
    if (isCmdRunning || isReplicaRunning) {
        if (!m_liveTimer.isActive()) {
            m_liveTimer.start(100);
        }
        updateLiveTimers();
    } else {
        m_liveTimer.stop();
    }

    m_lastState = snap.state;

    // System Tray Tooltip
    QString trayTip = tr("Codex Monitor Widget (CMW)\nStatus: %1\n5h limit: %2% (%3)")
        .arg(statusString)
        .arg(displayPercent)
        .arg(countdownStr);
    m_trayIcon->setToolTip(trayTip);
}

QString CodexWidget::formatTimeRemaining(qint64 resetsAt)
{
    if (resetsAt <= 0) {
        return "--";
    }
    qint64 nowSec = QDateTime::currentDateTime().toSecsSinceEpoch();
    qint64 diff = resetsAt - nowSec;

    if (diff <= 0) {
        return tr("now");
    }

    qint64 hours = diff / 3600;
    qint64 mins = (diff % 3600) / 60;

    if (hours > 0) {
        return tr("⏳ %1h %2m").arg(hours).arg(mins);
    } else {
        return tr("⏳ %1m").arg(mins);
    }
}

void CodexWidget::copyMessageToClipboard()
{
    QString text = m_currentSnapshot.lastMessageText;
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
        m_copyMsgBtn->setText("✓");
        QTimer::singleShot(1500, this, [this]() {
            m_copyMsgBtn->setText("📋");
        });
    }
}

void CodexWidget::copyCommandToClipboard()
{
    QString cmd = m_currentSnapshot.lastCommand.command;
    if (!cmd.isEmpty()) {
        QApplication::clipboard()->setText(cmd);
        m_copyCmdBtn->setText("✓");
        QTimer::singleShot(1500, this, [this]() {
            m_copyCmdBtn->setText("📋");
        });
    }
}

void CodexWidget::toggleAlwaysOnTop()
{
    m_isAlwaysOnTop = !m_isAlwaysOnTop;
    setWindowFlag(Qt::WindowStaysOnTopHint, m_isAlwaysOnTop);
    m_pinBtn->setProperty("pinned", m_isAlwaysOnTop);
    m_pinBtn->style()->unpolish(m_pinBtn);
    m_pinBtn->style()->polish(m_pinBtn);
    m_alwaysOnTopAction->setChecked(m_isAlwaysOnTop);
    show();
}

void CodexWidget::setCompactMode(bool compact)
{
    m_isCompact = compact;
    if (m_isCompact) {
        m_messageCard->hide();
        m_cmdCard->hide();
        if (m_compactBtn) m_compactBtn->setText("▼");
    } else {
        m_messageCard->show();
        if (m_currentSnapshot.lastCommand.isValid && !m_currentSnapshot.lastCommand.command.isEmpty()) {
            m_cmdCard->show();
        }
        if (m_compactBtn) m_compactBtn->setText("▲");
    }
    resize(1, 1);
    adjustSize();
    ensureVisibleOnScreen();
    saveSettings();
}

void CodexWidget::toggleCompactMode()
{
    setCompactMode(!m_isCompact);
}

void CodexWidget::setMsgTextExpanded(bool expanded)
{
    m_isMsgTextExpanded = expanded;
    if (m_msgTextLabel) {
        m_msgTextLabel->setVisible(m_isMsgTextExpanded);
    }
    if (m_msgToggleBtn) {
        m_msgToggleBtn->setText(m_isMsgTextExpanded ? "▼" : "▶");
    }
    resize(1, 1);
    adjustSize();
    ensureVisibleOnScreen();
    saveSettings();
}

void CodexWidget::toggleMsgTextExpanded()
{
    setMsgTextExpanded(!m_isMsgTextExpanded);
}

QString CodexWidget::formatDuration(double elapsedSec, bool isRunning)
{
    if (elapsedSec < 0.0) elapsedSec = 0.0;
    QString prefix = isRunning ? "⏳ " : "✓ ";
    if (elapsedSec < 60.0) {
        return tr("%1%2 s").arg(prefix).arg(elapsedSec, 0, 'f', 1);
    } else {
        int mins = static_cast<int>(elapsedSec) / 60;
        double secs = elapsedSec - (mins * 60);
        return QString("%1%2:%3").arg(prefix).arg(mins).arg(secs, 4, 'f', 1, QChar('0'));
    }
}

void CodexWidget::updateLiveTimers()
{
    const CommandInfo &cmd = m_currentSnapshot.lastCommand;
    bool isCmdRunning = (cmd.isValid && cmd.status == "running" && cmd.timestamp.isValid());
    bool isReplicaRunning = (m_currentSnapshot.state == CodexState::Working && !m_replicaCompleted);

    // 1. Command Running Timer (large badge in command card)
    if (isCmdRunning) {
        qint64 elapsedMs = cmd.timestamp.msecsTo(QDateTime::currentDateTimeUtc());
        double elapsedSec = qMax(0.0, elapsedMs / 1000.0);
        m_cmdStatusBadge->setText(formatDuration(elapsedSec, true));
        m_cmdStatusBadge->setStyleSheet(
            "background-color: #3b1d06; color: #fde047; border: 1px solid #f59e0b; "
            "border-radius: 4px; padding: 2px 8px; font-size: 13px; font-weight: 800;"
        );
    }

    // 2. Replica Running Timer (sleek header badge in message card)
    if (isReplicaRunning) {
        QDateTime start = m_replicaStartTime.isValid() ? 
                          m_replicaStartTime : 
                          m_currentSnapshot.lastMessageTime;
        if (!start.isValid()) {
            start = QDateTime::currentDateTimeUtc();
            m_replicaStartTime = start;
        }
        qint64 elapsedMs = start.msecsTo(QDateTime::currentDateTimeUtc());
        double elapsedSec = qMax(0.0, elapsedMs / 1000.0);
        QString timeText = formatDuration(elapsedSec, true);
        m_msgDurationBadge->setText(timeText);
        m_msgDurationBadge->setToolTip(tr("Current replica: %1").arg(timeText));
        m_msgDurationBadge->setStyleSheet(
            "background-color: #3b1d06; color: #fde047; border: 1px solid #f59e0b; "
            "border-radius: 3px; padding: 1px 5px; font-size: 10.5px; font-weight: 700;"
        );
        m_msgDurationBadge->setVisible(true);
    }

    // If neither is running, stop timer
    if (!isCmdRunning && !isReplicaRunning) {
        m_liveTimer.stop();
    }
}

void CodexWidget::manualRefresh()
{
    if (m_monitor) {
        CodexSnapshot snap = m_monitor->pollOnce();
        onSnapshotUpdated(snap);
    }
}

void CodexWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureVisibleOnScreen();
}

void CodexWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
#ifdef Q_OS_WIN
        // Delegate dragging to Windows native modal move loop
        // This completely prevents coordinate jumps and jitter across mixed-DPI monitors
        ReleaseCapture();
        SendMessage(reinterpret_cast<HWND>(winId()), WM_SYSCOMMAND, 0xF012 /* SC_MOVE | HTCAPTION */, 0);
        event->accept();
        ensureVisibleOnScreen();
        saveSettings();
#else
        m_isDragging = true;
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
#endif
    }
}

void CodexWidget::mouseMoveEvent(QMouseEvent *event)
{
#ifndef Q_OS_WIN
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
#else
    Q_UNUSED(event);
#endif
}

void CodexWidget::mouseReleaseEvent(QMouseEvent *event)
{
    m_isDragging = false;
    event->accept();
    ensureVisibleOnScreen();
    saveSettings();
}

void CodexWidget::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
    qApp->quit();
}

#ifdef Q_OS_WIN
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

bool CodexWidget::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_DPICHANGED) {
            RECT *r = reinterpret_cast<RECT *>(msg->lParam);
            SetWindowPos(msg->hwnd, NULL,
                         r->left, r->top,
                         r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);

            QTimer::singleShot(10, this, [this]() {
                adjustSize();
                ensureVisibleOnScreen();
            });

            if (result) *result = 0;
            return true;
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}
#else
bool CodexWidget::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    return QWidget::nativeEvent(eventType, message, result);
}
#endif

void CodexWidget::ensureVisibleOnScreen()
{
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return;

    QRect winRect = frameGeometry();
    if (winRect.width() <= 0 || winRect.height() <= 0) {
        winRect.setSize(QSize(270, 170));
    }

    bool isVisible = false;
    for (QScreen *screen : screens) {
        QRect avail = screen->availableGeometry();
        QRect inter = avail.intersected(winRect);
        if (inter.width() >= 60 && inter.height() >= 30) {
            isVisible = true;
            break;
        }
    }

    if (!isVisible) {
        QScreen *primary = QGuiApplication::primaryScreen();
        if (!primary) primary = screens.first();
        if (primary) {
            QRect avail = primary->availableGeometry();
            int newX = avail.right() - 290;
            int newY = avail.top() + 60;
            move(newX, newY);
            saveSettings();
        }
    }
}

void CodexWidget::loadSettings()
{
    QSettings settings("Madness Studio", "Codex Monitor Widget");
    if (!settings.contains("pos")) {
        // Seamlessly migrate settings from previous location
        QSettings oldSettings("CodexTools", "CodexWidget");
        if (oldSettings.contains("pos")) {
            settings.setValue("alwaysOnTop", oldSettings.value("alwaysOnTop"));
            settings.setValue("compactMode", oldSettings.value("compactMode"));
            settings.setValue("msgTextExpanded", oldSettings.value("msgTextExpanded"));
            settings.setValue("pos", oldSettings.value("pos"));
        }
    }

    m_isAlwaysOnTop = settings.value("alwaysOnTop", true).toBool();
    m_isCompact = settings.value("compactMode", false).toBool();
    m_isMsgTextExpanded = settings.value("msgTextExpanded", false).toBool(); // False (collapsed) by default!
    m_currentLanguage = settings.value("language", "auto").toString();

    if (m_msgTextLabel) {
        m_msgTextLabel->setVisible(m_isMsgTextExpanded);
    }
    if (m_msgToggleBtn) {
        m_msgToggleBtn->setText(m_isMsgTextExpanded ? "▼" : "▶");
    }
    QPoint pos = settings.value("pos", QPoint(100, 100)).toPoint();
    move(pos);
    ensureVisibleOnScreen();
}

void CodexWidget::saveSettings()
{
    QSettings settings("Madness Studio", "Codex Monitor Widget");
    settings.setValue("alwaysOnTop", m_isAlwaysOnTop);
    settings.setValue("compactMode", m_isCompact);
    settings.setValue("msgTextExpanded", m_isMsgTextExpanded);
    settings.setValue("language", m_currentLanguage);
    settings.setValue("pos", pos());
}

void CodexWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

void CodexWidget::setLanguage(const QString &langCode)
{
    m_currentLanguage = langCode;

    qApp->removeTranslator(&m_translator);

    QString effective = langCode;
    if (effective == "auto") {
        QLocale sys = QLocale::system();
        effective = (sys.language() == QLocale::Russian) ? "ru" : "en";
    }

    if (effective == "ru") {
        if (m_translator.load(":/translations/cmw_ru.qm")) {
            qApp->installTranslator(&m_translator);
        }
    } else if (effective == "en") {
        if (m_translator.load(":/translations/cmw_en.qm")) {
            qApp->installTranslator(&m_translator);
        }
    }

    if (m_langAutoAction) m_langAutoAction->setChecked(langCode == "auto");
    if (m_langEnAction) m_langEnAction->setChecked(langCode == "en");
    if (m_langRuAction) m_langRuAction->setChecked(langCode == "ru");

    retranslateUi();
    saveSettings();
}

void CodexWidget::retranslateUi()
{
    setWindowTitle(tr("Codex Monitor Widget (CMW)"));
    if (m_pinBtn) m_pinBtn->setToolTip(tr("Always on Top"));
    if (m_compactBtn) m_compactBtn->setToolTip(tr("Collapse / Expand"));
    if (m_closeBtn) m_closeBtn->setToolTip(tr("Close application"));

    if (m_msgToggleBtn) {
        m_msgToggleBtn->setToolTip(tr("Collapse / Expand"));
    }
    if (m_msgAuthor) {
        m_msgAuthor->setToolTip(tr("Click to expand / collapse text"));
    }
    if (m_copyMsgBtn) m_copyMsgBtn->setToolTip(tr("Copy message"));
    if (m_copyCmdBtn) m_copyCmdBtn->setToolTip(tr("Copy command"));

    if (m_showAction) m_showAction->setText(tr("Show / Hide"));
    if (m_alwaysOnTopAction) m_alwaysOnTopAction->setText(tr("Always on Top"));
    if (m_refreshAction) m_refreshAction->setText(tr("Refresh Now"));
    if (m_resetPosAction) m_resetPosAction->setText(tr("Reset Position"));
    if (m_langMenu) m_langMenu->setTitle(tr("Language"));
    if (m_langAutoAction) m_langAutoAction->setText(tr("Auto (System)"));
    if (m_aboutAction) m_aboutAction->setText(tr("About..."));
    if (m_quitAction) m_quitAction->setText(tr("Exit"));

    // Re-render data and badges in active language
    onSnapshotUpdated(m_currentSnapshot);

    if (m_currentSnapshot.lastMessageText.isEmpty()) {
        m_msgTextLabel->setText(tr("No messages yet..."));
        m_msgAuthor->setText(tr("🤖 Assistant"));
    }

    if (!m_currentSnapshot.lastCommand.isValid || m_currentSnapshot.lastCommand.command.isEmpty()) {
        m_cmdStatusBadge->setText(tr("Command"));
        m_cmdTextLabel->setText(tr("No commands yet"));
    }
}

bool CodexWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_limitCard && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            m_showRemainingLimit = !m_showRemainingLimit;
            onSnapshotUpdated(m_currentSnapshot);
            return true;
        }
    } else if ((watched == m_msgAuthor || watched == m_msgDurationBadge) && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            toggleMsgTextExpanded();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
