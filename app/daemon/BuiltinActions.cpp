// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuiltinActions.h"
#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

#if !defined(Q_OS_WIN)
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <csignal>
#include <sys/types.h>
#endif

namespace galleond {

namespace {

using Option = BuiltinActions::Option;
using Spec = BuiltinActions::Spec;

Option option(const char *key, Option::Kind kind)
{
    Option o;
    o.key = QString::fromLatin1(key);
    o.kind = kind;
    return o;
}
Option boolean(const char *key) { return option(key, Option::Bool); }
Option integer(const char *key, int min, int max) { Option o = option(key, Option::Int); o.min = min; o.max = max; return o; }
Option string(const char *key, int longest) { Option o = option(key, Option::String); o.max = longest; return o; }
Option choice(const char *key, QStringList choices) { Option o = option(key, Option::Choice); o.choices = std::move(choices); return o; }

Spec spec(const char *name, QList<Option> options = {}, bool stateful = false, bool turn = false)
{
    Spec s;
    s.name = QString::fromLatin1(name);
    s.options = std::move(options);
    s.stateful = stateful;
    s.turn = turn;
    return s;
}

const QStringList OnOff{QStringLiteral("toggle"), QStringLiteral("on"), QStringLiteral("off")};

// The words follow the language chosen in the settings window (bosun.conf), else the system's.
bool vietnamese()
{
    const QSettings settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                 + QStringLiteral("/galleon-customizer/bosun.conf"), QSettings::IniFormat);
    const QString chosen = settings.value(QStringLiteral("interface/language"), QStringLiteral("system")).toString();
    if (chosen != QLatin1String("system"))
        return chosen == QLatin1String("vi");
    return QLocale().language() == QLocale::Vietnamese;
}

QString tr(const char *text)
{
    static const QHash<QString, QString> vi{
        {QStringLiteral("%1 is not installed"), QStringLiteral("Chưa cài %1")},
        {QStringLiteral("Clipboard cleared"), QStringLiteral("Đã xóa clipboard")},
        {QStringLiteral("Colour copied"), QStringLiteral("Đã chép màu")},
        {QStringLiteral("Colour picker failed: %1"), QStringLiteral("Không lấy được màu: %1")},
        {QStringLiteral("Could not change do not disturb"), QStringLiteral("Không đổi được Không làm phiền")},
        {QStringLiteral("Could not change the night light"), QStringLiteral("Không đổi được Ánh sáng đêm")},
        {QStringLiteral("Could not change the volume"), QStringLiteral("Không đổi được âm lượng")},
        {QStringLiteral("Could not lock the screen"), QStringLiteral("Không khóa được màn hình")},
        {QStringLiteral("Could not start %1"), QStringLiteral("Không mở được %1")},
        {QStringLiteral("Could not start flameshot"), QStringLiteral("Không mở được flameshot")},
        {QStringLiteral("Could not suspend"), QStringLiteral("Không cho máy ngủ được")},
        {QStringLiteral("Dark mode"), QStringLiteral("Chế độ tối")},
        {QStringLiteral("Do not disturb"), QStringLiteral("Không làm phiền")},
        {QStringLiteral("Failed (exit code %1)"), QStringLiteral("Thất bại (mã %1)")},
        {QStringLiteral("Formatting removed"), QStringLiteral("Đã bỏ định dạng")},
        {QStringLiteral("Light mode"), QStringLiteral("Chế độ sáng")},
        {QStringLiteral("Microphone"), QStringLiteral("Micro")},
        {QStringLiteral("muted"), QStringLiteral("tắt tiếng")},
        {QStringLiteral("Night light"), QStringLiteral("Ánh sáng đêm")},
        {QStringLiteral("No clipboard tool (wl-clipboard, xclip or xsel)"), QStringLiteral("Không có công cụ clipboard (wl-clipboard, xclip hoặc xsel)")},
        {QStringLiteral("No sound output found"), QStringLiteral("Không thấy đầu ra âm thanh")},
        {QStringLiteral("No terminal found"), QStringLiteral("Không thấy terminal nào")},
        {QStringLiteral("No text found"), QStringLiteral("Không thấy chữ nào")},
        {QStringLiteral("Not supported on this desktop"), QStringLiteral("Desktop này không hỗ trợ")},
        {QStringLiteral("off"), QStringLiteral("tắt")},
        {QStringLiteral("on"), QStringLiteral("bật")},
        {QStringLiteral("Password copied"), QStringLiteral("Đã chép mật khẩu")},
        {QStringLiteral("Recording"), QStringLiteral("Đang quay")},
        {QStringLiteral("Recording stopped"), QStringLiteral("Đã dừng quay")},
        {QStringLiteral("Screenshot failed: %1"), QStringLiteral("Chụp màn hình thất bại: %1")},
        {QStringLiteral("Screenshot saved"), QStringLiteral("Đã lưu ảnh chụp")},
        {QStringLiteral("Sound"), QStringLiteral("Âm thanh")},
        {QStringLiteral("Text copied"), QStringLiteral("Đã chép chữ")},
        {QStringLiteral("Text recognition failed"), QStringLiteral("Nhận dạng chữ thất bại")},
        {QStringLiteral("The clipboard has no link"), QStringLiteral("Clipboard không có liên kết")},
        {QStringLiteral("The clipboard is empty"), QStringLiteral("Clipboard trống")},
        {QStringLiteral("Volume %1%"), QStringLiteral("Âm lượng %1%")},
    };
    const QString english = QString::fromUtf8(text);
    return vietnamese() ? vi.value(english, english) : english;
}

// ---------------------------------------------------------------- helpers on the host

struct Out
{
    int code = -1;
    QByteArray out;
};

Out exec(const BuiltinActions::Host &host, const QStringList &argv, int timeoutMs = 5000, const QByteArray &input = {})
{
    Out r;
    r.code = host.run(argv, input, timeoutMs, &r.out);
    return r;
}

QString text(const Out &r) { return QString::fromUtf8(r.out).trimmed(); }

BuiltinResult okIf(const Out &r, const QString &failure = {})
{
    if (r.code == 0)
        return {};
    return BuiltinResult::failed(failure.isEmpty() ? tr("Failed (exit code %1)").arg(r.code) : failure);
}

QString missing(const QString &tool) { return tr("%1 is not installed").arg(tool); }

BuiltinResult unsupported() { return BuiltinResult::failed(tr("Not supported on this desktop")); }

// on: nullopt = the other state than now.
bool toggled(bool now, std::optional<bool> on) { return on ? *on : !now; }

std::optional<bool> modeOf(const QJsonObject &options)
{
    const QString mode = options.value(QStringLiteral("mode")).toString(QStringLiteral("toggle"));
    if (mode == QLatin1String("on"))
        return true;
    if (mode == QLatin1String("off"))
        return false;
    return std::nullopt;
}

BuiltinResult firstOf(const BuiltinActions::Host &host, const QStringList &programs, const QStringList &args = {})
{
    for (const QString &program : programs) {
        if (host.which(program))
            return host.spawn(QStringList{program} + args) ? BuiltinResult{} : BuiltinResult::failed(tr("Could not start %1").arg(program));
    }
    return BuiltinResult::failed(missing(programs.join(QStringLiteral(" / "))));
}

// ---------------------------------------------------------------- GNOME settings

const QString Interface = QStringLiteral("org.gnome.desktop.interface");
const QString Notifications = QStringLiteral("org.gnome.desktop.notifications");
const QString Color = QStringLiteral("org.gnome.settings-daemon.plugins.color");

QString gget(const BuiltinActions::Host &host, const QString &schema, const QString &key)
{
    QString value = text(exec(host, {QStringLiteral("gsettings"), QStringLiteral("get"), schema, key}));
    if (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')) && value.size() >= 2)
        value = value.mid(1, value.size() - 2);
    return value;
}

Out gset(const BuiltinActions::Host &host, const QString &schema, const QString &key, const QString &value)
{
    return exec(host, {QStringLiteral("gsettings"), QStringLiteral("set"), schema, key, value});
}

std::optional<bool> darkNow(const BuiltinActions::Host &host)
{
    if (host.desktop == QLatin1String("gnome"))
        return gget(host, Interface, QStringLiteral("color-scheme")) == QLatin1String("prefer-dark");
    if (host.desktop == QLatin1String("kde")) {
        for (const char *reader : {"kreadconfig6", "kreadconfig5"}) {
            if (host.which(QString::fromLatin1(reader)))
                return text(exec(host, {QString::fromLatin1(reader), QStringLiteral("--group"), QStringLiteral("General"),
                                       QStringLiteral("--key"), QStringLiteral("ColorScheme")})).contains(QLatin1String("dark"), Qt::CaseInsensitive);
        }
    }
    return std::nullopt;
}

BuiltinResult darkMode(const BuiltinActions::Host &host, std::optional<bool> on)
{
    if (host.desktop == QLatin1String("gnome") && host.which(QStringLiteral("gsettings"))) {
        const bool dark = toggled(darkNow(host).value_or(false), on);
        BuiltinResult r = okIf(gset(host, Interface, QStringLiteral("color-scheme"), dark ? QStringLiteral("prefer-dark") : QStringLiteral("default")));
        if (r.ok) { // GTK 3 applications follow the theme's name, not the colour scheme
            const QString theme = gget(host, Interface, QStringLiteral("gtk-theme"));
            if (theme == QLatin1String("Adwaita") || theme == QLatin1String("Adwaita-dark"))
                gset(host, Interface, QStringLiteral("gtk-theme"), dark ? QStringLiteral("Adwaita-dark") : QStringLiteral("Adwaita"));
            r.state = dark;
            r.message = dark ? tr("Dark mode") : tr("Light mode");
        }
        return r;
    }
    if (host.desktop == QLatin1String("kde") && host.which(QStringLiteral("plasma-apply-colorscheme"))) {
        const bool dark = toggled(darkNow(host).value_or(false), on);
        BuiltinResult r = okIf(exec(host, {QStringLiteral("plasma-apply-colorscheme"), dark ? QStringLiteral("BreezeDark") : QStringLiteral("BreezeLight")}, 10000));
        if (r.ok) {
            r.state = dark;
            r.message = dark ? tr("Dark mode") : tr("Light mode");
        }
        return r;
    }
    return unsupported();
}

BuiltinResult gnomeSwitch(const BuiltinActions::Host &host, const QString &schema, const QString &key, bool invert,
                          std::optional<bool> on, const QString &name)
{
    if (host.desktop != QLatin1String("gnome") || !host.which(QStringLiteral("gsettings")))
        return unsupported();
    const bool now = (gget(host, schema, key) == QLatin1String("true")) != invert;
    const bool want = toggled(now, on);
    BuiltinResult r = okIf(gset(host, schema, key, (want != invert) ? QStringLiteral("true") : QStringLiteral("false")));
    if (r.ok) {
        r.state = want;
        r.message = name + QStringLiteral(": ") + (want ? tr("on") : tr("off"));
    }
    return r;
}

std::optional<bool> kdeNightLightOn(const BuiltinActions::Host &host)
{
    QVariant reply;
    if (!host.dbus(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/NightLight"), QStringLiteral("org.freedesktop.DBus.Properties"),
                   QStringLiteral("Get"), {QStringLiteral("org.kde.KWin.NightLight"), QStringLiteral("inhibited")}, &reply))
        return std::nullopt;
    return !reply.toBool();
}

BuiltinResult nightLight(const BuiltinActions::Host &host, std::optional<bool> on)
{
    if (host.desktop == QLatin1String("gnome"))
        return gnomeSwitch(host, Color, QStringLiteral("night-light-enabled"), false, on, tr("Night light"));
    if (host.desktop == QLatin1String("kde")) {
        const std::optional<bool> now = kdeNightLightOn(host);
        if (!now)
            return unsupported();
        const bool want = toggled(*now, on);
        if (want != *now && !host.dbus(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/NightLight"),
                                       QStringLiteral("org.kde.KWin.NightLight"), QStringLiteral("toggle"), {}, nullptr))
            return BuiltinResult::failed(tr("Could not change the night light"));
        BuiltinResult r;
        r.state = kdeNightLightOn(host).value_or(want);
        r.message = tr("Night light") + QStringLiteral(": ") + (*r.state ? tr("on") : tr("off"));
        return r;
    }
    return unsupported();
}

BuiltinResult lock(const BuiltinActions::Host &host)
{
    if (host.dbus(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
                  QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("Lock"), {}, nullptr))
        return {};
    const QString session = qEnvironmentVariable("XDG_SESSION_ID");
    QStringList argv{QStringLiteral("loginctl"), QStringLiteral("lock-session")};
    if (!session.isEmpty())
        argv << session;
    if (exec(host, argv).code == 0)
        return {};
    return okIf(exec(host, {QStringLiteral("loginctl"), QStringLiteral("lock-sessions")}), tr("Could not lock the screen"));
}

BuiltinResult powerDialog(const BuiltinActions::Host &host)
{
    if (host.desktop == QLatin1String("gnome") && host.which(QStringLiteral("gnome-session-quit")))
        return host.spawn({QStringLiteral("gnome-session-quit"), QStringLiteral("--power-off")}) ? BuiltinResult{} : unsupported();
    if (host.desktop == QLatin1String("kde")
        && host.dbus(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"), QStringLiteral("org.kde.LogoutPrompt"),
                     QStringLiteral("promptShutDown"), {}, nullptr))
        return {};
    return unsupported();
}

QStringList terminals(const BuiltinActions::Host &host)
{
    QStringList list{QStringLiteral("xdg-terminal-exec")};
    const QString fromEnvironment = qEnvironmentVariable("TERMINAL").trimmed();
    if (!fromEnvironment.isEmpty())
        list << QProcess::splitCommand(fromEnvironment).value(0);
    if (host.desktop == QLatin1String("gnome"))
        list << QStringLiteral("ptyxis") << QStringLiteral("gnome-terminal") << QStringLiteral("kgx") << QStringLiteral("gnome-console");
    else if (host.desktop == QLatin1String("kde"))
        list << QStringLiteral("konsole");
    list << QStringLiteral("xfce4-terminal") << QStringLiteral("mate-terminal") << QStringLiteral("lxterminal") << QStringLiteral("qterminal")
         << QStringLiteral("tilix") << QStringLiteral("alacritty") << QStringLiteral("kitty") << QStringLiteral("foot") << QStringLiteral("wezterm")
         << QStringLiteral("x-terminal-emulator") << QStringLiteral("xterm");
    return list;
}

QStringList execFlag(const QString &terminal)
{
    static const QHash<QString, QStringList> flags{
        {QStringLiteral("gnome-terminal"), {QStringLiteral("--")}}, {QStringLiteral("ptyxis"), {QStringLiteral("--")}},
        {QStringLiteral("kgx"), {QStringLiteral("--")}}, {QStringLiteral("kitty"), {}}, {QStringLiteral("foot"), {}},
        {QStringLiteral("wezterm"), {QStringLiteral("start"), QStringLiteral("--")}}, {QStringLiteral("xdg-terminal-exec"), {}},
        {QStringLiteral("xfce4-terminal"), {QStringLiteral("-x")}}, {QStringLiteral("terminator"), {QStringLiteral("-x")}}};
    return flags.value(terminal, {QStringLiteral("-e")});
}

BuiltinResult openTerminal(const BuiltinActions::Host &host, const QString &command)
{
    for (const QString &terminal : terminals(host)) {
        if (terminal.isEmpty() || !host.which(terminal))
            continue;
        QStringList argv{terminal};
        if (!command.isEmpty())
            argv << execFlag(terminal) << QStringLiteral("sh") << QStringLiteral("-c") << command;
        return host.spawn(argv) ? BuiltinResult{} : BuiltinResult::failed(tr("Could not start %1").arg(terminal));
    }
    return BuiltinResult::failed(tr("No terminal found"));
}

// ---------------------------------------------------------------- sound

QString soundTool(const BuiltinActions::Host &host)
{
    if (host.which(QStringLiteral("wpctl")))
        return QStringLiteral("wpctl");
    if (host.which(QStringLiteral("pactl")))
        return QStringLiteral("pactl");
    return {};
}

QString wpDevice(bool input) { return input ? QStringLiteral("@DEFAULT_AUDIO_SOURCE@") : QStringLiteral("@DEFAULT_AUDIO_SINK@"); }
QString paKind(bool input) { return input ? QStringLiteral("source") : QStringLiteral("sink"); }
QString paDevice(bool input) { return input ? QStringLiteral("@DEFAULT_SOURCE@") : QStringLiteral("@DEFAULT_SINK@"); }

struct Level { int percent = 0; bool muted = false; };

std::optional<Level> soundLevel(const BuiltinActions::Host &host, bool input)
{
    const QString tool = soundTool(host);
    if (tool == QLatin1String("wpctl")) {
        const QString out = text(exec(host, {tool, QStringLiteral("get-volume"), wpDevice(input)}, 3000));
        static const QRegularExpression volume(QStringLiteral("Volume:\\s*([\\d.]+)"));
        const auto match = volume.match(out);
        if (!match.hasMatch())
            return std::nullopt;
        return Level{.percent = qRound(match.captured(1).toDouble() * 100), .muted = out.contains(QLatin1String("MUTED"))};
    }
    if (tool == QLatin1String("pactl")) {
        const QString out = text(exec(host, {tool, QStringLiteral("get-%1-volume").arg(paKind(input)), paDevice(input)}, 3000));
        static const QRegularExpression percent(QStringLiteral("(\\d+)%"));
        const auto match = percent.match(out);
        if (!match.hasMatch())
            return std::nullopt;
        const QString mute = text(exec(host, {tool, QStringLiteral("get-%1-mute").arg(paKind(input)), paDevice(input)}, 3000));
        return Level{.percent = match.captured(1).toInt(), .muted = mute.contains(QLatin1String("yes"))};
    }
    return std::nullopt;
}

BuiltinResult setVolume(const BuiltinActions::Host &host, int percent)
{
    percent = std::clamp(percent, 0, 100);
    const QString tool = soundTool(host);
    Out r;
    if (tool == QLatin1String("wpctl"))
        r = exec(host, {tool, QStringLiteral("set-volume"), QStringLiteral("-l"), QStringLiteral("1.0"), wpDevice(false), QStringLiteral("%1%").arg(percent)}, 3000);
    else if (tool == QLatin1String("pactl"))
        r = exec(host, {tool, QStringLiteral("set-sink-volume"), paDevice(false), QStringLiteral("%1%").arg(percent)}, 3000);
    else
        return BuiltinResult::failed(missing(QStringLiteral("wpctl / pactl")));
    BuiltinResult result = okIf(r, tr("Could not change the volume"));
    if (result.ok)
        result.message = tr("Volume %1%").arg(percent);
    return result;
}

BuiltinResult mute(const BuiltinActions::Host &host, bool input)
{
    const QString tool = soundTool(host);
    Out r;
    if (tool == QLatin1String("wpctl"))
        r = exec(host, {tool, QStringLiteral("set-mute"), wpDevice(input), QStringLiteral("toggle")}, 3000);
    else if (tool == QLatin1String("pactl"))
        r = exec(host, {tool, QStringLiteral("set-%1-mute").arg(paKind(input)), paDevice(input), QStringLiteral("toggle")}, 3000);
    else
        return BuiltinResult::failed(missing(QStringLiteral("wpctl / pactl")));
    BuiltinResult result = okIf(r);
    if (result.ok) {
        const bool muted = soundLevel(host, input).value_or(Level{}).muted;
        result.state = muted;
        result.message = (input ? tr("Microphone") : tr("Sound")) + QStringLiteral(": ") + (muted ? tr("muted") : tr("on"));
    }
    return result;
}

// ---------------------------------------------------------------- clipboard

QStringList clipboardGet(const BuiltinActions::Host &host)
{
    if (host.wayland && host.which(QStringLiteral("wl-paste")))
        return {QStringLiteral("wl-paste"), QStringLiteral("--no-newline")};
    if (host.which(QStringLiteral("xclip")))
        return {QStringLiteral("xclip"), QStringLiteral("-selection"), QStringLiteral("clipboard"), QStringLiteral("-o")};
    if (host.which(QStringLiteral("xsel")))
        return {QStringLiteral("xsel"), QStringLiteral("--clipboard"), QStringLiteral("--output")};
    return {};
}

QStringList clipboardSet(const BuiltinActions::Host &host, const QString &type = {})
{
    if (host.wayland && host.which(QStringLiteral("wl-copy")))
        return type.isEmpty() ? QStringList{QStringLiteral("wl-copy")} : QStringList{QStringLiteral("wl-copy"), QStringLiteral("--type"), type};
    if (host.which(QStringLiteral("xclip"))) {
        QStringList argv{QStringLiteral("xclip"), QStringLiteral("-selection"), QStringLiteral("clipboard")};
        if (!type.isEmpty())
            argv << QStringLiteral("-t") << type;
        return argv << QStringLiteral("-i");
    }
    if (type.isEmpty() && host.which(QStringLiteral("xsel")))
        return {QStringLiteral("xsel"), QStringLiteral("--clipboard"), QStringLiteral("--input")};
    return {};
}

QString noClipboard() { return tr("No clipboard tool (wl-clipboard, xclip or xsel)"); }

std::optional<QString> readClipboard(const BuiltinActions::Host &host)
{
    const QStringList argv = clipboardGet(host);
    if (argv.isEmpty())
        return std::nullopt;
    const Out r = exec(host, argv, 2000);
    return r.code == 0 ? QString::fromUtf8(r.out) : QString();
}

bool writeClipboard(const BuiltinActions::Host &host, const QByteArray &data, const QString &type = {})
{
    const QStringList argv = clipboardSet(host, type);
    if (argv.isEmpty())
        return false;
    const int code = host.run(argv, data, 2000, nullptr);
    return code == 0 || code == 124; // wl-copy may keep serving: a timeout still set it
}

BuiltinResult copied(const BuiltinActions::Host &host, const QString &value, const QString &message)
{
    if (!writeClipboard(host, value.toUtf8()))
        return BuiltinResult::failed(noClipboard());
    BuiltinResult r;
    r.message = message;
    return r;
}

BuiltinResult queryClipboard(const BuiltinActions::Host &host, QString templateUrl, const QString &target)
{
    const std::optional<QString> clip = readClipboard(host);
    if (!clip)
        return BuiltinResult::failed(noClipboard());
    const QString query = clip->trimmed();
    if (query.isEmpty())
        return BuiltinResult::failed(tr("The clipboard is empty"));
    templateUrl.replace(QStringLiteral("{q}"), QString::fromLatin1(QUrl::toPercentEncoding(query)));
    templateUrl.replace(QStringLiteral("{target}"), QString::fromLatin1(QUrl::toPercentEncoding(target)));
    BuiltinResult r;
    r.open = QUrl(templateUrl, QUrl::TolerantMode);
    return r;
}

// ---------------------------------------------------------------- capture

QString screenrec(const BuiltinActions::Host &host)
{
    const QString local = QDir::homePath() + QStringLiteral("/.local/bin/galleon-screenrec");
    if (QFileInfo(local).isExecutable())
        return local;
    return host.which(QStringLiteral("galleon-screenrec")) ? QStringLiteral("galleon-screenrec") : QString();
}

bool recording(const BuiltinActions::Host &host)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(host);
    return false;
#else
    QFile file(host.stateDir + QStringLiteral("/galleon-screenrec/pid"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    bool ok = false;
    const qint64 pid = file.readAll().trimmed().toLongLong(&ok);
    return ok && pid > 1 && ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

QString stamped(const QString &folder, const QString &prefix, const QString &suffix)
{
    return folder + QLatin1Char('/') + prefix + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")) + suffix;
}

BuiltinResult screenshot(const BuiltinActions::Host &host, const QJsonObject &options)
{
    const QString mode = options.value(QStringLiteral("mode")).toString(QStringLiteral("full"));
    const QString folder = host.picturesDir;
    QDir().mkpath(folder);
    const bool flameshot = host.which(QStringLiteral("flameshot"));
    if (flameshot && (options.value(QStringLiteral("use_flameshot")).toBool() || mode == QLatin1String("area"))) {
        QStringList argv{QStringLiteral("flameshot"), mode == QLatin1String("area") ? QStringLiteral("gui") : QStringLiteral("full"),
                         QStringLiteral("-p"), folder, QStringLiteral("-c")};
        if (mode == QLatin1String("delay"))
            argv << QStringLiteral("-d") << QString::number(options.value(QStringLiteral("delay")).toInt(3) * 1000);
        return host.spawn(argv) ? BuiltinResult{} : BuiltinResult::failed(tr("Could not start flameshot"));
    }
    if (mode == QLatin1String("delay"))
        host.sleep(std::clamp(options.value(QStringLiteral("delay")).toInt(3), 0, 60) * 1000);
    QString error;
    const QString shot = host.screenshot(mode == QLatin1String("area"), &error);
    if (shot.isEmpty())
        return BuiltinResult::failed(tr("Screenshot failed: %1").arg(error));
    QString saved = stamped(folder, QStringLiteral("Screenshot-"), QStringLiteral(".png"));
    if (!QFile::rename(shot, saved) && !(QFile::copy(shot, saved) && QFile::remove(shot)))
        saved = shot;
    QFile image(saved);
    if (image.open(QIODevice::ReadOnly))
        writeClipboard(host, image.readAll(), QStringLiteral("image/png"));
    BuiltinResult r;
    r.message = tr("Screenshot saved");
    r.notifyTitle = r.message;
    r.notifyBody = saved;
    return r;
}

BuiltinResult record(const BuiltinActions::Host &host)
{
    const QString tool = screenrec(host);
    if (tool.isEmpty())
        return BuiltinResult::failed(missing(QStringLiteral("galleon-screenrec")));
    const bool wasRecording = recording(host);
    if (!host.spawn({tool})) // the tool starts a recording, or stops the one running
        return BuiltinResult::failed(tr("Could not start %1").arg(tool));
    BuiltinResult r;
    r.state = !wasRecording;
    r.message = wasRecording ? tr("Recording stopped") : tr("Recording");
    return r;
}

BuiltinResult colorPicker(const BuiltinActions::Host &host)
{
    QString error;
    const std::optional<QColor> color = host.pickColor(&error);
    if (!color)
        return BuiltinResult::failed(tr("Colour picker failed: %1").arg(error));
    const QString hex = color->name(QColor::HexRgb).toUpper();
    BuiltinResult r = copied(host, hex, hex);
    if (r.ok) {
        r.notifyTitle = tr("Colour copied");
        r.notifyBody = hex;
    }
    return r;
}

BuiltinResult ocr(const BuiltinActions::Host &host, const QJsonObject &options)
{
    if (!host.which(QStringLiteral("tesseract")))
        return BuiltinResult::failed(missing(QStringLiteral("tesseract")));
    QString error;
    const QString shot = host.screenshot(true, &error);
    if (shot.isEmpty())
        return BuiltinResult::failed(tr("Screenshot failed: %1").arg(error));
    const QString languages = options.value(QStringLiteral("languages")).toString(QStringLiteral("eng+vie"));
    const Out r = exec(host, {QStringLiteral("tesseract"), shot, QStringLiteral("-"), QStringLiteral("-l"), languages}, 60000);
    QFile::remove(shot);
    if (r.code != 0)
        return BuiltinResult::failed(tr("Text recognition failed"));
    const QString found = text(r);
    if (found.isEmpty())
        return BuiltinResult::failed(tr("No text found"));
    BuiltinResult result = copied(host, found, tr("Text copied"));
    if (result.ok) {
        result.notifyTitle = result.message;
        result.notifyBody = found.left(200);
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------- the catalog

const QList<Spec> &BuiltinActions::catalog()
{
    static const QList<Spec> specs{
        spec("system.lock"),
        spec("system.suspend"),
        spec("system.power"),
        spec("system.dark_mode", {choice("mode", OnOff)}, true),
        spec("system.dnd", {choice("mode", OnOff)}, true),
        spec("system.night_light", {choice("mode", OnOff)}, true),
        spec("system.settings"),
        spec("system.terminal", {string("command", 1000)}),
        spec("system.monitor"),
        spec("sound.volume", {integer("step", -100, 100)}, false, true),
        spec("sound.set_volume", {integer("percent", 0, 100)}),
        spec("sound.mute_output", {}, true),
        spec("sound.mute_input", {}, true),
        spec("clipboard.datetime", {string("format", 64), boolean("paste")}),
        spec("clipboard.password", {integer("length", 4, 256)}),
        spec("clipboard.search", {string("engine_url", 500)}),
        spec("clipboard.translate", {string("target", 16), string("url", 500)}),
        spec("clipboard.open_link"),
        spec("clipboard.plain_text"),
        spec("clipboard.clear"),
        spec("capture.screenshot", {choice("mode", {QStringLiteral("full"), QStringLiteral("area"), QStringLiteral("delay")}), integer("delay", 0, 60), boolean("use_flameshot")}),
        spec("capture.record", {}, true),
        spec("capture.color_picker"),
        spec("capture.ocr", {string("languages", 64)}),
        spec("capture.open_folder", {choice("which", {QStringLiteral("pictures"), QStringLiteral("videos")})}),
    };
    return specs;
}

const Spec *BuiltinActions::find(const QString &name)
{
    for (const Spec &spec : catalog()) {
        if (spec.name == name)
            return &spec;
    }
    return nullptr;
}

QString BuiltinActions::checkOptions(const Spec &spec, const QJsonObject &options)
{
    for (auto it = options.begin(); it != options.end(); ++it) {
        const auto option = std::ranges::find_if(spec.options, [&](const Option &o) { return o.key == it.key(); });
        if (option == spec.options.end())
            return QStringLiteral("unknown member \"%1\"").arg(it.key());
        const QJsonValue value = it.value();
        switch (option->kind) {
        case Option::Bool:
            if (!value.isBool())
                return QStringLiteral("%1 must be true or false").arg(it.key());
            break;
        case Option::Int: {
            const double number = value.toDouble(-1e18);
            if (!value.isDouble() || number != std::floor(number) || number < option->min || number > option->max)
                return QStringLiteral("%1 must be a whole number from %2 to %3").arg(it.key()).arg(option->min).arg(option->max);
            break;
        }
        case Option::String:
            if (!value.isString() || value.toString().size() > option->max || value.toString().contains(QChar(0)))
                return QStringLiteral("%1 must be a text of at most %2 characters").arg(it.key()).arg(option->max);
            break;
        case Option::Choice:
            if (!option->choices.contains(value.toString()))
                return QStringLiteral("%1 must be one of %2").arg(it.key(), option->choices.join(QStringLiteral(", ")));
            break;
        }
    }
    const auto url = [&](const QString &key) -> QString {
        if (!options.contains(key))
            return {};
        const QString address = options.value(key).toString();
        if (!(address.startsWith(QLatin1String("https://")) || address.startsWith(QLatin1String("http://"))) || !address.contains(QLatin1String("{q}")))
            return QStringLiteral("%1 must be an http or https address with {q} for the text").arg(key);
        return {};
    };
    if (spec.name == QLatin1String("clipboard.search"))
        return url(QStringLiteral("engine_url"));
    if (spec.name == QLatin1String("clipboard.translate")) {
        static const QRegularExpression code(QStringLiteral("^[A-Za-z]{2,3}([-_][A-Za-z0-9]{2,8})?$"));
        if (options.contains(QStringLiteral("target")) && !code.match(options.value(QStringLiteral("target")).toString()).hasMatch())
            return QStringLiteral("target must be a language code such as vi or en");
        return url(QStringLiteral("url"));
    }
    if (spec.name == QLatin1String("capture.ocr") && options.contains(QStringLiteral("languages"))) {
        static const QRegularExpression languages(QStringLiteral("^[a-z_]+(\\+[a-z_]+)*$"));
        if (!languages.match(options.value(QStringLiteral("languages")).toString()).hasMatch())
            return QStringLiteral("languages must be tesseract language names joined by +, such as eng+vie");
    }
    if (spec.name == QLatin1String("sound.volume") && options.value(QStringLiteral("step")).toInt(2) == 0)
        return QStringLiteral("step must not be 0");
    if (spec.name == QLatin1String("sound.set_volume") && !options.contains(QStringLiteral("percent")))
        return QStringLiteral("percent is required");
    return {};
}

// ---------------------------------------------------------------- the work

BuiltinResult BuiltinActions::execute(const Host &host, const QString &name, const QJsonObject &options, int times)
{
    const std::optional<bool> mode = modeOf(options);
    if (name == QLatin1String("system.lock"))
        return lock(host);
    if (name == QLatin1String("system.suspend"))
        return okIf(exec(host, {QStringLiteral("systemctl"), QStringLiteral("suspend")}, 10000), tr("Could not suspend"));
    if (name == QLatin1String("system.power"))
        return powerDialog(host);
    if (name == QLatin1String("system.dark_mode"))
        return darkMode(host, mode);
    if (name == QLatin1String("system.dnd"))
        return gnomeSwitch(host, Notifications, QStringLiteral("show-banners"), true, mode, tr("Do not disturb"));
    if (name == QLatin1String("system.night_light"))
        return nightLight(host, mode);
    if (name == QLatin1String("system.settings")) {
        if (host.desktop == QLatin1String("gnome"))
            return firstOf(host, {QStringLiteral("gnome-control-center")});
        if (host.desktop == QLatin1String("kde"))
            return firstOf(host, {QStringLiteral("systemsettings"), QStringLiteral("systemsettings5")});
        return firstOf(host, {QStringLiteral("xfce4-settings-manager"), QStringLiteral("cinnamon-settings"), QStringLiteral("mate-control-center"), QStringLiteral("lxqt-config")});
    }
    if (name == QLatin1String("system.terminal"))
        return openTerminal(host, options.value(QStringLiteral("command")).toString().trimmed());
    if (name == QLatin1String("system.monitor")) {
        if (host.desktop == QLatin1String("gnome"))
            return firstOf(host, {QStringLiteral("resources"), QStringLiteral("gnome-system-monitor")});
        if (host.desktop == QLatin1String("kde"))
            return firstOf(host, {QStringLiteral("plasma-systemmonitor"), QStringLiteral("ksysguard")});
        return firstOf(host, {QStringLiteral("xfce4-taskmanager"), QStringLiteral("mate-system-monitor"), QStringLiteral("gnome-system-monitor"), QStringLiteral("lxtask")});
    }

    if (name == QLatin1String("sound.volume")) {
        if (soundTool(host).isEmpty())
            return BuiltinResult::failed(missing(QStringLiteral("wpctl / pactl")));
        const std::optional<Level> level = soundLevel(host, false);
        if (!level)
            return BuiltinResult::failed(tr("No sound output found"));
        return setVolume(host, level->percent + options.value(QStringLiteral("step")).toInt(2) * (times == 0 ? 1 : times));
    }
    if (name == QLatin1String("sound.set_volume"))
        return setVolume(host, options.value(QStringLiteral("percent")).toInt(50));
    if (name == QLatin1String("sound.mute_output"))
        return mute(host, false);
    if (name == QLatin1String("sound.mute_input"))
        return mute(host, true);

    if (name == QLatin1String("clipboard.datetime")) {
        const QString now = QDateTime::currentDateTime().toString(options.value(QStringLiteral("format")).toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        BuiltinResult r = copied(host, now, now);
        r.paste = r.ok && options.value(QStringLiteral("paste")).toBool(true);
        return r;
    }
    if (name == QLatin1String("clipboard.password")) {
        static const QString alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*-_=+?");
        const int length = std::clamp(options.value(QStringLiteral("length")).toInt(20), 4, 256);
        QString password;
        for (int i = 0; i < length; ++i)
            password += alphabet.at(static_cast<qsizetype>(QRandomGenerator::system()->bounded(static_cast<quint32>(alphabet.size()))));
        return copied(host, password, tr("Password copied"));
    }
    if (name == QLatin1String("clipboard.search"))
        return queryClipboard(host, options.value(QStringLiteral("engine_url")).toString(QStringLiteral("https://www.google.com/search?q={q}")), {});
    if (name == QLatin1String("clipboard.translate"))
        return queryClipboard(host, options.value(QStringLiteral("url")).toString(QStringLiteral("https://translate.google.com/?sl=auto&tl={target}&text={q}")),
                              options.value(QStringLiteral("target")).toString(QStringLiteral("vi")));
    if (name == QLatin1String("clipboard.open_link")) {
        const std::optional<QString> clip = readClipboard(host);
        if (!clip)
            return BuiltinResult::failed(noClipboard());
        const QString link = clip->trimmed();
        static const QRegularExpression isLink(QStringLiteral("^(https?://|www\\.)\\S+$"), QRegularExpression::CaseInsensitiveOption);
        if (!isLink.match(link).hasMatch())
            return BuiltinResult::failed(tr("The clipboard has no link"));
        BuiltinResult r;
        r.open = QUrl(link.contains(QLatin1String("://")) ? link : QStringLiteral("https://") + link, QUrl::TolerantMode);
        return r;
    }
    if (name == QLatin1String("clipboard.plain_text")) {
        const std::optional<QString> clip = readClipboard(host);
        if (!clip)
            return BuiltinResult::failed(noClipboard());
        return copied(host, *clip, tr("Formatting removed"));
    }
    if (name == QLatin1String("clipboard.clear"))
        return copied(host, QString(), tr("Clipboard cleared"));

    if (name == QLatin1String("capture.screenshot"))
        return screenshot(host, options);
    if (name == QLatin1String("capture.record"))
        return record(host);
    if (name == QLatin1String("capture.color_picker"))
        return colorPicker(host);
    if (name == QLatin1String("capture.ocr"))
        return ocr(host, options);
    if (name == QLatin1String("capture.open_folder")) {
        const QString folder = options.value(QStringLiteral("which")).toString() == QLatin1String("videos") ? host.videosDir : host.picturesDir;
        QDir().mkpath(folder);
        BuiltinResult r;
        r.open = QUrl::fromLocalFile(folder);
        return r;
    }
    return BuiltinResult::failed(QStringLiteral("unknown action %1").arg(name));
}

std::optional<bool> BuiltinActions::queryState(const Host &host, const QString &name)
{
    if (name == QLatin1String("system.dark_mode"))
        return darkNow(host);
    if (name == QLatin1String("system.dnd")) {
        if (host.desktop != QLatin1String("gnome"))
            return std::nullopt;
        const QString banners = gget(host, Notifications, QStringLiteral("show-banners"));
        return banners.isEmpty() ? std::nullopt : std::optional<bool>(banners == QLatin1String("false"));
    }
    if (name == QLatin1String("system.night_light")) {
        if (host.desktop == QLatin1String("gnome")) {
            const QString enabled = gget(host, Color, QStringLiteral("night-light-enabled"));
            return enabled.isEmpty() ? std::nullopt : std::optional<bool>(enabled == QLatin1String("true"));
        }
        return host.desktop == QLatin1String("kde") ? kdeNightLightOn(host) : std::nullopt;
    }
    if (name == QLatin1String("sound.mute_output") || name == QLatin1String("sound.mute_input")) {
        const std::optional<Level> level = soundLevel(host, name.endsWith(QLatin1String("input")));
        return level ? std::optional<bool>(level->muted) : std::nullopt;
    }
    if (name == QLatin1String("capture.record"))
        return recording(host);
    return std::nullopt;
}

// ---------------------------------------------------------------- the real system

#if !defined(Q_OS_WIN)
namespace {

// Waits for the Response of one portal request, on the thread that made it.
class PortalRequest : public QObject
{
    Q_OBJECT
public:
    QEventLoop loop;
    uint code = 2;
    QVariantMap results;
public Q_SLOTS:
    void response(uint answer, const QVariantMap &values)
    {
        code = answer;
        results = values;
        loop.quit();
    }
};

const QString PortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString PortalPath = QStringLiteral("/org/freedesktop/portal/desktop");

std::optional<QVariantMap> portalCall(const QString &method, bool interactive, QString *error)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        *error = QStringLiteral("no session bus");
        return std::nullopt;
    }
    const QString token = QStringLiteral("galleon") + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    QString handle = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    PortalRequest request;
    const auto subscribe = [&](const QString &path) {
        return bus.connect(PortalService, path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                           &request, SLOT(response(uint,QVariantMap)));
    };
    subscribe(handle);
    QVariantMap options{{QStringLiteral("handle_token"), token}};
    if (method == QLatin1String("Screenshot"))
        options.insert(QStringLiteral("interactive"), interactive);
    QDBusMessage call = QDBusMessage::createMethodCall(PortalService, PortalPath, QStringLiteral("org.freedesktop.portal.Screenshot"), method);
    call << QString() << options;
    const QDBusMessage reply = bus.call(call, QDBus::Block, 10000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        *error = reply.errorMessage();
        return std::nullopt;
    }
    const QString returned = reply.arguments().value(0).value<QDBusObjectPath>().path();
    if (!returned.isEmpty() && returned != handle) { // an old portal: its own path
        bus.disconnect(PortalService, handle, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                       &request, SLOT(response(uint,QVariantMap)));
        handle = returned;
        subscribe(handle);
    }
    QTimer::singleShot(120000, &request.loop, &QEventLoop::quit);
    request.loop.exec();
    if (request.code != 0) {
        *error = request.code == 1 ? QStringLiteral("cancelled") : QStringLiteral("no answer");
        return std::nullopt;
    }
    return request.results;
}

QString portalScreenshot(bool interactive, QString *error)
{
    const std::optional<QVariantMap> results = portalCall(QStringLiteral("Screenshot"), interactive, error);
    if (!results)
        return {};
    const QString file = QUrl(results->value(QStringLiteral("uri")).toString()).toLocalFile();
    if (file.isEmpty())
        *error = QStringLiteral("the portal gave no file");
    return file;
}

std::optional<QColor> portalPickColor(QString *error)
{
    const std::optional<QVariantMap> results = portalCall(QStringLiteral("PickColor"), false, error);
    if (!results)
        return std::nullopt;
    const QDBusArgument argument = results->value(QStringLiteral("color")).value<QDBusArgument>();
    double r = 0, g = 0, b = 0;
    argument.beginStructure();
    argument >> r >> g >> b;
    argument.endStructure();
    return QColor::fromRgbF(static_cast<float>(std::clamp(r, 0.0, 1.0)), static_cast<float>(std::clamp(g, 0.0, 1.0)),
                            static_cast<float>(std::clamp(b, 0.0, 1.0)));
}

bool sessionCall(const QString &service, const QString &path, const QString &interface, const QString &method,
                 const QVariantList &args, QVariant *result)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, interface, method);
    call.setArguments(args);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage)
        return false;
    if (result) {
        QVariant value = reply.arguments().value(0);
        if (value.canConvert<QDBusVariant>())
            value = value.value<QDBusVariant>().variant();
        *result = value;
    }
    return true;
}

} // namespace
#endif

BuiltinActions::Host BuiltinActions::systemHost()
{
    Host host;
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    if (desktop.contains(QLatin1String("kde")))
        host.desktop = QStringLiteral("kde");
    else if (desktop.contains(QLatin1String("gnome")) || desktop.contains(QLatin1String("unity")) || desktop.contains(QLatin1String("ubuntu")))
        host.desktop = QStringLiteral("gnome");
    else
        host.desktop = QStringLiteral("generic");
    host.wayland = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    host.which = [](const QString &program) {
        if (!QStandardPaths::findExecutable(program).isEmpty())
            return true;
        // A user service may not have ~/.local/bin in its PATH.
        return !QStandardPaths::findExecutable(program, {QDir::homePath() + QStringLiteral("/.local/bin")}).isEmpty();
    };
    host.run = [](const QStringList &argv, const QByteArray &input, int timeoutMs, QByteArray *out) {
        QProcess process;
        process.setProgram(argv.value(0));
        process.setArguments(argv.mid(1));
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start();
        if (!process.waitForStarted(3000))
            return -1;
        if (!input.isEmpty())
            process.write(input);
        process.closeWriteChannel();
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(1000);
            return 124;
        }
        if (out)
            *out = process.readAllStandardOutput();
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    };
    host.spawn = [](const QStringList &argv) {
        QString program = argv.value(0);
        if (QStandardPaths::findExecutable(program).isEmpty() && !program.contains(QLatin1Char('/'))) {
            const QString local = QStandardPaths::findExecutable(program, {QDir::homePath() + QStringLiteral("/.local/bin")});
            if (!local.isEmpty())
                program = local;
        }
        return QProcess::startDetached(program, argv.mid(1));
    };
#if defined(Q_OS_WIN)
    host.screenshot = [](bool, QString *error) { *error = QStringLiteral("not supported"); return QString(); };
    host.pickColor = [](QString *error) -> std::optional<QColor> { *error = QStringLiteral("not supported"); return std::nullopt; };
    host.dbus = [](const QString &, const QString &, const QString &, const QString &, const QVariantList &, QVariant *) { return false; };
#else
    host.screenshot = &portalScreenshot;
    host.pickColor = &portalPickColor;
    host.dbus = &sessionCall;
#endif
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    host.picturesDir = (pictures.isEmpty() ? QDir::homePath() + QStringLiteral("/Pictures") : pictures) + QStringLiteral("/Screenshots");
    host.videosDir = (videos.isEmpty() ? QDir::homePath() + QStringLiteral("/Videos") : videos) + QStringLiteral("/Screencasts");
    const QString state = qEnvironmentVariable("XDG_STATE_HOME");
    host.stateDir = state.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/state") : state;
    host.sleep = [](int ms) { QThread::msleep(static_cast<unsigned long>(ms)); };
    return host;
}

BuiltinActions::BuiltinActions(QObject *parent)
    : QObject(parent), m_host(systemHost())
{
}

BuiltinActions::~BuiltinActions()
{
    for (const QPointer<QThread> &thread : std::as_const(m_threads)) {
        if (thread && !thread->wait(3000))
            qWarning() << "galleon-customizerd: an action was still running at exit";
    }
}

void BuiltinActions::inWorker(std::function<void()> work)
{
    if (m_synchronous) {
        work();
        return;
    }
    m_threads.removeAll(nullptr);
    QThread *thread = QThread::create(std::move(work));
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_threads << thread;
    thread->start();
}

QString BuiltinActions::words(const char *text) { return galleond::tr(text); }

void BuiltinActions::run(const QString &name, const QJsonObject &options, int times, const Done &done)
{
#if !defined(Q_OS_WIN)
    if (name == QLatin1String("system.dnd") && m_host.desktop == QLatin1String("kde")) {
        done(kdeDnd(options)); // held by our D-Bus connection: on this thread
        return;
    }
#endif
    // A volume step reads the level and writes it back: two at once would both read the same
    // level and the dial would seem stuck. The detents that come while one runs are added up
    // and applied together once it is done.
    const bool volume = name == QLatin1String("sound.volume") && !m_synchronous;
    if (volume && m_volumeBusy) {
        m_volumePending += times == 0 ? 1 : times;
        m_volumeOptions = options;
        m_volumeDone = done;
        return;
    }
    if (volume)
        m_volumeBusy = true;
    const Host host = m_host;
    QPointer<BuiltinActions> self(this);
    inWorker([this, self, host, name, options, times, done, volume] {
        const BuiltinResult result = execute(host, name, options, times);
        if (m_synchronous) {
            done(result);
            return;
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, done, result, volume, name] {
            if (!self)
                return;
            done(result);
            if (!volume)
                return;
            self->m_volumeBusy = false;
            if (self->m_volumePending != 0) {
                const int pending = std::exchange(self->m_volumePending, 0);
                self->run(name, self->m_volumeOptions, pending, self->m_volumeDone);
            }
        }, Qt::QueuedConnection);
    });
}

void BuiltinActions::readState(const QString &name, const std::function<void(std::optional<bool>)> &done)
{
    if (name == QLatin1String("system.dnd") && m_host.desktop == QLatin1String("kde")) {
        done(m_kdeDndCookie.has_value());
        return;
    }
    const Host host = m_host;
    QPointer<BuiltinActions> self(this);
    inWorker([this, self, host, name, done] {
        const std::optional<bool> state = queryState(host, name);
        if (m_synchronous) {
            done(state);
            return;
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, done, state] {
            if (self)
                done(state);
        }, Qt::QueuedConnection);
    });
}

void BuiltinActions::notify(const QString &title, const QString &body)
{
    const QString service = QStringLiteral("org.freedesktop.Notifications");
    m_host.dbus(service, QStringLiteral("/org/freedesktop/Notifications"), service, QStringLiteral("Notify"),
                {QStringLiteral("Galleon Customizer"), QVariant::fromValue(0U), QStringLiteral("input-keyboard"), title, body,
                 QStringList(), QVariantMap(), QVariant::fromValue(5000)}, nullptr);
}

BuiltinResult BuiltinActions::kdeDnd(const QJsonObject &options)
{
    const bool want = toggled(m_kdeDndCookie.has_value(), modeOf(options));
    const QString service = QStringLiteral("org.freedesktop.Notifications");
    const QString path = QStringLiteral("/org/freedesktop/Notifications");
    if (want && !m_kdeDndCookie) {
        QVariant cookie;
        if (!m_host.dbus(service, path, service, QStringLiteral("Inhibit"),
                         {QStringLiteral("galleon-customizer"), tr("Do not disturb"), QVariantMap()}, &cookie))
            return BuiltinResult::failed(tr("Could not change do not disturb"));
        m_kdeDndCookie = cookie.toUInt();
    } else if (!want && m_kdeDndCookie) {
        m_host.dbus(service, path, service, QStringLiteral("UnInhibit"), {QVariant::fromValue(*m_kdeDndCookie)}, nullptr);
        m_kdeDndCookie.reset();
    }
    BuiltinResult r;
    r.state = want;
    r.message = tr("Do not disturb") + QStringLiteral(": ") + (want ? tr("on") : tr("off"));
    return r;
}

}

#if !defined(Q_OS_WIN)
#include "BuiltinActions.moc"
#endif
