#include <QCoreApplication>
#include <QTimer>
#include <QVariant>
#include <QRect>
#include <QDebug>
#include <QMap>
#include <iostream>
static constexpr const char* SETTINGS_SECTION_MAIN = "main";
static constexpr const char* SETTINGS_SECTION_STATE = "state";
class SettingsComponent {
public:
  static SettingsComponent& Get() { static SettingsComponent s; return s; }
  QVariant value(const char*, const char* key) const { return values.value(key); }
  QMap<QString, QVariant> values;
};
class SystemComponent {
public:
  static SystemComponent& Get() { static SystemComponent s; return s; }
  bool isOpenELEC() const { return openELEC; }
  bool openELEC = false;
};
class InputComponent {
public:
  static InputComponent& Get() { static InputComponent s; return s; }
  void cancelAutoRepeat() {}
};
class QWindow {
public:
  enum Visibility { FullScreen, Windowed, Maximized };
};
class QScreen {
public:
  QString name() const { return "forced"; }
  QRect geometry() const { return QRect(0, 0, 1920, 1080); }
};
class QGuiApplication {
public:
  static QList<QScreen*> screens() { static QScreen s; return {&s}; }
};
class KonvergoWindow : public QObject {
public:
  void updateForcedScreen();
  void updateWindowState(bool saveGeo = true);
  void onScreenAdded(QScreen* screen);
  void updateScreens() {}
  void setScreen(QScreen*) { ++forcedMoves; }
  void setGeometry(QRect) { ++geometryWrites; }
  void setVisibility(QWindow::Visibility v) { current = v; }
  bool isFullScreen() const { return current == QWindow::FullScreen; }
  void saveGeometry() { ++geometrySaves; }
  QRect loadGeometry(bool restoreVisibility = false);
  QRect loadGeometryRect() { return QRect(0, 0, 2048, 1082); }
  QScreen* loadLastScreen() { return QGuiApplication::screens().first(); }
  QScreen* screen() { return QGuiApplication::screens().first(); }
  Qt::WindowFlags flags() const { return {}; }
  void setFlags(Qt::WindowFlags) {}
  bool m_closing = false;
  quint64 m_windowStateGeneration = 0;
  QWindow::Visibility current = QWindow::Windowed;
  int forcedMoves = 0;
  int geometryWrites = 0;
  int geometrySaves = 0;
};
#include "WindowFullscreenMethods.inc"
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc != 2) return 2;
  const QString scenario = QString::fromUtf8(argv[1]);
  auto& settings = SettingsComponent::Get().values;
  settings["forceFSScreen"] = "forced";
  KonvergoWindow window;
  int expectedMoves = 0;
  bool expectedFullscreen = false;
  if (scenario == "windowed-setting") {
    window.updateForcedScreen();
  } else if (scenario == "enter-exit") {
    settings["fullscreen"] = true;
    window.updateWindowState();
    settings["fullscreen"] = false;
    window.updateWindowState();
  } else if (scenario == "enter-exit-enter") {
    settings["fullscreen"] = true;
    window.updateWindowState();
    settings["fullscreen"] = false;
    window.updateWindowState();
    settings["fullscreen"] = true;
    window.updateWindowState();
    expectedMoves = 1;
    expectedFullscreen = true;
  } else if (scenario == "screen-added-exit") {
    settings["fullscreen"] = true;
    window.onScreenAdded(nullptr);
    settings["fullscreen"] = false;
    window.updateWindowState();
  } else if (scenario == "always-fullscreen" || scenario == "openelec") {
    settings["forceAlwaysFS"] = scenario == "always-fullscreen";
    SystemComponent::Get().openELEC = scenario == "openelec";
    window.updateWindowState();
    expectedMoves = 1;
    expectedFullscreen = true;
  } else if (scenario == "windowed-roundtrips") {
    settings["forceFSScreen"] = "";
    for (int i = 0; i < 3; ++i) {
      settings["fullscreen"] = true;
      window.updateWindowState();
      settings["fullscreen"] = false;
      window.updateWindowState();
    }
    if (window.current != QWindow::Windowed || window.geometryWrites != 3 || window.geometrySaves != 3)
      return 1;
  } else if (scenario == "maximized-roundtrips") {
    settings["forceFSScreen"] = "";
    settings["maximized"] = true;
    window.current = QWindow::Maximized;
    for (int i = 0; i < 3; ++i) {
      settings["fullscreen"] = true;
      window.updateWindowState();
      settings["fullscreen"] = false;
      window.updateWindowState();
    }
    if (window.current != QWindow::Maximized || window.geometryWrites != 0 || window.geometrySaves != 3)
      return 1;
  } else return 2;
  QTimer::singleShot(350, &app, &QCoreApplication::quit);
  app.exec();
  if (window.forcedMoves != expectedMoves || window.isFullScreen() != expectedFullscreen) {
    std::cerr << "Unexpected forced screen moves or fullscreen state\n";
    return 1;
  }
  return 0;
}
