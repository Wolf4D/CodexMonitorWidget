#include "codex_monitor.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <algorithm>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

CodexMonitor::CodexMonitor(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &CodexMonitor::onTimerTick);
}

void CodexMonitor::start(int intervalMs)
{
    m_timer.start(intervalMs);
    // Initial immediate process check and scan
    checkProcesses(m_lastCodexRunning, m_lastRunnerRunning);
    m_cachedSessionFile = findLatestSessionFile();
    onTimerTick();
}

void CodexMonitor::stop()
{
    m_timer.stop();
}

void CodexMonitor::onTimerTick()
{
    CodexSnapshot snap = pollOnce();
    emit snapshotUpdated(snap);
}

void CodexMonitor::checkProcesses(bool &outCodexRunning, bool &outRunnerRunning)
{
    outCodexRunning = false;
    outRunnerRunning = false;

#ifdef Q_OS_WIN
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        return;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            QString name = QString::fromWCharArray(pe.szExeFile).toLower();
            if (name == "codex.exe" || name == "chatgpt.exe") {
                outCodexRunning = true;
            } else if (name.contains("codex-command-runner")) {
                outRunnerRunning = true;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
#endif
}

QString CodexMonitor::findLatestSessionFile()
{
    QString sessionsRoot = QDir::homePath() + "/.codex/sessions";
    QDir rootDir(sessionsRoot);
    if (!rootDir.exists()) {
        return QString();
    }

    QString latestPath;
    QDateTime latestTime;

    QDirIterator it(sessionsRoot, QStringList() << "rollout-*.jsonl", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        QDateTime mtime = fi.lastModified();
        if (latestPath.isEmpty() || mtime > latestTime) {
            latestTime = mtime;
            latestPath = fi.absoluteFilePath();
        }
    }

    return latestPath;
}

QString CodexMonitor::extractCommandText(const QString &input)
{
    // 1. Check for cmd: "..."
    static QRegularExpression reCmdDouble(R"re(cmd:\s*"((?:[^"\\]|\\.)*)")re");
    QRegularExpressionMatch m = reCmdDouble.match(input);
    if (m.hasMatch()) {
        QString cmd = m.captured(1);
        cmd.replace("\\\\", "\\").replace("\\\"", "\"").replace("\\n", " ");
        return cmd.simplified();
    }

    // 2. Check for cmd: '...'
    static QRegularExpression reCmdSingle(R"re(cmd:\s*'((?:[^'\\]|\\.)*)')re");
    m = reCmdSingle.match(input);
    if (m.hasMatch()) {
        QString cmd = m.captured(1);
        cmd.replace("\\\\", "\\").replace("\\'", "'").replace("\\n", " ");
        return cmd.simplified();
    }

    // 3. Check for apply_patch or Begin Patch
    if (input.contains("apply_patch") || input.contains("Begin Patch")) {
        static QRegularExpression reFile(R"(Update File:\s*([^\r\n]+))");
        QRegularExpressionMatch mf = reFile.match(input);
        if (mf.hasMatch()) {
            return "apply_patch " + QFileInfo(mf.captured(1).trimmed()).fileName();
        }
        return "apply_patch";
    }

    // 4. Check for MCP tool call
    static QRegularExpression reMcp(R"(tools\.(mcp__\w+))");
    QRegularExpressionMatch mm = reMcp.match(input);
    if (mm.hasMatch()) {
        QString toolName = mm.captured(1);
        toolName.replace("mcp__", "");

        static QRegularExpression reClass(R"re(className:\s*"([^"]+)")re");
        QRegularExpressionMatch mc = reClass.match(input);
        if (mc.hasMatch()) {
            toolName += " " + mc.captured(1);
        } else {
            static QRegularExpression reScenario(R"re(firstScenario:\s*(\d+))re");
            QRegularExpressionMatch ms = reScenario.match(input);
            if (ms.hasMatch()) {
                toolName += " #" + ms.captured(1);
            }
        }
        return toolName;
    }

    // 5. Fallback: first line
    QString firstLine = input.trimmed().section('\n', 0, 0).simplified();
    return firstLine.left(140);
}

QString CodexMonitor::extractThreadIdFromPath(const QString &path)
{
    static QRegularExpression reUuid(R"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");
    QRegularExpressionMatch m = reUuid.match(path);
    if (m.hasMatch()) {
        return m.captured(0);
    }
    return QString();
}

bool CodexMonitor::parseSessionTail(const QString &filePath, CodexSnapshot &snapshot)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    qint64 fileSize = file.size();
    snapshot.sessionFileSize = fileSize;

    // Read last 8 MB (fast < 3ms read, covers multiple full turns with large tool outputs)
    qint64 readChunk = qMin<qint64>(fileSize, 8388608LL);
    if (readChunk <= 0) {
        file.close();
        return false;
    }

    file.seek(fileSize - readChunk);
    QByteArray data = file.readAll();
    file.close();

    QList<QByteArray> lines = data.split('\n');
    bool foundLimits = false;
    bool foundMessage = false;
    bool foundCommand = false;
    bool hasUnfinishedToolCall = false;
    bool foundTurnLifecycle = false;
    bool foundTurnStart = false;
    bool foundTurnComplete = false;

    // Direct event-based lifecycle tracking
    enum class LifecycleState {
        Unknown,
        Completed, // The most recent lifecycle event is task_complete -> Idle
        Active     // The most recent lifecycle event is an active turn/tool/message -> Working
    };
    LifecycleState lifecycle = LifecycleState::Unknown;
    QDateTime latestEventTime;

    QMap<QString, QString> toolOutputs; // call_id -> outputText
    QMap<QString, QDateTime> toolOutputTimes; // call_id -> output eventTime

    // Scan backwards from end of file
    for (int i = lines.size() - 1; i >= 0; --i) {
        QByteArray line = lines[i].trimmed();
        if (line.isEmpty() || !line.startsWith('{') || !line.endsWith('}')) {
            continue;
        }

        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(line, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        QJsonObject root = doc.object();
        QString type = root.value("type").toString();
        QJsonObject payload = root.value("payload").toObject();
        QString payloadType = payload.value("type").toString();

        QDateTime eventTime = QDateTime::fromString(root.value("timestamp").toString(), Qt::ISODateWithMs);
        if (!latestEventTime.isValid() && eventTime.isValid()) {
            latestEventTime = eventTime;
        }

        // Turn Lifecycle and Duration Tracking
        if (type == "event_msg") {
            if (payloadType == "task_complete") {
                if (!foundTurnLifecycle) {
                    foundTurnLifecycle = true;
                    foundTurnComplete = true;
                    snapshot.isTurnActive = false;
                    snapshot.turnDurationMs = payload.value("duration_ms").toVariant().toLongLong();
                    if (payload.contains("started_at")) {
                        qint64 startSec = payload.value("started_at").toVariant().toLongLong();
                        if (startSec > 0) {
                            snapshot.turnStartTime = QDateTime::fromSecsSinceEpoch(startSec, Qt::UTC);
                            foundTurnStart = true;
                        }
                    }
                }
            } else if (payloadType == "task_started") {
                if (!foundTurnLifecycle) {
                    foundTurnLifecycle = true;
                    foundTurnStart = true;
                    snapshot.isTurnActive = true;
                    snapshot.turnDurationMs = 0;
                    if (eventTime.isValid()) {
                        snapshot.turnStartTime = eventTime;
                    } else if (payload.contains("started_at")) {
                        qint64 startSec = payload.value("started_at").toVariant().toLongLong();
                        snapshot.turnStartTime = QDateTime::fromSecsSinceEpoch(startSec, Qt::UTC);
                    }
                } else if (foundTurnComplete && !foundTurnStart) {
                    foundTurnStart = true;
                    if (eventTime.isValid()) {
                        snapshot.turnStartTime = eventTime;
                    } else if (payload.contains("started_at")) {
                        qint64 startSec = payload.value("started_at").toVariant().toLongLong();
                        snapshot.turnStartTime = QDateTime::fromSecsSinceEpoch(startSec, Qt::UTC);
                    }
                }
            }
        }

        // 1. Lifecycle State Detection (the very first lifecycle event encountered from EOF)
        if (lifecycle == LifecycleState::Unknown) {
            // Skip purely passive records
            if (type != "token_usage_record" && type != "session_meta" &&
                payloadType != "thread_settings_applied" && payloadType != "token_count") {
                if (payloadType == "task_complete") {
                    lifecycle = LifecycleState::Completed;
                } else if (payloadType == "task_started" ||
                           payloadType == "agent_reasoning" ||
                           payloadType == "agent_message" ||
                           payloadType == "user_message" ||
                           type == "response_item") {
                    lifecycle = LifecycleState::Active;
                }
            }
        }

        // 2. Rate limits
        if (!foundLimits) {
            if (type == "event_msg" && payloadType == "token_count" && payload.contains("rate_limits")) {
                QJsonObject rateLimits = payload.value("rate_limits").toObject();
                if (rateLimits.value("primary").isObject()) {
                    QJsonObject primary = rateLimits.value("primary").toObject();
                    if (primary.contains("used_percent") && primary.contains("resets_at")) {
                        int windowMin = primary.value("window_minutes").toInt(300);
                        if (windowMin <= 300) {
                            // Valid 5-hour rolling quota
                            snapshot.primaryUsedPercent = primary.value("used_percent").toDouble(0.0);
                            snapshot.primaryResetsAt = primary.value("resets_at").toVariant().toLongLong();
                            snapshot.primaryWindowMinutes = windowMin;
                            snapshot.primaryLimitTimestamp = eventTime.isValid() ? eventTime.toSecsSinceEpoch() : QDateTime::currentDateTime().toSecsSinceEpoch();

                            if (rateLimits.value("secondary").isObject()) {
                                QJsonObject secondary = rateLimits.value("secondary").toObject();
                                snapshot.secondaryUsedPercent = secondary.value("used_percent").toDouble(0.0);
                                snapshot.secondaryResetsAt = secondary.value("resets_at").toVariant().toLongLong();
                            }
                            foundLimits = true;
                        } else {
                            // Reserve / 7-day model quota (e.g. gpt-reserve with window_minutes: 10080)
                            // Never overwrite primary 5h limit with weekly numbers!
                            if (snapshot.secondaryResetsAt == 0) {
                                snapshot.secondaryUsedPercent = primary.value("used_percent").toDouble(0.0);
                                snapshot.secondaryResetsAt = primary.value("resets_at").toVariant().toLongLong();
                            }
                        }
                    }
                }
            }
        }

        // 3. Last user/agent message
        if (!foundMessage) {
            if (type == "event_msg") {
                if (payloadType == "agent_message") {
                    QString text = payload.value("message").toString().trimmed();
                    if (!text.isEmpty() && !text.startsWith("{\"risk_level\"")) {
                        snapshot.lastMessageAuthor = "Codex";
                        snapshot.lastMessageText = text;
                        snapshot.lastMessageTime = eventTime;
                        foundMessage = true;
                    }
                } else if (payloadType == "user_message") {
                    QString text = payload.value("message").toString().trimmed();
                    if (!text.isEmpty()) {
                        snapshot.lastMessageAuthor = "Вы";
                        snapshot.lastMessageText = text;
                        snapshot.lastMessageTime = eventTime;
                        foundMessage = true;
                    }
                }
            } else if (type == "response_item" && payloadType == "message") {
                QString role = payload.value("role").toString();
                QJsonArray content = payload.value("content").toArray();
                QString fullText;
                for (const auto &it : content) {
                    QJsonObject itemObj = it.toObject();
                    if (itemObj.value("type").toString() == "output_text") {
                        fullText += itemObj.value("text").toString();
                    }
                }
                fullText = fullText.trimmed();
                if (!fullText.isEmpty() && !fullText.startsWith("{\"risk_level\"")) {
                    snapshot.lastMessageAuthor = (role == "assistant") ? "Codex" : "Вы";
                    snapshot.lastMessageText = fullText;
                    snapshot.lastMessageTime = eventTime;
                    foundMessage = true;
                }
            }
        }

        // 4. Command execution: collect outputs and match with calls
        if (type == "response_item") {
            if (payloadType == "custom_tool_call_output") {
                QString callId = payload.value("call_id").toString();
                QJsonArray outArr = payload.value("output").toArray();
                QString outText;
                for (const auto &it : outArr) {
                    outText += it.toObject().value("text").toString();
                }
                toolOutputs.insert(callId, outText);
                toolOutputTimes.insert(callId, eventTime);
            } else if (payloadType == "custom_tool_call" && !foundCommand) {
                QString callId = payload.value("call_id").toString();
                QString input = payload.value("input").toString();
                QString cmdText = extractCommandText(input);

                if (!cmdText.isEmpty()) {
                    CommandInfo cmdInfo;
                    cmdInfo.isValid = true;
                    cmdInfo.command = cmdText;
                    cmdInfo.timestamp = eventTime;

                    if (toolOutputs.contains(callId)) {
                        QString out = toolOutputs.value(callId);
                        if (out.contains("Script failed") || out.contains("error", Qt::CaseInsensitive)) {
                            cmdInfo.status = "failed";
                            cmdInfo.statusText = tr("Error");
                        } else {
                            cmdInfo.status = "completed";
                            cmdInfo.statusText = tr("Completed");
                        }

                        static QRegularExpression reTime(R"(Wall time\s+([0-9\.]+)\s+seconds)");
                        QRegularExpressionMatch mw = reTime.match(out);
                        if (mw.hasMatch()) {
                            cmdInfo.wallTime = mw.captured(1) + " " + tr("s");
                        } else if (toolOutputTimes.contains(callId) && cmdInfo.timestamp.isValid()) {
                            qint64 ms = cmdInfo.timestamp.msecsTo(toolOutputTimes.value(callId));
                            if (ms > 0) {
                                cmdInfo.wallTime = QString("%1 %2").arg(ms / 1000.0, 0, 'f', 1).arg(tr("s"));
                            }
                        }
                    } else {
                        cmdInfo.status = "running";
                        cmdInfo.statusText = tr("Running");
                        hasUnfinishedToolCall = true;
                    }

                    snapshot.lastCommand = cmdInfo;
                    foundCommand = true;
                }
            }
        }

        // If we resolved lifecycle and all main fields, we can exit early
        if (lifecycle != LifecycleState::Unknown && foundLimits && foundMessage && foundCommand && foundTurnLifecycle) {
            break;
        }
    }

    // Direct event-based state determination: zero artificial delay
    qint64 nowSec = QDateTime::currentDateTime().toSecsSinceEpoch();
    qint64 lastEventSec = latestEventTime.isValid() ? latestEventTime.toSecsSinceEpoch() : 0;
    qint64 diffSec = (lastEventSec > 0) ? (nowSec - lastEventSec) : 9999;

    if (!snapshot.isProcessRunning) {
        snapshot.state = CodexState::Stopped;
        snapshot.stateDescription = tr("Not running");
        snapshot.isTurnActive = false;
    } else if (snapshot.isRunnerRunning || hasUnfinishedToolCall) {
        snapshot.state = CodexState::Working;
        snapshot.stateDescription = tr("Executing command");
        snapshot.isTurnActive = true;
    } else if (lifecycle == LifecycleState::Active) {
        if (diffSec > 600) { // Safety timeout: 10 minutes without any disk activity
            snapshot.state = CodexState::Idle;
            snapshot.stateDescription = tr("Waiting for prompt");
            snapshot.isTurnActive = false;
        } else {
            snapshot.state = CodexState::Working;
            snapshot.stateDescription = tr("Generating response");
            snapshot.isTurnActive = true;
        }
    } else if (lifecycle == LifecycleState::Completed) {
        snapshot.state = CodexState::Idle;
        snapshot.stateDescription = tr("Waiting for prompt");
        snapshot.isTurnActive = false;
    } else {
        snapshot.state = CodexState::Idle;
        snapshot.stateDescription = tr("Ready");
        snapshot.isTurnActive = false;
    }

    if (snapshot.state != CodexState::Working && snapshot.lastCommand.status == "running") {
        snapshot.lastCommand.status = "completed";
        snapshot.lastCommand.statusText = tr("Completed");
    }

    // Ensure turnStartTime tracks the latest replica message
    if (snapshot.lastMessageTime.isValid()) {
        snapshot.turnStartTime = snapshot.lastMessageTime;
    } else if (!snapshot.turnStartTime.isValid()) {
        if (latestEventTime.isValid()) {
            snapshot.turnStartTime = latestEventTime;
        } else {
            snapshot.turnStartTime = QDateTime::currentDateTimeUtc();
        }
    }

    // If turn completed and duration_ms was 0, compute fallback from turnStartTime to latestEventTime
    if (!snapshot.isTurnActive && snapshot.turnDurationMs <= 0 && snapshot.turnStartTime.isValid() && latestEventTime.isValid()) {
        qint64 diffMs = snapshot.turnStartTime.msecsTo(latestEventTime);
        if (diffMs > 0) {
            snapshot.turnDurationMs = diffMs;
        }
    }

    return foundLimits;
}

bool CodexMonitor::readRateLimitsFromLogsDb(CodexSnapshot &snapshot, const QString &threadId)
{
    QString dbPath = QDir::homePath() + "/.codex/logs_2.sqlite";
    if (!QFile::exists(dbPath)) {
        return false;
    }

    const QString connName = "cmw_logs_reader";
    bool success = false;
    {
        QSqlDatabase db;
        if (QSqlDatabase::contains(connName)) {
            db = QSqlDatabase::database(connName);
        } else {
            db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            db.setDatabaseName(dbPath);
        }

        if (db.isOpen() || db.open()) {
            QSqlQuery q(db);
            q.setForwardOnly(true);

            bool hasRow = false;
            // 1. Try finding logs specifically for the active thread
            if (!threadId.isEmpty()) {
                q.prepare("SELECT ts, feedback_log_body FROM logs WHERE feedback_log_body LIKE '%x-codex-primary-used-percent%' AND feedback_log_body LIKE :tId ORDER BY id DESC LIMIT 1");
                q.bindValue(":tId", QString("%%%1%%").arg(threadId));
                if (q.exec() && q.next()) {
                    hasRow = true;
                }
            }

            // 2. Fallback to latest global response header
            if (!hasRow) {
                if (q.exec("SELECT ts, feedback_log_body FROM logs WHERE feedback_log_body LIKE '%x-codex-primary-used-percent%' ORDER BY id DESC LIMIT 1") && q.next()) {
                    hasRow = true;
                }
            }

            if (hasRow) {
                qint64 rowTs = q.value(0).toLongLong();
                QString body = q.value(1).toString();

                // If DB row is older than what our snapshot already recorded, ignore it
                if (snapshot.primaryLimitTimestamp > 0 && rowTs < snapshot.primaryLimitTimestamp) {
                    return false;
                }

                static QRegularExpression rePrimaryUsed(R"re("x-codex-primary-used-percent":\s*"([^"]+)")re");
                static QRegularExpression rePrimaryReset(R"re("x-codex-primary-reset-at":\s*"([^"]+)")re");
                static QRegularExpression rePrimaryWindow(R"re("x-codex-primary-window-minutes":\s*"([^"]+)")re");
                static QRegularExpression reSecondaryUsed(R"re("x-codex-secondary-used-percent":\s*"([^"]+)")re");
                static QRegularExpression reSecondaryReset(R"re("x-codex-secondary-reset-at":\s*"([^"]+)")re");

                auto mPU = rePrimaryUsed.match(body);
                auto mPR = rePrimaryReset.match(body);
                auto mPW = rePrimaryWindow.match(body);
                auto mSU = reSecondaryUsed.match(body);
                auto mSR = reSecondaryReset.match(body);

                if (mPU.hasMatch() && mPR.hasMatch()) {
                    qint64 pReset = mPR.captured(1).toLongLong();
                    qint64 nowSec = QDateTime::currentDateTime().toSecsSinceEpoch();

                    // If quota window has already elapsed, full quota is restored
                    if (pReset > 0 && nowSec >= pReset) {
                        snapshot.primaryUsedPercent = 0.0;
                        snapshot.primaryResetsAt = 0;
                        snapshot.primaryWindowMinutes = 300;
                        snapshot.primaryLimitTimestamp = rowTs;
                        return true;
                    }

                    snapshot.primaryUsedPercent = mPU.captured(1).toDouble();
                    snapshot.primaryResetsAt = pReset;
                    snapshot.primaryWindowMinutes = mPW.hasMatch() ? mPW.captured(1).toInt() : 300;
                    snapshot.primaryLimitTimestamp = rowTs;

                    if (mSU.hasMatch() && mSR.hasMatch()) {
                        snapshot.secondaryUsedPercent = mSU.captured(1).toDouble();
                        snapshot.secondaryResetsAt = mSR.captured(1).toLongLong();
                    }
                    success = true;
                }
            }
        }
    }
    return success;
}

bool CodexMonitor::findRecentPrimaryRateLimit(CodexSnapshot &snapshot)
{
    QString sessionsRoot = QDir::homePath() + "/.codex/sessions";
    QDir rootDir(sessionsRoot);
    if (!rootDir.exists()) {
        return false;
    }

    QFileInfoList sessionFiles;
    QDirIterator it(sessionsRoot, QStringList() << "rollout-*.jsonl", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        sessionFiles.append(it.fileInfo());
    }

    std::sort(sessionFiles.begin(), sessionFiles.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });

    int checked = 0;
    for (const auto &fi : sessionFiles) {
        if (++checked > 10) break;
        if (fi.absoluteFilePath() == m_cachedSessionFile) {
            continue;
        }

        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        qint64 sz = file.size();
        qint64 chunk = qMin<qint64>(sz, 2097152LL);
        file.seek(sz - chunk);
        QByteArray data = file.readAll();
        file.close();

        QList<QByteArray> lines = data.split('\n');
        for (int i = lines.size() - 1; i >= 0; --i) {
            const QByteArray &line = lines[i];
            if (!line.contains("\"rate_limits\"")) continue;

            QJsonDocument doc = QJsonDocument::fromJson(line.trimmed());
            if (!doc.isObject()) continue;
            QJsonObject root = doc.object();
            QJsonObject payload = root.value("payload").toObject();
            if (payload.contains("rate_limits")) {
                QJsonObject rl = payload.value("rate_limits").toObject();
                QJsonObject primary = rl.value("primary").toObject();
                int win = primary.value("window_minutes").toInt(300);
                if (win <= 300 && primary.contains("used_percent") && primary.contains("resets_at")) {
                    snapshot.primaryUsedPercent = primary.value("used_percent").toDouble(0.0);
                    snapshot.primaryResetsAt = primary.value("resets_at").toVariant().toLongLong();
                    snapshot.primaryWindowMinutes = win;

                    if (rl.value("secondary").isObject()) {
                        QJsonObject secondary = rl.value("secondary").toObject();
                        snapshot.secondaryUsedPercent = secondary.value("used_percent").toDouble(0.0);
                        snapshot.secondaryResetsAt = secondary.value("resets_at").toVariant().toLongLong();
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

CodexSnapshot CodexMonitor::pollOnce()
{
    m_pollCounter++;
    // Start with last snapshot to preserve values when parsing partial tails
    CodexSnapshot snapshot = m_lastSnapshot;

    // 1. Process check every 2 ticks (~400ms) to ensure instant detection of process start/stop
    if (m_pollCounter % 2 == 0 || m_pollCounter == 1) {
        checkProcesses(m_lastCodexRunning, m_lastRunnerRunning);
    }
    snapshot.isProcessRunning = m_lastCodexRunning;
    snapshot.isRunnerRunning = m_lastRunnerRunning;

    if (!snapshot.isProcessRunning) {
        snapshot.state = CodexState::Stopped;
        snapshot.stateDescription = tr("Not running");
        m_lastSnapshot = snapshot;
        return snapshot;
    }

    // 2. Locate active session file (cached, refreshed every ~2.5 seconds or if invalid)
    if (m_cachedSessionFile.isEmpty() || !QFile::exists(m_cachedSessionFile) || (m_pollCounter % 12 == 0)) {
        m_cachedSessionFile = findLatestSessionFile();
    }
    snapshot.activeSessionPath = m_cachedSessionFile;

    bool foundSessionLimits = false;
    if (!m_cachedSessionFile.isEmpty()) {
        QFile file(m_cachedSessionFile);
        if (file.open(QIODevice::ReadOnly)) {
            qint64 currentSize = file.size();
            snapshot.sessionFileSize = currentSize;
            file.close();

            snapshot.sessionLastModified = QFileInfo(m_cachedSessionFile).lastModified();

            // Parse immediately on byte changes OR periodically every 3 ticks (600ms)
            if (currentSize != m_lastKnownSize || (m_pollCounter % 3 == 0)) {
                foundSessionLimits = parseSessionTail(m_cachedSessionFile, snapshot);
                m_lastKnownSize = currentSize;
            } else {
                // If not re-parsed this tick, preserve known 5h limit status
                foundSessionLimits = (snapshot.primaryWindowMinutes <= 300 && snapshot.primaryResetsAt > 0);
            }
        }
    } else {
        snapshot.state = CodexState::Idle;
        snapshot.stateDescription = tr("Ready (no sessions)");
    }

    // 3. Fallback: Query account-wide rate limits from logs_2.sqlite or previous sessions
    // ONLY if active session does NOT provide a 5-hour limit (e.g. gpt-reserve sessions or initial launch)
    if (!foundSessionLimits && (snapshot.primaryResetsAt == 0 || snapshot.primaryWindowMinutes > 300)) {
        if (m_pollCounter % 10 == 0 || snapshot.primaryResetsAt == 0) {
            QString threadId = extractThreadIdFromPath(m_cachedSessionFile);
            if (!readRateLimitsFromLogsDb(snapshot, threadId)) {
                if (snapshot.primaryResetsAt == 0) {
                    findRecentPrimaryRateLimit(snapshot);
                }
            }
        }
    }

    m_lastSnapshot = snapshot;
    return snapshot;
}
