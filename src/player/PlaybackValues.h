#ifndef PLAYBACKVALUES_H
#define PLAYBACKVALUES_H

#include <cmath>
#include <cstdint>
#include <limits>

namespace PlaybackValues
{
inline bool isNativeRequestId(double request)
{
  return std::isfinite(request) && request >= 1 && request <= 9007199254740991.0 &&
         std::floor(request) == request;
}
inline bool tryMillisecondsFromSeconds(double seconds, std::int64_t& result)
{
  if (!std::isfinite(seconds) || seconds < 0.0) return false;
  const double milliseconds = seconds * 1000.0;
  if (!std::isfinite(milliseconds) ||
      milliseconds >= static_cast<double>((std::numeric_limits<std::int64_t>::max)())) return false;
  result = static_cast<std::int64_t>(std::llround(milliseconds));
  return true;
}
// WebChannel exposes milliseconds; libmpv's time properties use seconds.
inline std::int64_t millisecondsFromSeconds(double seconds)
{
  std::int64_t result = 0;
  return tryMillisecondsFromSeconds(seconds, result) ? result : 0;
}

// The native boundary also accepts temporary SyncPlay corrections below the
// minimum base rate (250). Never silently clamp a correction to a different rate.
inline bool isEffectiveRate(double ratePerMille)
{
  return std::isfinite(ratePerMille) && ratePerMille >= 200 && ratePerMille <= 5000 &&
         std::floor(ratePerMille) == ratePerMille;
}
}

#endif
