#include "DiagnosticsBuffer.h"
#include <QJsonDocument>
#include <QStringList>
#include <QIODevice>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace DesktopDiagnostics {
static QStringList fields(Event event)
{
  switch (event) {
    case Event::Process: return {"cpuPercent", "privateBytes", "childPrivateBytes", "childCpuPercent", "childCount", "sampleMs"};
    case Event::Gui: return {"delayMs"};
    case Event::Playback: return {"positionMs", "durationMs", "speed", "avsync", "decoder-frame-drop-count", "frame-drop-count", "demuxer-cache-duration", "estimated-vf-fps", "display-fps", "video-bitrate", "audio-bitrate", "sampleMs"};
    case Event::Buffering: return {"percent"};
    case Event::State: return {"state"};
    case Event::Command: return {"command", "durationMs"};
    case Event::SyncPlay: return {"event", "positionMs", "driftMs", "playbackRate", "status", "rttMs", "whenUtcMs"};
    case Event::WebChannel: return {"durationMs"};
  }
  return {};
}

bool Buffer::append(qint64 elapsedMs, Event event, const QJsonObject& values)
{
  if (elapsedMs < 0 || elapsedMs > SessionMs) return false;
  QJsonObject clean;
  for (const auto& key : fields(event)) {
    const auto value = values.value(key);
    if (value.isDouble() && std::isfinite(value.toDouble())) clean.insert(key, value);
  }
  if (clean.isEmpty()) return false;
  clean.insert("atMs", double(elapsedMs));
  clean.insert("kind", int(event));
  const auto json = QJsonDocument(clean).toJson(QJsonDocument::Compact);
  prune(elapsedMs);
  const auto charge = qsizetype(sizeof(Header)) + json.size();
  if (charge > m_capacity) { ++m_dropped; return false; }
  if (m_storage.isEmpty()) m_storage.resize(m_capacity);
  while (m_bytes + charge > m_capacity && m_bytes) {
    removeFront();
    ++m_dropped;
  }
  Header header{};
  header.at = elapsedMs;
  header.size = quint32(json.size());
  copyIn((m_head + m_bytes) % m_capacity, reinterpret_cast<const char*>(&header), sizeof(header));
  copyIn((m_head + m_bytes + sizeof(header)) % m_capacity, json.constData(), json.size());
  m_bytes += charge;
  return true;
}

void Buffer::prune(qint64 elapsedMs)
{
  while (m_bytes && elapsedMs - front().at > WindowMs) removeFront();
}
void Buffer::clear() { m_storage = {}; m_head = m_bytes = 0; m_dropped = 0; }
Buffer::Header Buffer::front() const {
  Header header{};
  copyOut(m_head, reinterpret_cast<char*>(&header), sizeof(header));
  return header;
}
void Buffer::removeFront() {
  const auto size = qsizetype(sizeof(Header)) + front().size;
  m_head = (m_head + size) % m_capacity;
  m_bytes -= size;
}
void Buffer::copyOut(qsizetype offset, char* target, qsizetype size) const {
  const auto first = std::min(size, m_capacity - offset);
  std::memcpy(target, m_storage.constData() + offset, size_t(first));
  if (size > first) std::memcpy(target + first, m_storage.constData(), size_t(size - first));
}
void Buffer::copyIn(qsizetype offset, const char* source, qsizetype size) {
  const auto first = std::min(size, m_capacity - offset);
  std::memcpy(m_storage.data() + offset, source, size_t(first));
  if (size > first) std::memcpy(m_storage.data(), source + first, size_t(size - first));
}
QList<QByteArray> Buffer::snapshot(qint64 elapsedMs)
{
  prune(elapsedMs);
  QList<QByteArray> result;
  qsizetype consumed = 0;
  while (consumed < m_bytes) {
    Header header{};
    copyOut((m_head + consumed) % m_capacity, reinterpret_cast<char*>(&header), sizeof(header));
    QByteArray json;
    json.resize(header.size);
    copyOut((m_head + consumed + sizeof(header)) % m_capacity, json.data(), json.size());
    result.append(json);
    consumed += sizeof(header) + header.size;
  }
  return result;
}
bool Buffer::writeRecords(QIODevice& device) const
{
  qsizetype consumed = 0;
  while (consumed < m_bytes) {
    Header header{};
    copyOut((m_head + consumed) % m_capacity, reinterpret_cast<char*>(&header), sizeof(header));
    QByteArray json;
    json.resize(header.size);
    copyOut((m_head + consumed + sizeof(header)) % m_capacity, json.data(), json.size());
    if (consumed && device.write(",", 1) != 1) return false;
    if (device.write(json) != json.size()) return false;
    consumed += sizeof(header) + header.size;
  }
  return true;
}
}
