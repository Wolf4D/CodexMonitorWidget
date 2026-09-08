#pragma once

#include <QString>

namespace Styles {

inline QString getAppStyle() {
    return R"(
        QWidget#centralWidget {
            background-color: #0d0f14;
            border: 1px solid #3b4252;
            border-radius: 10px;
        }

        QLabel {
            color: #f1f5f9;
            font-family: "Segoe UI", "Inter", "Helvetica Neue", sans-serif;
        }

        QLabel#headerTitle {
            font-size: 12px;
            font-weight: 700;
            color: #ffffff;
            letter-spacing: 0.2px;
        }

        QFrame#card {
            background-color: #171a22;
            border: 1px solid #2e3545;
            border-radius: 7px;
        }

        /* Status Badge */
        QFrame#statusBadge {
            background-color: #191d27;
            border: 1px solid #374151;
            border-radius: 6px;
            padding: 2px 7px;
        }

        QLabel#statusDot {
            min-width: 8px;
            max-width: 8px;
            min-height: 8px;
            max-height: 8px;
            border-radius: 4px;
        }

        QLabel#statusText {
            font-size: 11px;
            font-weight: 700;
            color: #f8fafc;
        }

        /* Limit Section */
        QLabel#limitPercent {
            font-size: 15px;
            font-weight: 800;
            color: #10b981;
        }

        QLabel#resetCountdown {
            font-size: 11px;
            font-weight: 600;
            color: #cbd5e1;
        }

        QProgressBar#limitBar {
            border: 1px solid #2d3748;
            background-color: #181d29;
            height: 5px;
            max-height: 5px;
            border-radius: 2px;
            text-align: center;
        }

        QProgressBar#limitBar::chunk {
            border-radius: 2px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 #10b981, stop:1 #34d399);
        }

        /* Message Section */
        QLabel#msgAuthor {
            font-size: 11px;
            font-weight: 700;
            color: #38bdf8;
        }

        QLabel#msgDurationBadge {
            font-size: 10.5px;
            font-weight: 700;
            border-radius: 3px;
            padding: 1px 5px;
            letter-spacing: 0.1px;
        }

        QLabel#msgTime {
            font-size: 10px;
            font-weight: 500;
            color: #94a3b8;
        }

        QPushButton#msgToggleBtn {
            background-color: transparent;
            border: none;
            color: #94a3b8;
            font-size: 9px;
            padding: 0px;
            margin-right: 1px;
        }

        QPushButton#msgToggleBtn:hover {
            background-color: rgba(255, 255, 255, 0.12);
            color: #38bdf8;
            border-radius: 3px;
        }

        QLabel#msgAuthor:hover {
            color: #7dd3fc;
        }

        QLabel#msgTextLabel {
            color: #e2e8f0;
            font-size: 10.5px;
            line-height: 1.3;
            padding: 2px 0px 0px 0px;
        }

        /* Command Section */
        QLabel#cmdStatusBadge {
            font-size: 13px;
            font-weight: 800;
            border-radius: 4px;
            padding: 2px 8px;
            letter-spacing: 0.2px;
        }

        QLabel#cmdTime {
            font-size: 10px;
            font-weight: 500;
            color: #94a3b8;
        }

        QLabel#cmdTextLabel {
            font-family: "Cascadia Code", "Consolas", "Courier New", monospace;
            font-size: 10px;
            font-weight: 600;
            color: #38bdf8;
            background-color: #0a0c10;
            border: 1px solid #334155;
            border-radius: 4px;
            padding: 3px 6px;
            line-height: 1.25;
        }

        /* Control Buttons */
        QPushButton {
            background-color: #212530;
            border: 1px solid #3b4252;
            border-radius: 4px;
            color: #e2e8f0;
            font-size: 11px;
            font-weight: bold;
            padding: 1px 4px;
        }

        QPushButton:hover {
            background-color: #2d3444;
            border-color: #64748b;
            color: #ffffff;
        }

        QPushButton:pressed {
            background-color: #3b4252;
        }

        QPushButton#closeBtn:hover {
            background-color: #ef4444;
            color: #ffffff;
            border-color: #dc2626;
        }

        QPushButton#compactBtn:hover {
            background-color: #38bdf8;
            color: #0f172a;
            border-color: #38bdf8;
        }

        QPushButton#pinBtn[pinned="true"] {
            background-color: #38bdf8;
            color: #0f172a;
            font-weight: bold;
            border-color: #38bdf8;
        }

        /* Scrollbar */
        QScrollBar:vertical {
            border: none;
            background: transparent;
            width: 4px;
            margin: 0px;
        }

        QScrollBar::handle:vertical {
            background: rgba(255, 255, 255, 0.2);
            min-height: 20px;
            border-radius: 2px;
        }

        QScrollBar::handle:vertical:hover {
            background: rgba(255, 255, 255, 0.4);
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }

        QToolTip {
            background-color: #1e1f24;
            color: #f4f4f5;
            border: 1px solid #3f3f46;
            padding: 4px 8px;
            border-radius: 6px;
            font-size: 11px;
        }
    )";
}

} // namespace Styles
