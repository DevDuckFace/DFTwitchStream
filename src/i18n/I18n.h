#pragma once
#include "core/Config.h"

#include <string>
#include <unordered_map>

namespace dfts {

// Simple key -> string table with English and Brazilian Portuguese entries.
// Missing keys fall back to English, then to the key itself, so a typo shows up
// in the UI instead of rendering as an empty label.
class I18n {
public:
    static I18n& get();

    void setLanguage(Language lang);
    Language language() const { return m_lang; }

    const char* tr(const char* key) const;

    // Formats a string containing "{}" placeholders, replacing them in order.
    std::string fmt(const char* key, const std::string& a) const;
    std::string fmt(const char* key, const std::string& a, const std::string& b) const;

private:
    I18n();
    Language m_lang = Language::English;
    std::unordered_map<std::string, std::string> m_en;
    std::unordered_map<std::string, std::string> m_pt;
};

} // namespace dfts

// Shorthand used throughout the UI code.
inline const char* T(const char* key) { return ::dfts::I18n::get().tr(key); }
