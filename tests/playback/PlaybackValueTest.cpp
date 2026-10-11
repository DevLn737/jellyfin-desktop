#include "PlaybackValues.h"
#include "ExternalAudioRequest.h"
#include "VideoTrackRestore.h"
#include "profiling/TracyPrivacy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

static void check(bool condition, const char* message)
{
  if (!condition)
  {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

int main()
{
  using PlaybackValues::millisecondsFromSeconds;
  check(millisecondsFromSeconds(123.456) == 123456, "seconds must become milliseconds");
  check(millisecondsFromSeconds(1.001) == 1001, "binary rounding must not lose a millisecond");
  check(millisecondsFromSeconds(0.0004) == 0, "sub-millisecond values round down");
  check(millisecondsFromSeconds(0.0005) == 1, "half-millisecond values round up");
  check(millisecondsFromSeconds(0) == 0, "zero is a valid position");
  check(millisecondsFromSeconds(-1) == 0, "negative times are unavailable");
  check(millisecondsFromSeconds(std::numeric_limits<double>::quiet_NaN()) == 0, "NaN is unavailable");
  check(millisecondsFromSeconds(std::numeric_limits<double>::infinity()) == 0, "infinity is unavailable");
  check(millisecondsFromSeconds(-std::numeric_limits<double>::infinity()) == 0, "negative infinity is unavailable");
  check(millisecondsFromSeconds(std::numeric_limits<double>::max()) == 0, "multiplication overflow is unavailable");
  check(millisecondsFromSeconds(static_cast<double>(std::numeric_limits<std::int64_t>::max()) / 1000) == 0,
        "integer overflow must not cause an undefined conversion");
  check(PlaybackValues::isEffectiveRate(200), "slow correction below 250 must remain valid");
  check(PlaybackValues::isEffectiveRate(225), "fractional correction must remain valid");
  check(PlaybackValues::isEffectiveRate(250), "minimum base rate must remain valid");
  check(PlaybackValues::isEffectiveRate(1000), "normal rate must remain valid");
  check(PlaybackValues::isEffectiveRate(5000), "maximum rate must remain valid");
  check(!PlaybackValues::isEffectiveRate(199), "rate below the effective minimum must be rejected");
  check(!PlaybackValues::isEffectiveRate(5001), "rate above the maximum must be rejected");
  check(!PlaybackValues::isEffectiveRate(0), "zero rate must be rejected");
  check(!PlaybackValues::isEffectiveRate(-1000), "negative rate must be rejected");
  check(!PlaybackValues::isEffectiveRate(1250.5), "fractional per-mille input must be rejected before conversion");
  check(!PlaybackValues::isEffectiveRate(std::numeric_limits<double>::quiet_NaN()), "NaN rate must be rejected");
  check(!PlaybackValues::isEffectiveRate(std::numeric_limits<double>::infinity()), "infinite rate must be rejected");
  check(!PlaybackValues::isEffectiveRate(4294968296.0), "large WebChannel numbers must not wrap into valid int rates");

  ExternalAudioRequest request;
  const auto first = request.begin();
  check(first != 0 && request.pending() == first, "new addition must have a nonzero pending ID");
  check(request.cancel() == first, "track switch must cancel the pending addition");
  const auto second = request.begin();
  check(second != first, "a new track must use a new request ID");
  check(!request.complete(first), "stale completion must not clear a newer track request");
  check(request.pending() == second, "new selection must stay pending after stale completion");
  check(request.complete(second), "current completion must clear its pending request");
  check(!request.complete(second), "duplicate completion must have no effect");
  check(request.pending() == 0, "completion releases pending state");

  const auto failed = request.begin();
  check(request.complete(failed), "a failed current addition must also release pending state");
  const auto retry = request.begin();
  check(retry != failed, "retry after failure must not reuse the failed request ID");
  check(!request.complete(failed), "late failure must not cancel a retry");
  check(request.nextFile() == retry, "START_FILE or stop must cancel the previous file's addition");
  const auto nextEpisode = request.begin();
  check(!request.complete(retry), "previous file's reply must not select a track in the next episode");
  check(request.pending() == nextEpisode, "next episode's pending request must survive old replies");
  check(!request.complete(0), "unrelated command reply must be ignored");
  check(request.complete(nextEpisode), "next episode can complete normally");
  VideoTrackRestore video;
  video.nextFile();
  video.selection(5);
  video.contextLost();
  video.selection(0); // mpv_render_context_free publishes vid=no.
  check(video.takeRestoreTrack() == 5, "context recreation must retain the selected video track, not auto");
  check(video.takeRestoreTrack() == 0, "duplicate recreation must not restore a track twice");
  video.selection(0); // User disables video while the render context exists.
  video.contextLost();
  check(video.takeRestoreTrack() == 0, "recreation must not enable intentionally disabled video");
  video.selection(2);
  video.selection(3);
  video.contextLost();
  video.selection(0);
  check(video.takeRestoreTrack() == 3, "latest selected video track must be restored");
  video.selection(7);
  video.contextLost();
  video.nextFile();
  video.selection(9);
  check(video.takeRestoreTrack() == 0, "old context loss must not apply a selection to the next file");
  video.contextLost();
  video.selection(0);
  check(video.takeRestoreTrack() == 9, "next file can restore its own selected track");
  video.contextLost();
  video.nextFile(); // Stop or END_FILE invalidates the current selection.
  check(video.takeRestoreTrack() == 0, "stop must prevent restoring the previous file's video");
  video.contextLost();
  video.selection(0);
  check(video.takeRestoreTrack() == 0, "audio-only files must not gain a video track on recreation");
  check(std::strcmp(tracy::PlatformGetUserLogin(), "local") == 0, "Tracy must not query the OS login");
  check(tracy::PlatformGetUserFullName() == nullptr, "Tracy must omit the user's display name");
  char hostname[16]{};
  tracy::PlatformGetHostname(hostname, sizeof(hostname));
  check(std::strcmp(hostname, "localhost") == 0, "Tracy must not record the real machine name");
  char tiny[3] = {'X', 'X', 'X'};
  tracy::PlatformGetHostname(tiny, 0);
  check(tiny[0] == 'X', "zero-capacity host buffer must remain untouched");
  tracy::PlatformGetHostname(tiny, 1);
  check(tiny[0] == '\0' && tiny[1] == 'X', "one-byte host buffer must be NUL terminated without overflow");
  tracy::PlatformGetHostname(tiny, 2);
  check(tiny[0] == 'l' && tiny[1] == '\0' && tiny[2] == 'X', "short host buffer must be bounded and terminated");
  std::cout << "native playback lifecycle and Tracy identity privacy tests passed\n";
}
