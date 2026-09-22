#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QTimer>

enum class CodexState {
    Stopped = 0, // No processes running
    Idle = 1,    // Running and idle (ready for input)
    Working = 2  // Running and actively generating/executing
};

struct CommandInfo {
    QString command;       // Command string or summary
    QString status;        // "completed", "failed", "running"
    QString statusText;    // "Выполнена", "Ошибка", "Выполняется"
    QString wallTime;      // "0.9 с", "7.5 с"
    QDateTime timestamp;
    bool isValid = false;
};

struct CodexSnapshot {
    CodexState state = CodexState::Stopped;
    QString stateDescription;
    
    // 5-hour limit
    double primaryUsedPercent = 0.0;
    qint64 primaryResetsAt = 0; // Unix timestamp (seconds)
    int primaryWindowMinutes = 300;
    qint64 primaryLimitTimestamp = 0; // Unix timestamp of when limit was recorded
    
    // Secondary limit (weekly)
    double secondaryUsedPercent = 0.0;
    qint64 secondaryResetsAt = 0;
    
    // Last message
    QString lastMessageAuthor; // "Codex" or "Вы"
    QString lastMessageText;
    QDateTime lastMessageTime;

    // Replica / Turn timing
    bool isTurnActive = false;
    QDateTime turnStartTime;
    qint64 turnDurationMs = 0; // Milliseconds if completed
    
    // Last executed command
    CommandInfo lastCommand;

    // Process & File metadata
    bool isProcessRunning = false;
    bool isRunnerRunning = false;
    QString activeSessionPath;
    QDateTime sessionLastModified;
    qint64 sessionFileSize = 0;
};

class CodexMonitor : public QObject {
    Q_OBJECT

public:
    explicit CodexMonitor(QObject *parent = nullptr);
    ~CodexMonitor() override = default;

    void start(int intervalMs = 250);
    void stop();
    CodexSnapshot pollOnce();

signals:
    void snapshotUpdated(const CodexSnapshot &snapshot);

private slots:
    void onTimerTick();

private:
    void checkProcesses(bool &outCodexRunning, bool &outRunnerRunning);
    QString findLatestSessionFile();
    bool parseSessionTail(const QString &filePath, CodexSnapshot &snapshot);
    QString extractCommandText(const QString &input);
    QString extractThreadIdFromPath(const QString &path);
    bool readRateLimitsFromLogsDb(CodexSnapshot &snapshot, const QString &threadId = QString());
    bool findRecentPrimaryRateLimit(CodexSnapshot &snapshot);

    QTimer m_timer;
    CodexSnapshot m_lastSnapshot;
    QString m_cachedSessionFile;
    qint64 m_lastKnownSize = 0;
    qint64 m_lastDbCheckTime = 0;
    int m_pollCounter = 0;
    qint64 m_lastAuthMtime = 0;
    bool m_lastCodexRunning = false;
    bool m_lastRunnerRunning = false;
};
