#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <iostream>
#include "codex_monitor.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
    QCoreApplication app(argc, argv);

    CodexMonitor monitor;
    QElapsedTimer timer;
    timer.start();
    CodexSnapshot snap = monitor.pollOnce();
    qint64 elapsed1 = timer.elapsed();

    timer.restart();
    for (int k = 0; k < 10; ++k) {
        monitor.pollOnce();
    }
    qint64 elapsed10 = timer.elapsed();

    std::cout << "pollOnce first call: " << elapsed1 << " ms" << std::endl;
    std::cout << "pollOnce 10 calls:   " << elapsed10 << " ms (avg " << (elapsed10 / 10.0) << " ms)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "       CODEX MONITOR VERIFICATION       " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "State:               " << (snap.state == CodexState::Working ? "WORKING (GREEN)" :
                                          snap.state == CodexState::Idle ? "IDLE (YELLOW)" : "STOPPED (RED)") << std::endl;
    std::cout << "State Description:   " << snap.stateDescription.toStdString() << std::endl;
    std::cout << "Codex Process:       " << (snap.isProcessRunning ? "RUNNING" : "NOT RUNNING") << std::endl;
    std::cout << "5-Hour Limit Used:   " << snap.primaryUsedPercent << " % (Remaining: " << (100.0 - snap.primaryUsedPercent) << " %)" << std::endl;
    std::cout << "5-Hour Window:       " << snap.primaryWindowMinutes << " min" << std::endl;
    std::cout << "5-Hour Resets At:    " << snap.primaryResetsAt << " (Unix timestamp)" << std::endl;
    std::cout << "7-Day Limit Used:    " << snap.secondaryUsedPercent << " % (Remaining: " << (100.0 - snap.secondaryUsedPercent) << " %)" << std::endl;
    std::cout << "7-Day Resets At:     " << snap.secondaryResetsAt << " (Unix timestamp)" << std::endl;
    
    // Message preview
    std::cout << "Last Message Author: " << snap.lastMessageAuthor.toStdString() << std::endl;
    std::cout << "Last Message Time:   " << snap.lastMessageTime.toString("yyyy-MM-dd HH:mm:ss").toStdString() << std::endl;
    QString elided = snap.lastMessageText.simplified();
    if (elided.length() > 130) elided = elided.left(127).trimmed() + "...";
    std::cout << "Message (1-2 lines): " << elided.toStdString() << std::endl;
    
    // Command info
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Last Command Valid:  " << (snap.lastCommand.isValid ? "YES" : "NO") << std::endl;
    std::cout << "Command Status:      " << snap.lastCommand.statusText.toStdString() << std::endl;
    std::cout << "Command Wall Time:   " << snap.lastCommand.wallTime.toStdString() << std::endl;
    std::cout << "Command Time:        " << snap.lastCommand.timestamp.toString("yyyy-MM-dd HH:mm:ss").toStdString() << std::endl;
    std::cout << "Active File:         " << snap.activeSessionPath.toStdString() << std::endl;
    std::cout << "Command Text:        " << snap.lastCommand.command.toStdString() << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
