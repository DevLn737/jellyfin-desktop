#pragma once
#include <atomic>
#include <cstdint>

// Selection/file changes run on the GUI thread. Only contextLost is called by
// the renderer; it marks loss before mpv deselects video during context free.
class VideoTrackRestore
{
public:
  void nextFile()
  {
    m_fileGeneration.fetch_add(1, std::memory_order_relaxed);
    m_selectedTrack = 0;
  }
  void selection(std::int64_t track)
  {
    if (track > 0 || !m_contextLost.load(std::memory_order_acquire))
      m_selectedTrack = track > 0 ? track : 0;
  }
  void contextLost()
  {
    m_lostGeneration.store(m_fileGeneration.load(std::memory_order_relaxed), std::memory_order_relaxed);
    m_contextLost.store(true, std::memory_order_release);
  }
  std::int64_t takeRestoreTrack()
  {
    if (!m_contextLost.exchange(false, std::memory_order_acquire) ||
        m_lostGeneration.load(std::memory_order_relaxed) != m_fileGeneration.load(std::memory_order_relaxed))
      return 0;
    return m_selectedTrack;
  }
private:
  std::atomic<std::uint64_t> m_fileGeneration{0};
  std::atomic<std::uint64_t> m_lostGeneration{0};
  std::atomic<bool> m_contextLost{false};
  std::int64_t m_selectedTrack = 0;
};
