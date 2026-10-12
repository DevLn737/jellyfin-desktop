#pragma once
#include <QElapsedTimer>
class DiagnosticsComponent;

namespace DesktopDiagnostics {
enum class Command { Load, Queue, Stop, Seek, Pause, Play, AudioTrack, SubtitleTrack, Rate, MpvEvents };
void setCaptureTarget(DiagnosticsComponent* component);
class CommandScope {
public:
  explicit CommandScope(Command command);
  ~CommandScope();
  CommandScope(const CommandScope&) = delete;
  CommandScope& operator=(const CommandScope&) = delete;
private:
  Command m_command;
  DiagnosticsComponent* m_target = nullptr;
  QElapsedTimer m_timer;
};
}
