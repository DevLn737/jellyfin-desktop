#include <cstdlib>
#include <iostream>

// Exercise the exact production method; only replace the Qt signal machinery.
#define emit
class PlayerComponent
{
public:
  bool m_windowVisible = false;
  bool m_playbackActive = false;
  bool m_inPlayback = false;
  int refreshes = 0;
  void videoUiBackgroundReady() { ++refreshes; }
  void videoUiReady();
};
#include "VideoUiReadyMethod.inc"

static void check(bool visible, bool active, bool loaded, int expected, const char* scenario)
{
  PlayerComponent player;
  player.m_windowVisible = visible;
  player.m_playbackActive = active;
  player.m_inPlayback = loaded;
  player.videoUiReady();
  if (player.refreshes != expected)
  {
    std::cerr << scenario << ": unexpected background refresh count\n";
    std::exit(1);
  }
}

int main()
{
  check(true, true, true, 1, "playing video");
  check(true, false, true, 1, "paused SyncPlay video");
  check(true, false, true, 1, "buffering video");
  check(true, false, false, 0, "stopped video with stale visible output");
  check(true, true, false, 0, "stopped video with stale active state");
  check(false, true, true, 0, "playing audio only");
  check(false, false, true, 0, "paused audio only");
  check(false, false, false, 0, "idle player");
  std::cout << "Production video UI readiness accepts loaded video, including paused and buffering states\n";
}
