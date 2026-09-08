#include <QCoreApplication>
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
    CodexSnapshot snap = monitor.pollOnce();

    std::cout << "========================================" << std::endl;
    std::cout << "       CODEX MONITOR VERIFICATION       " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "State:               " << (snap.state == CodexState::Working ? "WORKING (GREEN)" :
                                          snap.state == CodexState::Idle ? "IDLE (YELLOW)" : "STOPPED (RED)") << std::endl;
    std::cout << "State Description:   " << snap.stateDescription.toStdString() << std::endl;
    std::cout << "Codex Process:       " << (snap.isProcessRunning ? "RUNNING" : "NOT RUNNING") << std::endl;
    std::cout << "5-Hour Limit Used:   " << snap.primaryUsedPercent << " %" << std::endl;
    std::cout << "Limit Resets At:     " << snap.primaryResetsAt << " (Unix timestamp)" << std::endl;
    
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
    std::cout << "Command Text:        " << snap.lastCommand.command.toStdString() << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
