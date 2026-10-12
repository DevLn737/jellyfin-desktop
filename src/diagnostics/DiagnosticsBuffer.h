#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
class QIODevice;

namespace DesktopDiagnostics {
enum class Event { Process, Gui, Playback, Buffering, State, Command, SyncPlay, WebChannel };
// Bounded storage of pre-serialized, allowlisted numeric records. No raw input is retained.
class Buffer {
public:
  static constexpr qint64 WindowMs = 10 * 60 * 1000;
  static constexpr qint64 SessionMs = 15 * 60 * 1000;
  static constexpr qsizetype MaxBytes = 16 * 1024 * 1024;
  explicit Buffer(qsizetype capacity = MaxBytes - 8192) : m_capacity(capacity) {}
  bool append(qint64 elapsedMs, Event event, const QJsonObject& values);
  void clear();
  QList<QByteArray> snapshot(qint64 elapsedMs);
  Buffer freeze(qint64 elapsedMs) { prune(elapsedMs); return *this; }
  bool writeRecords(QIODevice& device) const;
  qsizetype bytes() const { return m_bytes; }
  quint64 dropped() const { return m_dropped; }
private:
  void prune(qint64 elapsedMs);
  struct Header { qint64 at; quint32 size; };
  Header front() const;
  void removeFront();
  void copyOut(qsizetype offset, char* target, qsizetype size) const;
  void copyIn(qsizetype offset, const char* source, qsizetype size);
  QByteArray m_storage;
  qsizetype m_head = 0;
  qsizetype m_capacity;
  qsizetype m_bytes = 0;
  quint64 m_dropped = 0;
};
}
