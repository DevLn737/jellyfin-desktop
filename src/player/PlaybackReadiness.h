#pragma once
#include <cstdint>
#include <cmath>
#include <optional>

// GUI-thread state for the existing mpv event consumer. Only an accepted
// START_FILE may publish readiness; draining a later seek/end invalidates an
// earlier restart before any WebChannel signal is emitted.
class PlaybackReadiness
{
public:
  void invalidate()
  {
    ++m_generation;
    m_active = m_ready = m_seeking = false;
    m_request = 0;
    m_seekTargetMs.reset();
  }
  void startFile(bool expectedFile, std::uint64_t request)
  {
    invalidate();
    m_active = expectedFile;
    m_request = expectedFile ? request : 0;
  }
  bool beginSeek(std::uint64_t request, std::optional<std::int64_t> targetMs = {})
  {
    if (!m_active) return false;
    m_request = request;
    m_seekTargetMs = targetMs;
    m_ready = m_seeking = false;
    return true;
  }
  void failedRequest(std::uint64_t request)
  {
    if (m_request != request) return;
    m_request = 0;
    m_seekTargetMs.reset();
    m_ready = m_seeking = false;
  }
  void seekEvent()
  {
    if (!m_active) return;
    m_ready = false;
    m_seeking = true;
    m_seekGeneration = m_generation;
  }
  void restartEvent()
  {
    if (!m_active) return;
    m_ready = true;
    m_readyGeneration = m_generation;
  }
  std::optional<std::uint64_t> takeSeeking()
  {
    if (!m_active || !m_seeking || m_seekGeneration != m_generation) return {};
    m_seeking = false;
    if (!m_request) return {};
    return m_request;
  }
  std::optional<std::uint64_t> takeReady(bool positionAvailable, std::int64_t positionMs = 0,
                                       bool seekComplete = true, bool paused = true)
  {
    if (!positionAvailable || !seekComplete || !m_active || !m_ready || m_readyGeneration != m_generation) return {};
    // An earlier restart may already be queued when a new seek is accepted.
    // Never relabel its old position as readiness for the latest request.
    if (paused && m_seekTargetMs && std::fabs(double(positionMs) - double(*m_seekTargetMs)) > 500.0) return {};
    m_ready = false;
    // The target guards this seek's first completion only. Later restarts
    // (buffer recovery or track changes) report the current file position.
    m_seekTargetMs.reset();
    if (!m_request) return {};
    return m_request;
  }
  bool readyPending() const { return m_active && m_ready && m_readyGeneration == m_generation; }
private:
  std::uint64_t m_generation = 0;
  std::uint64_t m_request = 0;
  std::uint64_t m_seekGeneration = 0;
  std::uint64_t m_readyGeneration = 0;
  std::optional<std::int64_t> m_seekTargetMs;
  bool m_active = false;
  bool m_ready = false;
  bool m_seeking = false;
};
