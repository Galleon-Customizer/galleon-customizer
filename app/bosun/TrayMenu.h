// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

namespace bosun {

// What the menu of the icon in the notification area offers.
struct TrayEntry
{
    enum class Action : unsigned char {
        ToggleWindow,   // show the window, or hide it when it is shown
        StartDaemon,    // ask systemd to start galleon-customizerd
        QuitAndStop,    // close the window and stop galleon-customizerd too
        Quit            // close the window only
    };
    Action action = Action::Quit;
    QString label;
};

// The entries, in order, for the state of the window and of galleon-customizerd. Pure.
//   - Show / Hide the window.
//   - galleon-customizerd running: "Quit and stop galleon-customizerd" (the module goes back to its numeric keypad)
//     and "Quit" (only this window; galleon-customizerd stays); galleon-customizerd not running: "Start galleon-customizerd"
//     and "Quit".
[[nodiscard]] QList<TrayEntry> trayEntries(bool windowShown, bool daemonRunning);

} // namespace bosun
