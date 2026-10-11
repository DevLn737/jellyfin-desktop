include(BuildManifest)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Windows distributions require x64")
endif()
set(INSTALLER_ARCH_STR windows-x64)
set(WINDOWS_ASSET_BASE "JellyfinMediaPlayer-${VERSION_STRING}-${INSTALLER_ARCH_STR}")
option(WIX_ACCEPT_EULA "Accept the published WiX 7 binary EULA when building packages" OFF)
option(JMP_PORTABLE_PROFILE "Include an explicit marker for isolated portable data" OFF)
set(WIX_EXECUTABLE "" CACHE FILEPATH "Locked WiX 7 CLI executable")
set(WIX_UTIL_EXTENSION "" CACHE FILEPATH "Locked WiX Util extension DLL")
set(WIX_BAL_EXTENSION "" CACHE FILEPATH "Locked WiX BootstrapperApplications extension DLL")
set(VCREDIST_EXE "" CACHE FILEPATH "Locked Microsoft x64 redistributable")
set(JMP_VCRUNTIME_DIR "" CACHE PATH "Microsoft x64 app-local CRT for portable deployment")
set(MPV_RUNTIME "" CACHE FILEPATH "libmpv runtime DLL (separate from MPV_LIBRARY)")
if(NOT MPV_RUNTIME)
  get_filename_component(_mpv_directory "${MPV_LIBRARY}" DIRECTORY)
  set(MPV_RUNTIME "${_mpv_directory}/libmpv-2.dll")
endif()
configure_file("${PROJECT_SOURCE_DIR}/bundle/win/Version.wxi.in" "${CMAKE_BINARY_DIR}/Version.wxi" @ONLY)
add_custom_target(windows_deploy
  COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config RelWithDebInfo
  DEPENDS ${MAIN_TARGET} COMMENT "Deploying Qt, WebEngine and libmpv" VERBATIM)
if(JMP_DIAGNOSTIC)
  set(_portable_suffix diagnostic-tracy-portable)
else()
  set(_portable_suffix portable)
endif()
set(WINDOWS_PORTABLE_ZIP "${CMAKE_BINARY_DIR}/${WINDOWS_ASSET_BASE}-${_portable_suffix}.zip")
add_custom_target(windows_zip
  COMMAND "${CMAKE_COMMAND}" "-DDEPLOYMENT_DIR=${CMAKE_INSTALL_PREFIX}"
    "-DSTAGING_DIR=${CMAKE_BINARY_DIR}/portable-stage" "-DOUTPUT_ZIP=${WINDOWS_PORTABLE_ZIP}"
    "-DRUNTIME_DIR=${JMP_VCRUNTIME_DIR}" "-DDIAGNOSTIC=${JMP_DIAGNOSTIC}"
    "-DISOLATED_PROFILE=${JMP_PORTABLE_PROFILE}"
    "-DMANIFEST_PATH=${PROJECT_SOURCE_DIR}/build-manifest.json"
    -P "${PROJECT_SOURCE_DIR}/CMakeModules/PreparePortableZip.cmake"
  DEPENDS windows_deploy VERBATIM)
if(JMP_DIAGNOSTIC)
  set(WINDOWS_SYMBOLS_ZIP "${CMAKE_BINARY_DIR}/${WINDOWS_ASSET_BASE}-diagnostic-symbols.zip")
  add_custom_target(windows_symbols
    COMMAND "${CMAKE_COMMAND}" "-DPDB_PATH=$<TARGET_PDB_FILE:${MAIN_TARGET}>"
      "-DSTAGING_DIR=${CMAKE_BINARY_DIR}/symbols-stage" "-DOUTPUT_ZIP=${WINDOWS_SYMBOLS_ZIP}"
      "-DMANIFEST_PATH=${PROJECT_SOURCE_DIR}/build-manifest.json" "-DBUILD_COMMIT=${JMP_BUILD_COMMIT}"
      -P "${PROJECT_SOURCE_DIR}/CMakeModules/PrepareDiagnosticSymbols.cmake"
    DEPENDS ${MAIN_TARGET} VERBATIM)
  add_dependencies(windows_zip windows_symbols)
endif()
# Native development and portable builds do not require the installer CLI.
if(NOT EXISTS "${WIX_EXECUTABLE}")
  message(STATUS "WiX CLI is absent; windows_zip remains available. Bootstrap installer tools to enable windows_package.")
  return()
endif()
foreach(_dependency WIX_UTIL_EXTENSION WIX_BAL_EXTENSION VCREDIST_EXE)
  if(NOT EXISTS "${${_dependency}}")
    message(FATAL_ERROR "${_dependency} must point to a locked bootstrap dependency")
  endif()
endforeach()
if(NOT WIX_ACCEPT_EULA)
  message(STATUS "WiX package targets require explicit WIX_ACCEPT_EULA=ON; native and portable targets remain available.")
  return()
endif()
set(_eula_arguments)
if(WIX_ACCEPT_EULA)
  list(APPEND _eula_arguments -acceptEula wix7)
endif()
execute_process(COMMAND "${WIX_EXECUTABLE}" ${_eula_arguments} --version OUTPUT_VARIABLE _wix_version
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT _wix_version MATCHES "^7\\.0\\.0([.+-]|$)")
  message(FATAL_ERROR "Expected locked WiX ${JMP_WIX_VERSION}, found ${_wix_version}")
endif()
set(_wix_include -i "${PROJECT_SOURCE_DIR}/bundle/win" -i "${CMAKE_BINARY_DIR}")
set(_msi "${CMAKE_BINARY_DIR}/JellyfinMediaPlayer.msi")
add_custom_target(windows_msi
  COMMAND "${WIX_EXECUTABLE}" build ${_eula_arguments} -arch x64 ${_wix_include}
    -ext "${WIX_UTIL_EXTENSION}" -bindpath "Deployment=${CMAKE_INSTALL_PREFIX}"
    -bindpath "${PROJECT_SOURCE_DIR}/bundle/win" -o "${_msi}"
    "${PROJECT_SOURCE_DIR}/bundle/win/JMP.wxs"
  DEPENDS windows_deploy VERBATIM)
if(NOT JMP_DIAGNOSTIC)
  add_custom_target(windows_package
    COMMAND "${WIX_EXECUTABLE}" build ${_eula_arguments} -arch x64 ${_wix_include}
      -ext "${WIX_UTIL_EXTENSION}" -ext "${WIX_BAL_EXTENSION}"
      -d "MsiPath=${_msi}" -d "VCRedistPath=${VCREDIST_EXE}"
      -bindpath "${PROJECT_SOURCE_DIR}/bundle/win"
      -o "${CMAKE_BINARY_DIR}/${WINDOWS_ASSET_BASE}.exe" "${PROJECT_SOURCE_DIR}/bundle/win/Bundle.wxs"
    DEPENDS windows_msi VERBATIM)
endif()
