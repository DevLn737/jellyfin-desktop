#ifndef PLAYBACKVALUES_H
#define PLAYBACKVALUES_H

#include <cmath>
#include <cstdint>
#include <limits>

namespace PlaybackValues
{
// WebChannel exposes milliseconds; libmpv's time properties use seconds.
inline std::int64_t millisecondsFromSeconds(double seconds)
{
  if (!std::isfinite(seconds) || seconds < 0.0)
    return 0;
  const double milliseconds = seconds * 1000.0;
  if (!std::isfinite(milliseconds) ||
      milliseconds >= static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
    return 0;
  return static_cast<std::int64_t>(std::llround(milliseconds));
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
