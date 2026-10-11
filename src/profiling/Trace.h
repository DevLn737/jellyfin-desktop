#pragma once

// Only constant zone labels are allowed. Never attach media, URLs or IPC payloads.
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
// Zones retain their function and line while excluding the absolute checkout path.
#undef TracyFile
#define TracyFile "desktop-client"
#define JMP_PROFILE_ZONE(label) ZoneScopedN(label)
#define JMP_PROFILE_FRAME() FrameMark
#else
#define JMP_PROFILE_ZONE(label) do {} while (false)
#define JMP_PROFILE_FRAME() do {} while (false)
#endif
