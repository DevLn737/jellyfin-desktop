#pragma once

#include "PlaybackValues.h"
#include <algorithm>
#include <optional>

// Per-file public media timeline. Music uses mpv's rebased clock so a paused
// audio output cannot clamp original PTS to the shorter HLS duration. Video
// keeps original decoder PTS to preserve independent audio/subtitle alignment.
class PlaybackTimeline
{
public:
  void reset()
  {
    ++m_generation;
    m_preserve = m_music = m_ready = false;
    m_startSeconds = 0;
    m_requestedStartMs = 0;
  }
  void startFile(bool preserve, bool music, std::int64_t startMs)
  {
    reset();
    m_preserve = preserve;
    m_music = music;
    m_requestedStartMs = startMs;
    m_ready = !preserve;
  }
  std::uint64_t generation() const { return m_generation; }
  bool preservesTimestamps() const { return m_preserve; }
  bool mapsMusic() const { return m_preserve && m_music; }
  bool configure(std::uint64_t generation, double sourceStartSeconds)
  {
    if (generation != m_generation || !m_preserve || !std::isfinite(sourceStartSeconds)) return false;
    std::int64_t magnitude = 0;
    if (!PlaybackValues::tryMillisecondsFromSeconds(std::fabs(sourceStartSeconds), magnitude)) return false;
    m_startSeconds = sourceStartSeconds;
    m_ready = true;
    return true;
  }
  std::optional<double> initialSeekSeconds() const
  {
    // A zero-start live stream begins at its first packet, even with nonzero PTS.
    if (m_ready && m_requestedStartMs == 0) return 0.0;
    return seekSeconds(m_requestedStartMs);
  }
  std::optional<double> seekSeconds(std::int64_t publicMs) const
  {
    if (!m_ready || publicMs < 0) return {};
    const double seconds = publicMs / 1000.0;
    return mapsMusic() ? std::max(0.0, seconds - m_startSeconds) : seconds;
  }
  bool positionMilliseconds(double rawSeconds, std::int64_t& result) const
  {
    if (!m_ready || !std::isfinite(rawSeconds) || rawSeconds < 0) return false;
    return PlaybackValues::tryMillisecondsFromSeconds(
      std::max(0.0, rawSeconds + (mapsMusic() ? m_startSeconds : 0.0)), result);
  }
  bool durationMilliseconds(double durationSeconds, std::int64_t& result) const
  {
    if (!m_ready || !std::isfinite(durationSeconds) || durationSeconds < 0) return false;
    return PlaybackValues::tryMillisecondsFromSeconds(
      std::max(0.0, durationSeconds + (m_preserve ? m_startSeconds : 0.0)), result);
  }
private:
  std::uint64_t m_generation = 0;
  bool m_preserve = false;
  bool m_music = false;
  bool m_ready = false;
  double m_startSeconds = 0;
  std::int64_t m_requestedStartMs = 0;
};
