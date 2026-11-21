// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "KeyInjector.h"

#include <QStringList>
#include <QDebug>
#include <stdexcept>
#include <utility>
#include <algorithm>

namespace galleond {
namespace {
constexpr std::array<const char *, AllowedKeyCount> Names{
    "kp0", "kp1", "kp2", "kp3", "kp4", "kp5", "kp6", "kp7", "kp8", "kp9",
    "kpdot", "kpenter", "kpplus", "kpminus", "kpasterisk", "kpslash", "numlock", "backspace",
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
    "u", "v", "w", "x", "y", "z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "space", "enter", "tab", "esc", "minus", "equal", "leftbrace", "rightbrace", "backslash",
    "semicolon", "apostrophe", "grave", "comma", "dot", "slash",
    "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
    "up", "down", "left", "right", "home", "end", "pageup", "pagedown", "delete", "insert", "print",
    "shift", "ctrl", "alt", "super",
};
static_assert(static_cast<int>(KeyCode::Super) == AllowedKeyCount);

int code(KeyCode key) { return static_cast<int>(key); }

// US layout: the key of each printable ASCII character, and whether Shift is held.
std::optional<KeyTap> tapForCharacter(char32_t c)
{
    if (c >= U'a' && c <= U'z')
        return KeyTap{code(KeyCode::A) + static_cast<int>(c - U'a'), false};
    if (c >= U'A' && c <= U'Z')
        return KeyTap{code(KeyCode::A) + static_cast<int>(c - U'A'), true};
    if (c >= U'0' && c <= U'9')
        return KeyTap{code(KeyCode::D0) + static_cast<int>(c - U'0'), false};
    struct Symbol { char32_t c; KeyCode key; bool shift; };
    static constexpr Symbol Symbols[] = {
        {U' ', KeyCode::Space, false}, {U'-', KeyCode::Minus, false}, {U'_', KeyCode::Minus, true},
        {U'=', KeyCode::Equal, false}, {U'+', KeyCode::Equal, true}, {U'[', KeyCode::LeftBrace, false},
        {U'{', KeyCode::LeftBrace, true}, {U']', KeyCode::RightBrace, false}, {U'}', KeyCode::RightBrace, true},
        {U'\\', KeyCode::Backslash, false}, {U'|', KeyCode::Backslash, true}, {U';', KeyCode::Semicolon, false},
        {U':', KeyCode::Semicolon, true}, {U'\'', KeyCode::Apostrophe, false}, {U'"', KeyCode::Apostrophe, true},
        {U'`', KeyCode::Grave, false}, {U'~', KeyCode::Grave, true}, {U',', KeyCode::Comma, false},
        {U'<', KeyCode::Comma, true}, {U'.', KeyCode::Dot, false}, {U'>', KeyCode::Dot, true},
        {U'/', KeyCode::Slash, false}, {U'?', KeyCode::Slash, true}, {U'!', KeyCode::D1, true},
        {U'@', KeyCode::D2, true}, {U'#', KeyCode::D3, true}, {U'$', KeyCode::D4, true},
        {U'%', KeyCode::D5, true}, {U'^', KeyCode::D6, true}, {U'&', KeyCode::D7, true},
        {U'*', KeyCode::D8, true}, {U'(', KeyCode::D9, true}, {U')', KeyCode::D0, true},
    };
    for (const Symbol &symbol : Symbols) {
        if (symbol.c == c)
            return KeyTap{code(symbol.key), symbol.shift};
    }
    return std::nullopt;
}
}

bool isAllowedKeyCode(int code)
{
    return code >= static_cast<int>(KeyCode::Kp0) && code <= static_cast<int>(KeyCode::Super);
}

std::optional<int> keyCodeForName(const QString &name)
{
    for (std::size_t i = 0; i < Names.size(); ++i) {
        if (name == QLatin1String(Names.at(i)))
            return static_cast<int>(i) + 1;
    }
    return std::nullopt;
}

QString allowedKeyNames()
{
    QStringList names;
    for (const char *name : Names)
        names << QLatin1String(name);
    return names.join(QStringLiteral(", "));
}

std::optional<std::vector<int>> keyCodesForCombo(const QString &combo)
{
    const QStringList names = combo.split(QLatin1Char('+'));
    if (names.isEmpty() || names.size() > MaxComboKeys)
        return std::nullopt;
    std::vector<int> codes;
    for (const QString &name : names) {
        const auto found = keyCodeForName(name.trimmed().toLower());
        if (!found || std::ranges::find(codes, *found) != codes.end())
            return std::nullopt;
        codes.push_back(*found);
    }
    return codes;
}

std::optional<std::vector<KeyTap>> keyTapsForText(const QString &text)
{
    if (text.isEmpty() || text.size() > MaxTextLength)
        return std::nullopt;
    std::vector<KeyTap> taps;
    for (const char32_t c : text.toStdU32String()) {
        const auto tap = tapForCharacter(c);
        if (!tap)
            return std::nullopt;
        taps.push_back(*tap);
    }
    return taps;
}

TrackedKeyboard::TrackedKeyboard(Sender sender) : m_send(std::move(sender))
{
    if (!m_send)
        throw std::invalid_argument("A keyboard sender is required");
}

TrackedKeyboard::~TrackedKeyboard()
{
    for (std::size_t i = 0; i < m_held.size(); ++i) {
        if (m_held.at(i)) {
            try {
                (void)m_send(static_cast<KeyCode>(i + 1), false);
            } catch (...) {
                // Continue releasing the other keys even if an injected sender throws.
                qWarning() << "galleon-customizerd: keyboard release failed during shutdown";
            }
        }
    }
}

bool TrackedKeyboard::key(int code, bool down)
{
    if (!isAllowedKeyCode(code))
        return false;
    bool &held = m_held.at(static_cast<std::size_t>(code - 1));
    if (held == down)
        return true;
    if (!m_send(static_cast<KeyCode>(code), down))
        return false;
    held = down;
    return true;
}
} // namespace galleond
