// The launcher's sound: the game's own menu sounds and its EA Trax soundtrack,
// read from the player's game and played with XAudio2.
//
// EA sound banks: a ".sbr" (the bank: a DATA section with the sounds kept in
// memory and DSET tables describing its entries), its ".sbr_debug" (the same
// tables with each entry's source file name) and, for streamed sounds such as
// music, a ".sbs" holding the streams. Every sound is an EA "SPS" stream of
// EA-XMA: XMA2 packets with their padding trimmed, which padded back to 2048
// bytes decode with FFmpeg's XMA2 decoder (the SDK's own FFmpeg build).

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher {

class GameFiles;

// Decoded sound: 16-bit interleaved samples.
struct Pcm {
  int sample_rate = 0;
  int channels = 0;
  std::vector<int16_t> samples;
};

// One sound bank's entries (each distinct sound once, in entry order).
class SoundBank {
 public:
  struct Entry {
    std::string name;    // source file name, e.g. "WC_FE_ListNav.wav"
    uint32_t offset;     // in the bank's DATA (in memory) or in its .sbs (streamed)
  };

  // `bank`: the path without extension, e.g. "audiodata/uisfx/ui_sfx_core_menu".
  bool Load(const GameFiles& files, const std::string& bank);
  const std::vector<Entry>& entries() const { return entries_; }
  // The entry's SPS stream (read from the .sbs for streamed banks).
  std::vector<uint8_t> Stream(const GameFiles& files, size_t index) const;

 private:
  std::string bank_;
  bool streamed_ = false;
  std::vector<uint8_t> sbr_;
  size_t data_start_ = 0;
  std::vector<Entry> entries_;
};

// An EA-XMA SPS stream decoded. False if it isn't one or doesn't decode.
bool DecodeSps(const std::vector<uint8_t>& sps, Pcm& out);

// A song of the soundtrack, as shown on the main menu.
struct Song {
  std::string artist;
  std::string title;
};

class Audio {
 public:
  enum class Effect { kMove, kList, kChange, kSelect, kBack, kSection };

  Audio();
  ~Audio();
  // Loads the menu sounds and the soundtrack's song list. Audio stays silent
  // if the sound device or the banks aren't there.
  // `anthem_first`: the playlist starts with We Are One (Ole Ola), the
  // tournament's official song (a newcomer's first start).
  void Load(const GameFiles& files, bool anthem_first = false);

  void Play(Effect effect);
  // Call every frame: starts the next song when one ends.
  void Update();
  void SkipSong();
  // The song playing (empty title if none).
  Song CurrentSong() const;

  // Settings: 0..1 volumes; music off stops it.
  void SetSoundsEnabled(bool enabled) { sounds_enabled_ = enabled; }
  void SetSoundsVolume(float volume) { sounds_volume_ = volume; }
  void SetMusic(bool enabled, float volume);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool sounds_enabled_ = true;
  float sounds_volume_ = 0.8f;
};

}  // namespace launcher
