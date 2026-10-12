#pragma once
#include <cstddef>

// Tracy's supported custom-user-info hook replaces login, display name and
// hostname in its welcome header. No OS identity is queried or retained.
#define TRACY_HAS_CUSTOM_USER_INFO
namespace tracy {
inline void PlatformGetHostname(char* buffer, std::size_t capacity)
{
  if (!buffer || !capacity) return;
  constexpr char name[] = "localhost";
  std::size_t index = 0;
  while (index + 1 < capacity && index + 1 < sizeof(name))
  {
    buffer[index] = name[index];
    ++index;
  }
  buffer[index] = '\0';
}
inline const char* PlatformGetUserLogin() { return "local"; }
inline const char* PlatformGetUserFullName() { return nullptr; }
}
