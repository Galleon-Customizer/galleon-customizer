// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The button pages of the Stream Deck module, read from a JSON file
// (~/.config/galleon-customizer/pages.json). The file is external data: it is
// validated completely, and a file with any error is refused as a whole, so a
// typo can never leave a half-understood page on the keys.

#include <galleon/Protocol.h>

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

#include <array>
#include <QJsonObject>
#include <cstdint>
#include <optional>
#include <vector>

namespace galleond {

enum class ActionType : std::uint8_t {
    None,
    Media, // a command for the music player (MPRIS)
    Page,  // open another page
    Back,  // return to the previous page
    App,   // start an application, named by its .desktop file
    Theme, // go to the next theme (see Theme)
    WallpaperMode, // switch the dial to choosing the screen's wallpaper, and back
    Key,   // press a keyboard key (the numeric keypad...), held as long as the key is
    Steam, // start a Steam game by its application number
    Combo, // tap a combination such as super+shift+s
    Text,  // type a short ASCII text, then Enter if asked
    Url,     // open a web address (http, https, mailto) in the default browser
    File,    // open a file or folder with its default application
    Command, // run a shell command line, in a terminal if asked
    Multi,   // several actions in a row, each after an optional delay
    Toggle,  // on/off: the "on" action, then the "off" one, with a picture for each
    Builtin, // a ready-made action (system.lock, sound.mute_input...), see BuiltinActions.h
};

inline constexpr int MaxMultiSteps = 16;
inline constexpr int MaxStepDelayMs = 60000;
inline constexpr int MaxCommandLength = 1000;
inline constexpr int LongPressMs = 500;

enum class MediaCommand : std::uint8_t {
    PlayPause,
    Next,
    Previous,
    Stop,
    Volume, // `step`: percent of the full volume, signed
    Seek,   // `seconds`, signed
    Shuffle,    // switch shuffle on or off
    Repeat,     // none -> playlist -> track
    Mute,       // mute or unmute the system sound
    Raise,      // bring the player's window to the front
    Fullscreen, // full screen on or off (a video player)
    Restart,    // back to the start of the track
};

struct Action
{
    ActionType type = ActionType::None;
    MediaCommand command = MediaCommand::PlayPause;
    int step = 0;    // Volume: -100..100, never 0
    int seconds = 0; // Seek: -3600..3600, never 0
    QString page;    // Page: the name of a page of the file
    int appid = 0;   // Steam: the game's application number (1..2000000000)
    QString keyName; // Key: a name of the fixed list (see KeyInjector.h)
    QString desktop; // App: a desktop file ID ("firefox.desktop") or the path of a .desktop file
    QString keys;    // Combo: names of the fixed list joined by '+' (see KeyInjector.h)
    QString text;    // Text: printable ASCII, at most MaxTextLength characters
    bool enter = false; // Text: press Enter after the text
    QString url;     // Url: the address; File: the path (absolute, or starting with ~/)
    QString shell;   // Command: run with /bin/sh -c; Toggle: the status command (exit 0 = on)
    bool terminal = false; // Command: run in a terminal window
    // Multi: the steps in order. Toggle: [0] = the "on" action, [1] = the "off" one.
    std::vector<Action> steps;
    std::vector<int> delays; // Multi: milliseconds before each step (same size as steps)
    QString builtin;      // Builtin: its name, "system.lock"
    QJsonObject options;  // Builtin: its options, checked by BuiltinActions::checkOptions

    friend bool operator==(const Action &, const Action &) = default;
};

// A key picture: one icon name, or one per state of the player.
struct IconSpec
{
    QString name;                   // used when no state-specific name applies
    QHash<QString, QString> byState; // "playing", "paused", "stopped"; a toggle key: "on", "off"

    [[nodiscard]] QString forState(const QString &state) const
    {
        const auto it = byState.constFind(state);
        return it != byState.constEnd() ? it.value() : name;
    }
    friend bool operator==(const IconSpec &, const IconSpec &) = default;
};

// How a key is drawn, beyond its icon and label (the "look" of the Python Galleon Customizer).
enum class TitlePosition : std::uint8_t { Auto, Top, Middle, Bottom };
struct KeyLook
{
    QString background;  // "#RRGGBB", or empty for the theme's
    QString titleColor;  // "#RRGGBB", or empty for white
    int fontSize = 0;    // the label's size in pixels (6..64), 0 = the largest that fits
    TitlePosition titlePosition = TitlePosition::Auto;
    bool showTitle = true;

    friend bool operator==(const KeyLook &, const KeyLook &) = default;
};

struct KeyConfig
{
    int key = 0; // 0 = top left, row by row, to 11
    QString label;
    IconSpec icon;
    Action action;
    // Done instead of `action` when the key is held for LongPressMs; `action` then runs on release.
    std::optional<Action> longPress;
    // The words shown for the state of a shuffle or repeat key. Shuffle: off, on. Repeat: none, list, track.
    QStringList stateLabels;
    KeyLook look;

    friend bool operator==(const KeyConfig &, const KeyConfig &) = default;
};

// How the application keys of a page are ordered.
enum class PageSort : std::uint8_t {
    None,  // as written
    Usage, // the most used first (see UsageStats); the first one is marked
};

struct Page
{
    std::array<std::optional<KeyConfig>, galleon::protocol::KeyCount> keys;
    PageSort sort = PageSort::None;
    int steamMax = 0; // up to this many installed Steam games fill the free keys, most played first (0: none)
    QString menuCategory; // the desktop menu category ("Game") whose applications fill the free keys...
    int menuMax = 0;      // ...up to this many, most used first (0: none)

    friend bool operator==(const Page &, const Page &) = default;
};

struct DialConfig
{
    Action turn;  // Media volume or seek: applied once per click, in the direction of the turn
    Action press;

    friend bool operator==(const DialConfig &, const DialConfig &) = default;
};

// When the screen shows the track being played instead of the wallpaper.
enum class NowPlayingMode : std::uint8_t {
    Playing,         // while a player is playing (default)
    PlayingOrPaused, // also while it is paused
    Never,
};

struct ScreenConfig
{
    NowPlayingMode nowPlaying = NowPlayingMode::Playing;
    bool remoteCovers = true; // download https covers; false: local files only
    int wallpaperStepDelayMs = 150;   // while choosing a wallpaper: the rest after a step's picture is shown, before the next turn counts (0 = no rule: every detent counts)
    int wallpaperValidateSeconds = 4; // without touching the dial this long while choosing a wallpaper, the choice is made (1..120)
    int wallpaperHoldSeconds = 4; // how long a chosen wallpaper stays before a playing track takes the screen (1..600)
    bool videoThumbnails = true; // a picture of a local video made with FFmpeg when there is no cover

    friend bool operator==(const ScreenConfig &, const ScreenConfig &) = default;
};

// A theme: a page of keys with a colour, shown as a line on the screen and
// on the rings around the dials. The `theme` action steps through them.
// When a theme is chosen by itself: when a player starts ("media") or when
// there is none left ("no-media"). Only these moments: the user's own choice
// stays until the next one.
enum class ThemeTrigger : std::uint8_t { None, Media, NoMedia };

struct Theme
{
    QString name;          // shown on the screen
    QString page;          // the page it opens (the stack is reset to it)
    std::uint32_t rgb = 0; // 0xRRGGBB
    ThemeTrigger when = ThemeTrigger::None;

    friend bool operator==(const Theme &, const Theme &) = default;
};

// The animated screensaver: a vertical video spread over the screen and the 12
// keys as if seen through them, shown after a time without use. The physical
// sizes come from the user's measurements (never guessed); without them the
// screensaver stays off.
struct ScreensaverConfig
{
    QStringList videos;     // files, absolute or relative to the pages file's folder, shown in turn; empty = off
    QStringList titles;     // the title of each video (same order; empty when it has none)
    int idleMinutes = 5;    // 1..720
    int fps = 15;           // 1..20: the module takes about 20 screen + keys rounds per second (measured)
    bool whilePlaying = false; // false: never while a track is playing
    bool hasGeometry = false;
    double screenWidthMm = 0, screenHeightMm = 0;
    double keyWidthMm = 0, keyHeightMm = 0;
    double gapXMm = 0, gapYMm = 0;
    double screenToKeysMm = 0; // from the bottom of the screen to the top of the first row of keys

    [[nodiscard]] bool enabled() const { return !videos.isEmpty() && hasGeometry; }
    friend bool operator==(const ScreensaverConfig &, const ScreensaverConfig &) = default;
};

struct PagesConfig
{
    ScreenConfig screen;
    ScreensaverConfig screensaver;
    std::vector<Theme> themes;
    QString home;
    QMap<QString, Page> pages;
    std::array<DialConfig, galleon::protocol::DialCount> dials;

    friend bool operator==(const PagesConfig &, const PagesConfig &) = default;
};

struct PagesParseResult
{
    std::optional<PagesConfig> config; // empty when `errors` is not
    QStringList errors;                // one line each, with the path of the field
};

inline constexpr qsizetype MaxPagesFileBytes = qsizetype{256} * 1024;
inline constexpr int MaxPages = 32;
inline constexpr int MaxLabelLength = 24;
inline constexpr int MaxNameLength = 64;
inline constexpr int MaxThemes = 8;
inline constexpr int MaxSteamKeys = 12;
inline constexpr int MaxVideoPathLength = 400;
inline constexpr int MaxScreensaverVideos = 8;
inline constexpr int MaxVideoTitleLength = 60;
inline constexpr int MaxPageDepth = 8; // pages opened one from another, at run time

// Parses and validates the content of a pages file. Never throws, never reads
// outside `json`; any problem is an entry of `errors` and no config is returned.
[[nodiscard]] PagesParseResult parsePagesConfig(const QByteArray &json);

// The configuration used when the user has no (valid) file.
[[nodiscard]] QByteArray defaultPagesJson();

} // namespace galleond
