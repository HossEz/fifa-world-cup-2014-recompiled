// The launcher's languages. Text is written in English in the code and looked
// up by that English text (T), so a missing translation shows the English.
//
// The language is the player's choice (launcher.toml `language`), or Windows'
// display language until they choose one. The game is told the same one
// (user_language, the console's language setting), so its language screen at
// start has it selected.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace launcher {

enum class Language { kEnglish, kSpanish, kPortuguese };

struct LanguageInfo {
  Language id;
  const char* code;      // in launcher.toml: "en", "es", "pt"
  const char* name;      // in its own language
  uint32_t xbox;         // the console's language ID (user_language)
};
const std::vector<LanguageInfo>& Languages();
const LanguageInfo& Info(Language language);

Language CurrentLanguage();
void SetLanguage(Language language);
// Windows' display language, if it's one of these (else English).
Language SystemLanguage();
// "es" -> Spanish; an unknown code -> `fallback`.
Language LanguageFromCode(std::string_view code, Language fallback);

// `english` in the current language. The result lives as long as the program.
const char* T(const char* english);
// T, with {0}, {1}... replaced by `args`.
std::string Tf(const char* english, std::initializer_list<std::string_view> args);

}  // namespace launcher
