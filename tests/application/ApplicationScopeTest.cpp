#include "shared/ApplicationScope.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

static void check(bool condition, const char* description)
{
  if (!condition) { std::fprintf(stderr, "%s\n", description); std::exit(1); }
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  check(temporary.isValid(), "temporary profile root unavailable");
  const QString first = temporary.filePath(QStringLiteral("portable Тест_日本語/data"));
  const QString second = temporary.filePath(QStringLiteral("other/data"));
  check(QDir().mkpath(first) && QDir().mkpath(second), "temporary profiles unavailable");
  const QString base = QStringLiteral("pmpUniqueApplication");
  check(ApplicationScope::socketName(base, false, first) == base, "installed socket identity changed");
  check(ApplicationScope::socketName(base, false, second) == base, "unmarked socket depends on path");
  const QString firstSocket = ApplicationScope::socketName(base, true, first);
  check(firstSocket == ApplicationScope::socketName(base, true, first + "/../data/./"), "equivalent profiles get different sockets");
  check(firstSocket != ApplicationScope::socketName(base, true, second), "portable profiles share socket");
  check(firstSocket.startsWith(base + '-') && firstSocket.size() == base.size() + 65, "portable socket lacks full SHA256");
  const QString inputBase = QStringLiteral("inputSocket");
  check(ApplicationScope::socketName(inputBase, false, first) == inputBase, "installed input socket identity changed");
  check(ApplicationScope::socketName(inputBase, true, first) != ApplicationScope::socketName(inputBase, true, second), "portable input sockets collide");
  check(ApplicationScope::socketName(inputBase, true, first) != firstSocket, "input and application endpoints collide");
#ifdef Q_OS_WIN
  check(firstSocket == ApplicationScope::socketName(base, true, QDir::toNativeSeparators(first.toUpper())), "Windows case/separator normalization changed socket");
#endif
  check(ApplicationScope::devToolsPort("9222") == 9222, "default port unavailable");
  check(ApplicationScope::devToolsPort("9223") == 9223, "independent port unavailable");
  check(ApplicationScope::devToolsPort("1024") == 1024 && ApplicationScope::devToolsPort("65535") == 65535, "port boundaries rejected");
  for (const QString invalid : {QString(), QStringLiteral("1023"), QStringLiteral("65536"), QStringLiteral("+9222"),
       QStringLiteral("-9222"), QStringLiteral(" 9222"), QStringLiteral("9222 "), QStringLiteral("9222x"),
       QStringLiteral("9222.0"), QStringLiteral("٩٢٢٢"), QStringLiteral("99999999999999999999")})
    check(!ApplicationScope::devToolsPort(invalid), "invalid/non-ASCII port accepted");
  const QString ordinaryFlags = QStringLiteral("--proxy-pac-url=\"C:\\Two Words\\proxy.pac\" --disable-features=MediaSessionService,Example --test-value=\"a b\" --literal='unchanged' --remote-debugging-portable=allowed");
  const QStringList expectedFlags = ApplicationScope::chromiumArguments(ordinaryFlags);
  for (const QString debugging : {
       QStringLiteral("--remote-debugging-port=9222 --remote-debugging-address=0.0.0.0 --remote-debugging-pipe"),
       QStringLiteral("\"--remote-debugging-port=9222\" \"--remote-debugging-address=0.0.0.0\" \"--remote-debugging-pipe\""),
       QStringLiteral("--remote-\"debugging\"-port=9222 --remote-debugging-address=\"0.0.0.0\""),
       QStringLiteral("--remote-debugging-port \"9222\" --remote-debugging-address 0.0.0.0"),
       QStringLiteral("--remote-debugging-port = 9222 --remote-debugging-address= 0.0.0.0"),
       QStringLiteral("-remote-debugging-port=9222 /remote-debugging-address=0.0.0.0 --REMOTE-DEBUGGING-PIPE")})
  {
    const QString sanitized = ApplicationScope::withoutRemoteDebugging(debugging + ' ' + ordinaryFlags);
    check(ApplicationScope::chromiumArguments(sanitized) == expectedFlags, "debugger flags survived or legitimate Chromium arguments changed");
  }
  check(ApplicationScope::withoutRemoteDebugging("--remote-debugging-port --disable-gpu") == "--disable-gpu", "missing debugger value consumed the next legitimate flag");
  check(ApplicationScope::chromiumArguments("  --a=\"b c\" x\"y\" \"\" z\tq  ")
      == QStringList({"--a=b c", "xy", "", "z\tq"}), "Chromium environment quote grammar differs from Qt");
  check(ApplicationScope::webEngineSoftwareRendering(false, false, true) == true,
    "Windows/Linux default did not select software WebEngine");
  check(ApplicationScope::webEngineSoftwareRendering(false, false, false) == false,
    "other platform WebEngine default changed");
  check(ApplicationScope::webEngineSoftwareRendering(true, false, false) == true,
    "explicit software mode was ignored");
  check(ApplicationScope::webEngineSoftwareRendering(false, true, true) == false &&
    ApplicationScope::webEngineSoftwareRendering(false, true, false) == false,
    "diagnostic GPU opt-in did not override the platform default");
  check(!ApplicationScope::webEngineSoftwareRendering(true, true, true) &&
    !ApplicationScope::webEngineSoftwareRendering(true, true, false),
    "contradictory CLI graphics modes were accepted");
  const QStringList defaults = {"--disable-features=MediaSessionService"};
  const QStringList normal = ApplicationScope::chromiumArguments(ApplicationScope::chromiumLaunchFlags("", defaults, false));
  check(normal == defaults, "default Chromium switches were lost");
  const QStringList fallback = ApplicationScope::chromiumArguments(ApplicationScope::chromiumLaunchFlags(
    "\"--remote-debugging-port=9222\" --disable-features=One,Two --proxy-pac-url=\"C:\\Two Words\\proxy.pac\"", defaults, true));
  check(fallback == QStringList({"--disable-features=One,Two,MediaSessionService", "--proxy-pac-url=C:\\Two Words\\proxy.pac",
    "--disable-gpu"}), "GPU fallback lost inherited flags or default feature merge");
  check(ApplicationScope::chromiumArguments(ApplicationScope::chromiumLaunchFlags(
    "--disable-features=One,MediaSessionService<Trial:param/value --disable-gpu", defaults, true))
    == QStringList({"--disable-features=One,MediaSessionService<Trial:param/value", "--disable-gpu"}),
    "feature parameters changed or GPU/default features duplicated");
  const QStringList softwareRetained = {"--proxy-pac-url=C:\\Two Words\\proxy.pac",
    "--disable-features=One,Two,MediaSessionService", "--enable-features=Example<Trial:param/value",
    "--disable-gpu-rasterization", "--disable-gpu"};
  for (const QString graphics : {
       QStringLiteral("--use-gl=angle --use-angle=gl --use-vulkan=native --enable-gpu-rasterization"),
       QStringLiteral("\"--use-gl=angle\" --use-angle \"d3d11\" --use-vulkan=native --force-gpu-rasterization"),
       QStringLiteral("--use-gl = angle --use-angle= gl --use-vulkan native --gpu-preferences=opaque"),
       QStringLiteral("-USE-GL=angle /use-angle=gl --enable-gpu --enable-gpu-compositing --ignore-gpu-blocklist"),
       QStringLiteral("--use-\"gl\"=angle --use-angle=gl --disable-software-rasterizer --enable-accelerated-2d-canvas --disable-gpu --disable-gpu=1")})
  {
    const QString launch = ApplicationScope::chromiumLaunchFlags(graphics + QStringLiteral(
      " --proxy-pac-url=\"C:\\Two Words\\proxy.pac\" --disable-features=One,Two"
      " --enable-features=Example<Trial:param/value --disable-gpu-rasterization"
      " \"--remote-debugging-port=9222\""), defaults, true);
    check(ApplicationScope::chromiumArguments(launch) == softwareRetained,
      "software WebEngine retained a competing GPU backend or changed unrelated arguments");
  }
  check(ApplicationScope::chromiumArguments(ApplicationScope::chromiumLaunchFlags(
    "--use-gl --disable-features=One", defaults, true))
    == QStringList({"--disable-features=One,MediaSessionService", "--disable-gpu"}),
    "missing GPU backend value consumed a legitimate following switch");
  check(ApplicationScope::chromiumArguments(ApplicationScope::chromiumLaunchFlags(
    "--use-gl=angle --disable-gpu --use-angle=d3d11", defaults, false, true))
    == QStringList({"--use-gl=angle", "--use-angle=d3d11", "--disable-features=MediaSessionService"}),
    "explicit diagnostic GPU opt-in retained inherited software mode or lost backend flags");
  check(ApplicationScope::chromiumArguments(ApplicationScope::chromiumLaunchFlags(
    "--disable-gpu --use-gl=angle --enable-gpu-rasterization", defaults, false))
    == QStringList({"--disable-features=MediaSessionService", "--disable-gpu"}),
    "inherited software mode did not normalize competing hardware switches");
  check(ApplicationScope::chromiumArguments(ApplicationScope::chromiumLaunchFlags(
    "--use-angle=d3d11 --enable-gpu-rasterization", defaults, false))
    == QStringList({"--use-angle=d3d11", "--enable-gpu-rasterization", "--disable-features=MediaSessionService"}),
    "automatic mode changed other platforms' explicit environment choices");
  const QStringList legacyLinux = {"--disable-features=MediaSessionService", "--disable-gpu"};
  check(ApplicationScope::chromiumArguments(ApplicationScope::chromiumLaunchFlags(
    "--use-gl=angle", legacyLinux, false))
    == QStringList({"--disable-features=MediaSessionService", "--disable-gpu"}),
    "Linux software default lost normalization");
  std::puts("portable IPC, strict DevTools and reliable WebEngine graphics mode checks passed");
  return 0;
}
