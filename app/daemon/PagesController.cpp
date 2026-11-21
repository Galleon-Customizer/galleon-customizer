// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <InitialPages.h>

#include "PagesController.h"

#include "PageOrder.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include <galleon/StreamDeck.h>

#include <QDebug>
#include <QPainter>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileDevice>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include "FileTrust.h"

namespace galleond {

namespace {

constexpr int ReloadDelayMs = 250; // editors write a file in several steps
constexpr int MinLaunchIntervalMs = 700; // a double press must not start an application twice
#ifndef Q_OS_WIN
constexpr int MaxLookedUpApps = 64;
#endif

bool isMediaPresent(MediaController::State state)
{
    return state == MediaController::State::Playing || state == MediaController::State::Paused;
}
constexpr int ThemeApplyMs = 30; // rest between two themes drawn: a theme takes 13 pictures to show
constexpr int ModeTimeoutMs = 30000; // back to the player control when left alone
constexpr std::uint32_t WallpaperModeRgb = 0xff00ff; // magenta

} // namespace

PagesController::PagesController(galleon::StreamDeck *deck, MediaController *media, TileRenderer tiles, QObject *parent)
    : QObject(parent)
    , m_deck(deck)
    , m_media(media)
    , m_tiles(std::move(tiles))
    , m_mediaPresent(isMediaPresent(m_media->state())), m_launcher([](const DesktopApp &app) { return DesktopEntries::launch(app); })
    , m_steamLauncher([](int appid) {
        // A fixed program and one argument made of a whole number: nothing from outside goes in.
#ifdef Q_OS_WIN
        return QDesktopServices::openUrl(QUrl(QStringLiteral("steam://rungameid/%1").arg(appid)));
#else
        const QString steam = QStandardPaths::findExecutable(QStringLiteral("steam"));
        return !steam.isEmpty()
               && QProcess::startDetached(steam, {QStringLiteral("steam://rungameid/%1").arg(appid)});
#endif
    })
    , m_sourceProvider([](const QString &menuCategory) {
        ArrangeSources sources;
        sources.installedSteam = SteamLibrary::installedGames();
        sources.playedSteam = SteamLibrary::playedGames();
        if (!menuCategory.isEmpty()) {
            sources.menu = DesktopEntries::listCategory(menuCategory);
            // The Steam game behind each shortcut of the whole menu, to score it by play time.
            for (const DesktopApp &app : DesktopEntries::list()) {
                if (app.steamAppId > 0)
                    sources.steamOfApp.insert(app.id, app.steamAppId);
            }
        }
        return sources;
    })
{
    m_opener = [](const QUrl &url) { return QDesktopServices::openUrl(url); };
    m_commandRunner = &PagesController::runCommandLine;
    m_statusReader = &PagesController::readStatusCommand;
    m_builtins = new BuiltinActions(this);
    m_togglePoll.setInterval(5000); // the state a toggle shows follows its status command
    connect(&m_togglePoll, &QTimer::timeout, this, &PagesController::pollToggles);
    m_togglePoll.start();
#ifdef Q_OS_WIN
    m_launcherCache = std::make_unique<LauncherCache>();
    connect(m_launcherCache.get(), &LauncherCache::ready, this, [this](const QString &reference) {
        if (m_suspended)
            return;
        for (std::size_t index = 0; index < m_dirty.size(); ++index) {
            const auto *key = keyConfig(static_cast<int>(index));
            if (key && ((key->action.type == ActionType::App && key->action.desktop == reference)
                || (key->action.type == ActionType::Steam && reference == QStringLiteral("steam:%1").arg(key->action.appid))))
                m_dirty.at(index) = true;
        }
        if (!m_drawTimer.isActive() && std::ranges::any_of(m_dirty, [](bool dirty) { return dirty; }))
            m_drawTimer.start();
    });
    m_drawTimer.setSingleShot(true);
    m_drawTimer.setInterval(1);
    connect(&m_drawTimer, &QTimer::timeout, this, [this] {
        if (m_suspended)
            return;
        for (std::size_t key = 0; key < m_dirty.size(); ++key) {
            if (!m_dirty.at(key))
                continue;
            m_dirty.at(key) = false;
            const bool force = std::exchange(m_forceDraw.at(key), false);
            drawKey(static_cast<int>(key), force);
            break; // Yield to keep-alive/input between keys, not after an entire page.
        }
        if (std::ranges::any_of(m_dirty, [](bool dirty) { return dirty; }))
            m_drawTimer.start();
    });
#endif
    m_clock.start();
    m_lastPressAt.fill(-static_cast<qint64>(ClickAfterMs) * 4);
    
    // The built-in pages: always valid, so there is always something to show.
    if (const auto fallback = parsePagesConfig(initialPagesJson()); fallback.config)
        setConfig(*fallback.config);

    connect(m_deck, &galleon::StreamDeck::keyPressed, this, &PagesController::onKeyPressed);
    connect(m_deck, &galleon::StreamDeck::keyReleased, this, &PagesController::onKeyReleased);
    connect(m_deck, &galleon::StreamDeck::dialRotated, this, &PagesController::onDialRotated);
    connect(m_deck, &galleon::StreamDeck::dialPressed, this, &PagesController::onDialPressed);
    connect(m_deck, &galleon::StreamDeck::dialReleased, this, &PagesController::onDialReleased);
    connect(m_media, &MediaController::stateChanged, this, [this] {
        onMediaChanged();
        redraw();
    });

    m_wallpaperTimeout.setInterval(1000);
    connect(&m_wallpaperTimeout, &QTimer::timeout, this, [this] {
        if (m_gameArtworkPending) return; // Never validate an image before it can be previewed.
        // The countdown: the dial stays in the choice, magenta, until it is over.
        if (m_wallpaperLeft > 1) {
            --m_wallpaperLeft;
            qInfo() << "galleon-customizerd: validation in" << m_wallpaperLeft << "s";
            Q_EMIT wallpaperCountdown(m_wallpaperLeft);
        } else {
            m_wallpaperTimeout.stop();
            setWallpaperMode(-1); // validated: the ring goes white and the choice is closed
        }
    });
    // If the screen never says the label is shown, the turns count after a moment all the same.
    m_wallpaperReadyFallback.setSingleShot(true);
    m_wallpaperReadyFallback.setInterval(1500);
    connect(&m_wallpaperReadyFallback, &QTimer::timeout, this, [this] {
        m_wallpaperReady = true;
        m_wallpaperBusy = false;
        m_lastWallpaperStepAt = m_clock.elapsed();
    });

    m_themeApply.setSingleShot(true);
    m_themeApply.setInterval(ThemeApplyMs);
    connect(&m_themeApply, &QTimer::timeout, this, [this] {
        if (!m_themePending)
            return;
        applyTheme();
        m_themeApply.start(); // rest again before the next one
    });
    m_themeTimeout.setSingleShot(true);
    m_themeTimeout.setInterval(ModeTimeoutMs);
    connect(&m_themeTimeout, &QTimer::timeout, this, [this] { setThemeMode(-1); });

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(ReloadDelayMs);
    connect(&m_reloadTimer, &QTimer::timeout, this, &PagesController::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
}

void PagesController::setRecordingController(RecordingController *recording)
{
    if (m_recording) disconnect(m_recording, nullptr, this, nullptr);
    m_recording = recording;
    if (recording) connect(recording, &RecordingController::changed, this, [this] {
        if (currentPage() == m_recordingPage) redraw();
    });
    if (m_sourceConfig) {
        const auto source = *m_sourceConfig;
        setConfig(source);
    }
}

bool PagesController::showGaming()
{
    if (!m_gameActive || !m_recording || m_suspended || !m_deck->isOpen()) return false;
    for (std::size_t i = 0; i < m_config.themes.size(); ++i) {
        if (m_config.themes[i].page == m_recordingPage) {
            m_themeApply.stop();
            m_themePending = false;
            m_themeTimeout.stop();
            m_themeDial = -1;
            setWallpaperMode(-1);
            m_sent.fill(QImage());
            selectTheme(static_cast<int>(i));
            Q_EMIT themeModeChanged();
            return true;
        }
    }
    return false;
}

void PagesController::addGamingPage()
{
    if (!m_gameActive || !m_recording || !m_recordingPage.isEmpty()) return;
    QString name = QStringLiteral("_galleon_gaming");
    while (m_config.pages.contains(name)) name += QLatin1Char('_');
    m_recordingPage = name;
    Page gaming;
    for (int i = 0; i < 4; ++i) {
        KeyConfig key; key.key = i;
        gaming.keys.at(static_cast<std::size_t>(i)) = key;
    }
    m_config.pages.insert(name, gaming);
    m_config.themes.push_back(Theme{.name = QStringLiteral("Gaming"), .page = name, .rgb = 0x80e5d1, .when = ThemeTrigger::None});
}

void PagesController::setGameActive(bool active)
{
    if (active == m_gameActive) return;
    m_gameActive = active;
    if (active) {
        m_beforeGameTheme = m_theme;
        m_beforeGameStack = m_stack;
        addGamingPage();
        showGaming();
        return;
    }
    if (m_recordingPage.isEmpty()) return;
    const bool restore = currentPage() == m_recordingPage;
    m_themeApply.stop();
    m_themePending = false;
    const QString removedPage = m_recordingPage;
    // This runtime theme was appended, so removing it leaves all ordinary indices stable.
    std::erase_if(m_config.themes, [&removedPage](const Theme &theme) { return theme.page == removedPage; });
    m_config.pages.remove(removedPage);
    m_recordingPage.clear();
    if (restore) {
        const bool validPrevious = m_beforeGameTheme >= 0 && std::cmp_less(m_beforeGameTheme, m_config.themes.size());
        if (validPrevious || !m_config.themes.empty()) {
            selectTheme(validPrevious ? m_beforeGameTheme : 0);
            if (m_beforeGameStack.size() > 1 && std::ranges::all_of(m_beforeGameStack,
                [this](const QString &name) { return m_config.pages.contains(name); })) {
                m_stack = m_beforeGameStack;
                rebuildPage();
                redraw();
            }
        } else {
            m_theme = -1;
            m_stack = {m_config.home};
            rebuildPage();
            redraw();
        }
    } else {
        // A manually selected ordinary page stays selected, but Gaming disappears everywhere.
        applyTheme();
    }
    m_beforeGameStack.clear();
}

void PagesController::setConfig(const PagesConfig &config)
{
    ++m_generation; // steps of a multi-action still waiting are dropped
    m_longPressGen.fill(0);
    m_longPressFired.fill(false);
    m_apps.clear();
#ifdef Q_OS_WIN
    m_launcherCache->clear();
    m_drawTimer.stop();
    m_dirty.fill(false);
    m_forceDraw.fill(false);
#endif
    const bool screenChanged = !(config.screen == m_config.screen);
    const bool saverChanged = !(config.screensaver == m_config.screensaver);
    m_sourceConfig = config;
    m_config = config;
    m_recordingPage.clear();
    addGamingPage();
    releaseHeldKeys();
    m_stack = {config.home};
    m_theme = -1;
    m_sourcesAge.invalidate();
    for (std::size_t i = 0; i < config.themes.size(); ++i) {
        if (config.themes[i].page == config.home) {
            m_theme = static_cast<int>(i);
            break;
        }
    }
    // A theme tied to the player's presence wins over the home page.
    if (const int automatic = themeFor(isMediaPresent(m_media->state()) ? ThemeTrigger::Media : ThemeTrigger::NoMedia);
        automatic >= 0) {
        m_theme = automatic;
        m_stack = {config.themes[static_cast<std::size_t>(automatic)].page};
    }
    if (m_injector && hasKeyActions(config))
        m_injector->prepare();
    rebuildPage();
    m_pressed.fill(false);
    m_wallpaperDial = -1; // a new file: back to the player control
    m_wallpaperTimeout.stop();
    Q_EMIT wallpaperCountdown(0);
    m_themeDial = -1;
    m_themeTimeout.stop();
    redraw(true);
    applyRings();
    Q_EMIT themeChanged();
    Q_EMIT wallpaperModeChanged();
    Q_EMIT themeModeChanged();
    if (screenChanged)
        Q_EMIT screenConfigChanged();
    if (saverChanged)
        Q_EMIT screensaverConfigChanged();
}

void PagesController::loadFile(const QString &path)
{
    m_file = path;
    reload();
}

void PagesController::watch(const QString &path)
{
    // A file that an editor replaces drops out of the watch list: add it again
    // after each change, and watch its folder to see it appear.
    const QFileInfo info(path);
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (info.exists() && !watched.contains(info.absoluteFilePath()))
        m_watcher.addPath(info.absoluteFilePath());
    if (QDir(info.absolutePath()).exists() && !watched.contains(info.absolutePath()))
        m_watcher.addPath(info.absolutePath());
}

void PagesController::reload()
{
    if (m_file.isEmpty())
        return;
    watch(m_file);

    QFile file(m_file);
    if (!file.exists()) {
        qInfo() << "galleon-customizerd: no pages file at" << m_file << "- the built-in pages are used";
        return;
    }
    // Read one byte more than the limit: parsePagesConfig refuses a longer file.
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "galleon-customizerd: cannot read the pages file" << m_file << file.errorString();
        return;
    }
    // Applications are started from this file, so it must be the user's own and
    // not writable by anyone else (as for a shell startup file).
    const bool owned = isTrustedPagesFile(file);
    if (!owned && m_appsAllowed)
        qWarning() << "galleon-customizerd: the pages file" << m_file << "is not owned by you or is writable by others: its application keys are disabled";
    m_appsAllowed = owned;
    const QByteArray bytes = preparePagesJson(file.read(MaxPagesFileBytes + 1));
    const PagesParseResult result = parsePagesConfig(bytes);
    if (!result.config) {
        qWarning() << "galleon-customizerd: the pages file" << m_file << "has errors and is not used:";
        for (const QString &line : result.errors)
            qWarning().noquote() << "   " << line;
        return;
    }
    if (m_sourceConfig && *result.config == *m_sourceConfig)
        return;
    qInfo() << "galleon-customizerd: pages loaded from" << m_file;
    setConfig(*result.config);
}

const Page *PagesController::effectivePage() const
{
    if (m_dynamic && m_dynamicFor == currentPage())
        return &*m_dynamic;
    const auto page = m_config.pages.constFind(currentPage());
    return page == m_config.pages.constEnd() ? nullptr : &*page;
}

const KeyConfig *PagesController::keyConfig(int key) const
{
    const Page *page = effectivePage();
    if (!page || key < 0 || key >= galleon::protocol::KeyCount)
        return nullptr;
    const auto &configured = page->keys.at(static_cast<std::size_t>(key));
    return configured ? &*configured : nullptr;
}

void PagesController::setUsageFile(const QString &file)
{
    m_usage = std::make_unique<UsageStats>(file);
    rebuildPage();
}

void PagesController::recordUse(const QString &id)
{
    m_usage->record(id);
    rebuildPage(); // the most used may have changed
    redraw();
}

void PagesController::rebuildPage()
{
#ifdef Q_OS_WIN
    m_launcherCache->cancelPending();
#endif
    m_dynamic.reset();
    m_highlightKey = -1;
    const auto found = m_config.pages.constFind(currentPage());
    if (found == m_config.pages.constEnd()
        || (found->sort == PageSort::None && found->steamMax == 0 && found->menuMax == 0))
        return;

    // Steam's files and the desktop menu are read when a page needs them, then kept for five minutes.
    const bool needsSources = found->steamMax > 0 || found->menuMax > 0 || found->sort == PageSort::Usage;
    if (needsSources && (!m_sourcesAge.isValid() || m_sourcesAge.elapsed() > 300000 || m_sourcesFor != found->menuCategory)) {
        m_sources = m_sourceProvider ? m_sourceProvider(found->menuMax > 0 ? found->menuCategory : QString()) : ArrangeSources{};
        m_sourcesFor = found->menuCategory;
        m_sourcesAge.start();
    }
    const ArrangedPage arranged = arrangePage(*found, *m_usage, m_sources);
    m_dynamic = arranged.page;
    m_highlightKey = arranged.highlightKey;
    m_dynamicFor = currentPage();
}

void PagesController::drawKey(int key, bool force)
{
    if (!m_deck->isOpen())
        return;
    const auto index = static_cast<std::size_t>(key);
    const KeyConfig *configured = keyConfig(key);

    const Theme *theme = currentTheme();
    const qint64 tint = theme ? static_cast<qint64>(theme->rgb) : -1;

    QImage image;
    if (configured) {
        QString iconFile; // an application's icon given by path
        const KeyConfig effective = TileRenderer::effectiveKey(
            *configured, configured->action.type == ActionType::App ? appFor(configured->action.desktop) : DesktopApp{}, iconFile);
        if (configured->action.type == ActionType::Steam && configured->icon.name.isEmpty()) {
#ifdef Q_OS_WIN
            iconFile = m_launcherCache->lookup(QStringLiteral("steam:%1").arg(configured->action.appid)).value(QStringLiteral("icon")).toString();
#else
            iconFile = SteamLibrary::coverImage(configured->action.appid);
#endif
        }
        // Shuffle and repeat show their state in words, and a lighter key when it is on.
        KeyConfig shown = effective;
        bool active = false;
        bool recordingDisabled = false;
        if (m_recording && currentPage() == m_recordingPage) {
            const auto button = m_recording->buttons().at(key).toMap();
            shown.label = button.value(QStringLiteral("label")).toString().left(MaxLabelLength);
            shown.icon.name = QStringLiteral("galleon-recording-") + button.value(QStringLiteral("icon")).toString();
            active = button.value(QStringLiteral("active")).toBool();
            recordingDisabled = !button.value(QStringLiteral("enabled")).toBool();
        }
        if (configured->action.type == ActionType::Media
            && (configured->action.command == MediaCommand::Shuffle || configured->action.command == MediaCommand::Repeat)) {
            const bool shuffle = configured->action.command == MediaCommand::Shuffle;
            int level = 0; // shuffle: off, on; repeat: none, the playlist, the track
            if (shuffle)
                level = m_media->shuffleOn() ? 1 : 0;
            else if (m_media->loopMode() == MediaController::Loop::Playlist)
                level = 1;
            else if (m_media->loopMode() != MediaController::Loop::None)
                level = 2;
            active = level > 0;
            QStringList words = configured->stateLabels;
            if (words.isEmpty())
                words = shuffle ? QStringList{QStringLiteral("off"), QStringLiteral("on")}
                                : QStringList{QStringLiteral("off"), QStringLiteral("list"), QStringLiteral("track")};
            // Two words for a three-state key: off, and on.
            const QString word = words.value(std::min<qsizetype>(level, words.size() - 1));
            shown.label = (configured->label.isEmpty() ? word : configured->label + QLatin1Char(' ') + word).left(MaxLabelLength);
        }
        QString iconState = MediaController::stateName(m_media->state());
        const BuiltinActions::Spec *builtin = configured->action.type == ActionType::Builtin ? BuiltinActions::find(configured->action.builtin) : nullptr;
        if (configured->action.type == ActionType::Toggle || (builtin && builtin->stateful)) {
            const bool on = m_toggles.value(toggleId(key), false);
            iconState = on ? QStringLiteral("on") : QStringLiteral("off");
            active = on;
        }
        image = m_tiles.render(shown, iconState, m_pressed.at(index), iconFile, tint,
                               key == m_highlightKey, active);
        if (recordingDisabled) {
            QPainter painter(&image);
            painter.fillRect(image.rect(), QColor(0, 0, 0, 170));
        }
    } else {
        image = TileRenderer::blank(tint);
    }

    if (!force && image == m_sent.at(index))
        return;
    // A failed send is forgotten, so that the next redraw tries again.
    const bool sent = m_deck->setKeyImage(key, image);
    if (!sent && (m_failedSends++ % 20) == 0) // said now and then, not at every key
        qWarning() << "galleon-customizerd: the picture of key" << key << "could not be sent to the module";
    m_sent.at(index) = sent ? image : QImage();
}

void PagesController::setSuspended(bool suspended)
{
    if (suspended == m_suspended)
        return;
    m_suspended = suspended;
    m_pressed.fill(false);
    releaseHeldKeys();
    if (suspended) {
        for (int dial = 0; dial < galleon::protocol::DialCount; ++dial)
            m_deck->setDialRing(dial, 0, 0, 0);
        return;
    }
    redraw(true);
    applyRings();
}

void PagesController::redraw(bool force)
{
    if (m_suspended)
        return;
#ifdef Q_OS_WIN
    m_dirty.fill(true);
    for (auto &pending : m_forceDraw)
        pending = pending || force;
    if (!m_drawTimer.isActive())
        m_drawTimer.start();
#else
    for (int key = 0; key < galleon::protocol::KeyCount; ++key)
        drawKey(key, force);
#endif
}

void PagesController::perform(const Action &action, int times)
{
    switch (action.type) {
    case ActionType::None:
        return;
    case ActionType::Page:
        if (m_config.pages.contains(action.page) && m_stack.size() < MaxPageDepth) {
            m_stack.append(action.page);
            rebuildPage();
            m_pressed.fill(false);
            releaseHeldKeys();
            redraw();
        }
        return;
    case ActionType::Back:
        if (m_stack.size() > 1) {
            m_stack.removeLast();
            rebuildPage();
            m_pressed.fill(false);
            releaseHeldKeys();
            redraw();
        }
        return;
    case ActionType::App:
        launchApp(action.desktop);
        return;
    case ActionType::Steam:
        launchSteam(action.appid);
        return;
    case ActionType::WallpaperMode:
        setWallpaperMode(m_pressedDial);
        return;
    case ActionType::Key:
        // From a key press the key is held (see onKeyPressed); here, a tap.
        if (m_injector) {
            if (const auto code = keyCodeForName(action.keyName)) {
                m_injector->key(*code, true);
                m_injector->key(*code, false);
            }
        }
        return;
    case ActionType::Combo: // local patch: pressed in order, released in reverse
        if (m_injector) {
            if (const auto codes = keyCodesForCombo(action.keys)) {
                for (const int code : *codes)
                    m_injector->key(code, true);
                for (auto it = codes->rbegin(); it != codes->rend(); ++it)
                    m_injector->key(*it, false);
            }
        }
        return;
    case ActionType::Text: // local patch: one tap per character, Shift where needed
        if (m_injector) {
            if (const auto taps = keyTapsForText(action.text)) {
                const int shift = static_cast<int>(KeyCode::Shift);
                for (const KeyTap &tap : *taps) {
                    if (tap.shift)
                        m_injector->key(shift, true);
                    m_injector->key(tap.code, true);
                    m_injector->key(tap.code, false);
                    if (tap.shift)
                        m_injector->key(shift, false);
                }
                if (action.enter) {
                    m_injector->key(static_cast<int>(KeyCode::Enter), true);
                    m_injector->key(static_cast<int>(KeyCode::Enter), false);
                }
            }
        }
        return;
    case ActionType::Url:
        if (m_appsAllowed && !m_opener(QUrl(action.url)))
            qWarning() << "galleon-customizerd: could not open" << action.url;
        return;
    case ActionType::File: {
        if (!m_appsAllowed)
            return;
        const QString file = action.url.startsWith(QLatin1String("~/")) ? QDir::homePath() + action.url.mid(1) : action.url;
        if (!m_opener(QUrl::fromLocalFile(file)))
            qWarning() << "galleon-customizerd: could not open" << file;
        return;
    }
    case ActionType::Command:
        if (m_appsAllowed && !m_commandRunner(action.shell, action.terminal))
            qWarning() << "galleon-customizerd: could not run the command of a key";
        return;
    case ActionType::Multi:
        runSteps(action.steps, action.delays, 0, m_generation);
        return;
    case ActionType::Toggle:
        flipToggle(m_toggleContext.isEmpty() ? QStringLiteral("dial") : m_toggleContext, action);
        return;
    case ActionType::Builtin: {
        if (!m_appsAllowed)
            return;
        const QString id = m_toggleContext.isEmpty() ? QStringLiteral("dial") : m_toggleContext;
        const quint64 generation = m_generation;
        m_builtins->run(action.builtin, action.options, times, [this, id, generation](const BuiltinResult &result) {
            if (generation == m_generation)
                onBuiltinDone(id, result);
        });
        return;
    }
    case ActionType::Theme:
        if (m_pressedDial >= 0)
            setThemeMode(m_pressedDial); // a dial: choose the theme by turning it
        else
            stepTheme(1);
        return;
    case ActionType::Media:
        break;
    }

    switch (action.command) {
    case MediaCommand::PlayPause:
        m_media->playPause();
        break;
    case MediaCommand::Next:
        m_media->next();
        break;
    case MediaCommand::Previous:
        m_media->previous();
        break;
    case MediaCommand::Stop:
        m_media->stop();
        break;
    case MediaCommand::Shuffle:
        m_media->toggleShuffle();
        break;
    case MediaCommand::Repeat:
        m_media->cycleRepeat();
        break;
    case MediaCommand::Mute:
        m_media->toggleMute();
        break;
    case MediaCommand::Raise:
        m_media->raisePlayer();
        break;
    case MediaCommand::Fullscreen:
        m_media->toggleFullscreen();
        break;
    case MediaCommand::Restart:
        m_media->restartTrack();
        break;
    case MediaCommand::Volume:
        m_media->changeVolume(action.step * times);
        break;
    case MediaCommand::Seek:
        m_media->seek(action.seconds * times);
        break;
    }
}

int PagesController::themeFor(ThemeTrigger trigger) const
{
    for (std::size_t i = 0; i < m_config.themes.size(); ++i) {
        if (m_config.themes[i].when == trigger)
            return static_cast<int>(i);
    }
    return -1;
}

void PagesController::onMediaChanged()
{
    const bool present = isMediaPresent(m_media->state());
    if (present == m_mediaPresent)
        return;
    m_mediaPresent = present;
    // The theme tied to this moment, unless the user is choosing one right now.
    const int index = themeFor(present ? ThemeTrigger::Media : ThemeTrigger::NoMedia);
    if (index >= 0 && index != m_theme && !themeMode() && !m_suspended && !m_gameActive)
        selectTheme(index);
}

bool PagesController::hasKeyActions(const PagesConfig &config)
{
    const auto typesKeys = [](const Action &action) {
        return action.type == ActionType::Key || action.type == ActionType::Combo || action.type == ActionType::Text;
    };
    const auto isKey = [&](const KeyConfig &key) { return typesKeys(key.action); };
    const bool onAKey = std::ranges::any_of(config.pages, [&](const Page &page) {
        return std::ranges::any_of(page.keys, [&](const auto &key) { return key && isKey(*key); });
    });
    return onAKey || std::ranges::any_of(config.dials, [&](const DialConfig &dial) { return typesKeys(dial.press); });
}

void PagesController::releaseHeldKeys()
{
    for (int &code : m_heldKey) {
        if (code != 0 && m_injector)
            m_injector->key(code, false);
        code = 0;
    }
}

const Theme *PagesController::currentTheme() const
{
    if (m_theme < 0 || std::cmp_greater_equal(m_theme, m_config.themes.size()))
        return nullptr;
    return &m_config.themes[static_cast<std::size_t>(m_theme)];
}

void PagesController::stepTheme(int steps)
{
    const auto count = static_cast<int>(m_config.themes.size());
    if (count == 0)
        return;
    // `steps` is bounded by the dial decoder; the modulo keeps any value in range.
    selectTheme((((m_theme < 0 ? -1 : m_theme) + steps) % count + count) % count);
}

void PagesController::setGameArtworkMode(bool active)
{
    m_gameArtworkMode = active;
    if (!active) m_gameArtworkPending = false;
    if (active && wallpaperMode()) restartWallpaperCountdown();
}
void PagesController::setGameArtworkPending(bool pending)
{
    if (pending == m_gameArtworkPending) return;
    m_gameArtworkPending = pending;
    if (!pending && wallpaperMode()) restartWallpaperCountdown();
}
void PagesController::cancelGameArtworkChoice()
{
    m_gameArtworkPending = false;
    setWallpaperMode(-1);
}

void PagesController::restartWallpaperCountdown()
{
    m_wallpaperLeft = m_gameArtworkMode ? 4 : std::max(1, m_config.screen.wallpaperValidateSeconds);
    qInfo() << "galleon-customizerd: validation in" << m_wallpaperLeft << "s (started again)";
    m_wallpaperTimeout.start();
    Q_EMIT wallpaperCountdown(m_wallpaperLeft);
}

void PagesController::wallpaperLabelShown()
{
    if (!wallpaperMode())
        return;
    if (!m_wallpaperReady) {
        m_wallpaperReady = true; // the name was not shown yet: now it is
        m_wallpaperReadyFallback.stop();
        return;
    }
    if (m_wallpaperBusy) {
        // The picture of the last step is on the screen: the rest before the next turn starts now.
        m_wallpaperBusy = false;
        m_lastWallpaperStepAt = m_clock.elapsed();
        m_wallpaperReadyFallback.stop();
    }
}

void PagesController::setThemeMode(int dial)
{
    // A press toggles: the dial that is in the mode leaves it, any other enters it.
    const int wanted = dial >= 0 && dial != m_themeDial && !m_config.themes.empty() ? dial : -1;
    if (wanted == m_themeDial)
        return;
    if (wanted >= 0)
        setWallpaperMode(-1); // one choice at a time: the screen shows one of them
    m_themeDial = wanted;
    qInfo() << "galleon-customizerd: theme mode" << (themeMode() ? "on, dial" : "off") << (themeMode() ? m_themeDial : -1);
    if (themeMode())
        m_themeTimeout.start();
    else
        m_themeTimeout.stop();
    Q_EMIT themeModeChanged();
}

void PagesController::selectTheme(int index)
{
    m_theme = index;
    qInfo() << "galleon-customizerd: theme" << m_config.themes[static_cast<std::size_t>(index)].name;
    m_stack = {m_config.themes[static_cast<std::size_t>(index)].page};
    rebuildPage();
    m_pressed.fill(false);
    releaseHeldKeys();
    // The first step is drawn at once (a turn must be answered at once); steps that come
    // while it is being drawn are gathered, and the last theme is drawn when the dial rests.
    if (m_themeApply.isActive()) {
        m_themePending = true;
        return;
    }
    applyTheme();
    m_themeApply.start();
}

void PagesController::applyTheme()
{
    m_themePending = false;
    redraw();
    applyRings();
    Q_EMIT themeChanged();
}

void PagesController::applyRings()
{
    if (!m_deck->isOpen() || m_suspended)
        return;
    // The left ring shows the theme (dark without one); the right ring stays
    // white, for the music player control.
    const Theme *theme = currentTheme();
    const std::uint32_t rgb = theme ? theme->rgb : 0;
    if (!m_deck->setDialRing(0, static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                            static_cast<std::uint8_t>(rgb)))
        qWarning() << "galleon-customizerd: could not update the left dial ring";
    // The right ring: white for the player control, magenta while it chooses the wallpaper.
    const std::uint32_t right = wallpaperMode() ? WallpaperModeRgb : 0xffffff;
    if (!m_deck->setDialRing(1, static_cast<std::uint8_t>(right >> 16), static_cast<std::uint8_t>(right >> 8),
                            static_cast<std::uint8_t>(right)))
        qWarning() << "galleon-customizerd: could not update the right dial ring";
}

void PagesController::setWallpaperMode(int dial)
{
    // A press toggles: the dial that is in the mode leaves it, any other enters it.
    const int wanted = dial >= 0 && dial != m_wallpaperDial ? dial : -1;
    if (wanted == m_wallpaperDial || (wanted < 0 && m_gameArtworkPending))
        return;
    if (wanted >= 0)
        setThemeMode(-1); // one choice at a time: the screen shows one of them
    m_wallpaperDial = wanted;
    qInfo() << "galleon-customizerd: wallpaper mode" << (wallpaperMode() ? "on, dial" : "off") << (wallpaperMode() ? m_wallpaperDial : -1);
    if (wallpaperMode()) {
        m_wallpaperReady = false; // turns are ignored until the screen shows the label
        m_wallpaperBusy = false;
        m_wallpaperReadyFallback.start();
        restartWallpaperCountdown();
    } else {
        m_wallpaperTimeout.stop();
        m_wallpaperReadyFallback.stop();
        Q_EMIT wallpaperCountdown(0);
    }
    applyRings();
    Q_EMIT wallpaperModeChanged();
}

DesktopApp PagesController::appFor(const QString &desktop)
{
#ifdef Q_OS_WIN
    const auto data = m_launcherCache->lookup(desktop);
    if (!DesktopEntries::isValidReference(desktop))
        return {};
    DesktopApp app;
    app.file = desktop; // Launch validates the local file; it never needs a Shell icon lookup.
    app.id = desktop;
    app.name = data.value(QStringLiteral("name")).toString();
    if (app.name.isEmpty())
        app.name = QFileInfo(desktop).completeBaseName();
    app.iconPath = data.value(QStringLiteral("icon")).toString();
    return app;
#else
    if (const auto known = m_apps.constFind(desktop); known != m_apps.constEnd())
        return known.value();
    if (m_apps.size() >= MaxLookedUpApps)
        m_apps.clear();
    const DesktopApp app = DesktopEntries::lookup(desktop);
    m_apps.insert(desktop, app);
    return app;
#endif
}

void PagesController::launchApp(const QString &desktop)
{
    if (!m_appsAllowed)
        return;
    if (m_lastLaunch.isValid() && m_lastLaunch.elapsed() < MinLaunchIntervalMs)
        return;
    const DesktopApp app = appFor(desktop);
    if (!app.isValid()) {
        if (!m_warned.value(desktop)) { // once, not at every press
            m_warned.insert(desktop, true);
            qWarning() << "galleon-customizerd: no application named" << desktop;
        }
        return;
    }
    m_lastLaunch.start();
    if (!m_launcher(app)) {
        qWarning() << "galleon-customizerd: could not start" << app.name;
        return;
    }
    recordUse(desktop);
}

void PagesController::launchSteam(int appid)
{
    if (!m_appsAllowed || appid <= 0)
        return;
    if (m_lastLaunch.isValid() && m_lastLaunch.elapsed() < MinLaunchIntervalMs)
        return;
    m_lastLaunch.start();
    if (!m_steamLauncher(appid)) {
        qWarning() << "galleon-customizerd: could not start the Steam game" << appid << "(is steam installed?)";
        return;
    }
    recordUse(QStringLiteral("steam:%1").arg(appid));
}

void PagesController::onKeyPressed(int key)
{
    if (m_suspended)
        return;
    if (key < 0 || key >= galleon::protocol::KeyCount)
        return;
    const KeyConfig *configured = keyConfig(key);
    if (!configured)
        return;
    const Action action = configured->action; // a copy: performing it may change the page
    m_pressed.at(static_cast<std::size_t>(key)) = true;
    drawKey(key, false);
    if (action.type == ActionType::Key) {
        // Held until the key is released, so a long press repeats as on a keyboard.
        if (m_injector) {
            if (const auto code = keyCodeForName(action.keyName); code && m_injector->key(*code, true))
                m_heldKey.at(static_cast<std::size_t>(key)) = *code;
        }
        return;
    }
    if (m_recording && currentPage() == m_recordingPage) {
        m_recording->trigger(key);
        return;
    }
    if (configured->longPress) {
        // Held for LongPressMs: the long action. Released before: the usual one, on release.
        const auto index = static_cast<std::size_t>(key);
        const quint64 generation = ++m_longPressGen.at(index);
        m_longPressFired.at(index) = false;
        const QString page = currentPage();
        const Action held = *configured->longPress;
        QTimer::singleShot(LongPressMs, this, [this, key, index, generation, page, held] {
            if (!m_pressed.at(index) || m_longPressGen.at(index) != generation || currentPage() != page)
                return;
            m_longPressFired.at(index) = true;
            performKey(key, held);
        });
        return;
    }
    performKey(key, action); // may change the page: the pressed flags are reset there
}

void PagesController::onKeyReleased(int key)
{
    if (m_suspended)
        return;
    if (key < 0 || key >= galleon::protocol::KeyCount || !m_pressed.at(static_cast<std::size_t>(key)))
        return;
    m_pressed.at(static_cast<std::size_t>(key)) = false;
    if (const KeyConfig *configured = keyConfig(key); configured && configured->longPress
        && !(m_recording && currentPage() == m_recordingPage)) {
        const auto index = static_cast<std::size_t>(key);
        ++m_longPressGen.at(index); // the long press timer, if any, does nothing now
        const bool fired = std::exchange(m_longPressFired.at(index), false);
        if (!fired) {
            const Action action = configured->action;
            drawKey(key, false);
            if (action.type == ActionType::Key) {
                if (m_injector) {
                    if (const auto code = keyCodeForName(action.keyName)) {
                        m_injector->key(*code, true);
                        m_injector->key(*code, false);
                    }
                }
                return;
            }
            performKey(key, action);
            return;
        }
    }
    if (int &held = m_heldKey.at(static_cast<std::size_t>(key)); held != 0) {
        if (m_injector)
            m_injector->key(held, false);
        held = 0;
    }
    drawKey(key, false);
}

QString PagesController::toggleId(int key) const
{
    return currentPage() + QLatin1Char('/') + QString::number(key);
}

bool PagesController::toggleOn(int key) const
{
    return m_toggles.value(toggleId(key), false);
}

void PagesController::performKey(int key, const Action &action)
{
    m_toggleContext = toggleId(key);
    perform(action);
    m_toggleContext.clear();
}

void PagesController::runSteps(const std::vector<Action> &steps, const std::vector<int> &delays, std::size_t from, quint64 generation)
{
    for (std::size_t i = from; i < steps.size(); ++i) {
        if (generation != m_generation || m_suspended)
            return; // a new pages file, or the module asleep: the rest is dropped
        const int delay = i < delays.size() ? delays.at(i) : 0;
        if (delay > 0) {
            QTimer::singleShot(delay, this, [this, steps, delays, i, generation] {
                if (generation != m_generation || m_suspended)
                    return;
                perform(steps.at(i));
                runSteps(steps, delays, i + 1, generation);
            });
            return;
        }
        perform(steps.at(i));
    }
}

void PagesController::flipToggle(const QString &id, const Action &toggle)
{
    if (toggle.steps.size() != 2)
        return;
    const bool on = m_toggles.value(id, false);
    perform(on ? toggle.steps.at(1) : toggle.steps.at(0));
    m_toggles.insert(id, !on);
    redraw();
    if (!toggle.shell.isEmpty() && m_appsAllowed) {
        // The real state, once the action had time to act.
        const QString command = toggle.shell;
        QTimer::singleShot(400, this, [this, id, command] { readToggleStatus(id, command); });
    }
}

void PagesController::readToggleStatus(const QString &id, const QString &command)
{
    if (!m_statusReader)
        return;
    const quint64 generation = m_generation;
    m_statusReader(command, [this, id, generation](int status) {
        if (generation != m_generation || status < 0)
            return;
        const bool on = status == 1;
        if (m_toggles.value(id, false) == on && m_toggles.contains(id))
            return;
        m_toggles.insert(id, on);
        redraw();
    });
}

void PagesController::pollToggles()
{
    if (m_suspended || !m_appsAllowed)
        return;
    for (int key = 0; key < galleon::protocol::KeyCount; ++key) {
        const KeyConfig *configured = keyConfig(key);
        if (configured && configured->action.type == ActionType::Toggle && !configured->action.shell.isEmpty())
            readToggleStatus(toggleId(key), configured->action.shell);
        const BuiltinActions::Spec *builtin = configured && configured->action.type == ActionType::Builtin
            ? BuiltinActions::find(configured->action.builtin) : nullptr;
        if (builtin && builtin->stateful) {
            const QString id = toggleId(key);
            const quint64 generation = m_generation;
            m_builtins->readState(builtin->name, [this, id, generation](std::optional<bool> on) {
                if (generation != m_generation || !on || (m_toggles.contains(id) && m_toggles.value(id) == *on))
                    return;
                m_toggles.insert(id, *on);
                redraw();
            });
        }
    }
}

void PagesController::onBuiltinDone(const QString &id, const BuiltinResult &result)
{
    if (!result.ok)
        qWarning() << "galleon-customizerd: a key's action failed:" << result.message;
    if (!result.message.isEmpty())
        Q_EMIT actionMessage(result.message, result.ok);
    if (result.state && m_toggles.value(id, !*result.state) != *result.state) {
        m_toggles.insert(id, *result.state);
        redraw();
    }
    if (!result.open.isEmpty() && !m_opener(result.open))
        qWarning() << "galleon-customizerd: could not open" << result.open.toString();
    if (!result.notifyTitle.isEmpty())
        m_builtins->notify(result.notifyTitle, result.notifyBody);
    if (result.paste) {
        const quint64 generation = m_generation;
        QTimer::singleShot(50, this, [this, generation] {
            if (generation != m_generation)
                return;
            Action paste;
            paste.type = ActionType::Combo;
            paste.keys = QStringLiteral("ctrl+v");
            perform(paste);
        });
    }
}

bool PagesController::runCommandLine(const QString &command, bool terminal)
{
#ifdef Q_OS_WIN
    Q_UNUSED(terminal)
    return QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), command});
#else
    if (!terminal)
        return QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
    // The first terminal found; the window stays open until Enter is pressed.
    const QString held = command + QStringLiteral("; printf '\\n[Enter] '; read -r _");
    const QList<QStringList> terminals{
        {QStringLiteral("xdg-terminal-exec"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), held},
        {QStringLiteral("ptyxis"), QStringLiteral("--new-window"), QStringLiteral("-x"), QStringLiteral("/bin/sh -c ") + shellQuote(held)},
        {QStringLiteral("kgx"), QStringLiteral("--"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), held},
        {QStringLiteral("gnome-terminal"), QStringLiteral("--"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), held},
        {QStringLiteral("konsole"), QStringLiteral("-e"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), held},
        {QStringLiteral("x-terminal-emulator"), QStringLiteral("-e"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), held},
        {QStringLiteral("xterm"), QStringLiteral("-e"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), held},
    };
    for (const QStringList &argv : terminals) {
        if (!QStandardPaths::findExecutable(argv.first()).isEmpty())
            return QProcess::startDetached(argv.first(), argv.mid(1));
    }
    return false;
#endif
}

QString PagesController::shellQuote(const QString &text)
{
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

void PagesController::readStatusCommand(const QString &command, std::function<void(int)> done)
{
    auto *process = new QProcess;
    QObject::connect(process, &QProcess::finished, process, [process, done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit ? (code == 0 ? 1 : 0) : -1);
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            done(-1);
            process->deleteLater();
        }
    });
    QTimer::singleShot(5000, process, [process] { process->kill(); }); // a hung command counts as unknown
#ifdef Q_OS_WIN
    process->start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), command});
#else
    process->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
#endif
}

void PagesController::onDialRotated(int dial, int steps)
{
    if (m_suspended)
        return;
    if (dial < 0 || dial >= galleon::protocol::DialCount || steps == 0)
        return;
    const qint64 now = m_clock.elapsed();
    const auto index = static_cast<std::size_t>(dial);
    if (now - m_lastPressAt.at(index) < ClickAfterMs) {
        qInfo() << "galleon-customizerd: dial" << dial << "step" << steps << "ignored: it is the click";
        return; // the click's own movement, not a turn
    }
    int counted = steps; // as many places as the dial counts detents
    bool countdownRestarted = false;
    if (dial == m_wallpaperDial && m_config.screen.wallpaperStepDelayMs > 0) {
        // One place at a time, and the next turn is listened to only once the wallpaper
        // of this one is really on the screen (the screen says so, see wallpaperLabelShown)
        // and has been there for a short rest. Turns before that are ignored, not queued:
        // the screen never runs behind the dial, and the index never jumps by several.
        if (!m_wallpaperReady) {
            qInfo() << "galleon-customizerd: dial" << dial << "step" << steps << "ignored: the wallpaper name is not shown yet";
            return;
        }
        restartWallpaperCountdown(); // the dial is being touched: the choice is not validated yet
        countdownRestarted = true;
        if (m_wallpaperBusy || now - m_lastWallpaperStepAt < m_config.screen.wallpaperStepDelayMs)
            return;
        counted = std::clamp(steps, -1, 1);
        m_wallpaperBusy = true;
        m_wallpaperReadyFallback.start(); // if the screen never answers, listen again after a moment
    }
    auto &recent = m_recentTurns.at(index);
    std::ranges::rotate(recent, std::next(recent.begin())); // the oldest slot is reused
    recent.back() = {.at=now, .steps=counted};
    if (dial == m_themeDial) {
        m_themeTimeout.start(); // in use: keep the mode
        stepTheme(counted);
        return;
    }
    if (dial == m_wallpaperDial) {
        if (!m_wallpaperReady) {
            qInfo() << "galleon-customizerd: dial" << dial << "step" << steps << "ignored: the wallpaper name is not shown yet";
            return;
        }
        if (!countdownRestarted)
            restartWallpaperCountdown(); // in use: keep the mode
        Q_EMIT wallpaperStep(counted);
        return;
    }
    const Action &turn = m_config.dials.at(static_cast<std::size_t>(dial)).turn;
    if (turn.type == ActionType::Theme) {
        stepTheme(std::clamp(counted, -1, 1)); // one page per turn event, never skip pages
        return;
    }
    perform(turn, steps);
}

void PagesController::onDialReleased(int dial)
{
    if (dial < 0 || dial >= galleon::protocol::DialCount)
        return;
    m_lastPressAt.at(static_cast<std::size_t>(dial)) = m_clock.elapsed(); // letting go moves it too
}

void PagesController::onDialPressed(int dial)
{
    if (m_suspended)
        return;
    if (dial < 0 || dial >= galleon::protocol::DialCount)
        return;
    // A step that came just before this press was the click: take it back.
    const qint64 now = m_clock.elapsed();
    const auto index = static_cast<std::size_t>(dial);
    int wobble = 0;
    for (RecentTurn &turn : m_recentTurns.at(index)) {
        if (turn.steps != 0 && now - turn.at < ClickBeforeMs)
            wobble += turn.steps;
        turn = {};
    }
    m_lastPressAt.at(index) = now;
    if (wobble != 0 && (dial == m_themeDial || dial == m_wallpaperDial))
        qInfo() << "galleon-customizerd: dial" << dial << "step" << wobble << "taken back: it came with the click";
    if (wobble != 0 && dial == m_themeDial)
        stepTheme(-wobble);
    else if (wobble != 0 && dial == m_wallpaperDial)
        Q_EMIT wallpaperStep(-wobble);
    m_pressedDial = dial;
    perform(m_config.dials.at(static_cast<std::size_t>(dial)).press);
    m_pressedDial = -1;
}

} // namespace galleond
