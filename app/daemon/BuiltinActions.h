// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <functional>
#include <optional>

namespace galleond {

// What a ready-made action did.
struct BuiltinResult
{
    bool ok = true;
    QString message;           // a few words to show (the strip), may be empty
    std::optional<bool> state; // an on/off action: the state now
    QUrl open;                 // to open on the main thread (a link, a folder)
    bool paste = false;        // press ctrl+v once the clipboard is set
    QString notifyTitle, notifyBody; // a desktop notification, if any

    static BuiltinResult failed(const QString &why) { BuiltinResult r; r.ok = false; r.message = why; return r; }
};

// The ready-made actions of the Python Galleon Customizer: system.*, sound.*, clipboard.*
// and capture.*. Each is named by its "type" in the pages file; its options are the other
// members of the same JSON object. The work runs on a worker thread (tools, D-Bus, portal),
// and the result comes back on the thread of this object.
class BuiltinActions : public QObject
{
    Q_OBJECT
public:
    struct Option
    {
        enum Kind : std::uint8_t { Bool, Int, String, Choice };
        QString key;
        Kind kind = Bool;
        int min = 0, max = 0;     // Int: the range; String: max = the longest
        QStringList choices;      // Choice
    };
    struct Spec
    {
        QString name;
        QList<Option> options;
        bool turn = false;     // also a dial turn (the volume)
        bool stateful = false; // on/off: the key shows its state
    };
    [[nodiscard]] static const QList<Spec> &catalog();
    [[nodiscard]] static const Spec *find(const QString &name);
    // What is wrong with the options, or an empty string.
    [[nodiscard]] static QString checkOptions(const Spec &spec, const QJsonObject &options);

    // How the actions reach the system. Every function is called on the worker thread.
    struct Host
    {
        QString desktop; // "gnome", "kde" or "generic"
        bool wayland = false;
        std::function<bool(const QString &program)> which;
        // Runs and waits: the exit code, -1 if it could not start, 124 when it took too long.
        std::function<int(const QStringList &argv, const QByteArray &input, int timeoutMs, QByteArray *out)> run;
        std::function<bool(const QStringList &argv)> spawn;
        // A file written by the Screenshot portal, or empty with *error set.
        std::function<QString(bool interactive, QString *error)> screenshot;
        std::function<std::optional<QColor>(QString *error)> pickColor;
        // A D-Bus method of the session bus: true if it answered without an error.
        std::function<bool(const QString &service, const QString &path, const QString &interface,
                           const QString &method, const QVariantList &args, QVariant *reply)> dbus;
        QString picturesDir, videosDir, stateDir; // Screenshots, Screencasts, ~/.local/state
        std::function<void(int ms)> sleep;
    };
    [[nodiscard]] static Host systemHost();
    // A text of these actions in the chosen language (the screen compares messages with it).
    [[nodiscard]] static QString words(const char *text);

    explicit BuiltinActions(QObject *parent = nullptr);
    ~BuiltinActions() override;
    void setHost(Host host) { m_host = std::move(host); }
    // Tests: the work runs on the calling thread, and `done` before run() returns.
    void setSynchronous(bool synchronous) { m_synchronous = synchronous; }

    using Done = std::function<void(const BuiltinResult &)>;
    // `times`: dial detents for a turn (signed), 1 otherwise.
    void run(const QString &name, const QJsonObject &options, int times, const Done &done);
    // The state of an on/off action, read without changing it; nullopt when unknown.
    void readState(const QString &name, const std::function<void(std::optional<bool>)> &done);

    // A desktop notification (org.freedesktop.Notifications), on this thread.
    void notify(const QString &title, const QString &body);

    // The work itself, on the current thread.
    [[nodiscard]] static BuiltinResult execute(const Host &host, const QString &name, const QJsonObject &options, int times);
    [[nodiscard]] static std::optional<bool> queryState(const Host &host, const QString &name);

private:
    void inWorker(std::function<void()> work);
    BuiltinResult kdeDnd(const QJsonObject &options);
    Host m_host;
    bool m_synchronous = false;
    QList<QPointer<QThread>> m_threads;
    std::optional<uint> m_kdeDndCookie;
    bool m_volumeBusy = false; // a sound.volume step is running
    int m_volumePending = 0;   // detents turned meanwhile
    QJsonObject m_volumeOptions;
    Done m_volumeDone;
};

}
