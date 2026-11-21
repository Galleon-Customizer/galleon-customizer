// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LinuxKeyInjector.h"

#include <QDebug>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <span>
#include <string_view>
#include <unistd.h>

namespace galleond {

namespace {

struct KeyMapping
{
    KeyCode key;
    int native;
};

// Linux input-event-codes.h translates the shared whitelist to uinput codes.
constexpr std::array<KeyMapping, AllowedKeyCount> Mapping{{
    {KeyCode::Kp0, KEY_KP0}, {KeyCode::Kp1, KEY_KP1}, {KeyCode::Kp2, KEY_KP2},
    {KeyCode::Kp3, KEY_KP3}, {KeyCode::Kp4, KEY_KP4}, {KeyCode::Kp5, KEY_KP5},
    {KeyCode::Kp6, KEY_KP6}, {KeyCode::Kp7, KEY_KP7}, {KeyCode::Kp8, KEY_KP8},
    {KeyCode::Kp9, KEY_KP9}, {KeyCode::KpDot, KEY_KPDOT}, {KeyCode::KpEnter, KEY_KPENTER},
    {KeyCode::KpPlus, KEY_KPPLUS}, {KeyCode::KpMinus, KEY_KPMINUS}, {KeyCode::KpAsterisk, KEY_KPASTERISK},
    {KeyCode::KpSlash, KEY_KPSLASH}, {KeyCode::NumLock, KEY_NUMLOCK}, {KeyCode::Backspace, KEY_BACKSPACE},
    // Local patch: the rest of a US keyboard.
    {KeyCode::A, KEY_A}, {KeyCode::B, KEY_B}, {KeyCode::C, KEY_C}, {KeyCode::D, KEY_D},
    {KeyCode::E, KEY_E}, {KeyCode::F, KEY_F}, {KeyCode::G, KEY_G}, {KeyCode::H, KEY_H},
    {KeyCode::I, KEY_I}, {KeyCode::J, KEY_J}, {KeyCode::K, KEY_K}, {KeyCode::L, KEY_L},
    {KeyCode::M, KEY_M}, {KeyCode::N, KEY_N}, {KeyCode::O, KEY_O}, {KeyCode::P, KEY_P},
    {KeyCode::Q, KEY_Q}, {KeyCode::R, KEY_R}, {KeyCode::S, KEY_S}, {KeyCode::T, KEY_T},
    {KeyCode::U, KEY_U}, {KeyCode::V, KEY_V}, {KeyCode::W, KEY_W}, {KeyCode::X, KEY_X},
    {KeyCode::Y, KEY_Y}, {KeyCode::Z, KEY_Z},
    {KeyCode::D0, KEY_0}, {KeyCode::D1, KEY_1}, {KeyCode::D2, KEY_2}, {KeyCode::D3, KEY_3},
    {KeyCode::D4, KEY_4}, {KeyCode::D5, KEY_5}, {KeyCode::D6, KEY_6}, {KeyCode::D7, KEY_7},
    {KeyCode::D8, KEY_8}, {KeyCode::D9, KEY_9},
    {KeyCode::Space, KEY_SPACE}, {KeyCode::Enter, KEY_ENTER}, {KeyCode::Tab, KEY_TAB},
    {KeyCode::Esc, KEY_ESC}, {KeyCode::Minus, KEY_MINUS}, {KeyCode::Equal, KEY_EQUAL},
    {KeyCode::LeftBrace, KEY_LEFTBRACE}, {KeyCode::RightBrace, KEY_RIGHTBRACE},
    {KeyCode::Backslash, KEY_BACKSLASH}, {KeyCode::Semicolon, KEY_SEMICOLON},
    {KeyCode::Apostrophe, KEY_APOSTROPHE}, {KeyCode::Grave, KEY_GRAVE}, {KeyCode::Comma, KEY_COMMA},
    {KeyCode::Dot, KEY_DOT}, {KeyCode::Slash, KEY_SLASH},
    {KeyCode::F1, KEY_F1}, {KeyCode::F2, KEY_F2}, {KeyCode::F3, KEY_F3}, {KeyCode::F4, KEY_F4},
    {KeyCode::F5, KEY_F5}, {KeyCode::F6, KEY_F6}, {KeyCode::F7, KEY_F7}, {KeyCode::F8, KEY_F8},
    {KeyCode::F9, KEY_F9}, {KeyCode::F10, KEY_F10}, {KeyCode::F11, KEY_F11}, {KeyCode::F12, KEY_F12},
    {KeyCode::Up, KEY_UP}, {KeyCode::Down, KEY_DOWN}, {KeyCode::Left, KEY_LEFT}, {KeyCode::Right, KEY_RIGHT},
    {KeyCode::Home, KEY_HOME}, {KeyCode::End, KEY_END}, {KeyCode::PageUp, KEY_PAGEUP},
    {KeyCode::PageDown, KEY_PAGEDOWN}, {KeyCode::Delete, KEY_DELETE}, {KeyCode::Insert, KEY_INSERT},
    {KeyCode::Print, KEY_SYSRQ},
    {KeyCode::Shift, KEY_LEFTSHIFT}, {KeyCode::Ctrl, KEY_LEFTCTRL}, {KeyCode::Alt, KEY_LEFTALT},
    {KeyCode::Super, KEY_LEFTMETA},
}};

} // namespace

UinputKeyboard::~UinputKeyboard()
{
    if (m_fd >= 0) {
        ioctl(m_fd, UI_DEV_DESTROY);
        close(m_fd);
    }
}

void UinputKeyboard::prepare()
{
    if (m_fd >= 0 || m_failed)
        return;
    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        m_failed = true;
        qWarning() << "galleon-customizerd: cannot open /dev/uinput (" << std::strerror(errno)
                   << "): key actions do nothing. See tools/udev/71-galleon-uinput.rules";
        return;
    }
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0;
    for (const KeyMapping &entry : Mapping)
        ok = ioctl(fd, UI_SET_KEYBIT, entry.native) == 0 && ok;
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1b1c; // the Galleon's maker, so that the device is recognisable
    setup.id.product = 0x2b18;
    setup.id.version = 1;
    constexpr std::string_view DeviceName = "Galleon Customizer virtual keyboard"; // the rest of setup.name stays zero
    static_assert(DeviceName.size() < UINPUT_MAX_NAME_SIZE);
    std::ranges::copy(DeviceName, std::span<char>(setup.name).begin());
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        m_failed = true;
        qWarning() << "galleon-customizerd: cannot create the virtual keyboard (" << std::strerror(errno) << ")";
        close(fd);
        return;
    }
    m_fd = fd;
    qInfo() << "galleon-customizerd: virtual keypad created";
}

bool UinputKeyboard::key(int code, bool down)
{
    const auto *const mapping = std::ranges::find_if(Mapping, [code](const KeyMapping &entry) {
        return static_cast<int>(entry.key) == code;
    });
    if (mapping == Mapping.end())
        return false;
    prepare();
    if (m_fd < 0)
        return false;

    std::array<input_event, 2> events{};
    events[0].type = EV_KEY;
    events[0].code = static_cast<unsigned short>(mapping->native);
    events[0].value = down ? 1 : 0;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[1].value = 0;
    return write(m_fd, events.data(), sizeof events) == static_cast<ssize_t>(sizeof events);
}

} // namespace galleond
