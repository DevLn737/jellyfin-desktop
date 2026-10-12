#include <QCoreApplication>
#include <QTimer>
#include <QVariant>
#include <iostream>

static constexpr const char* SETTINGS_SECTION_MAIN = "main";

// Persistence is the only substituted boundary. Both shutdown method bodies and
// the aboutToQuit connection target are taken from the production source.
class SettingsComponent
{
public:
  static SettingsComponent& Get() { static SettingsComponent settings; return settings; }
  QVariant value(const char*, const char*) const { return fullscreen; }
  bool fullscreen = false;
};

class KonvergoWindow : public QObject
{
public:
  explicit KonvergoWindow(QCoreApplication& application)
  {
    m_infoTimer = new QTimer(this);
    m_infoTimer->start(10000);
    connect(&application, &QCoreApplication::aboutToQuit, this, &KonvergoWindow::saveStateBeforeQuit);
  }
  void closingWindow();
  void saveStateBeforeQuit();
  void saveGeometry()
  {
    ++saves;
    if (reenterSave)
    {
      closingWindow();
      saveStateBeforeQuit();
    }
  }
  bool m_closing = false;
  QTimer* m_infoTimer = nullptr;
  bool reenterSave = false;
  int saves = 0;
};

#include "WindowCloseMethods.inc"

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  if (argc != 2)
    return 2;
  const QString scenario = QString::fromUtf8(argv[1]);
  const bool fullscreen = scenario.startsWith("fullscreen-");
  const bool directQuit = scenario == "direct-quit" || scenario == "fullscreen-quit";
  if (!directQuit && scenario != "native-close" && scenario != "reentrant-save" &&
      scenario != "duplicate-close" && scenario != "fullscreen-close")
    return 2;
  SettingsComponent::Get().fullscreen = fullscreen;
  KonvergoWindow window(application);
  window.reenterSave = scenario == "reentrant-save";
  int aboutToQuitCount = 0;
  bool timedOut = false;
  QObject::connect(&application, &QCoreApplication::aboutToQuit, &window, [&]
  {
    ++aboutToQuitCount;
    // A repeated close request while Qt is already exiting must be a no-op.
    window.closingWindow();
    window.saveStateBeforeQuit();
  });
  QTimer::singleShot(2000, &application, [&]
  {
    timedOut = true;
    application.exit(3);
  });
  QTimer::singleShot(0, &application, [&]
  {
    if (directQuit)
      application.quit();
    else
    {
      window.closingWindow();
      if (scenario == "duplicate-close")
        window.closingWindow();
    }
  });
  const int result = application.exec();
  if (result != 0 || timedOut || !window.m_closing || window.m_infoTimer->isActive() ||
      aboutToQuitCount != 1 || window.saves != (fullscreen ? 0 : 1))
  {
    std::cerr << "Shutdown must finish once, save windowed geometry once and stop its timer\n";
    return 1;
  }
  std::cout << "PASS " << scenario.toStdString() << '\n';
  return 0;
}
