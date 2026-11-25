#include "ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <imgui.h>

#include "about.h"
#include "game_files.h"
#include "i18n.h"

namespace launcher {

namespace {

// The palette of the game's 2014 front end.
constexpr ImU32 kAtlantico = IM_COL32(11, 47, 107, 255);  // the menus' deep blue
constexpr ImU32 kCream = IM_COL32(244, 239, 200, 255);
constexpr ImU32 kCreamDim = IM_COL32(244, 239, 200, 216);
constexpr ImU32 kInk = IM_COL32(12, 30, 20, 255);
constexpr ImU32 kAlarm = IM_COL32(200, 51, 31, 255);  // an armed (confirm) button

// Art in the game's archives.
constexpr const char* kBackground = "data/ui/imgassets/fluxwaves/fluxwaves_1.dds";
// Menu backgrounds the backdrop is made from (BuildBackdrop).
constexpr const char* kBackdropLeft = "data/ui/game/background/backgroundonlineposter.big";
constexpr const char* kBackdropRight = "data/ui/game/background/backgroundonline.big";
constexpr const char* kPanel = "data/ui/imgassets/custompanels/custompanels_27.dds";
// The update popup's light painted panel, with leaves at its edges.
constexpr const char* kUpdatePanel = "data/ui/imgassets/custompanels/custompanels_5.dds";
constexpr const char* kQuitPanel = "data/ui/imgassets/custompanels/custompanels_66.dds";
// Crumpled paper, under Settings' help text.
constexpr const char* kNote = "data/ui/imgassets/custompanels/custompanels_38.dds";
constexpr const char* kStroke = "data/ui/imgassets/custompanels/custompanels_61.dds";
constexpr const char* kLogo = "data/ui/imgassets/custompanels/custompanels_39.dds";
constexpr const char* kTiles = "data/ui/imgassets/tiles/mainhub/";
constexpr const char* kHomeIcons[] = {"ct_selectarenaprofile", "ct_settings", "ct_controls",
                                      "ct_credits"};
// Settings sections (Section::icon): Display, Graphics, Data, Sound, Controls.
constexpr const char* kSectionIcons[] = {"ct_videocalibration", "ct_gamesettings",
                                         "ct_loadfifa2014wc_profile", "ct_musicandchants",
                                         "ct_basiccontrols"};
constexpr const char* kHomeItems[] = {"Play", "Settings", "Controls", "About"};

float EaseOut(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
  ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a), cb = ImGui::ColorConvertU32ToFloat4(b);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
                                               ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
}

ImU32 WithAlpha(ImU32 color, float alpha) {
  ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
  c.w *= std::clamp(alpha, 0.0f, 1.0f);
  return ImGui::ColorConvertFloat4ToU32(c);
}

// A texture's visible part between two corners (mirrored left-right if asked).
void Image(ImDrawList* draw, const Texture& texture, ImVec2 p0, ImVec2 p1,
           ImU32 color = IM_COL32_WHITE, bool mirrored = false) {
  ImVec2 uv0(mirrored ? texture.u1 : texture.u0, texture.v0);
  ImVec2 uv1(mirrored ? texture.u0 : texture.u1, texture.v1);
  draw->AddImage(ImTextureRef(texture.id()), p0, p1, uv0, uv1, color);
}

// A small filled arrow (left or right) centred at (x, y).
void Arrow(ImDrawList* draw, float x, float y, float size, bool right, ImU32 color) {
  float d = right ? 1.0f : -1.0f;
  draw->AddTriangleFilled(ImVec2(x - d * size * 0.35f, y - size * 0.5f),
                          ImVec2(x + d * size * 0.45f, y),
                          ImVec2(x - d * size * 0.35f, y + size * 0.5f), color);
}

// One of the game's 1280x720 menu backgrounds as RGBA. Its archive holds 15
// tiles of 256x256 (entries "4" to "18", XPR2), laid out 5 by 3 in reverse
// order: "18" is the top left. The bottom row's last 48 rows are padding.
constexpr int kBackdropWidth = 1280, kBackdropHeight = 720;

std::vector<uint8_t> LoadMenuBackground(const GameFiles& files, const char* path) {
  auto archive = files.Read(path);
  if (!archive) return {};
  std::vector<uint8_t> rgba(size_t(kBackdropWidth) * kBackdropHeight * 4);
  for (int k = 0; k < 15; ++k) {
    auto entry = ReadNested(*archive, std::to_string(18 - k));
    int width = 0, height = 0;
    std::vector<uint8_t> tile = entry ? DecodeXpr2(*entry, &width, &height) : std::vector<uint8_t>{};
    if (width != 256 || height != 256) return {};
    int x0 = (k % 5) * 256, y0 = (k / 5) * 256;
    for (int y = 0; y < 256 && y0 + y < kBackdropHeight; ++y) {
      std::memcpy(&rgba[(size_t(y0 + y) * kBackdropWidth + x0) * 4], &tile[size_t(y) * 256 * 4],
                  256 * 4);
    }
  }
  return rgba;
}

// The backdrop: the left half of the online poster background and the right
// half of the online one (the same artwork, with a footballer added on the
// left), blended over 240 pixels in the middle so there's no seam.
Texture BuildBackdrop(ID3D11Device* device, const GameFiles& files) {
  std::vector<uint8_t> left = LoadMenuBackground(files, kBackdropLeft);
  std::vector<uint8_t> right = LoadMenuBackground(files, kBackdropRight);
  if (left.empty() || right.empty()) return {};
  for (int x = 0; x < kBackdropWidth; ++x) {
    float t = std::clamp((x - (kBackdropWidth / 2 - 120)) / 240.0f, 0.0f, 1.0f);
    t = t * t * (3 - 2 * t);
    for (int y = 0; y < kBackdropHeight; ++y) {
      uint8_t* l = &left[(size_t(y) * kBackdropWidth + x) * 4];
      const uint8_t* r = &right[(size_t(y) * kBackdropWidth + x) * 4];
      for (int c = 0; c < 3; ++c) l[c] = uint8_t(std::lround(l[c] + (r[c] - l[c]) * t));
      l[3] = 255;
    }
  }
  return LoadRgba(device, left, kBackdropWidth, kBackdropHeight);
}

}  // namespace

// ------------------------------------------------------------------ setup

bool Ui::Load(ID3D11Device* device, const GameFiles& files, const std::filesystem::path& data_folder,
              const std::filesystem::path& user_folder) {
  backdrop_ = BuildBackdrop(device, files);
  for (size_t i = 1; i < covers_.size(); ++i) {
    auto archive = files.Read("data/ui/artassets/eatrax_thumbs/et" + std::to_string(i) + ".big");
    auto xpr2 = archive ? ReadNestedByMagic(*archive, "XPR2") : std::nullopt;
    int width = 0, height = 0;
    std::vector<uint8_t> rgba = xpr2 ? DecodeXpr2(*xpr2, &width, &height) : std::vector<uint8_t>{};
    covers_[i] = LoadRgba(device, rgba, width, height);
  }
  background_ = LoadGameTexture(device, files, kBackground);
  panel_ = LoadGameTexture(device, files, kPanel);
  update_panel_ = LoadGameTexture(device, files, kUpdatePanel);
  quit_panel_ = LoadGameTexture(device, files, kQuitPanel);
  note_ = LoadGameTexture(device, files, kNote);
  update_icon_ = LoadGameTexture(device, files, std::string(kTiles) + "ct_downloadupdates.dds");
  stroke_ = LoadGameTexture(device, files, kStroke);
  logo_ = LoadGameTexture(device, files, kLogo);
  for (size_t i = 0; i < home_icons_.size(); ++i) {
    home_icons_[i] = LoadGameTexture(device, files, std::string(kTiles) + kHomeIcons[i] + ".dds");
  }
  for (size_t i = 0; i < section_icons_.size(); ++i) {
    section_icons_[i] =
        LoadGameTexture(device, files, std::string(kTiles) + kSectionIcons[i] + ".dds");
  }

  ImGuiIO& io = ImGui::GetIO();
  auto add_font = [&](const char* path) -> ImFont* {
    auto data = files.Read(path);
    if (!data) return nullptr;
    font_data_.push_back(std::move(*data));
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    std::vector<uint8_t>& bytes = font_data_.back();
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(bytes.data(), int(bytes.size()), 32.0f, &config);
    // Futura has only Latin letters: arrows and other symbols (a "→" in the
    // release notes) come from Windows' Segoe UI fonts instead of showing "?".
    const char* windir = std::getenv("WINDIR");
    std::filesystem::path fonts = std::filesystem::path(windir ? windir : "C:\\Windows") / "Fonts";
    for (const char* fallback : {"segoeui.ttf", "seguisym.ttf"}) {
      std::error_code ec;
      if (!font || !std::filesystem::exists(fonts / fallback, ec)) continue;
      ImFontConfig merge;
      merge.MergeMode = true;
      merge.DstFont = font;
      std::u8string path = (fonts / fallback).u8string();
      io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(path.c_str()), 32.0f, &merge);
    }
    return font;
  };
  bold_ = add_font("data/ui/fonts/futurastd-bold.ttf");
  book_ = add_font("data/ui/fonts/futurastd-book.ttf");
  condensed_ = add_font("data/ui/fonts/futurastd-condensed.ttf");

  config_ = std::make_unique<GameConfig>(data_folder / "fifawc14.toml");
  launcher_config_ = std::make_unique<GameConfig>(data_folder / "launcher.toml");
  data_folder_ = data_folder;
  user_folder_ = user_folder;
  BuildSections();
  anims_["home0"] = 1.0f;
  // The first start's welcome.
  if (launcher_config_->Get("welcome_seen", "false") != "true") screen_ = Screen::kNotice;
  return background_ && panel_ && stroke_ && logo_ && bold_ && book_ && condensed_;
}

// The settings in the current language.
void Ui::BuildSections() {
  infos_.clear();
  sections_ = MakeSections(data_folder_, user_folder_);
  // Data starts with where the game is read from; changing it is the host's
  // (it reopens the setup screen).
  for (Section& section : sections_) {
    if (section.icon != 2) continue;  // Data
    std::string location = config_->Get("game_data_root", "");
    if (location.size() >= 2 && location.front() == '"') location = location.substr(1, location.size() - 2);
    std::filesystem::path path = std::filesystem::u8path(location).make_preferred();
    std::u8string utf8 = path.u8string(), file = path.filename().u8string();
    location.assign(utf8.begin(), utf8.end());
    std::string name(file.begin(), file.end());
    if (name.empty()) name = location;  // a drive's root ("E:/")
    if (name.size() > 22) name = name.substr(0, 21) + "\xE2\x80\xA6";
    Option game;
    game.label = T("Game");
    game.action = T("Change");
    game.info = [name] { return name; };
    game.help = Tf("Your copy of the game, read from {0}. Change it if you've moved it, or to use "
                   "its .iso instead.",
                   {location});
    game.run = [this] { if (on_change_game) on_change_game(); };
    section.options.insert(section.options.begin(), game);
  }
}

void Ui::SetControllerStyle(ControllerStyle detected) {
  std::string choice = config_ ? config_->Get("button_icons", "\"auto\"") : "\"auto\"";
  controller_style_ = choice == "\"playstation\"" ? ControllerStyle::kPlayStation
                      : choice == "\"xbox\""      ? ControllerStyle::kXbox
                                                  : detected;
}

// After a launcher setting changed: the sound, and the language.
void Ui::ApplyLocalSettings() {
  ApplySoundSettings();
  std::string code = launcher_config_->Get("language", "");
  if (code.size() >= 2 && code.front() == '"') code = code.substr(1, code.size() - 2);
  Language language = LanguageFromCode(code, CurrentLanguage());
  if (language != CurrentLanguage()) {
    SetLanguage(language);
    BuildSections();
  }
}

void Ui::SaveLanguage() {
  launcher_config_->Set("language", std::string("\"") + launcher::Info(CurrentLanguage()).code + "\"");
  launcher_config_->Save();
  BuildSections();
}

void Ui::SetAudio(Audio* audio) {
  audio_ = audio;
  ApplySoundSettings();
}

void Ui::ApplySoundSettings() {
  if (!audio_) return;
  const GameConfig& config = *launcher_config_;
  audio_->SetSoundsEnabled(config.Get("menu_sounds", "true") == "true");
  // 100% in the settings is a comfortable level below full volume.
  audio_->SetSoundsVolume(std::stoi(config.Get("sounds_volume", "100")) / 100.0f * 0.8f);
  audio_->SetMusic(config.Get("menu_music", "true") == "true",
                   std::stoi(config.Get("music_volume", "100")) / 100.0f * 0.6f);
}

void Ui::Sound(Audio::Effect effect) {
  if (audio_) audio_->Play(effect);
}

void Ui::SkipSong() {
  if (!audio_ || audio_->CurrentSong().title.empty()) return;
  audio_->SkipSong();
  skip_pressed_ = 0.2f;
}

std::string Ui::OptionInfo(const Option& option) {
  Info& info = infos_[&option];
  info.age += dt_;
  if (info.pending.valid() &&
      info.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    info.text = info.pending.get();
  }
  if (!info.pending.valid() && info.age > 3.0f) {
    info.age = 0;
    info.pending = std::async(std::launch::async, option.info);
  }
  return info.text;
}

// ------------------------------------------------------------------ input

void Ui::OnCommand(Command command, InputDevice device) {
  device_ = device;
  DismissMessage();
  if (quit_asked_) {
    QuitCommand(command);
    return;
  }
  if (UpdateVisible()) {
    UpdateCommand(command);
    return;
  }
  switch (screen_) {
    case Screen::kHome: HomeCommand(command); break;
    case Screen::kSettings: SettingsCommand(command); break;
    case Screen::kNotice:
      if (command == Command::kAccept || command == Command::kBack) {
        launcher_config_->Set("welcome_seen", "true");
        launcher_config_->Save();
        Sound(Audio::Effect::kSelect);
        screen_ = Screen::kHome;
        screen_appear_ = 0.0f;
        splash_appear_ = 0.0f;
      }
      break;
    case Screen::kAbout:
      if (command == Command::kUp || command == Command::kDown) {
        int count = int(AboutTopics().size());
        topic_focus_ = (topic_focus_ + (command == Command::kUp ? -1 : 1) + count) % count;
        Sound(Audio::Effect::kList);
      } else if (command == Command::kBack) {
        Go(Screen::kHome);
      }
      break;
    case Screen::kControls:
      ControlsCommand(command);
      break;
    default:
      if (command == Command::kBack) Go(Screen::kHome);
      break;
  }
}

void Ui::Go(Screen screen) {
  if (screen == Screen::kHome && screen_ != Screen::kHome) Sound(Audio::Effect::kBack);
  screen_ = screen;
  splash_appear_ = 0.0f;
  screen_appear_ = 0.0f;
  in_options_ = false;
  armed_option_ = -1;
}

void Ui::HomeCommand(Command command) {
  int count = int(std::size(kHomeItems));
  switch (command) {
    case Command::kUp:
    case Command::kDown:
      home_focus_ = (home_focus_ + (command == Command::kUp ? -1 : 1) + count) % count;
      splash_appear_ = 0.0f;
      Sound(Audio::Effect::kMove);
      break;
    case Command::kAccept:
      Sound(Audio::Effect::kSelect);
      if (home_focus_ == 0) {
        if (on_play) on_play();
      } else {
        Go(home_focus_ == 1 ? Screen::kSettings : home_focus_ == 2 ? Screen::kControls
                                                                   : Screen::kAbout);
      }
      break;
    case Command::kBack:
      // Asked first: B is also "back" everywhere else, easily pressed once too
      // often.
      quit_asked_ = true;
      quit_focus_ = 0;
      quit_appear_ = 0.0f;
      Sound(Audio::Effect::kBack);
      break;
    default: break;
  }
}

void Ui::SettingsCommand(Command command) {
  settings_list_.mouse_focus = false;
  const Section& section = sections_[section_focus_];
  if (!in_options_) {
    int count = int(sections_.size());
    switch (command) {
      case Command::kUp:
      case Command::kDown:
        section_focus_ = (section_focus_ + (command == Command::kUp ? -1 : 1) + count) % count;
        splash_appear_ = 0.0f;
        Sound(Audio::Effect::kSection);
        break;
      case Command::kRight:
      case Command::kAccept:
        in_options_ = true;
        option_focus_ = 0;
        Sound(Audio::Effect::kSelect);
        break;
      case Command::kBack:
        Go(Screen::kHome);
        break;
      default: break;
    }
    return;
  }
  int count = int(section.options.size());
  const Option& option = section.options[option_focus_];
  bool applies = !option.applies || option.applies(ConfigFor(option));
  switch (command) {
    case Command::kUp:
    case Command::kDown:
      option_focus_ = (option_focus_ + (command == Command::kUp ? -1 : 1) + count) % count;
      armed_option_ = -1;
      Sound(Audio::Effect::kList);
      break;
    case Command::kLeft:
    case Command::kRight:
      if (!option.is_action() && applies) {
        option.Step(ConfigFor(option), command == Command::kLeft ? -1 : 1);
        Sound(Audio::Effect::kChange);
        if (option.local) {
          ApplyLocalSettings();  // may rebuild the sections
        } else if (option.choices[0].writes.count("monitor")) {
          BuildSections();  // Window size lists what fits the new monitor
        }
      }
      break;
    case Command::kAccept:
      if (!applies) {
        break;
      } else if (!option.is_action()) {
        option.Step(ConfigFor(option), 1);
        Sound(Audio::Effect::kChange);
        if (option.local) {
          ApplyLocalSettings();  // may rebuild the sections
        } else if (option.choices[0].writes.count("monitor")) {
          BuildSections();  // Window size lists what fits the new monitor
        }
      } else if (option.confirm && armed_option_ != option_focus_) {
        armed_option_ = option_focus_;  // press again to confirm
        Sound(Audio::Effect::kSelect);
      } else {
        armed_option_ = -1;
        Sound(Audio::Effect::kSelect);
        if (option.run) {
          option.run();
          // A file dialog it opened took the click's release.
          for (int button = 0; button < ImGuiMouseButton_COUNT; ++button) {
            ImGui::GetIO().AddMouseButtonEvent(button, false);
          }
        }
        if (option.outcome) {
          auto [text, ok] = option.outcome();
          if (!text.empty()) ShowMessage(text, ok);
        }
      }
      break;
    case Command::kBack:
      in_options_ = false;
      armed_option_ = -1;
      Sound(Audio::Effect::kBack);
      break;
    default: break;
  }
}

bool Ui::MouseIn(float x0, float y0, float x1, float y1) const {
  if (blocked_) return false;  // behind the update popup
  if (on_scroll_bar_) return false;
  if (language_.Captures()) return false;
  ImVec2 mouse = ImGui::GetIO().MousePos;
  return mouse.x >= x0 && mouse.x < x1 && mouse.y >= y0 && mouse.y < y1;
}

// ------------------------------------------------------------------ frame

float Ui::Anim(const std::string& key, float target, float seconds) {
  float& value = anims_[key];
  float step = seconds > 0 ? dt_ / seconds : 1.0f;
  value = target > value ? std::min(target, value + step) : std::max(target, value - step);
  return value;
}

void Ui::Frame(float width, float height, float dt) {
  // Sizes are in design units scaled to the window: the launcher's own 1000x700
  // window shows them at ~0.9, fullscreen at 1080p at ~1.4.
  scale_ = std::min(height / 770.0f, width / 1100.0f);
  dt_ = dt;
  ImGuiIO& io = ImGui::GetIO();
  mouse_moved_ = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
  hint_clicked_ = false;
  splash_appear_ = std::min(1.0f, splash_appear_ + dt / 0.26f);
  skip_pressed_ = std::max(0.0f, skip_pressed_ - dt);
  screen_appear_ = std::min(1.0f, screen_appear_ + dt / 0.22f);
  draw_ = ImGui::GetBackgroundDrawList();
  DrawBackground(width, height);
  // A new screen fades in and slides a little from the left: everything drawn
  // for it gets its alpha and position eased.
  int first_vertex = draw_->VtxBuffer.Size;
  blocked_ = UpdateVisible() || quit_asked_;
  switch (screen_) {
    case Screen::kHome: HomeScreen(width, height); break;
    case Screen::kSettings: SettingsScreen(width, height); break;
    case Screen::kControls: ControlsScreen(width, height); break;
    case Screen::kAbout: AboutScreen(width, height); break;
    case Screen::kNotice: NoticeScreen(width, height); break;
  }
  DrawMessage(width, height);
  if (!blocked_) {
    float s = scale_;
    ImDrawList* screen_draw = draw_;
    // Off the home screen it fades back, so the section's art behind the corner
    // shows; full again under the mouse or with its list open.
    float target = screen_ == Screen::kHome || language_.Captures() ? 1.0f : 0.35f;
    language_alpha_ += (target - language_alpha_) * std::min(1.0f, dt_ * 8.0f);
    ImDrawList* foreground = ImGui::GetForegroundDrawList();
    int first = foreground->VtxBuffer.Size;
    if (language_.Frame(foreground, condensed_, width - 40 * s, 24 * s, 21 * s, true)) {
      Sound(Audio::Effect::kChange);
      SaveLanguage();
    }
    for (int i = first; i < foreground->VtxBuffer.Size; ++i) {
      ImU32& col = foreground->VtxBuffer[i].col;
      ImU32 a = uint32_t(float((col >> IM_COL32_A_SHIFT) & 0xFF) * language_alpha_ + 0.5f);
      col = (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    }
    draw_ = screen_draw;
  } else {
    language_.Close();
  }
  if (screen_appear_ < 1.0f) {
    float t = EaseOut(screen_appear_), shift = (1.0f - t) * -24.0f * scale_;
    for (int i = first_vertex; i < draw_->VtxBuffer.Size; ++i) {
      ImDrawVert& v = draw_->VtxBuffer[i];
      v.pos.x += shift;
      uint32_t alpha = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
      v.col = (v.col & ~IM_COL32_A_MASK) | (uint32_t(alpha * t) << IM_COL32_A_SHIFT);
    }
  }
  blocked_ = false;
  if (UpdateVisible() && !quit_asked_) UpdatePopup(width, height);
  if (quit_asked_) QuitPopup(width, height);
}

void Ui::ShowMessage(std::string text, bool ok) {
  message_ = std::move(text);
  message_ok_ = ok;
  message_left_ = 8.0f;
  message_shown_ = 0.0f;
  // A problem stays until the player presses or clicks something: they need
  // time to read it (and to take a screenshot to report it).
  message_stays_ = !ok;
}

// A problem's message fades out on the player's next input, but not one
// that came with it (the press that showed it).
void Ui::DismissMessage() {
  if (!message_stays_ || message_shown_ < 0.5f) return;
  message_stays_ = false;
  message_left_ = std::min(message_left_, 0.4f);
}

// The message, bottom left (the hints are bottom right), on a dark band.
void Ui::DrawMessage(float width, float height) {
  message_shown_ += dt_;
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) DismissMessage();
  if (!message_stays_) message_left_ = std::max(0.0f, message_left_ - dt_);
  if (message_left_ <= 0) return;
  float s = scale_, size = 21 * s;
  float alpha = std::min({1.0f, message_left_ / 0.4f, message_shown_ / 0.2f});
  float wrap = width * 0.5f;
  ImVec2 text = condensed_->CalcTextSizeA(size, FLT_MAX, wrap, message_.c_str());
  float x = 64 * s, y = height - 40 * s - text.y, pad = 12 * s;
  draw_->AddRectFilled(ImVec2(x - pad, y - pad * 0.6f), ImVec2(x + text.x + pad, y + text.y + pad * 0.6f),
                       WithAlpha(IM_COL32(8, 20, 50, 230), alpha), 8 * s);
  draw_->AddRectFilled(ImVec2(x - pad, y - pad * 0.6f), ImVec2(x - pad + 4 * s, y + text.y + pad * 0.6f),
                       WithAlpha(message_ok_ ? IM_COL32(93, 184, 47, 255) : kAlarm, alpha), 2 * s);
  draw_->AddText(condensed_, size, ImVec2(x, y), WithAlpha(kCream, alpha), message_.c_str(),
                 nullptr, wrap);
}

// ------------------------------------------------------------------ drawing

void Ui::DrawBackground(float width, float height) {
  if (backdrop_) {
    // Fills the window, cropped evenly on the sides that don't fit.
    float fit = std::max(width / backdrop_.width, height / backdrop_.height);
    float du = (1.0f - width / (backdrop_.width * fit)) / 2;
    float dv = (1.0f - height / (backdrop_.height * fit)) / 2;
    draw_->AddImage(ImTextureRef(backdrop_.id()), ImVec2(0, 0), ImVec2(width, height),
                    ImVec2(du, dv), ImVec2(1 - du, 1 - dv));
  } else {
    draw_->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), kAtlantico);
  }
  // The watercolour covers the window's height, anchored to the right edge.
  float image_width = height * background_.width / std::max(1, background_.height);
  Image(draw_, background_, ImVec2(width - image_width, 0), ImVec2(width, height));
  // Seats the boards: darker under the left side.
  ImU32 dark = IM_COL32(6, 25, 52, 178), mid = IM_COL32(6, 25, 52, 64),
        clear = IM_COL32(6, 25, 52, 0);
  draw_->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width * 0.42f, height), dark, mid, mid, dark);
  draw_->AddRectFilledMultiColor(ImVec2(width * 0.42f, 0), ImVec2(width * 0.68f, height), mid,
                                 clear, clear, mid);
}

void Ui::DrawBoard(float x, float y, float w, float h, bool mirrored) {
  // Drop shadow: the board's own shape in black, softened by a few offsets.
  float s = scale_;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      ImVec2 offset(dx * 10.0f * s, 18.0f * s + dy * 10.0f * s);
      Image(draw_, panel_, ImVec2(x + offset.x, y + offset.y),
            ImVec2(x + w + offset.x, y + h + offset.y), IM_COL32(0, 0, 0, 28), mirrored);
    }
  }
  Image(draw_, panel_, ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32_WHITE, mirrored);
}

float Ui::DrawItem(float x, float y, const std::string& label, ImFont* font, float size,
                   float focus, float faint, unsigned int rest) {
  ImVec2 text = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
  float em = size;
  // The item's box: padding 0.08em 0.55em 0.12em 0.15em around the text.
  float box_x1 = x + 0.15f * em + text.x + 0.55f * em;
  float box_y1 = y + 0.08f * em + size * 1.15f + 0.12f * em;
  float sweep = std::max(EaseOut(focus), faint > 0 ? 1.0f : 0.0f);
  if (sweep > 0.0f) {
    // The brush stroke, from the left, a little larger than the box.
    float x0 = x - 0.45f * em, x1 = box_x1 + 0.35f * em;
    float alpha = std::max(EaseOut(focus), faint);
    Image(draw_, stroke_, ImVec2(x0, y - 0.02f * em),
          ImVec2(x0 + (x1 - x0) * sweep, box_y1 + 0.08f * em), WithAlpha(IM_COL32_WHITE, alpha));
  }
  ImU32 color = LerpColor(rest ? ImU32(rest) : kCream, kInk, std::clamp(focus * 1.5f, 0.0f, 1.0f));
  draw_->AddText(font, size, ImVec2(x + 0.15f * em, y + 0.08f * em + size * 0.07f), color,
                 label.c_str());
  return box_y1;
}

void Ui::DrawSplash(const Texture& texture, float cx, float cy, float width, float appear) {
  if (!texture) return;
  float t = EaseOut(appear);
  float w = width * (0.9f + 0.1f * t);
  float h = w * texture.height / std::max(1, texture.width);
  float angle = (1.0f - t) * -3.0f * 3.14159265f / 180.0f;
  float c = std::cos(angle), sn = std::sin(angle);
  auto corner = [&](float dx, float dy, float drop) {
    return ImVec2(cx + dx * c - dy * sn, cy + dx * sn + dy * c + drop);
  };
  const ImVec2 uv[4] = {{texture.u0, texture.v0}, {texture.u1, texture.v0},
                        {texture.u1, texture.v1}, {texture.u0, texture.v1}};
  float hw = w * 0.5f, hh = h * 0.5f, drop = 10 * scale_;
  draw_->AddImageQuad(ImTextureRef(texture.id()), corner(-hw, -hh, drop), corner(hw, -hh, drop),
                      corner(hw, hh, drop), corner(-hw, hh, drop), uv[0], uv[1], uv[2], uv[3],
                      IM_COL32(0, 0, 0, int(80 * t)));
  draw_->AddImageQuad(ImTextureRef(texture.id()), corner(-hw, -hh, 0), corner(hw, -hh, 0),
                      corner(hw, hh, 0), corner(-hw, hh, 0), uv[0], uv[1], uv[2], uv[3],
                      IM_COL32(255, 255, 255, int(255 * t)));
}

void Ui::DrawNowPlaying(float x, float bottom, float width) {
  // A yellow rule, the song's cover, its title over its artist, and the
  // next-song button; the song's part fades out and in when the song changes.
  float s = scale_;
  Song song = audio_ ? audio_->CurrentSong() : Song{};
  bool changing = song.title != shown_song_.title;
  float appear = Anim("song", changing || song.title.empty() ? 0.0f : 1.0f, 0.4f);
  if (changing && appear <= 0.0f) {
    shown_song_ = song;
    shown_song_at_ = ImGui::GetTime();
  }
  if (shown_song_.title.empty() || appear <= 0.0f) return;
  float title_size = 18 * s, artist_size = 18 * s;
  float top = bottom - title_size * 1.2f - artist_size * 1.2f;
  float slide = (1.0f - EaseOut(appear)) * -12 * s;

  draw_->AddRectFilled(ImVec2(x + slide, top), ImVec2(x + slide + 4 * s, bottom),
                       WithAlpha(IM_COL32(247, 209, 23, 255), appear));
  // The cover beside the rule, as tall as it.
  float text_x = x + slide + 10 * s;
  const Texture* cover = shown_song_.cover > 0 && size_t(shown_song_.cover) < covers_.size()
                             ? &covers_[size_t(shown_song_.cover)]
                             : nullptr;
  if (cover && *cover) {
    float side = bottom - top;
    Image(draw_, *cover, ImVec2(text_x, top), ImVec2(text_x + side, bottom),
          WithAlpha(IM_COL32_WHITE, appear));
    text_x += side + 8 * s;
  }

  // The next-song button at the board's right edge: outlined, filled with the
  // focus yellow when pointed at or pressed.
  float r = 17 * s, cx = x + width - r, cy = (top + bottom) * 0.5f;
  float text_end = cx - r - 12 * s;
  bool hover = MouseIn(cx - r, cy - r, cx + r, cy + r);
  if (hover) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) SkipSong();
  bool lit = hover || skip_pressed_ > 0;
  ImU32 yellow = IM_COL32(247, 209, 23, 255);
  if (lit) draw_->AddCircleFilled(ImVec2(cx, cy), r, WithAlpha(yellow, appear));
  draw_->AddCircle(ImVec2(cx, cy), r - 1 * s, WithAlpha(lit ? yellow : kCream, appear), 0, 2 * s);
  ImU32 icon = WithAlpha(lit ? kInk : kCream, appear);
  float u = r * 0.42f;
  draw_->AddTriangleFilled(ImVec2(cx - u, cy - u), ImVec2(cx + u * 0.55f, cy), ImVec2(cx - u, cy + u),
                           icon);
  draw_->AddRectFilled(ImVec2(cx + u * 0.6f, cy - u), ImVec2(cx + u * 0.95f, cy + u), icon);
  // A line too long for the space scrolls to its end and back: still for 2 s,
  // then 30 px a second (at 1080p), still at the end for 2 s, then from the start.
  float since = float(ImGui::GetTime() - shown_song_at_);
  auto scroll = [&](ImFont* font, float size, const std::string& text) {
    float overflow = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x - (text_end - text_x);
    if (overflow <= 0) return 0.0f;
    float speed = 30 * s, pause = 2.0f, travel = overflow / speed;
    float t = std::fmod(since, 2 * pause + travel);
    return -std::clamp((t - pause) * speed, 0.0f, overflow);
  };
  draw_->PushClipRect(ImVec2(text_x, top - 4 * s), ImVec2(text_end, bottom + 4 * s), true);
  draw_->AddText(bold_, title_size,
                 ImVec2(text_x + scroll(bold_, title_size, shown_song_.title), top),
                 WithAlpha(kCream, appear), shown_song_.title.c_str());
  draw_->AddText(condensed_, artist_size,
                 ImVec2(text_x + scroll(condensed_, artist_size, shown_song_.artist),
                        top + title_size * 1.2f),
                 WithAlpha(kCreamDim, appear), shown_song_.artist.c_str());
  draw_->PopClipRect();
}

// A note on crumpled paper (custompanels_38) between two corners: the sheet's
// top part at the note's width, unstretched, with its torn bottom edge faded in
// below, so every side keeps the sheet's own torn border.
static void PaperNote(ImDrawList* draw, const Texture& paper, float x0, float y0, float x1, float y1) {
  // In the 1024x512 picture: the sheet with its border (x 2-778, y 8-508), the
  // rows taken for the bottom edge and the rows the two parts fade over.
  constexpr float kL = 2, kR = 778, kT = 8, kB = 508, kBottomRows = 60, kFadeRows = 20;
  float k = (x1 - x0) / (kR - kL);  // screen pixels per picture pixel
  // Picture pixels to texture coordinates. Not through u0..u1: those cover
  // only the part with visible pixels (LoadDds trims to it), not the picture.
  auto uv = [](float px, float py) { return ImVec2(px / 1024.0f, py / 512.0f); };
  // A textured rectangle whose alpha goes from `top_alpha` to `bottom_alpha`.
  auto quad = [&](float top, float bottom, float py0, float py1, int top_alpha, int bottom_alpha) {
    ImU32 c0 = IM_COL32(255, 255, 255, top_alpha), c1 = IM_COL32(255, 255, 255, bottom_alpha);
    ImVec2 uv0 = uv(kL, py0), uv1 = uv(kR, py1);
    draw->PushTexture(ImTextureRef(paper.id()));
    draw->PrimReserve(6, 4);
    ImDrawIdx i = ImDrawIdx(draw->_VtxCurrentIdx);
    for (int n : {0, 1, 2, 0, 2, 3}) draw->PrimWriteIdx(ImDrawIdx(i + n));
    draw->PrimWriteVtx(ImVec2(x0, top), uv0, c0);
    draw->PrimWriteVtx(ImVec2(x1, top), ImVec2(uv1.x, uv0.y), c0);
    draw->PrimWriteVtx(ImVec2(x1, bottom), uv1, c1);
    draw->PrimWriteVtx(ImVec2(x0, bottom), ImVec2(uv0.x, uv1.y), c1);
    draw->PopTexture();
  };
  float bottom_h = std::min(kBottomRows * k, (y1 - y0) * 0.5f), fade_h = kFadeRows * k;
  float bottom_top = y1 - bottom_h;
  // The top part reaches into the bottom one's fade (no deeper than the
  // sheet's clean rows; a very tall note stretches instead).
  float top_end = std::min(y1, bottom_top + fade_h);
  float top_rows = std::min((top_end - y0) / k, kB - kBottomRows - kT);
  quad(y0, top_end, kT, kT + top_rows, 255, 255);
  float fade_end = std::min(y1, bottom_top + fade_h);
  float rows_per_px = kBottomRows / bottom_h;
  quad(bottom_top, fade_end, kB - kBottomRows, kB - kBottomRows + (fade_end - bottom_top) * rows_per_px,
       0, 255);
  quad(fade_end, y1, kB - kBottomRows + (fade_end - bottom_top) * rows_per_px, kB, 255, 255);
}

// The number of lines DrawWrapped breaks `text` into.
static int WrappedLines(ImFont* font, float size, float wrap, const std::string& text) {
  const char* line = text.c_str();
  const char* end = line + text.size();
  int lines = 0;
  while (line < end) {
    const char* next = font->CalcWordWrapPosition(size, line, end, wrap);
    if (next == line) ++next;
    ++lines;
    line = next;
    while (line < end && *line == ' ') ++line;
  }
  return lines;
}

float Ui::DrawWrapped(ImFont* font, float size, float x, float y, float wrap, ImU32 color,
                      const std::string& text) {
  const char* line = text.c_str();
  const char* end = line + text.size();
  float line_height = size * 1.4f, top = y;
  while (line < end) {
    const char* next = font->CalcWordWrapPosition(size, line, end, wrap);
    if (next == line) ++next;  // a word longer than the line
    draw_->AddText(font, size, ImVec2(x, y), color, line, next);
    y += line_height;
    line = next;
    while (line < end && *line == ' ') ++line;
  }
  return y - top;
}

void Ui::DrawHints(float width, float height, const std::vector<Hint>& hints) {
  if (blocked_) return;  // the update popup shows its own
  float s = scale_;
  float size = 21 * s;
  // Laid out right to left from the bottom-right corner. A hint is also a
  // button: clicking it does what its key does.
  float x = width - 64 * s, y = height - 40 * s - size * 1.3f;
  for (int i = int(hints.size()) - 1; i >= 0; --i) {
    const Hint& hint = hints[i];
    const char* text = T(hint.label);
    ImVec2 label = condensed_->CalcTextSizeA(size, FLT_MAX, 0, text);
    float hint_right = x;
    x -= label.x;
    // Pointed at: the label in the focus yellow and a hand cursor. The glyph's
    // width isn't known yet, so this frame tests the label plus a key's width.
    bool clickable = hint.skip_song || hint.command != Command::kNone;
    bool hover = clickable && MouseIn(x - 90 * s, y - 6 * s, hint_right + 4 * s,
                                      y + size * 1.3f + 6 * s);
    draw_->AddText(condensed_, size, ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 150), text);
    draw_->AddText(condensed_, size, ImVec2(x, y), hover ? IM_COL32(247, 209, 23, 255) : kCream,
                   text);
    float middle = y + size * 0.6f;
    bool controller = device_ == InputDevice::kController;
    const char* glyph = controller ? hint.button : hint.key;
    bool arrows = std::string(glyph) == "<>";
    if (controller && !arrows && std::strlen(glyph) == 1 &&
        controller_style_ == ControllerStyle::kPlayStation) {
      // A PlayStation face button: its symbol on a dark button.
      float r = size * 0.62f;
      x -= 10 * s + 2 * r;
      DrawPlayStationFace(x + r, middle, r, glyph[0]);
    } else if (controller && !arrows && std::strlen(glyph) == 1) {
      // A face button in its console colour.
      float r = size * 0.62f;
      x -= 10 * s + 2 * r;
      DrawXboxFace(x + r, middle, r, glyph[0]);
    } else {
      x -= 10 * s;
      x -= DrawKey(x, middle, size * 0.75f, glyph, controller);
    }
    bool over = clickable && MouseIn(x - 4 * s, y - 6 * s, hint_right + 4 * s,
                                     y + size * 1.3f + 6 * s);
    if (over) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hint_clicked_) {
      // Once a frame: the popup a hint opens (Quit) has its own hint in the same
      // place (Cancel), which the same click would press too.
      hint_clicked_ = true;
      if (hint.skip_song) {
        SkipSong();
      } else if (hint.command != Command::kNone) {
        OnCommand(hint.command, device_);
      }
    }
    x -= 28 * s;
  }
}

// ------------------------------------------------------------------ screens

void Ui::HomeScreen(float width, float height) {
  float s = scale_;
  float board_w = 440 * s, board_h = std::min(720 * s, height * 0.84f);
  float board_x = 64 * s, board_y = (height - board_h) * 0.5f;
  DrawBoard(board_x, board_y, board_w, board_h, false);

  float pad_x = 48 * s, y = board_y + 64 * s;
  float logo_w = 156 * s, logo_h = logo_w * logo_.height / std::max(1, logo_.width);
  Image(draw_, logo_, ImVec2(board_x + pad_x, y), ImVec2(board_x + pad_x + logo_w, y + logo_h));
  y += logo_h + 48 * s;

  // Leaves the board's foot to the song.
  float size = 44 * s;
  for (int i = 0; i < int(std::size(kHomeItems)); ++i) {
    float top = y;
    float focus = Anim("home" + std::to_string(i), i == home_focus_ ? 1.0f : 0.0f, 0.24f);
    y = DrawItem(board_x + pad_x, y, T(kHomeItems[i]), bold_, size, focus);
    if (MouseIn(board_x, top, board_x + board_w, y)) {
      if (mouse_moved_ && home_focus_ != i) {
        home_focus_ = i;
        splash_appear_ = 0.0f;
        Sound(Audio::Effect::kMove);
      }
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        home_focus_ = i;
        HomeCommand(Command::kAccept);
      }
    }
    y += 0.15f * size;
  }
  DrawNowPlaying(board_x + pad_x, board_y + board_h - 88 * s, board_w - 2 * pad_x);
  // The focused item's painted icon, large, over the board's right edge.
  // The focused item's painted icon, in the light upper right of the watercolour.
  DrawSplash(home_icons_[home_focus_], width - 170 * s, 200 * s, 290 * s, splash_appear_);
  if (audio_ && !audio_->CurrentSong().title.empty()) {
    DrawHints(width, height,
              {{"N", "Y", "Next song", Command::kNone, true}, {"Enter", "A", "Select", Command::kAccept}, {"Esc", "B", "Quit", Command::kBack}});
  } else {
    DrawHints(width, height, {{"Enter", "A", "Select", Command::kAccept}, {"Esc", "B", "Quit", Command::kBack}});
  }
}

void Ui::SettingsScreen(float width, float height) {
  float s = scale_;
  float top = std::max(height * 0.08f, height * 0.5f - 360 * s);

  // Sections board: the heading and the section list, as tall as they need.
  float list_x = 56 * s, list_w = 380 * s;
  float heading_size = 44 * s, item_size = 35 * s;
  float item_h = item_size * 1.35f + 0.15f * item_size;
  float list_h = 64 * s + heading_size * 1.1f + 40 * s + item_h * sections_.size() + 88 * s;
  DrawBoard(list_x, top, list_w, list_h, false);
  float x = list_x + 48 * s, y = top + 64 * s;
  draw_->AddText(bold_, heading_size, ImVec2(x, y), kCream, T("Settings"));
  y += heading_size * 1.1f + 40 * s;
  for (int i = 0; i < int(sections_.size()); ++i) {
    float row_top = y;
    bool focused = !in_options_ && i == section_focus_;
    float focus = Anim("section" + std::to_string(i), focused ? 1.0f : 0.0f, 0.24f);
    float faint = in_options_ && i == section_focus_ ? 0.35f : 0.0f;
    y = DrawItem(x, y, sections_[i].label, bold_, item_size, focus, faint);
    if (MouseIn(list_x, row_top, list_x + list_w, y)) {
      if ((mouse_moved_ && (section_focus_ != i || in_options_)) ||
          ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (section_focus_ != i) {
          splash_appear_ = 0.0f;
          Sound(Audio::Effect::kSection);
        } else if (in_options_) {
          Sound(Audio::Effect::kBack);
        }
        section_focus_ = i;
        in_options_ = false;
        armed_option_ = -1;
      }
    }
    y += 0.15f * item_size;
  }

  // Options board, mirrored, with the section's icon over its top-right corner.
  const Section& section = sections_[section_focus_];
  float board_x = list_x + list_w + 40 * s;
  float board_w = std::min(580 * s, width - board_x - 48 * s);
  float board_h = std::min(720 * s, height * 0.84f), board_y = (height - board_h) * 0.5f;
  board_y = std::max(board_y, top);
  DrawBoard(board_x, board_y, board_w, board_h, true);
  // Data's tile (the World Cup cover) is a smaller picture than the others: drawn larger.
  float icon_w = (section.icon == 2 ? 310 : 230) * s;
  DrawSplash(section_icons_[section.icon], std::min(board_x + board_w + 50 * s - icon_w * 0.5f, width - icon_w * 0.55f),
             board_y - 70 * s + icon_w * 0.33f, icon_w, splash_appear_);

  float option_size = 22 * s, em = option_size;
  // The help on a paper note at the board's foot, as tall as the section's
  // longest help, so the list above doesn't move from option to option.
  float help_size = 18 * s, pad_x = 18 * s, pad_y = 14 * s;
  float note_x0 = board_x + 48 * s, note_x1 = board_x + board_w - 48 * s;
  float help_wrap = note_x1 - note_x0 - 2 * pad_x;
  int help_lines = 1;
  for (const Option& option : section.options) {
    help_lines = std::max(help_lines, WrappedLines(book_, help_size, help_wrap, option.help));
  }
  float note_bottom = board_y + board_h - 74 * s;
  float note_top = note_bottom - float(help_lines) * help_size * 1.4f - 2 * pad_y;
  // The options between the board's top and the note; they scroll when they don't fit.
  float row_h = 0.4f * em + option_size * 1.25f + 0.4f * em, step = row_h + 4 * s;
  float list_top = board_y + 64 * s + 70 * s, list_bottom = note_top - 12 * s;
  int count = int(section.options.size());
  bool scrolls = float(count) * step > list_bottom - list_top + 0.5f;
  float row_x0 = board_x + 48 * s + 0.9f * em;
  float row_x1 = board_x + board_w - 48 * s - 0.6f * em - (scrolls ? 24 * s : 0.0f);
  float first_y = BeginScrollList(settings_list_, "settings_options", section_focus_, board_x,
                                  board_x + board_w, list_top, list_bottom, step, count,
                                  in_options_ ? option_focus_ : -1, true);
  bool accept_click = false;
  for (int i = 0; i < count; ++i) {
    const Option& option = section.options[i];
    float row_top = first_y + float(i) * step;
    if (row_top + row_h < list_top || row_top > list_bottom) continue;
    bool focused = in_options_ && i == option_focus_;
    float focus = Anim("option" + std::to_string(section_focus_) + "_" + std::to_string(i),
                       focused ? 1.0f : 0.0f, 0.24f);
    float sweep = EaseOut(focus);
    if (sweep > 0) {
      float x0 = row_x0 - 0.9f * em - 0.9f * em, x1 = row_x1 + 0.6f * em + 0.4f * em;
      Image(draw_, stroke_, ImVec2(x0, row_top - 0.1f * em),
            ImVec2(x0 + (x1 - x0) * sweep, row_top + row_h + 0.1f * em));
    }
    ImU32 ink = LerpColor(kCream, kInk, std::clamp(focus * 1.5f, 0.0f, 1.0f));
    bool applies = !option.applies || option.applies(ConfigFor(option));
    if (!applies) ink = WithAlpha(ink, 0.45f);  // greyed out: doesn't apply now
    float text_y = row_top + 0.4f * em + option_size * 0.08f;
    draw_->AddText(book_, option_size, ImVec2(row_x0, text_y), ink, option.label.c_str());

    // The value (or the action's button), right-aligned.
    float value_size = option_size * 1.15f;
    float vx = row_x1, vy = row_top + (row_h - value_size * 1.1f) * 0.5f;
    if (!option.is_action()) {
      std::string value = option.choices[option.Current(ConfigFor(option))].label;
      ImVec2 text = condensed_->CalcTextSizeA(value_size, FLT_MAX, 0, value.c_str());
      float arrow = value_size * 0.5f;
      ImU32 arrow_color = WithAlpha(ink, applies ? focus : 0.0f);
      Arrow(draw_, vx - arrow * 0.5f, vy + value_size * 0.55f, arrow, true, arrow_color);
      vx -= arrow + 0.5f * value_size + text.x;
      draw_->AddText(condensed_, value_size, ImVec2(vx, vy), ink, value.c_str());
      Arrow(draw_, vx - 0.5f * value_size - arrow * 0.5f, vy + value_size * 0.55f, arrow, false,
            arrow_color);
    } else {
      bool armed = armed_option_ == i;
      std::string label = armed ? T("Sure? Press again") : option.action;
      ImVec2 text = condensed_->CalcTextSizeA(value_size, FLT_MAX, 0, label.c_str());
      float pad = 0.6f * value_size, pill_w = text.x + 2 * pad, pill_h = value_size * 1.3f;
      float px = vx - pill_w, py = row_top + (row_h - pill_h) * 0.5f;
      if (armed) {
        draw_->AddRectFilled(ImVec2(px, py), ImVec2(px + pill_w, py + pill_h), kAlarm, pill_h);
      } else {
        // Focused, it fills white as the brush stroke sweeps in under it.
        if (focus > 0) {
          draw_->AddRectFilled(ImVec2(px, py), ImVec2(px + pill_w, py + pill_h),
                               WithAlpha(IM_COL32_WHITE, EaseOut(focus) * (applies ? 0.7f : 0.3f)),
                               pill_h);
        }
        draw_->AddRect(ImVec2(px, py), ImVec2(px + pill_w, py + pill_h), ink, pill_h, 0,
                       2 * s);
      }
      draw_->AddText(condensed_, value_size, ImVec2(px + pad, py + (pill_h - value_size) * 0.45f),
                     armed ? kCream : ink, label.c_str());
      if (option.info) {
        std::string info = OptionInfo(option);
        ImVec2 info_size = condensed_->CalcTextSizeA(value_size, FLT_MAX, 0, info.c_str());
        draw_->AddText(condensed_, value_size, ImVec2(px - 0.6f * value_size - info_size.x, vy),
                       WithAlpha(ink, 0.85f), info.c_str());
      }
    }

    // Mouse: pointing focuses the row, clicking changes it.
    if (MouseIn(board_x, row_top, board_x + board_w, row_top + row_h) &&
        MouseIn(board_x, list_top, board_x + board_w, list_bottom)) {
      if (mouse_moved_ && (!in_options_ || option_focus_ != i)) {
        settings_list_.mouse_focus = true;
        in_options_ = true;
        option_focus_ = i;
        armed_option_ = -1;
        Sound(Audio::Effect::kList);
      }
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        settings_list_.mouse_focus = true;
        in_options_ = true;
        option_focus_ = i;
        accept_click = true;  // after the frame: it may rebuild the sections drawn here
      }
    }
  }
  EndScrollList(settings_list_, board_x + board_w - 48 * s - 10 * s, true);

  // The focused option's help (or the section's first) on the note.
  const Option& described = section.options[in_options_ ? option_focus_ : 0];
  if (note_) PaperNote(draw_, note_, note_x0, note_top, note_x1, note_bottom);
  DrawWrapped(book_, help_size, note_x0 + pad_x, note_top + pad_y, help_wrap,
              note_ ? kInk : kCreamDim, described.help);

  if (in_options_) {
    if (section.options[option_focus_].is_action()) {
      DrawHints(width, height, {{"Enter", "A", "Select", Command::kAccept}, {"Esc", "B", "Back", Command::kBack}});
    } else {
      DrawHints(width, height, {{"<>", "<>", "Change", Command::kRight}, {"Esc", "B", "Back", Command::kBack}});
    }
  } else {
    DrawHints(width, height, {{"Enter", "A", "Select", Command::kAccept}, {"Esc", "B", "Back", Command::kBack}});
  }
  if (accept_click) SettingsCommand(Command::kAccept);
}

float Ui::DrawKey(float right, float middle, float key_size, const char* text, bool dark) {
  float s = scale_;
  bool arrows = std::string(text) == "<>";
  ImVec2 key = bold_->CalcTextSizeA(key_size, FLT_MAX, 0, arrows ? "" : text);
  float key_w = arrows ? 2.6f * key_size : std::max(key.x + 0.9f * key_size, 1.8f * key_size);
  float key_h = key_size * 1.4f;
  float x = right - key_w, top = middle - key_h * 0.5f;
  ImU32 cap = dark ? IM_COL32(43, 47, 51, 255) : kCream;
  ImU32 ink = dark ? IM_COL32_WHITE : kInk;
  draw_->AddRectFilled(ImVec2(x, top), ImVec2(x + key_w, top + key_h), cap, 6 * s);
  draw_->AddRectFilled(ImVec2(x, top + key_h - 3 * s), ImVec2(x + key_w, top + key_h),
                       IM_COL32(12, 30, 20, 64), 6 * s, ImDrawFlags_RoundCornersBottom);
  if (arrows) {
    Arrow(draw_, x + key_w * 0.3f, middle - 1 * s, key_size * 0.7f, false, ink);
    Arrow(draw_, x + key_w * 0.7f, middle - 1 * s, key_size * 0.7f, true, ink);
  } else {
    draw_->AddText(bold_, key_size, ImVec2(x + (key_w - key.x) * 0.5f, top + key_size * 0.2f), ink,
                   text);
  }
  return key_w;
}

void Ui::DrawXboxFace(float x, float y, float r, char letter) {
  float s = scale_;
  ImU32 colors[] = {IM_COL32(93, 184, 47, 255), IM_COL32(226, 65, 47, 255),
                    IM_COL32(47, 127, 226, 255), IM_COL32(242, 194, 27, 255)};
  int index = letter == 'A' ? 0 : letter == 'B' ? 1 : letter == 'X' ? 2 : 3;
  ImVec2 c(x, y);
  draw_->AddCircleFilled(ImVec2(c.x, c.y + 2 * s), r, IM_COL32(0, 0, 0, 90));
  draw_->AddCircleFilled(c, r, colors[index]);
  draw_->AddCircle(c, r - 0.5f * s, IM_COL32(0, 0, 0, 60), 0, std::max(1.0f, s));
  // The letter centred on its own outline (the font's line box sits it low).
  float size = r * 1.3f;
  ImWchar ch = ImWchar(letter);
  if (const ImFontGlyph* glyph = bold_->GetFontBaked(size)->FindGlyph(ch)) {
    ImVec2 at(c.x - (glyph->X0 + glyph->X1) * 0.5f, c.y - (glyph->Y0 + glyph->Y1) * 0.5f);
    const char text[2] = {letter, 0};
    draw_->AddText(bold_, size, at, index == 3 ? kInk : IM_COL32_WHITE, text);
  }
}

void Ui::DrawPlayStationFace(float x, float y, float r, char xbox_letter) {
  float s = scale_;
  ImVec2 c(x, y);
  draw_->AddCircleFilled(ImVec2(c.x, c.y + 2 * s), r, IM_COL32(0, 0, 0, 90));
  draw_->AddCircleFilled(c, r, IM_COL32(43, 47, 51, 255));
  float u = r * 0.45f, t = std::max(2.2f * s, r * 0.12f);
  switch (xbox_letter) {
    case 'A':  // Cross
      draw_->AddLine(ImVec2(c.x - u, c.y - u), ImVec2(c.x + u, c.y + u), IM_COL32(124, 178, 232, 255), t);
      draw_->AddLine(ImVec2(c.x - u, c.y + u), ImVec2(c.x + u, c.y - u), IM_COL32(124, 178, 232, 255), t);
      break;
    case 'B':  // Circle
      draw_->AddCircle(c, u, IM_COL32(255, 102, 102, 255), 0, t);
      break;
    case 'Y':  // Triangle
      draw_->AddTriangle(ImVec2(c.x, c.y - u), ImVec2(c.x + u, c.y + u * 0.75f),
                         ImVec2(c.x - u, c.y + u * 0.75f), IM_COL32(64, 226, 160, 255), t);
      break;
    default:  // Square
      draw_->AddRect(ImVec2(c.x - u * 0.8f, c.y - u * 0.8f), ImVec2(c.x + u * 0.8f, c.y + u * 0.8f),
                     IM_COL32(255, 105, 248, 255), 0, 0, t);
      break;
  }
}

void Ui::DrawGlyph(float x, float y, float size, const ControlRow& row) {
  float s = scale_;
  ImU32 plastic = IM_COL32(43, 47, 51, 255), shadow = IM_COL32(0, 0, 0, 100);
  ImU32 ink = IM_COL32(230, 230, 230, 255);
  float label_size = size * 0.45f;
  ImU32 label_color = IM_COL32_WHITE;
  auto rounded = [&](float w, float h, float rounding, ImDrawFlags flags) {
    draw_->AddRectFilled(ImVec2(x - w / 2, y - h / 2 + 2 * s), ImVec2(x + w / 2, y + h / 2 + 2 * s),
                         shadow, rounding, flags);
    draw_->AddRectFilled(ImVec2(x - w / 2, y - h / 2), ImVec2(x + w / 2, y + h / 2), plastic,
                         rounding, flags);
  };
  auto label_text = [&](const char* text) {
    ImVec2 size_text = bold_->CalcTextSizeA(label_size, FLT_MAX, 0, text);
    draw_->AddText(bold_, label_size, ImVec2(x - size_text.x * 0.5f, y - label_size * 0.55f),
                   label_color, text);
  };
  bool playstation = controller_style_ == ControllerStyle::kPlayStation;
  auto named = [&](const char* name) { return std::strcmp(row.button, name) == 0; };

  switch (row.glyph) {
    case Glyph::kFace: {
      if (playstation) {
        DrawPlayStationFace(x, y, size * 0.5f, row.button[0]);
        return;
      }
      DrawXboxFace(x, y, size * 0.5f, row.button[0]);
      return;
    }
    case Glyph::kBumper:
      rounded(size * 1.7f, size * 0.9f, size * 0.4f, 0);
      label_text(playstation ? (named("LB") ? "L1" : "R1") : row.button);
      return;
    case Glyph::kTrigger:
      rounded(size * 1.45f, size * 1.2f, size * 0.55f, ImDrawFlags_RoundCornersTop);
      label_text(playstation ? (named("LT") ? "L2" : "R2") : row.button);
      return;
    case Glyph::kStick: {
      // A stick from above; pressing it (L3 / R3, LS / RS on Xbox) has a
      // second ring.
      float r = size * 0.5f;
      bool press = named("L3") || named("R3");
      draw_->AddCircleFilled(ImVec2(x, y + 2 * s), r, shadow);
      draw_->AddCircleFilled(ImVec2(x, y), r, plastic);
      draw_->AddCircle(ImVec2(x, y), r - 2 * s, IM_COL32(85, 91, 97, 255), 0, 3 * s);
      if (press) draw_->AddCircle(ImVec2(x, y), r * 0.55f, IM_COL32(85, 91, 97, 255), 0, 2 * s);
      const char* text = row.button;
      if (!playstation && press) text = named("L3") ? "LS" : "RS";
      label_size = size * (press ? 0.38f : 0.45f);
      label_text(text);
      return;
    }
    case Glyph::kPad: {
      // The D-pad: a cross with the pressed arm lit.
      float arm = size * 0.36f, len = size * 0.52f, round = size * 0.08f;
      for (float dy : {2 * s, 0.0f}) {
        ImU32 color = dy ? shadow : plastic;
        draw_->AddRectFilled(ImVec2(x - arm * 0.5f, y - len + dy), ImVec2(x + arm * 0.5f, y + len + dy),
                             color, round);
        draw_->AddRectFilled(ImVec2(x - len, y - arm * 0.5f + dy), ImVec2(x + len, y + arm * 0.5f + dy),
                             color, round);
      }
      float h = arm * 0.5f;
      if (named("Up")) {
        draw_->AddRectFilled(ImVec2(x - h, y - len), ImVec2(x + h, y - h), ink, round, ImDrawFlags_RoundCornersTop);
      } else if (named("Down")) {
        draw_->AddRectFilled(ImVec2(x - h, y + h), ImVec2(x + h, y + len), ink, round, ImDrawFlags_RoundCornersBottom);
      } else if (named("Left")) {
        draw_->AddRectFilled(ImVec2(x - len, y - h), ImVec2(x - h, y + h), ink, round, ImDrawFlags_RoundCornersLeft);
      } else if (named("Right")) {
        draw_->AddRectFilled(ImVec2(x + h, y - h), ImVec2(x + len, y + h), ink, round, ImDrawFlags_RoundCornersRight);
      }
      draw_->AddCircleFilled(ImVec2(x, y), arm * 0.22f, IM_COL32(85, 91, 97, 255));
      return;
    }
    case Glyph::kSmall: {
      // Start / Back as on the pad: Menu and View on Xbox, Options and Create
      // on PlayStation (as the game's quit shortcut, pc/src/quit_shortcut.cpp).
      float h = size * 0.95f, w = size * 1.45f;
      rounded(w, h, h * 0.5f, 0);
      float u = h * 0.22f, t = std::max(1.5f * s, size * 0.07f);
      if (named("Start")) {
        for (int i = -1; i <= 1; ++i) {
          draw_->AddLine(ImVec2(x - u * 1.2f, y + i * u * 0.8f), ImVec2(x + u * 1.2f, y + i * u * 0.8f),
                         ink, t);
        }
      } else if (playstation) {
        ImVec2 base(x, y + u * 1.1f);
        draw_->AddLine(base, ImVec2(x, y - u * 1.1f), ink, t);
        draw_->AddLine(ImVec2(base.x - u * 0.4f, base.y), ImVec2(x - u * 1.3f, y - u * 0.6f), ink, t);
        draw_->AddLine(ImVec2(base.x + u * 0.4f, base.y), ImVec2(x + u * 1.3f, y - u * 0.6f), ink, t);
      } else {
        draw_->AddRect(ImVec2(x - u * 1.2f, y - u * 0.9f), ImVec2(x + u * 0.4f, y + u * 0.5f), ink, 0,
                       0, t);
        draw_->AddRect(ImVec2(x - u * 0.4f, y - u * 0.3f), ImVec2(x + u * 1.2f, y + u * 1.1f), ink, 0,
                       0, t);
      }
      return;
    }
  }
}

void Ui::TwoBoards(float width, float height, const char* heading, float list_height, float* x,
                   float* y, float* w, float* h, float* list_y) {
  float s = scale_;
  float top = std::max(height * 0.08f, height * 0.5f - 360 * s);
  float list_x = 56 * s, list_w = 380 * s, heading_size = 44 * s;
  float list_h = 64 * s + heading_size * 1.1f + 40 * s + list_height + 88 * s;
  DrawBoard(list_x, top, list_w, list_h, false);
  draw_->AddText(bold_, heading_size, ImVec2(list_x + 48 * s, top + 64 * s), kCream, heading);
  *list_y = top + 64 * s + heading_size * 1.1f + 40 * s;
  *x = list_x + list_w + 40 * s;
  *w = std::min(580 * s, width - *x - 48 * s);
  *h = std::min(720 * s, height * 0.84f);
  *y = std::max((height - *h) * 0.5f, top);
  DrawBoard(*x, *y, *w, *h, true);
}

// ------------------------------------------------------------------ scroll lists

float Ui::BeginScrollList(ScrollList& list, const char* key, int owner, float x0, float x1, float top,
                          float bottom, float row_h, int rows, int focus, bool active) {
  if (list.owner != owner) {  // another section: from the top, at once
    list.owner = owner;
    list.scroll = 0.0f;
    list.last_focus = focus;
    anims_[key] = 0.0f;
  }
  list.top = top;
  list.bottom = bottom;
  list.row_h = row_h;
  float view = (bottom - top) / row_h;  // rows that fit
  list.max_scroll = std::max(0.0f, float(rows) - view);
  if (active && list.max_scroll > 0 && MouseIn(x0, top, x1, bottom)) {
    list.scroll -= ImGui::GetIO().MouseWheel * 3.0f;
    // Near the top or bottom edge the list glides on, faster closer to it.
    float edge = row_h * 1.2f, mouse_y = ImGui::GetIO().MousePos.y;
    float pull = mouse_y < top + edge      ? -(1.0f - (mouse_y - top) / edge)
                 : mouse_y > bottom - edge ? 1.0f - (bottom - mouse_y) / edge
                                           : 0.0f;
    if (list.drag < 0) list.scroll += pull * 9.0f * dt_;
  }
  // A focused row moved by the keyboard or a controller comes into view (a
  // row the mouse points at is in view already).
  if (focus != list.last_focus) {
    if (!list.mouse_focus && focus >= 0) {
      if (focus < list.scroll) list.scroll = float(focus);
      if (focus + 1 > list.scroll + view) list.scroll = float(focus + 1) - view;
    }
    list.last_focus = focus;
  }
  list.scroll = std::clamp(list.scroll, 0.0f, list.max_scroll);
  list.shown = Anim(key, list.scroll, 0.12f);
  // Pressing the bar must not also press the row under it.
  if (list.max_scroll > 0 && list.bar_x >= 0) {
    on_scroll_bar_ = list.drag >= 0 || MouseIn(list.bar_x - 8 * scale_, top, list.bar_x + 14 * scale_, bottom);
  }
  // A little beyond the list, so the brush stroke of a row at its edge isn't cut.
  draw_->PushClipRect(ImVec2(x0, top - 6 * scale_), ImVec2(x1, bottom + 6 * scale_), true);
  return top - list.shown * row_h;
}

void Ui::EndScrollList(ScrollList& list, float bar_x, bool active, uint32_t color) {
  if (!color) color = kCream;
  draw_->PopClipRect();
  on_scroll_bar_ = false;
  list.bar_x = bar_x;
  if (list.max_scroll <= 0) return;
  // The bar: drag its thumb or click the track.
  float s = scale_, bar_w = 6 * s, top = list.top, bottom = list.bottom;
  float track_h = bottom - top, rows_shown = track_h / list.row_h;
  float thumb_h = std::max(30 * s, track_h * rows_shown / (rows_shown + list.max_scroll));
  float thumb_y = top + (track_h - thumb_h) * (list.shown / list.max_scroll);
  bool over_bar = MouseIn(bar_x - 8 * s, top, bar_x + bar_w + 8 * s, bottom);
  float mouse_y = ImGui::GetIO().MousePos.y;
  if (active && over_bar && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    bool on_thumb = mouse_y >= thumb_y && mouse_y <= thumb_y + thumb_h;
    list.drag = on_thumb ? mouse_y - thumb_y : thumb_h * 0.5f;
  }
  if (list.drag >= 0) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      float t = (mouse_y - list.drag - top) / std::max(1.0f, track_h - thumb_h);
      list.scroll = std::clamp(t, 0.0f, 1.0f) * list.max_scroll;
    } else {
      list.drag = -1.0f;
    }
  }
  bool lit = over_bar || list.drag >= 0;
  draw_->AddRectFilled(ImVec2(bar_x, top), ImVec2(bar_x + bar_w, bottom), WithAlpha(color, 0.12f),
                       bar_w * 0.5f);
  draw_->AddRectFilled(ImVec2(bar_x, thumb_y), ImVec2(bar_x + bar_w, thumb_y + thumb_h),
                       WithAlpha(color, lit ? 0.85f : 0.5f), bar_w * 0.5f);
}

// ------------------------------------------------------------------ Controls

std::vector<std::string> Ui::KeysOf(const std::string& setting) const {
  // As written in fifawc14.toml ("L,Return" in quotes), or the default.
  std::string value = config_ ? config_->Get(setting, "") : "";
  if (value.size() >= 2 && value.front() == '"') value = value.substr(1, value.size() - 2);
  if (value.empty()) value = DefaultKeys().at(setting);
  std::vector<std::string> keys;
  size_t start = 0;
  for (size_t comma; (comma = value.find(',', start)) != std::string::npos; start = comma + 1) {
    keys.push_back(value.substr(start, comma - start));
  }
  keys.push_back(value.substr(start));
  return keys;
}

void Ui::BindKey(const std::string& name) {
  const auto& layout = KeyboardLayout();
  const std::string& setting = layout[controls_row_].setting;
  const size_t column = size_t(controls_column_);
  // Every action's keys as they'll be, starting from the file's.
  std::map<std::string, std::vector<std::string>> keys;
  auto of = [&](const std::string& s) -> std::vector<std::string>& {
    auto it = keys.find(s);
    if (it == keys.end()) it = keys.emplace(s, KeysOf(s)).first;
    return it->second;
  };
  std::vector<std::string>& mine = of(setting);
  const std::string old = column < mine.size() ? mine[column] : "";
  if (name.empty()) {  // clear the second key
    if (column == 0 || mine.size() < 2) return;
    mine.resize(1);
  } else {
    if (name == old) return;
    if (old.empty() && std::find(mine.begin(), mine.end(), name) != mine.end()) return;
    // A key another action has swaps with this one's old key, so no key does
    // two things; without an old key it just moves, unless that leaves the
    // other action with none.
    for (const ControlRow& row : layout) {
      std::vector<std::string>& list = of(row.setting);
      for (size_t j = 0; j < list.size(); ++j) {
        if ((row.setting == setting && j == column) || list[j] != name) continue;
        if (!old.empty()) {
          list[j] = old;
        } else {
          list.erase(list.begin() + j);
          if (list.empty()) {
            ShowMessage(T("Every action needs a key."));
            return;
          }
        }
        break;
      }
    }
    if (column < mine.size()) {
      mine[column] = name;
    } else {
      mine.push_back(name);
    }
  }
  for (const auto& [s, list] : keys) {
    if (list == KeysOf(s)) continue;
    std::string joined;
    for (const std::string& key : list) joined += (joined.empty() ? "" : ",") + key;
    config_->Set(s, "\"" + joined + "\"");
  }
  if (!config_->Save()) ShowMessage(T("The keys couldn't be saved."));
}

bool Ui::OnKey(unsigned virtual_key, bool extended, bool repeat) {
  if (!binding_ || screen_ != Screen::kControls) return false;
  if (repeat) return true;
  if (virtual_key == VK_ESCAPE) {
    binding_ = false;
    Sound(Audio::Effect::kBack);
    return true;
  }
  // Delete clears a second key; it isn't one itself.
  std::string name = virtual_key == VK_DELETE ? "" : KeyName(virtual_key, extended);
  if (name.empty() && (virtual_key != VK_DELETE || controls_column_ == 0)) return true;
  BindKey(name);
  binding_ = false;
  Sound(Audio::Effect::kSelect);
  return true;
}

void Ui::ControlsCommand(Command command) {
  if (binding_) {
    if (command == Command::kBack) {  // a controller's B (Esc comes through OnKey)
      binding_ = false;
      Sound(Audio::Effect::kBack);
    }
    return;
  }
  const int reset_row = int(KeyboardLayout().size());
  switch (command) {
    case Command::kUp:
    case Command::kDown: {
      int row = std::clamp(controls_row_ + (command == Command::kUp ? -1 : 1), 0, reset_row);
      if (row != controls_row_) {
        controls_row_ = row;
        controls_list_.mouse_focus = false;
        Sound(Audio::Effect::kList);
      }
      break;
    }
    case Command::kLeft:
    case Command::kRight: {
      int column = command == Command::kLeft ? 0 : 1;
      if (controls_row_ < reset_row && column != controls_column_) {
        controls_column_ = column;
        Sound(Audio::Effect::kMove);
      }
      break;
    }
    case Command::kAccept:
      if (controls_row_ < reset_row) {
        binding_ = true;
        Sound(Audio::Effect::kSelect);
      } else {
        for (const auto& [setting, keys] : DefaultKeys()) config_->Set(setting, "\"" + keys + "\"");
        Sound(Audio::Effect::kSelect);
        if (config_->Save()) {
          ShowMessage(T("Keys reset to default."), true);
        } else {
          ShowMessage(T("The keys couldn't be saved."));
        }
      }
      break;
    case Command::kBack:
      Go(Screen::kHome);
      break;
    default:
      break;
  }
}

void Ui::ControlsScreen(float width, float height) {
  float s = scale_;
  float bx, by, bw, bh, list_y;
  float intro_size = 19 * s;
  TwoBoards(width, height, T("Controls"), intro_size * 1.35f * 5, &bx, &by, &bw, &bh, &list_y);
  DrawWrapped(book_, intro_size, 56 * s + 48 * s, list_y, 290 * s, kCreamDim,
              T("These are the keys for playing on the keyboard. Select an action to give it a new "
                "key. A controller works at any time too."));
  float icon_w = 230 * s;
  DrawSplash(home_icons_[2], std::min(bx + bw + 50 * s - icon_w * 0.5f, width - icon_w * 0.55f), by - 70 * s + icon_w * 0.33f,
             icon_w, splash_appear_);

  // One row per action, then Reset to default; the list scrolls.
  const auto& layout = KeyboardLayout();
  const int rows = int(layout.size()) + 1;
  float size = 18 * s, row_h = 40 * s, key_size = 15 * s;
  float top = by + 64 * s, bottom = by + bh - 70 * s;

  // Two key columns of one width (the widest cap), then the scroll bar, against
  // the board's right side.
  float cap_w = bold_->CalcTextSizeA(key_size, FLT_MAX, 0, "Num Enter").x + 0.9f * key_size;
  float bar_x = bx + bw - 48 * s - 24 * s;
  float second_x = bar_x - 22 * s - cap_w, first_x = second_x - 14 * s - cap_w;
  float glyph_x = bx + 48 * s + 30 * s, label_x = bx + 48 * s + 70 * s;
  auto stroke = [&](float x0, float y0, float x1, float y1) {
    Image(draw_, stroke_, ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32_WHITE);
  };

  float first_y = BeginScrollList(controls_list_, "controls", 0, bx, bx + bw, top, bottom, row_h, rows,
                                  controls_row_, !binding_);
  for (int r = 0; r < rows; ++r) {
    float row_top = first_y + float(r) * row_h, middle = row_top + row_h * 0.5f;
    if (row_top + row_h < top || row_top > bottom) continue;
    bool focused_row = controls_row_ == r;
    if (r == int(layout.size())) {  // Reset to default
      float focus = Anim("controls_reset", focused_row ? 1.0f : 0.0f, 0.24f);
      float end = DrawItem(label_x, middle - size * 0.7f, T("Reset to default"), bold_, size, focus);
      if (!binding_ && MouseIn(label_x - 10 * s, row_top, label_x + 260 * s, end) &&
          MouseIn(bx, top, bx + bw, bottom)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (mouse_moved_) {
          controls_row_ = r;
          controls_list_.mouse_focus = true;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          controls_row_ = r;
          controls_list_.mouse_focus = true;
          ControlsCommand(Command::kAccept);
        }
      }
      continue;
    }
    const ControlRow& row = layout[r];
    DrawGlyph(glyph_x, middle, row_h * 0.62f, row);
    // The label left of the keys: on one line if it fits, else attacking and
    // defending on two ("Finesse shot / Teammate contain"), shrunk if need be.
    {
      std::string label = T(row.label);
      float room = first_x - 16 * s - label_x;
      ImU32 color = focused_row ? kCream : kCreamDim;
      size_t slash = label.find(" / ");
      float one = book_->CalcTextSizeA(size, FLT_MAX, 0, label.c_str()).x;
      if (one <= room || slash == std::string::npos) {
        float fit = std::min(size, size * room / std::max(one, 1.0f));
        draw_->AddText(book_, fit, ImVec2(label_x, middle - fit * 0.6f), color, label.c_str());
      } else {
        std::string lines[2] = {label.substr(0, slash + 2), label.substr(slash + 3)};
        float two_size = size * 0.82f, widest = 0;
        for (const std::string& line : lines) {
          widest = std::max(widest, book_->CalcTextSizeA(two_size, FLT_MAX, 0, line.c_str()).x);
        }
        two_size = std::min(two_size, two_size * room / std::max(widest, 1.0f));
        draw_->AddText(book_, two_size, ImVec2(label_x, middle - two_size * 1.15f), color, lines[0].c_str());
        draw_->AddText(book_, two_size, ImVec2(label_x, middle + two_size * 0.02f), color, lines[1].c_str());
      }
    }
    std::vector<std::string> keys = KeysOf(row.setting);
    for (int column = 0; column < 2; ++column) {
      float x = column == 0 ? first_x : second_x, key_h = key_size * 1.4f;
      bool focused = focused_row && controls_column_ == column;
      std::string cap = focused && binding_ ? "..."
                        : column < int(keys.size()) ? KeyCap(keys[column])
                                                    : "";
      if (focused) stroke(x - 8 * s, middle - key_h * 0.5f - 5 * s, x + cap_w + 8 * s, middle + key_h * 0.5f + 5 * s);
      if (cap.empty()) {  // no second key: an empty slot
        draw_->AddRect(ImVec2(x, middle - key_h * 0.5f), ImVec2(x + cap_w, middle + key_h * 0.5f),
                       WithAlpha(kCream, focused ? 0.0f : 0.25f), 6 * s, 0, std::max(1.0f, s));
      } else {
        // DrawKey sizes the cap to its text; draw it at the column's width instead.
        ImVec2 text = bold_->CalcTextSizeA(key_size, FLT_MAX, 0, cap.c_str());
        float top_y = middle - key_h * 0.5f;
        draw_->AddRectFilled(ImVec2(x, top_y), ImVec2(x + cap_w, top_y + key_h), kCream, 6 * s);
        draw_->AddRectFilled(ImVec2(x, top_y + key_h - 3 * s), ImVec2(x + cap_w, top_y + key_h),
                             IM_COL32(12, 30, 20, 64), 6 * s, ImDrawFlags_RoundCornersBottom);
        draw_->AddText(bold_, key_size, ImVec2(x + (cap_w - text.x) * 0.5f, top_y + key_size * 0.2f),
                       kInk, cap.c_str());
      }
      if (!binding_ && MouseIn(x, middle - key_h * 0.5f, x + cap_w, middle + key_h * 0.5f) &&
          MouseIn(bx, top, bx + bw, bottom)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (mouse_moved_ || ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
          controls_row_ = r;
          controls_list_.mouse_focus = true;
          controls_column_ = column;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ControlsCommand(Command::kAccept);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) BindKey("");  // right-click clears a second key
      }
    }
  }
  EndScrollList(controls_list_, bar_x, !binding_);

  if (binding_) {
    std::vector<Hint> hints = {{"Esc", "B", "Cancel", Command::kBack}};
    if (controls_column_ == 1 && device_ == InputDevice::kKeyboard) {
      hints.insert(hints.begin(), {"Del", "", "Clear"});
    }
    DrawHints(width, height, hints);
  } else {
    DrawHints(width, height, {{"Enter", "A", controls_row_ < int(layout.size()) ? "Change key" : "Reset to default",
                               Command::kAccept},
                              {"Esc", "B", "Back", Command::kBack}});
  }
}

void Ui::AboutScreen(float width, float height) {
  float s = scale_;
  const auto& topics = AboutTopics();
  float item_size = 30 * s, item_h = item_size * 1.35f + 0.15f * item_size;
  float bx, by, bw, bh, list_y;
  TwoBoards(width, height, T("About"), item_h * float(topics.size()), &bx, &by, &bw, &bh, &list_y);
  float x = 56 * s + 48 * s, y = list_y;
  for (int i = 0; i < int(topics.size()); ++i) {
    float row_top = y;
    float focus = Anim("topic" + std::to_string(i), i == topic_focus_ ? 1.0f : 0.0f, 0.24f);
    y = DrawItem(x, y, topics[i].label, bold_, item_size, focus);
    if (MouseIn(56 * s, row_top, 56 * s + 380 * s, y) && topic_focus_ != i &&
        (mouse_moved_ || ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
      topic_focus_ = i;
      Sound(Audio::Effect::kList);
    }
    y += 0.15f * item_size;
  }
  float icon_w = 230 * s;
  DrawSplash(home_icons_[3], std::min(bx + bw + 50 * s - icon_w * 0.5f, width - icon_w * 0.55f), by - 70 * s + icon_w * 0.33f,
             icon_w, splash_appear_);

  // The topic's text, top-aligned; extra left padding for the mirrored board's torn edge.
  const Topic& topic = topics[topic_focus_];
  float text_size = 19 * s, text_x = bx + 48 * s + 24 * s, wrap = bw - 48 * s * 2 - 40 * s;
  float ty = by + 64 * s + 90 * s;
  for (const std::string& paragraph : topic.paragraphs) {
    if (paragraph.starts_with("github.com/") && on_open_page) {
      // The project's page, as a link: brighter and underlined under the mouse.
      ImVec2 size = book_->CalcTextSizeA(text_size, FLT_MAX, 0.0f, paragraph.c_str());
      bool over = MouseIn(text_x, ty, text_x + size.x, ty + size.y);
      draw_->AddText(book_, text_size, ImVec2(text_x, ty), over ? kCream : kCreamDim,
                     paragraph.c_str());
      draw_->AddLine(ImVec2(text_x, ty + size.y), ImVec2(text_x + size.x, ty + size.y),
                     WithAlpha(kCream, over ? 0.9f : 0.35f), std::max(1.0f, s));
      if (over) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          Sound(Audio::Effect::kSelect);
          on_open_page("https://" + paragraph);
        }
      }
      ty += text_size * 1.4f + text_size * 0.6f;
      continue;
    }
    ty += DrawWrapped(book_, text_size, text_x, ty, wrap, kCreamDim, paragraph) + text_size * 0.6f;
  }
  if (topic.build) {
    draw_->AddText(condensed_, 26 * s, ImVec2(text_x, ty), kCream, BuildLine().c_str());
  }
  DrawHints(width, height, {{"Esc", "B", "Back", Command::kBack}});
}

void Ui::NoticeScreen(float width, float height) {
  // The first start's welcome: one board in the middle, its choice in the
  // menu's brush-stroke style.
  float s = scale_;
  float board_w = std::min(620 * s, width - 128 * s), board_h = std::min(640 * s, height * 0.84f);
  float board_x = (width - board_w) * 0.5f, board_y = (height - board_h) * 0.5f;
  DrawBoard(board_x, board_y, board_w, board_h, false);
  float x = board_x + 48 * s, y = board_y + 56 * s, wrap = board_w - 96 * s;
  float logo_w = 120 * s, logo_h = logo_w * logo_.height / std::max(1, logo_.width);
  Image(draw_, logo_, ImVec2(x, y), ImVec2(x + logo_w, y + logo_h));
  y += logo_h + 28 * s;
  draw_->AddText(bold_, 40 * s, ImVec2(x, y), kCream, T("Before you play"));
  y += 40 * s * 1.1f + 22 * s;
  const char* paragraphs[] = {
      T("This is an unofficial PC version of 2014 FIFA World Cup Brazil, brought over from the "
        "Xbox 360 by recompiling the original game."),
      T("It plays best with a controller, but the keyboard works too, and you can set up your own "
        "keys on the Controls page."),
      T("If you run into a problem or need help, open an issue on GitHub."), T("Have fun!")};
  float text_size = 19 * s;
  for (const char* paragraph : paragraphs) {
    y += DrawWrapped(book_, text_size, x, y, wrap, kCreamDim, paragraph) + text_size * 0.5f;
  }
  y += 14 * s;
  float top = y;
  float bottom = DrawItem(x, y, T("Got it"), bold_, 34 * s, Anim("notice", 1.0f, 0.24f));
  if (MouseIn(x - 20 * s, top, x + 300 * s, bottom) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    OnCommand(Command::kAccept, device_);
  }
  DrawHints(width, height, {{"Enter", "A", "Continue", Command::kAccept}});
}

// ------------------------------------------------------------------ update popup

void Ui::ShowUpdate(const Release& release) {
  update_ = release;
  update_focus_ = 0;
  update_appear_ = 0;
}

bool Ui::UpdateVisible() const { return update_.has_value() && screen_ != Screen::kNotice; }

void Ui::UpdateCommand(Command command) {
  using State = UpdateJob::State;
  bool busy = update_job_ && update_job_->state() != State::kFailed;
  if (busy) return;  // downloading or installing: nothing to choose
  switch (command) {
    case Command::kUp:
    case Command::kDown:
    case Command::kLeft:
    case Command::kRight:
      update_focus_ = 1 - update_focus_;
      Sound(Audio::Effect::kList);
      break;
    case Command::kAccept:
      Sound(Audio::Effect::kSelect);
      if (update_focus_ == 0) {
        if (on_update) on_update();
      } else {
        update_.reset();  // Later: until the next start
      }
      break;
    case Command::kBack:
      Sound(Audio::Effect::kBack);
      update_.reset();
      break;
    default: break;
  }
}

// A painted panel over the dimmed menu, the game's "download updates" tile on
// its corner: the new version and what changed, then Update now /
// Later, or the download's progress.
void Ui::UpdatePopup(float width, float height) {
  using State = UpdateJob::State;
  float s = scale_;
  update_appear_ = std::min(1.0f, update_appear_ + dt_ / 0.25f);
  float t = EaseOut(update_appear_);
  draw_ = ImGui::GetForegroundDrawList();
  draw_->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), IM_COL32(0, 8, 24, int(200 * t)));

  const Texture& panel = update_panel_ ? update_panel_ : panel_;
  float panel_w = std::min(600 * s, width - 80 * s);
  float panel_h = panel_w * panel.height / std::max(1, panel.width);
  float px = (width - panel_w) * 0.5f, py = (height - panel_h) * 0.5f + (1 - t) * 16 * s;
  Image(draw_, panel, ImVec2(px, py), ImVec2(px + panel_w, py + panel_h), WithAlpha(IM_COL32_WHITE, t));
  float icon_w = 190 * s;
  DrawSplash(update_icon_, px + panel_w - 30 * s, py + 34 * s, icon_w, t);

  ImU32 ink = WithAlpha(kInk, t), soft = WithAlpha(kInk, 0.72f * t);
  float x = px + 62 * s, wrap = panel_w - 140 * s;
  State state = update_job_ ? update_job_->state() : State::kFailed;
  bool choosing = !update_job_ || state == State::kFailed;
  // The content's height, to centre it in the panel's painted area. The changes
  // take the room left (with the buttons' height, so the list doesn't move when
  // the download starts) and scroll when they don't fit.
  float fixed_h = 38 * s * 1.2f + 24 * s * 1.5f + 22 * s;
  if (update_job_ && state == State::kFailed) fixed_h += 18 * s * 2.6f;
  // Update now, and Later a size smaller.
  float buttons_h = (32 * s * 1.35f + 6 * s) + (26 * s * 1.35f + 6 * s);
  float room = std::max(18 * s * 1.4f, panel_h * 0.9f - 56 * s - 16 * s - fixed_h - buttons_h);
  float notes_h = 0;
  for (const std::string& change : update_->changes) {
    notes_h += book_->CalcTextSizeA(18 * s, FLT_MAX, wrap - 18 * s, change.c_str()).y * 1.4f + 6 * s;
  }
  float list_h = std::min(notes_h, room);
  float content_h = fixed_h + list_h + (choosing ? buttons_h : 24 * s * 1.5f + 12 * s);
  float y = py + std::max(56 * s, (panel_h * 0.9f - content_h) * 0.5f);
  draw_->AddText(bold_, 38 * s, ImVec2(x, y), ink, T("Update available"));
  y += 38 * s * 1.2f;
  std::string versions = Tf("Version {0}   \xC2\xB7   you have {1}",
                            {update_->version, *LauncherVersion() ? LauncherVersion() : "?"});
  draw_->AddText(condensed_, 24 * s, ImVec2(x, y), soft, versions.c_str());
  y += 24 * s * 1.5f;
  // Scrolled in steps of a line.
  float line_h = 18 * s * 1.4f;
  int lines = int(std::ceil(notes_h / line_h));
  notes_list_.scroll += scroll_input_ * 14.0f * dt_;  // lines a second, held fully
  float notes_y = BeginScrollList(notes_list_, "update_notes", 0, x - 4 * s, x + wrap + 4 * s, y,
                                  y + list_h, line_h, lines, -1, true);
  // "#23" in a change links to that issue, as on the release page. The issues
  // page is next to the release's page (.../releases/tag/v1.0 -> .../issues/).
  std::string issues;
  if (size_t at = update_->page_url.find("/releases/"); at != std::string::npos && on_open_page) {
    issues = update_->page_url.substr(0, at) + "/issues/";
  }
  float note_size = 18 * s, list_top = y, list_bottom = y + list_h;
  for (const std::string& change : update_->changes) {
    draw_->AddText(book_, note_size, ImVec2(x, notes_y), soft, "\xE2\x80\xA2");
    // Wrapped as DrawWrapped does, each line drawn in pieces so a link has its place.
    const char* line = change.c_str();
    const char* end = line + change.size();
    float text_x = x + 18 * s;
    while (line < end) {
      const char* next = book_->CalcWordWrapPosition(note_size, line, end, wrap - 18 * s);
      if (next == line) ++next;
      float lx = text_x;
      for (const char* p = line; p < next;) {
        // A link: '#' and digits standing alone, as in "(#23, #25)". After a
        // space or bracket and before a space or punctuation, so "C#1" or
        // "#1-ranked" stay text.
        const char* q = p;
        bool link = !issues.empty() && *p == '#' && p + 1 < next && std::isdigit((unsigned char)p[1]) &&
                    (p == change.c_str() || std::strchr(" ([", p[-1]));
        if (link) {
          q = p + 1;
          while (q < next && std::isdigit((unsigned char)*q)) ++q;
          link = q == next || std::strchr(" ,.;:!?)]", *q);
        }
        if (!link) {  // plain text up to the next '#'
          q = p + 1;
          while (q < next && *q != '#') ++q;
          draw_->AddText(book_, note_size, ImVec2(lx, notes_y), soft, p, q);
          lx += book_->CalcTextSizeA(note_size, FLT_MAX, 0.0f, p, q).x;
          p = q;
          continue;
        }
        float w = book_->CalcTextSizeA(note_size, FLT_MAX, 0.0f, p, q).x;
        bool over = MouseIn(lx, notes_y, lx + w, notes_y + note_size * 1.2f) &&
                    MouseIn(x, list_top, x + wrap, list_bottom);
        draw_->AddText(book_, note_size, ImVec2(lx, notes_y), WithAlpha(kInk, (over ? 1.0f : 0.85f) * t), p, q);
        float under = notes_y + note_size * 1.1f;
        draw_->AddLine(ImVec2(lx, under), ImVec2(lx + w, under), WithAlpha(kInk, (over ? 0.9f : 0.35f) * t),
                       std::max(1.0f, s));
        if (over) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            Sound(Audio::Effect::kSelect);
            on_open_page(issues + std::string(p + 1, q));
          }
        }
        lx += w;
        p = q;
      }
      notes_y += note_size * 1.4f;
      line = next;
      while (line < end && *line == ' ') ++line;
    }
    notes_y += 6 * s;
  }
  EndScrollList(notes_list_, x + wrap + 12 * s, true, kInk);
  y += list_h;
  y += 22 * s;
  if (update_job_ && state == State::kFailed) {
    DrawWrapped(book_, 18 * s, x, y, wrap, WithAlpha(kAlarm, t), update_job_->error());
    y += 18 * s * 2.6f;
  }
  if (choosing) {
    const char* labels[] = {update_job_ ? T("Try again") : T("Update now"), T("Later")};
    for (int i = 0; i < 2; ++i) {
      float focus = Anim("update" + std::to_string(i), i == update_focus_ ? 1.0f : 0.0f, 0.2f);
      float top = y;
      y = DrawItem(x, y, labels[i], bold_, (i == 0 ? 32 : 26) * s, focus * t, 0.0f, soft);
      if (MouseIn(x - 20 * s, top, x + 320 * s, y)) {
        if (mouse_moved_ && update_focus_ != i) {
          update_focus_ = i;
          Sound(Audio::Effect::kList);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          update_focus_ = i;
          UpdateCommand(Command::kAccept);
          return;
        }
      }
      y += 6 * s;
    }
    std::vector<Hint> hints = {{"Enter", "A", "Select", Command::kAccept},
                               {"Esc", "B", "Later", Command::kBack}};
    if (notes_h > room) hints.insert(hints.begin(), Hint{"PgDn", "RS", "Scroll"});
    DrawHints(width, height, hints);
  } else {
    // Progress: a bar while downloading, then what's happening.
    uint64_t done = update_job_->downloaded(), total = update_job_->total();
    std::string line;
    auto mb = [](uint64_t bytes) {
      char text[32];
      std::snprintf(text, sizeof(text), "%.1f", bytes / 1048576.0);
      return std::string(text);
    };
    if (state == State::kDownloading) {
      line = total ? Tf("Downloading   {0} of {1} MB", {mb(done), mb(total)})
                   : Tf("Downloading   {0} MB", {mb(done)});
    } else if (state == State::kVerifying) {
      line = T("Checking the download");
    } else if (state == State::kInstalling) {
      line = T("Installing");
    } else {
      line = T("Starting the update");
    }
    draw_->AddText(condensed_, 24 * s, ImVec2(x, y), ink, line.c_str());
    y += 24 * s * 1.5f;
    float bar_w = wrap, bar_h = 12 * s;
    float fraction = state == State::kDownloading ? (total ? float(double(done) / double(total)) : 0.0f)
                                                  : 1.0f;
    draw_->AddRectFilled(ImVec2(x, y), ImVec2(x + bar_w, y + bar_h), WithAlpha(kInk, 0.18f * t),
                         bar_h * 0.5f);
    if (fraction > 0) {
      draw_->AddRectFilled(ImVec2(x, y), ImVec2(x + bar_w * std::min(fraction, 1.0f), y + bar_h),
                           WithAlpha(IM_COL32(247, 209, 23, 255), t), bar_h * 0.5f);
    }
  }
}

void Ui::QuitCommand(Command command) {
  switch (command) {
    case Command::kUp:
    case Command::kDown:
    case Command::kLeft:
    case Command::kRight:
      quit_focus_ = 1 - quit_focus_;
      Sound(Audio::Effect::kList);
      break;
    case Command::kAccept:
      Sound(Audio::Effect::kSelect);
      quit_asked_ = false;
      if (quit_focus_ == 0 && on_quit) on_quit();
      break;
    case Command::kBack:
      Sound(Audio::Effect::kBack);
      quit_asked_ = false;
      break;
    default: break;
  }
}

// Quitting asked on one of the game's painted panels over the dimmed menu, as
// the update popup: Yes / No.
void Ui::QuitPopup(float width, float height) {
  float s = scale_;
  quit_appear_ = std::min(1.0f, quit_appear_ + dt_ / 0.2f);
  float t = EaseOut(quit_appear_);
  draw_ = ImGui::GetForegroundDrawList();
  draw_->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), IM_COL32(0, 8, 24, int(200 * t)));

  const Texture& panel = quit_panel_ ? quit_panel_ : panel_;
  float panel_w = std::min(660 * s, width - 80 * s);
  float panel_h = panel_w * panel.height / std::max(1, panel.width);
  float px = (width - panel_w) * 0.5f, py = (height - panel_h) * 0.5f + (1 - t) * 16 * s;
  Image(draw_, panel, ImVec2(px, py), ImVec2(px + panel_w, py + panel_h), WithAlpha(IM_COL32_WHITE, t));

  // In the panel's painted area (about 8-93% across, 10-85% down).
  ImU32 ink = WithAlpha(kInk, t), soft = WithAlpha(kInk, 0.72f * t);
  float x = px + panel_w * 0.17f;
  float content_h = 34 * s * 1.2f + 18 * s + 2 * (24 * s * 1.35f + 6 * s);
  float y = py + panel_h * 0.47f - content_h * 0.5f;
  draw_->AddText(bold_, 34 * s, ImVec2(x, y), ink, T("Quit the launcher?"));
  y += 34 * s * 1.2f + 18 * s;
  const char* labels[] = {T("Yes"), T("No")};
  for (int i = 0; i < 2; ++i) {
    float focus = Anim("quit" + std::to_string(i), i == quit_focus_ ? 1.0f : 0.0f, 0.2f);
    float top = y;
    y = DrawItem(x, y, labels[i], bold_, 24 * s, focus * t, 0.0f, soft);
    if (MouseIn(x - 20 * s, top, x + 300 * s, y)) {
      if (mouse_moved_ && quit_focus_ != i) {
        quit_focus_ = i;
        Sound(Audio::Effect::kList);
      }
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        quit_focus_ = i;
        QuitCommand(Command::kAccept);
        return;
      }
    }
    y += 6 * s;
  }
  DrawHints(width, height, {{"Enter", "A", "Select", Command::kAccept},
                            {"Esc", "B", "Cancel", Command::kBack}});
}

}  // namespace launcher
