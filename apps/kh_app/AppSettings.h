#pragma once

#include <QSettings>

#include "kh/plugin/Settings.h"

namespace kh::app {

// Plugins' settings, kept with the desktop app's own settings (QSettings), so
// they last between runs. Keys look like "plugins/example.ruler/unit".
class AppSettings final : public plugin::SettingsBackend {
public:
    explicit AppSettings(QSettings& settings) : settings_(settings) {}

    std::optional<std::string> read(std::string_view key) const override {
        const auto k = QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
        if (!settings_.contains(k)) return std::nullopt;
        return settings_.value(k).toString().toStdString();
    }
    void write(std::string_view key, std::string_view value) override {
        settings_.setValue(QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())),
                           QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())));
    }
    void remove(std::string_view key) override {
        settings_.remove(QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())));
    }

private:
    QSettings& settings_;
};

}  // namespace kh::app
