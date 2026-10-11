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
  std::puts("portable socket identity and strict DevTools port checks passed");
  return 0;
}
