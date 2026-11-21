// SPDX-License-Identifier: GPL-3.0-or-later
#include <BuiltinActions.h>

#include <QFile>
#include <QScopeGuard>
#include <QSettings>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace galleond;

namespace {

// A desktop that answers from a script, and remembers what was asked.
struct FakeSystem
{
    QSet<QString> installed;
    QHash<QString, QByteArray> answers; // "program arg..." -> stdout
    QHash<QString, int> codes;          // "program arg..." -> exit code (default 0)
    QStringList ran, spawned, dbus;
    QByteArray clipboard;
    QString shotFile;
    std::optional<QColor> color;

    BuiltinActions::Host host(const QString &desktop = QStringLiteral("gnome"))
    {
        BuiltinActions::Host h;
        h.desktop = desktop;
        h.wayland = true;
        h.which = [this](const QString &program) { return installed.contains(program); };
        h.run = [this](const QStringList &argv, const QByteArray &input, int, QByteArray *out) {
            const QString line = argv.join(QLatin1Char(' '));
            ran << line;
            if (argv.value(0) == QLatin1String("wl-copy")) {
                clipboard = input;
                return 0;
            }
            if (argv.value(0) == QLatin1String("wl-paste")) {
                if (out)
                    *out = clipboard;
                return 0;
            }
            if (argv.mid(0, 3) == QStringList{QStringLiteral("gsettings"), QStringLiteral("set"), argv.value(2)} && argv.size() == 5)
                answers.insert(QStringLiteral("gsettings get %1 %2").arg(argv.at(2), argv.at(3)), argv.at(4).toUtf8());
            if (out)
                *out = answers.value(line);
            return codes.value(line, 0);
        };
        h.spawn = [this](const QStringList &argv) { spawned << argv.join(QLatin1Char(' ')); return true; };
        h.screenshot = [this](bool interactive, QString *error) {
            ran << QStringLiteral("portal screenshot %1").arg(interactive ? QStringLiteral("area") : QStringLiteral("full"));
            if (shotFile.isEmpty())
                *error = QStringLiteral("cancelled");
            return shotFile;
        };
        h.pickColor = [this](QString *error) {
            if (!color)
                *error = QStringLiteral("cancelled");
            return color;
        };
        h.dbus = [this](const QString &service, const QString &, const QString &, const QString &method, const QVariantList &, QVariant *reply) {
            dbus << service + QLatin1Char('.') + method;
            if (reply)
                *reply = 7U;
            return service == QLatin1String("org.freedesktop.ScreenSaver") || method == QLatin1String("Inhibit") || method == QLatin1String("UnInhibit");
        };
        h.sleep = [this](int ms) { ran << QStringLiteral("sleep %1").arg(ms); };
        return h;
    }
};

BuiltinResult act(FakeSystem &system, const QString &name, const QJsonObject &options = {}, int times = 1,
                  const QString &desktop = QStringLiteral("gnome"))
{
    return BuiltinActions::execute(system.host(desktop), name, options, times);
}

} // namespace

class TestBuiltinActions : public QObject
{
    Q_OBJECT
    QTemporaryDir m_config;

private Q_SLOTS:
    void initTestCase()
    {
        QLocale::setDefault(QLocale(QLocale::English));
        QVERIFY(m_config.isValid());
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    }

    void theWordsFollowTheChosenLanguage()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        const auto restore = qScopeGuard([this] { qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8()); });
        FakeSystem system;
        system.installed = {QStringLiteral("wpctl")};
        QCOMPARE(act(system, QStringLiteral("sound.set_volume"), {{"percent", 20}}).message, QStringLiteral("Volume 20%"));
        {
            QSettings settings(config.filePath(QStringLiteral("galleon-customizer/bosun.conf")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("interface/language"), QStringLiteral("vi"));
        }
        QCOMPARE(act(system, QStringLiteral("sound.set_volume"), {{"percent", 20}}).message, QStringLiteral("Âm lượng 20%"));
    }

    void theCatalogChecksOptions()
    {
        const auto check = [](const QString &name, const QJsonObject &options) {
            const BuiltinActions::Spec *spec = BuiltinActions::find(name);
            return spec ? BuiltinActions::checkOptions(*spec, options) : QStringLiteral("no such action");
        };
        QVERIFY(check(QStringLiteral("system.lock"), {}).isEmpty());
        QVERIFY(!check(QStringLiteral("system.lock"), {{"mode", "on"}}).isEmpty());
        QVERIFY(check(QStringLiteral("system.dark_mode"), {{"mode", "on"}}).isEmpty());
        QVERIFY(!check(QStringLiteral("system.dark_mode"), {{"mode", "maybe"}}).isEmpty());
        QVERIFY(check(QStringLiteral("sound.set_volume"), {{"percent", 40}}).isEmpty());
        QVERIFY(!check(QStringLiteral("sound.set_volume"), {}).isEmpty());
        QVERIFY(!check(QStringLiteral("sound.set_volume"), {{"percent", 140}}).isEmpty());
        QVERIFY(!check(QStringLiteral("sound.set_volume"), {{"percent", 4.5}}).isEmpty());
        QVERIFY(!check(QStringLiteral("sound.volume"), {{"step", 0}}).isEmpty());
        QVERIFY(check(QStringLiteral("clipboard.search"), {{"engine_url", "https://duckduckgo.com/?q={q}"}}).isEmpty());
        QVERIFY(!check(QStringLiteral("clipboard.search"), {{"engine_url", "javascript:{q}"}}).isEmpty());
        QVERIFY(!check(QStringLiteral("clipboard.translate"), {{"target", "vi; rm"}}).isEmpty());
        QVERIFY(!check(QStringLiteral("capture.ocr"), {{"languages", "eng -c x"}}).isEmpty());
        QVERIFY(check(QStringLiteral("capture.screenshot"), {{"mode", "delay"}, {"delay", 5}}).isEmpty());
        QVERIFY(BuiltinActions::find(QStringLiteral("system.nothing")) == nullptr);
        QVERIFY(BuiltinActions::find(QStringLiteral("sound.volume"))->turn);
        QVERIFY(BuiltinActions::find(QStringLiteral("system.dnd"))->stateful);
    }

    void darkModeDndAndNightLightSwitchOnGnome()
    {
        FakeSystem system;
        system.installed = {QStringLiteral("gsettings")};
        system.answers.insert(QStringLiteral("gsettings get org.gnome.desktop.interface color-scheme"), "'default'");
        system.answers.insert(QStringLiteral("gsettings get org.gnome.desktop.interface gtk-theme"), "'Adwaita'");
        BuiltinResult r = act(system, QStringLiteral("system.dark_mode"));
        QVERIFY(r.ok);
        QCOMPARE(r.state, std::optional<bool>(true));
        QCOMPARE(r.message, QStringLiteral("Dark mode"));
        QVERIFY(system.ran.contains(QStringLiteral("gsettings set org.gnome.desktop.interface color-scheme prefer-dark")));
        QVERIFY(system.ran.contains(QStringLiteral("gsettings set org.gnome.desktop.interface gtk-theme Adwaita-dark")));
        r = act(system, QStringLiteral("system.dark_mode"), {{"mode", "on"}}); // already dark: stays dark
        QCOMPARE(r.state, std::optional<bool>(true));
        QCOMPARE(BuiltinActions::queryState(system.host(), QStringLiteral("system.dark_mode")), std::optional<bool>(true));

        system.answers.insert(QStringLiteral("gsettings get org.gnome.desktop.notifications show-banners"), "true");
        r = act(system, QStringLiteral("system.dnd"));
        QCOMPARE(r.state, std::optional<bool>(true)); // banners off
        QVERIFY(system.ran.contains(QStringLiteral("gsettings set org.gnome.desktop.notifications show-banners false")));
        QCOMPARE(r.message, QStringLiteral("Do not disturb: on"));

        system.answers.insert(QStringLiteral("gsettings get org.gnome.settings-daemon.plugins.color night-light-enabled"), "true");
        r = act(system, QStringLiteral("system.night_light"));
        QCOMPARE(r.state, std::optional<bool>(false));
        QVERIFY(system.ran.contains(QStringLiteral("gsettings set org.gnome.settings-daemon.plugins.color night-light-enabled false")));

        QVERIFY(!act(system, QStringLiteral("system.dark_mode"), {}, 1, QStringLiteral("generic")).ok);
    }

    void lockSettingsAndTerminal()
    {
        FakeSystem system;
        QVERIFY(act(system, QStringLiteral("system.lock")).ok);
        QCOMPARE(system.dbus, QStringList{QStringLiteral("org.freedesktop.ScreenSaver.Lock")});

        system.installed = {QStringLiteral("gnome-control-center"), QStringLiteral("ptyxis")};
        QVERIFY(act(system, QStringLiteral("system.settings")).ok);
        QVERIFY(act(system, QStringLiteral("system.terminal"), {{"command", "htop"}}).ok);
        QCOMPARE(system.spawned, (QStringList{QStringLiteral("gnome-control-center"), QStringLiteral("ptyxis -- sh -c htop")}));
        const BuiltinResult none = act(system, QStringLiteral("system.monitor"));
        QVERIFY(!none.ok);
        QVERIFY(none.message.contains(QLatin1String("not installed")));
    }

    void soundThroughWpctl()
    {
        FakeSystem system;
        system.installed = {QStringLiteral("wpctl")};
        system.answers.insert(QStringLiteral("wpctl get-volume @DEFAULT_AUDIO_SINK@"), "Volume: 0.40");
        BuiltinResult r = act(system, QStringLiteral("sound.volume"), {{"step", 5}}, -2);
        QVERIFY(r.ok);
        QCOMPARE(r.message, QStringLiteral("Volume 30%"));
        QVERIFY(system.ran.contains(QStringLiteral("wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 30%")));
        r = act(system, QStringLiteral("sound.set_volume"), {{"percent", 100}});
        QCOMPARE(r.message, QStringLiteral("Volume 100%"));

        system.answers.insert(QStringLiteral("wpctl get-volume @DEFAULT_AUDIO_SOURCE@"), "Volume: 1.00 [MUTED]");
        r = act(system, QStringLiteral("sound.mute_input"));
        QVERIFY(system.ran.contains(QStringLiteral("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle")));
        QCOMPARE(r.state, std::optional<bool>(true));
        QCOMPARE(r.message, QStringLiteral("Microphone: muted"));

        FakeSystem silent;
        QVERIFY(!act(silent, QStringLiteral("sound.mute_output")).ok);
    }

    void clipboardHelpers()
    {
        FakeSystem system;
        system.installed = {QStringLiteral("wl-copy"), QStringLiteral("wl-paste")};
        BuiltinResult r = act(system, QStringLiteral("clipboard.password"), {{"length", 32}});
        QVERIFY(r.ok);
        QCOMPARE(system.clipboard.size(), 32);
        QVERIFY(!r.message.contains(QString::fromUtf8(system.clipboard))); // never shown

        r = act(system, QStringLiteral("clipboard.datetime"), {{"format", "yyyy"}});
        QCOMPARE(QString::fromUtf8(system.clipboard), QString::number(QDate::currentDate().year()));
        QVERIFY(r.paste);

        system.clipboard = "  hello world ";
        r = act(system, QStringLiteral("clipboard.search"));
        QCOMPARE(r.open, QUrl(QStringLiteral("https://www.google.com/search?q=hello%20world")));
        r = act(system, QStringLiteral("clipboard.translate"), {{"target", "en"}});
        QCOMPARE(r.open.query(QUrl::FullyEncoded), QStringLiteral("sl=auto&tl=en&text=hello%20world"));
        QVERIFY(!act(system, QStringLiteral("clipboard.open_link")).ok);
        system.clipboard = "www.example.org/page";
        QCOMPARE(act(system, QStringLiteral("clipboard.open_link")).open, QUrl(QStringLiteral("https://www.example.org/page")));

        QVERIFY(act(system, QStringLiteral("clipboard.clear")).ok);
        QVERIFY(system.clipboard.isEmpty());
        QVERIFY(!act(system, QStringLiteral("clipboard.search")).ok); // empty

        FakeSystem none;
        QVERIFY(!act(none, QStringLiteral("clipboard.clear")).ok);
    }

    void captureActions()
    {
        QTemporaryDir dir;
        FakeSystem system;
        system.installed = {QStringLiteral("wl-copy")};
        BuiltinActions::Host host = system.host();
        host.picturesDir = dir.filePath(QStringLiteral("Pictures/Screenshots"));
        host.videosDir = dir.filePath(QStringLiteral("Videos/Screencasts"));
        host.stateDir = dir.filePath(QStringLiteral("state"));

        system.shotFile = dir.filePath(QStringLiteral("portal.png"));
        QFile shot(system.shotFile);
        QVERIFY(shot.open(QIODevice::WriteOnly));
        shot.write("PNG");
        shot.close();
        BuiltinResult r = BuiltinActions::execute(host, QStringLiteral("capture.screenshot"), {{"mode", "delay"}, {"delay", 2}}, 1);
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(system.ran.contains(QStringLiteral("sleep 2000")));
        QVERIFY(r.notifyBody.startsWith(host.picturesDir + QStringLiteral("/Screenshot-")));
        QVERIFY(QFile::exists(r.notifyBody));
        QCOMPARE(system.clipboard, QByteArray("PNG"));

        system.shotFile.clear();
        r = BuiltinActions::execute(host, QStringLiteral("capture.screenshot"), {}, 1);
        QVERIFY(!r.ok);
        QVERIFY(r.message.contains(QLatin1String("cancelled")));

        system.installed << QStringLiteral("flameshot");
        r = BuiltinActions::execute(host, QStringLiteral("capture.screenshot"), {{"mode", "area"}}, 1);
        QVERIFY(r.ok);
        QCOMPARE(system.spawned.last(), QStringLiteral("flameshot gui -p %1 -c").arg(host.picturesDir));

        system.color = QColor(255, 128, 0);
        r = BuiltinActions::execute(host, QStringLiteral("capture.color_picker"), {}, 1);
        QCOMPARE(r.message, QStringLiteral("#FF8000"));
        QCOMPARE(system.clipboard, QByteArray("#FF8000"));

        r = BuiltinActions::execute(host, QStringLiteral("capture.open_folder"), {{"which", "videos"}}, 1);
        QCOMPARE(r.open, QUrl::fromLocalFile(host.videosDir));

        QVERIFY(!BuiltinActions::execute(host, QStringLiteral("capture.ocr"), {}, 1).ok); // no tesseract
    }

    void kdeDoNotDisturbIsHeldByTheDaemon()
    {
        FakeSystem system;
        BuiltinActions actions;
        actions.setHost(system.host(QStringLiteral("kde")));
        actions.setSynchronous(true);
        std::optional<BuiltinResult> result;
        actions.run(QStringLiteral("system.dnd"), {}, 1, [&](const BuiltinResult &r) { result = r; });
        QVERIFY(result && result->ok);
        QCOMPARE(result->state, std::optional<bool>(true));
        actions.run(QStringLiteral("system.dnd"), {}, 1, [&](const BuiltinResult &r) { result = r; });
        QCOMPARE(result->state, std::optional<bool>(false));
        QCOMPARE(system.dbus, (QStringList{QStringLiteral("org.freedesktop.Notifications.Inhibit"), QStringLiteral("org.freedesktop.Notifications.UnInhibit")}));
    }

    void theWorkRunsOffTheCallingThread()
    {
        FakeSystem system;
        BuiltinActions actions;
        BuiltinActions::Host host = system.host();
        QThread *worker = nullptr;
        host.dbus = [&worker](const QString &, const QString &, const QString &, const QString &, const QVariantList &, QVariant *) {
            worker = QThread::currentThread();
            return true;
        };
        actions.setHost(host);
        bool done = false;
        QThread *answeredOn = nullptr;
        actions.run(QStringLiteral("system.lock"), {}, 1, [&](const BuiltinResult &) { done = true; answeredOn = QThread::currentThread(); });
        QTRY_VERIFY(done);
        QVERIFY(worker != nullptr && worker != QThread::currentThread());
        QCOMPARE(answeredOn, QThread::currentThread());
    }
};

QTEST_GUILESS_MAIN(TestBuiltinActions)
#include "test_builtinactions.moc"
