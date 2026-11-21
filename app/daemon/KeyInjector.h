// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <cstdint>
#include <array>
#include <functional>
#include <vector>
#include <QString>

namespace galleond {

// Internal identifiers, independent of Linux input codes or Windows scan codes.
// Zero is reserved for "no held key" in the page controller.
enum class KeyCode : std::uint8_t {
    Kp0 = 1, Kp1, Kp2, Kp3, Kp4, Kp5, Kp6, Kp7, Kp8, Kp9,
    KpDot, KpEnter, KpPlus, KpMinus, KpAsterisk, KpSlash, NumLock, Backspace,
    // Local patch: the rest of a US keyboard, for "combo" and "text" actions.
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    Space, Enter, Tab, Esc, Minus, Equal, LeftBrace, RightBrace, Backslash,
    Semicolon, Apostrophe, Grave, Comma, Dot, Slash,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Up, Down, Left, Right, Home, End, PageUp, PageDown, Delete, Insert, Print,
    Shift, Ctrl, Alt, Super,
};
inline constexpr int AllowedKeyCount = static_cast<int>(KeyCode::Super);
[[nodiscard]] bool isAllowedKeyCode(int code);

// The keys a pages file may press, by name. A fixed list: the numeric keypad,
// Num Lock and Backspace. A name outside it is refused when the file is read,
// and the virtual keyboard is only ever given these codes, so the pages file
// cannot make galleon-customizerd type anything else.
//
// Names: kp0..kp9, kpdot, kpenter, kpplus, kpminus, kpasterisk, kpslash,
// numlock, backspace; local patch: a..z, 0..9, space, enter, tab, esc, minus,
// equal, leftbrace, rightbrace, backslash, semicolon, apostrophe, grave, comma,
// dot, slash, f1..f12, up, down, left, right, home, end, pageup, pagedown,
// delete, insert, print, shift, ctrl, alt, super.
[[nodiscard]] std::optional<int> keyCodeForName(const QString &name);
[[nodiscard]] QString allowedKeyNames();

// Local patch. A combination such as "super+shift+s": 1 to 5 names joined by
// '+', pressed in that order and released in reverse. Empty if a name is unknown.
inline constexpr int MaxComboKeys = 5;
[[nodiscard]] std::optional<std::vector<int>> keyCodesForCombo(const QString &combo);

// Local patch. The taps that type `text` on a US layout: printable ASCII only
// (space to '~'). Empty if a character cannot be typed.
struct KeyTap
{
    int code = 0;
    bool shift = false;
};
inline constexpr int MaxTextLength = 200;
[[nodiscard]] std::optional<std::vector<KeyTap>> keyTapsForText(const QString &text);

// Presses and releases keys as if a keyboard did. The real one is a virtual
// keyboard (uinput on Linux, SendInput on Windows); the tests replace it.
class KeyInjector
{
public:
    virtual ~KeyInjector() = default;
    // Called when a pages file with key actions is applied: opens the device
    // early, so that the first press is not lost while the desktop notices it.
    virtual void prepare() {}
    // `code` is an internal KeyCode from keyCodeForName(). False if it was not sent.
    virtual bool key(int code, bool down) = 0;
};

// Tracks the fixed whitelist and releases held keys on shutdown. The sender is
// injectable so the Windows lifetime policy is tested without typing real keys.
class TrackedKeyboard final : public KeyInjector
{
public:
    using Sender = std::function<bool(KeyCode, bool)>;
    explicit TrackedKeyboard(Sender sender);
    ~TrackedKeyboard() override;
    TrackedKeyboard(const TrackedKeyboard &) = delete;
    TrackedKeyboard &operator=(const TrackedKeyboard &) = delete;
    bool key(int code, bool down) override;
private:
    Sender m_send;
    std::array<bool, AllowedKeyCount> m_held{};
};

} // namespace galleond
