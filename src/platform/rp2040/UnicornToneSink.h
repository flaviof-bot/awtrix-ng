#pragma once
#include "core/sound/AudioSinks.h"
#include "core/sound/ToneSamples.h"
#include "hal/BoardLog.h"
#include <I2S.h>

namespace awtrix {
// The Unicorn's speaker as AudioRouter's tone sink: RTTTL rendered by ToneSamples, streamed through
// the I2S amplifier. Melodies only; no MP3, track or radio.
class UnicornToneSink final : public sound::IToneSink {
 public:
  void setLog(BoardLogSink log) { log_ = log; }
  void begin() override;
  void setVolumes(const sound::Volumes& volumes) override;
  bool playRtttl(const std::string& text, sound::Group group) override;
  bool playMelodyFile(const std::string& name, sound::Group group) override;
  void stop() override;
  void tick() override;
  bool isPlaying() const override { return playing_; }
  bool ready() const { return ready_; }
 private:
  bool open();
  void applyVolume();
  static constexpr int BufferWords = 480; // 20 ms, three buffers + library silence buffer
  static constexpr int BufferCount = 3;
  I2S i2s_{OUTPUT};
  sound::ToneSamples samples_;
  sound::Volumes volumes_;
  sound::Group group_ = sound::Group::Alert;
  bool ready_ = false, running_ = false, playing_ = false, draining_ = false;
  BoardLogSink log_ = nullptr;
  uint32_t drainedAt_ = 0;
  int partial_ = 0;
};
}
