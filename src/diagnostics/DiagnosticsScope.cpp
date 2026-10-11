#include "DiagnosticsScope.h"
#include "DiagnosticsComponent.h"
#include <atomic>
namespace DesktopDiagnostics {
static std::atomic<DiagnosticsComponent*> captureTarget{nullptr};
void setCaptureTarget(DiagnosticsComponent* component) { captureTarget.store(component); }
CommandScope::CommandScope(Command command) : m_command(command)
{
  m_target = captureTarget.load();
  if (m_target) m_timer.start();
}
CommandScope::~CommandScope()
{
  if (m_timer.isValid() && captureTarget.load() == m_target)
    m_target->recordCommand(int(m_command), m_timer.nsecsElapsed() / 1000000.0);
}
}
