// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScreenLanguage.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QSettings>

static void initScreenLanguages() { Q_INIT_RESOURCE(screen_languages); } // NOLINT(misc-use-anonymous-namespace) -- Qt resource symbol is global.

namespace galleond {
namespace {
bool supported(const QString &language)
{
    return language == QLatin1String("en") || language == QLatin1String("fr") || language == QLatin1String("de")
           || language == QLatin1String("es") || language == QLatin1String("vi");
}
}

bool ScreenTranslator::select(const QString &language)
{
    if (!supported(language))
        return false;
    initScreenLanguages();
    QFile file(QStringLiteral(":/screen/translations/%1.json").arg(language));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const auto document = QJsonDocument::fromJson(file.readAll());
    m_messages = document.isObject() ? document.object() : QJsonObject();
    return !m_messages.isEmpty();
}

QString ScreenTranslator::translate(const char *, const char *source, const char *, int) const
{
    return m_messages.value(QString::fromUtf8(source)).toString();
}

QString screenLanguage(const QString &settingsFile)
{
    const QString preference = QSettings(settingsFile, QSettings::IniFormat).value(QStringLiteral("interface/language")).toString();
    if (supported(preference))
        return preference;
    for (const auto &name : QLocale::system().uiLanguages()) {
        const QString language = name.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
        if (supported(language))
            return language;
    }
    return QStringLiteral("en");
}

void applyScreenLanguage(ScreenTranslator &translator, const QString &settingsFile)
{
    const QString language = screenLanguage(settingsFile);
    if (language != QLatin1String("en") && translator.select(language))
        QCoreApplication::installTranslator(&translator);
    // The system's own locale when it already speaks that language (its region and formats).
    const QLocale system = QLocale::system();
    if (QLocale::languageToCode(system.language()) != language)
        QLocale::setDefault(QLocale(language));
}

}
