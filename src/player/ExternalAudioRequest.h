#ifndef EXTERNALAUDIOREQUEST_H
#define EXTERNALAUDIOREQUEST_H

#include <cstdint>

// One asynchronous audio addition belongs to one active file. Request IDs are
// never reused across ordinary file/selection changes, so old replies cannot
// clear a newer pending addition. No stream URL is needed by this state machine.
class ExternalAudioRequest
{
public:
  std::uint64_t begin()
  {
    m_pending = AudioNamespace | m_next++;
    m_pendingGeneration = m_generation;
    return m_pending;
  }
  std::uint64_t cancel()
  {
    const auto pending = m_pending;
    m_pending = 0;
    return pending;
  }
  std::uint64_t nextFile()
  {
    const auto pending = cancel();
    ++m_generation;
    return pending;
  }
  bool complete(std::uint64_t id)
  {
    if (!id || id != m_pending || m_pendingGeneration != m_generation)
      return false;
    m_pending = 0;
    return true;
  }
  std::uint64_t pending() const { return m_pending; }
private:
  static constexpr std::uint64_t AudioNamespace = std::uint64_t(1) << 62;
  std::uint64_t m_next = 1;
  std::uint64_t m_generation = 0;
  std::uint64_t m_pendingGeneration = 0;
  std::uint64_t m_pending = 0;
};

#endif
