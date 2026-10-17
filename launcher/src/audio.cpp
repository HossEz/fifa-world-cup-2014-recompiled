#include "audio.h"

#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <map>
#include <random>
#include <set>

#include "game_files.h"
#include "log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
// FFmpeg's XMA2 decoder (libavcodec/wmaprodec.c), compiled into the SDK's
// FFmpeg but not in its codec list.
extern AVCodec ff_xma2_decoder;
}

namespace launcher {

namespace {

constexpr uint32_t kXmaPacket = 0x800;
constexpr int kCodecEaXma = 3;

uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

size_t Find(const std::vector<uint8_t>& data, const char* tag, size_t from = 0,
            size_t to = SIZE_MAX) {
  size_t length = std::strlen(tag);
  to = std::min(to, data.size());
  for (size_t i = from; i + length <= to; ++i) {
    if (std::memcmp(&data[i], tag, length) == 0) return i;
  }
  return SIZE_MAX;
}

// A field of a bank's first DSET table: its value for each entry. Fields are
// records of a 4-byte tag (".RAM", ".OFF", ".SRC"), 4 type bytes (the third:
// element size; the fourth, if non-zero: index bits), u32 0, u32 parameter,
// u32 offset of the values. An indexed field holds `parameter` distinct values,
// then per entry an index of that many bits (low bits first). The entry count
// is the u32 at DSET + 0x38.
std::vector<uint32_t> FieldValues(const std::vector<uint8_t>& bank, const char* tag) {
  size_t dset = Find(bank, "DSET");
  if (dset == SIZE_MAX || dset + 0x3C > bank.size()) return {};
  uint32_t count = Be32(&bank[dset + 0x38]);
  size_t next = Find(bank, "DSET", dset + 4);
  size_t pos = Find(bank, tag, dset, next);
  if (pos == SIZE_MAX || pos + 20 > bank.size()) return {};
  uint32_t size = bank[pos + 6], index_bits = bank[pos + 7];
  uint32_t parameter = Be32(&bank[pos + 12]), offset = Be32(&bank[pos + 16]);
  uint32_t n = index_bits ? parameter : count;
  if (size != 1 && size != 2 && size != 4) return {};
  if (offset + size_t(size) * n > bank.size()) return {};
  std::vector<uint32_t> values(n);
  for (uint32_t i = 0; i < n; ++i) {
    const uint8_t* p = &bank[offset + i * size];
    values[i] = size == 1 ? p[0] : size == 2 ? uint32_t(p[0] << 8 | p[1]) : Be32(p);
  }
  if (!index_bits) return values;
  std::vector<uint32_t> indexed(count);
  size_t bits_start = offset + size_t(size) * n;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t index = 0;
    for (uint32_t b = 0; b < index_bits; ++b) {
      size_t bit = size_t(i) * index_bits + b, byte = bits_start + bit / 8;
      if (byte < bank.size() && (bank[byte] >> (bit % 8)) & 1) index |= 1u << b;
    }
    indexed[i] = index < values.size() ? values[index] : 0;
  }
  return indexed;
}

// Each entry's source file name from the bank's .sbr_debug: the .SRC values
// are offsets of the names in DATA, from a base the field doesn't state; the
// smallest offset is the first path (strings before it are encoder settings).
std::vector<std::string> EntryNames(const std::vector<uint8_t>& debug) {
  std::vector<uint32_t> offsets = FieldValues(debug, ".SRC");
  size_t data = Find(debug, "DATA");
  if (offsets.empty() || data == SIZE_MAX) return {};
  size_t pos = data + 4;
  while (pos < debug.size()) {
    size_t end = pos;
    while (end < debug.size() && debug[end]) ++end;
    if (std::find(debug.begin() + pos, debug.begin() + end, '\\') != debug.begin() + end) break;
    pos = end + 1;
  }
  size_t start = pos - *std::min_element(offsets.begin(), offsets.end());
  std::vector<std::string> names;
  for (uint32_t offset : offsets) {
    size_t at = start + offset, end = at;
    while (end < debug.size() && debug[end]) ++end;
    std::string path(debug.begin() + std::min(at, debug.size()), debug.begin() + end);
    names.push_back(path.substr(path.find_last_of('\\') + 1));
  }
  return names;
}

// The length of the SPS stream at `data[start]`: blocks of a flag byte and a
// 24-bit size; flag 0x45 ends the stream.
size_t SpsLength(const uint8_t* data, size_t size) {
  size_t pos = 0;
  while (pos + 4 <= size) {
    uint8_t flag = data[pos];
    uint32_t length = Be32(&data[pos]) & 0xFFFFFF;
    if (length < 4) return 0;
    pos += length;
    if (flag == 0x45) return pos <= size ? pos : 0;
  }
  return 0;
}

}  // namespace

// ------------------------------------------------------------------ banks

bool SoundBank::Load(const GameFiles& files, const std::string& bank) {
  bank_ = bank;
  auto sbr = files.Read(bank + ".sbr");
  auto debug = files.Read(bank + ".sbr_debug");
  if (!sbr || !debug || sbr->size() < 4 || std::memcmp(sbr->data(), "SBbe", 4) != 0) return false;
  sbr_ = std::move(*sbr);
  streamed_ = Find(sbr_, ".OFF") != SIZE_MAX && !files.ReadRaw(bank + ".sbs", 0, 4).empty();
  std::vector<uint32_t> offsets = FieldValues(sbr_, streamed_ ? ".OFF" : ".RAM");
  std::vector<std::string> names = EntryNames(*debug);
  size_t data = Find(sbr_, "DATA");
  if (data == SIZE_MAX) return false;
  data_start_ = data + 4;
  std::set<uint32_t> seen;
  for (size_t i = 0; i < offsets.size() && i < names.size(); ++i) {
    if (seen.insert(offsets[i]).second) entries_.push_back({names[i], offsets[i]});
  }
  return !entries_.empty();
}

std::vector<uint8_t> SoundBank::Stream(const GameFiles& files, size_t index) const {
  const Entry& entry = entries_[index];
  if (!streamed_) {
    size_t start = data_start_ + entry.offset;
    if (start >= sbr_.size()) return {};
    size_t length = SpsLength(&sbr_[start], sbr_.size() - start);
    return length ? std::vector<uint8_t>(sbr_.begin() + start, sbr_.begin() + start + length)
                  : std::vector<uint8_t>{};
  }
  // Streamed: read block headers until the end block, then the whole stream.
  std::string sbs = bank_ + ".sbs";
  uint64_t pos = entry.offset;
  for (int blocks = 0; blocks < 1000000; ++blocks) {
    std::vector<uint8_t> head = files.ReadRaw(sbs, pos, 4);
    if (head.size() < 4) return {};
    uint32_t length = Be32(head.data()) & 0xFFFFFF;
    if (length < 4) return {};
    pos += length;
    if (head[0] == 0x45) return files.ReadRaw(sbs, entry.offset, uint32_t(pos - entry.offset));
  }
  return {};
}

// ------------------------------------------------------------------ decoding

bool DecodeSps(const std::vector<uint8_t>& sps, Pcm& out) {
  // Header block (0x48): u32 version/codec/channels/sample rate, u32
  // type/loop/sample count.
  if (sps.size() < 12 || sps[0] != 0x48) return false;
  uint32_t h1 = Be32(&sps[4]), h2 = Be32(&sps[8]);
  int codec = (h1 >> 24) & 0xF, channels = int((h1 >> 18) & 0x3F) + 1;
  int sample_rate = int(h1 & 0x3FFFF);
  uint32_t samples = h2 & 0x1FFFFFFF;
  if (codec != kCodecEaXma || channels > 2 || !sample_rate) return false;

  // Data blocks (0x44): u32 samples, then a u32 holding the XMA stream's size
  // x 4 and the trimmed packets; padded back to 2048 bytes.
  std::vector<uint8_t> packets;
  size_t pos = Be32(&sps[0]) & 0xFFFFFF;
  while (pos + 12 <= sps.size() && sps[pos] != 0x45) {
    uint32_t size = Be32(&sps[pos]) & 0xFFFFFF;
    if (size < 4) break;
    if (sps[pos] == 0x44) {
      size_t stream_size = Be32(&sps[pos + 8]) / 4;
      size_t begin = pos + 12, end = std::min(pos + 8 + stream_size, sps.size());
      if (begin < end) {
        packets.insert(packets.end(), sps.begin() + begin, sps.begin() + end);
        packets.resize((packets.size() + kXmaPacket - 1) / kXmaPacket * kXmaPacket, 0xFF);
      }
    }
    pos += size;
  }
  if (packets.empty()) return false;

  AVCodecContext* context = avcodec_alloc_context3(&ff_xma2_decoder);
  if (!context) return false;
  context->sample_rate = sample_rate;
  context->channels = channels;
  context->channel_layout = channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
  // XMA2WAVEFORMATEX after cbSize (34 bytes): one stream of these channels.
  uint8_t extra[34] = {};
  uint32_t mask = channels == 1 ? 0x4 : 0x3, bytes = uint32_t(packets.size());
  auto put16 = [&](int at, uint16_t v) { std::memcpy(&extra[at], &v, 2); };
  auto put32 = [&](int at, uint32_t v) { std::memcpy(&extra[at], &v, 4); };
  put16(0, 1);           // NumStreams
  put32(2, mask);        // ChannelMask
  put32(6, samples);     // SamplesEncoded
  put32(10, bytes);      // BytesPerBlock
  put32(18, samples);    // PlayLength
  extra[31] = 4;         // EncoderVersion
  put16(32, 1);          // BlockCount
  context->extradata = static_cast<uint8_t*>(av_mallocz(sizeof(extra) + AV_INPUT_BUFFER_PADDING_SIZE));
  std::memcpy(context->extradata, extra, sizeof(extra));
  context->extradata_size = sizeof(extra);
  if (avcodec_open2(context, &ff_xma2_decoder, nullptr) < 0) {
    avcodec_free_context(&context);
    return false;
  }

  out.sample_rate = sample_rate;
  out.channels = channels;
  out.samples.clear();
  out.samples.reserve(size_t(samples) * channels);
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  auto drain = [&] {
    while (avcodec_receive_frame(context, frame) == 0) {
      for (int i = 0; i < frame->nb_samples; ++i) {
        for (int c = 0; c < channels; ++c) {
          float v = reinterpret_cast<const float*>(frame->extended_data[c])[i];
          out.samples.push_back(int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        }
      }
    }
  };
  for (size_t at = 0; at < packets.size(); at += kXmaPacket) {
    if (av_new_packet(packet, kXmaPacket) < 0) break;
    std::memcpy(packet->data, &packets[at], kXmaPacket);
    avcodec_send_packet(context, packet);
    av_packet_unref(packet);
    drain();
  }
  avcodec_send_packet(context, nullptr);
  drain();
  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&context);
  if (out.samples.size() > size_t(samples) * channels) out.samples.resize(size_t(samples) * channels);
  return !out.samples.empty();
}

// ------------------------------------------------------------------ playback

namespace {

// The soundtrack: bank entry -> artist, title. The game's own track list is in
// its compressed text database, so these are written out from the entry names.
const std::map<std::string, Song> kTracks = {
    {"1985_LETS GO_MIX4.wav", {"1985", "Let's Go"}},
    {"AdrianLuxMarcusSchossowfeatJJWildChild.wav",
     {"Adrian Lux & Marcus Sch\xc3\xb6ssow feat. JJ", "Wild Child"}},
    {"Alafia--MaisTarde_cleaned_04.wav", {"Alafia", "Mais Tarde"}},
    {"BearMountain_TwoStep.wav", {"Bear Mountain", "Two Step"}},
    {"Bleachers_LikeaRiverRuns.wav", {"Bleachers", "Like a River Runs"}},
    {"CharlesWilliam_We_Are_The_Ones.wav", {"Charles William", "We Are the Ones"}},
    {"David Correy - the world is ours ROCK MAFIA GLOBAL FINAL.wav",
     {"David Correy", "The World Is Ours"}},
    {"DJ Bitman - Blackbossa.wav", {"DJ Bitman", "Blackbossa"}},
    {"Dom Um E Jadir De Castro - Oz\xc3\xb4nio.wav",
     {"Dom Um & Jadir de Castro", "Oz\xc3\xb4nio"}},
    {"Emicida - Hino Vira-Lata [Part. Quinteto em Branco e Preto].wav",
     {"Emicida feat. Quinteto em Branco e Preto", "Hino Vira-Lata"}},
    {"EsterRada_LifeHappens.wav", {"Ester Rada", "Life Happens"}},
    {"FelipeCordeiro_LambadaAlucinada.wav", {"Felipe Cordeiro", "Lambada Alucinada"}},
    {"FelixWeber-jjs-energy.wav", {"Felix Weber", "JJ's Energy"}},
    {"Haerts - Animal _MASTD_PK_01_16.wav", {"Haerts", "Animal"}},
    {"Holger_beaver.wav", {"Holger", "Beaver"}},
    {"Joao Nabuco - Bundalele. cleaned_02.wav", {"Jo\xc3\xa3o Nabuco", "Bundalel\xc3\xaa"}},
    {"Kevin Rudolph_HeresToUs.wav", {"Kevin Rudolph", "Here's to Us"}},
    {"La Yegros - El Bendito. cleaned+_01.wav", {"La Yegros", "El Bendito"}},
    {"Los Macuanos - Las Memorias del Faro.wav", {"Los Macuanos", "Las Memorias del Faro"}},
    {"LucianaOlivares_SambaemPliet.wav", {"Luciana Olivares", "Samba em Pli\xc3\xaa"}},
    {"MaxandSimon_EVERYONE TONIGHT MIX 2.wav", {"Max & Simon", "Everyone Tonight"}},
    {"Nave_Raiou_(MASTER).wav", {"Nave", "Raiou"}},
    {"NoNoNo_Pumpin Blood.wav", {"NONONO", "Pumpin Blood"}},
    {"Rael - Caminho.wav", {"Rael", "Caminho"}},
    {"Ritmo Machine - Brazil .wav", {"Ritmo Machine", "Brazil"}},
    {"RockyDawuni _African Thriller.wav", {"Rocky Dawuni", "African Thriller"}},
    {"SaintsOfValory_Kids.wav", {"Saints of Valory", "Kids"}},
    {"Switchfoot_Who We Are.wav", {"Switchfoot", "Who We Are"}},
    {"TimMyers_HILLS TO CLIMB - FULL MIX.wav", {"Tim Myers", "Hills to Climb"}},
    {"Tinie Tempah--Children Of The Sun_cleaned_01.wav", {"Tinie Tempah", "Children of the Sun"}},
    {"TrioMocoto--BelezaBelezaBeleza_cleaned_01.wav",
     {"Trio Mocot\xc3\xb3", "Beleza Beleza Beleza"}},
    {"Uh Huh-Main Mix V3 CD.wav", {"", "Uh Huh"}},
    {"We Are One - 13014c 24bit.wav",
     {"Pitbull feat. Jennifer Lopez & Claudia Leitte", "We Are One (Ole Ola)"}},
    {"WeTheKings_JUSTKEEPBREATHING_MIX8.wav", {"We the Kings", "Just Keep Breathing"}},
};

// Menu events -> sound, as the game's front end pairs them (its UI bank's
// TILE_NAV / LR_SCROLL, PRIMARY_SELECT, CANCEL_BACK events). Lists use the
// scroll sound too: the bank's WC_FE_ListNav (GENERAL_NAV) is 10 ms of silence.
// Switching settings sections uses the bumpers' sound (LB_RB).
const char* kEffectSounds[] = {"wc_fe_lrscroll.wav",   "wc_fe_lrscroll.wav", "wc_fe_lrscroll.wav",
                               "wc_fe_mainselect.wav", "wc_fe_back.wav",
                               "38_g3_general_nav_lrb.wav"};

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

WAVEFORMATEX Format(const Pcm& pcm) {
  WAVEFORMATEX format = {};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = WORD(pcm.channels);
  format.nSamplesPerSec = DWORD(pcm.sample_rate);
  format.wBitsPerSample = 16;
  format.nBlockAlign = WORD(2 * pcm.channels);
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  return format;
}

}  // namespace

struct Audio::Impl {
  Microsoft::WRL::ComPtr<IXAudio2> engine;
  IXAudio2MasteringVoice* master = nullptr;
  std::vector<Pcm> effects;  // by Effect
  std::vector<IXAudio2SourceVoice*> playing_effects;

  // Music: the next song decodes on a worker thread while one plays.
  const GameFiles* files = nullptr;
  SoundBank banks[2];
  struct Track {
    int bank;
    size_t entry;
    Song song;
  };
  std::vector<Track> playlist;
  size_t next_track = 0;
  IXAudio2SourceVoice* music = nullptr;
  Pcm music_pcm;  // the song playing (its samples must outlive the voice)
  Song current;
  bool music_enabled = true;
  float music_volume = 0.5f;

  std::thread worker;
  std::mutex mutex;
  std::condition_variable wake;
  bool want_next = false, quit = false, next_ready = false;
  Pcm next_pcm;
  Song next_song;

  void Worker() {
    std::unique_lock lock(mutex);
    while (true) {
      wake.wait(lock, [&] { return quit || want_next; });
      if (quit) return;
      want_next = false;
      Track track = playlist[next_track % playlist.size()];
      next_track = (next_track + 1) % playlist.size();
      lock.unlock();
      Pcm pcm;
      bool ok = DecodeSps(banks[track.bank].Stream(*files, track.entry), pcm);
      lock.lock();
      if (ok) {
        next_pcm = std::move(pcm);
        next_song = track.song;
        next_ready = true;
      } else {
        want_next = true;  // a song that doesn't decode is skipped
      }
    }
  }

  void StopMusic() {
    if (music) {
      music->DestroyVoice();
      music = nullptr;
    }
    current = {};
  }
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {}

Audio::~Audio() {
  {
    std::lock_guard lock(impl_->mutex);
    impl_->quit = true;
  }
  impl_->wake.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
  impl_->StopMusic();
  for (IXAudio2SourceVoice* voice : impl_->playing_effects) voice->DestroyVoice();
  if (impl_->master) impl_->master->DestroyVoice();
}

void Audio::Load(const GameFiles& files, bool anthem_first) {
  Impl& s = *impl_;
  s.files = &files;
  HRESULT result = XAudio2Create(&s.engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (SUCCEEDED(result)) result = s.engine->CreateMasteringVoice(&s.master);
  if (FAILED(result)) {
    Log("audio: no sound device (XAudio2 error %08lX), menu stays silent", result);
    s.engine.Reset();
    return;
  }
  SoundBank menu;
  if (menu.Load(files, "audiodata/uisfx/ui_sfx_core_menu")) {
    s.effects.resize(std::size(kEffectSounds));
    for (size_t e = 0; e < std::size(kEffectSounds); ++e) {
      for (size_t i = 0; i < menu.entries().size(); ++i) {
        if (Lower(menu.entries()[i].name) == kEffectSounds[e]) {
          DecodeSps(menu.Stream(files, i), s.effects[e]);
          break;
        }
      }
    }
  }
  const char* bank_names[] = {"audiodata/eatrax/eatrax_01", "audiodata/eatrax/eatrax_02"};
  for (int b = 0; b < 2; ++b) {
    if (!s.banks[b].Load(files, bank_names[b])) continue;
    for (size_t i = 0; i < s.banks[b].entries().size(); ++i) {
      auto track = kTracks.find(s.banks[b].entries()[i].name);
      if (track != kTracks.end()) s.playlist.push_back({b, i, track->second});
    }
  }
  size_t effects = 0;
  for (const Pcm& pcm : s.effects) effects += !pcm.samples.empty();
  Log("audio: %zu menu sounds, %zu songs", effects, s.playlist.size());
  if (!s.playlist.empty()) {
    std::shuffle(s.playlist.begin(), s.playlist.end(), std::mt19937(std::random_device{}()));
    if (anthem_first) {
      auto anthem = std::find_if(s.playlist.begin(), s.playlist.end(), [](const Impl::Track& t) {
        return t.song.title == "We Are One (Ole Ola)";
      });
      if (anthem != s.playlist.end()) std::iter_swap(s.playlist.begin(), anthem);
    }
    s.worker = std::thread([&s] { s.Worker(); });
    std::lock_guard lock(s.mutex);
    s.want_next = true;
    s.wake.notify_all();
  }
}

void Audio::Play(Effect effect) {
  Impl& s = *impl_;
  if (!sounds_enabled_ || !s.engine || size_t(effect) >= s.effects.size()) return;
  const Pcm& pcm = s.effects[size_t(effect)];
  if (pcm.samples.empty()) return;
  WAVEFORMATEX format = Format(pcm);
  IXAudio2SourceVoice* voice = nullptr;
  if (FAILED(s.engine->CreateSourceVoice(&voice, &format))) return;
  XAUDIO2_BUFFER buffer = {};
  buffer.AudioBytes = UINT32(pcm.samples.size() * 2);
  buffer.pAudioData = reinterpret_cast<const BYTE*>(pcm.samples.data());
  buffer.Flags = XAUDIO2_END_OF_STREAM;
  voice->SetVolume(sounds_volume_ * sounds_volume_);  // the same squared curve as the music
  voice->SubmitSourceBuffer(&buffer);
  voice->Start();
  s.playing_effects.push_back(voice);
}

void Audio::Update() {
  Impl& s = *impl_;
  if (!s.engine) return;
  // Finished menu sounds.
  for (auto it = s.playing_effects.begin(); it != s.playing_effects.end();) {
    XAUDIO2_VOICE_STATE state;
    (*it)->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (!state.BuffersQueued) {
      (*it)->DestroyVoice();
      it = s.playing_effects.erase(it);
    } else {
      ++it;
    }
  }
  if (!s.music_enabled || s.playlist.empty()) return;
  bool finished = true;
  if (s.music) {
    XAUDIO2_VOICE_STATE state;
    s.music->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    finished = !state.BuffersQueued;
  }
  if (!finished) return;
  std::lock_guard lock(s.mutex);
  if (!s.next_ready) return;
  s.StopMusic();
  s.music_pcm = std::move(s.next_pcm);
  s.current = s.next_song;
  s.next_ready = false;
  WAVEFORMATEX format = Format(s.music_pcm);
  if (SUCCEEDED(s.engine->CreateSourceVoice(&s.music, &format))) {
    XAUDIO2_BUFFER buffer = {};
    buffer.AudioBytes = UINT32(s.music_pcm.samples.size() * 2);
    buffer.pAudioData = reinterpret_cast<const BYTE*>(s.music_pcm.samples.data());
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    s.music->SetVolume(s.music_volume * s.music_volume);
    s.music->SubmitSourceBuffer(&buffer);
    s.music->Start();
  }
  s.want_next = true;  // decode the following song meanwhile
  s.wake.notify_all();
}

void Audio::SkipSong() {
  Impl& s = *impl_;
  std::lock_guard lock(s.mutex);
  if (s.next_ready && s.music) {
    s.music->DestroyVoice();
    s.music = nullptr;
  }
}

Song Audio::CurrentSong() const { return impl_->music ? impl_->current : Song{}; }

void Audio::SetMusic(bool enabled, float volume) {
  Impl& s = *impl_;
  s.music_volume = volume;
  if (s.music) s.music->SetVolume(volume * volume);
  if (s.music_enabled && !enabled) s.StopMusic();
  s.music_enabled = enabled;
}

}  // namespace launcher
