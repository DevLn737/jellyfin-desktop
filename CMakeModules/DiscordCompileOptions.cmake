# Apply in the directory that owns the executable, not src/discord: source
# properties in that child directory do not reach the parent executable target.
# Keep production and standalone tests on the same MSVC character encoding.
function(configure_discord_sources)
  if(MSVC)
    set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS /utf-8 /we4566)
  endif()
endfunction()
