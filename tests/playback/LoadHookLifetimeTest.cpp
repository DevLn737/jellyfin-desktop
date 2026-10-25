#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QTimer>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>
#include "PlaybackTimeline.h"

static void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

class PlayerComponent : public QObject
{
public:
  PlaybackTimeline m_playbackTimeline;
  int codecStarts = 0;
  bool deferCodecs = false;
  std::function<void()> pendingCodecCompletion;
  void startCodecsLoading(std::function<void()> continuation)
  {
    ++codecStarts;
    if (deferCodecs) pendingCodecCompletion = std::move(continuation);
    else continuation();
  }
  void continueLoadHookAfterDelay(int delayMilliseconds, std::function<void()> continueHook);
};
#include "LoadHookMethod.inc"

static void runFor(int milliseconds)
{
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  {
    PlayerComponent player;
    int acknowledgements = 0;
    player.continueLoadHookAfterDelay(15, [&] { ++acknowledgements; });
    require(player.codecStarts == 0 && acknowledgements == 0, "Respect display-switch delay");
    runFor(60);
    require(player.codecStarts == 1 && acknowledgements == 1, "Current file continues once after probing codecs");
  }
  {
    PlayerComponent player;
    std::vector<int> acknowledgedTokens;
    player.continueLoadHookAfterDelay(15, [&] { acknowledgedTokens.push_back(11); });
    player.m_playbackTimeline.reset(); // stop invalidates the old file
    player.continueLoadHookAfterDelay(0, [&] { acknowledgedTokens.push_back(22); });
    require(player.codecStarts == 1, "The replacement may configure codecs immediately");
    runFor(60);
    require(player.codecStarts == 1, "A superseded timer must not configure the replacement's codecs");
    require(acknowledgedTokens == std::vector<int>({22, 11}), "Release each original token, never relabel the old hook");
  }
  {
    PlayerComponent player;
    int acknowledgements = 0;
    player.deferCodecs = true;
    player.continueLoadHookAfterDelay(0, [&] { ++acknowledgements; });
    player.m_playbackTimeline.reset();
    player.pendingCodecCompletion();
    require(player.codecStarts == 1 && acknowledgements == 1, "An in-flight codec completion only releases its own old hook");
  }
  {
    int acknowledgements = 0;
    auto* player = new PlayerComponent;
    player->continueLoadHookAfterDelay(15, [&] { ++acknowledgements; });
    delete player;
    runFor(60);
    require(acknowledgements == 0, "Destroying the owner cancels callbacks before they dereference the player");
  }
  return 0;
}
