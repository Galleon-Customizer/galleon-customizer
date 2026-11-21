// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QString>
#include <QTranslator>

namespace galleond {

// The texts drawn on the module's screen follow the language chosen in the settings
// window ([interface] language of bosun.conf), read from the same JSON catalogs.
class ScreenTranslator final : public QTranslator
{
public:
    bool select(const QString &language);
    [[nodiscard]] QString translate(const char *context, const char *source,
                                    const char *disambiguation = nullptr, int n = -1) const override;
    [[nodiscard]] bool isEmpty() const override { return m_messages.isEmpty(); }
private:
    QJsonObject m_messages;
};

// "en", "fr", "de", "es" or "vi": the preference saved in `settingsFile`, or the system's.
[[nodiscard]] QString screenLanguage(const QString &settingsFile);

// Installs the translator and makes the language's locale the default (dates, numbers).
void applyScreenLanguage(ScreenTranslator &translator, const QString &settingsFile);

}
