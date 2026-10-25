#include <QDebug>
#include <QMargins>
#include <QRect>
#include <QVariantMap>
#include <cstdlib>
#include <iostream>

#define WEBUI_SIZE QSize(1280, 720)
#define SETTINGS_SECTION_STATE "state"
class QScreen
{
public:
  QRect desktop;
  QRect workArea;
  QRect geometry() const { return desktop; }
  QRect availableGeometry() const { return workArea; }
};
class QGuiApplication
{
public:
  static QList<QScreen*> displays;
  static QList<QScreen*> screens() { return displays; }
};
QList<QScreen*> QGuiApplication::displays;
class SettingsComponent
{
public:
  QVariantMap saved;
  static SettingsComponent& Get() { static SettingsComponent settings; return settings; }
  QVariant value(const char*, const char*) const { return saved; }
};
class KonvergoWindow
{
public:
  QScreen display;
  QMargins decoration;
  QMargins nativeDecoration {6, 29, 6, 6};
  QMargins m_windowedFrameMargins;
  bool nativeCreated = false;
  bool fullscreen = false;
  int nativeCreates = 0;
  void* handle() const { return nativeCreated ? (void*)this : nullptr; }
  void create() { nativeCreated = true; ++nativeCreates; decoration = nativeDecoration; }
  bool isFullScreen() const { return fullscreen; }
  QScreen* loadLastScreen() { return &display; }
  void setScreen(QScreen*) {}
  QScreen* screen() { return &display; }
  QMargins frameMargins() const { return decoration; }
  QSize windowMinSize() const { return QSize(213, 120); }
  bool fitsInScreens(const QRect& rect) const { return display.desktop.contains(rect); }
  QRect loadGeometryRect();
};
#include "WindowGeometryMethod.inc"

static void require(bool value, const char* message)
{
  if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main()
{
  KonvergoWindow window;
  window.display = {QRect(0, 0, 995, 633), QRect(0, 0, 995, 613)};
  QGuiApplication::displays = {&window.display};
  require(window.frameMargins().isNull(), "Before native creation the platform reports zero margins");
  auto rect = window.loadGeometryRect();
  require(rect == QRect(6, 29, 983, 578), "DPR2 default must include real caption margins discovered after native creation");
  require(window.nativeCreates == 1, "Create the native window before querying its frame margins");
  require(rect.width() * 2 == 1966 && rect.height() * 2 == 1156, "DPR2 client plus native frame remains inside the work area");
  window.decoration = QMargins(6, 29, 6, 6);
  rect = window.loadGeometryRect();
  require(rect == QRect(6, 29, 983, 578), "Known decorations must fit as well as the client rectangle");
  require(rect.size().expandedTo(window.windowMinSize()) == rect.size(), "Default respects supported minimum size");
  window.display = {QRect(1920, -200, 1920, 1080), QRect(1920, -200, 1920, 1040)};
  rect = window.loadGeometryRect();
  require(rect.size() == QSize(1280, 720), "Normal large screens retain preferred 720p size");
  require(rect == QRect(2240, -29, 1280, 720), "Center within the work area using its real multi-monitor origin");
  window.decoration = {};
  window.display = {QRect(0, 0, 1280, 800), QRect(0, 0, 1280, 760)};
  require(window.loadGeometryRect() == QRect(0, 20, 1280, 720), "Normal 1280-wide screen retains centered 720p default");
  SettingsComponent::Get().saved = {{"x", 100}, {"y", 100}, {"width", 800}, {"height", 500}};
  require(window.loadGeometryRect() == QRect(100, 100, 800, 500), "Valid saved geometry remains unchanged");
  SettingsComponent::Get().saved["x"] = -10000;
  require(window.loadGeometryRect() == QRect(0, 20, 1280, 720), "Off-screen saved geometry falls back to bounded default");
  window.decoration = QMargins(6, 29, 6, 6);
  SettingsComponent::Get().saved = {{"x", 0}, {"y", 0}, {"width", 600}, {"height", 400}};
  require(window.loadGeometryRect() == QRect(6, 29, 600, 400), "Older saved client rects must expose their native caption without changing a fitting size");
  window.fullscreen = true;
  window.decoration = {};
  require(window.loadGeometryRect() == QRect(6, 29, 600, 400), "Fullscreen restoration must use retained normal-frame margins");
  window.fullscreen = false;
  window.decoration = QMargins(6, 29, 6, 6);
  for (int i = 0; i < 4; ++i) window.loadGeometryRect();
  require(window.nativeCreates == 1, "Geometry restoration never recreates an existing native playback window");
  window.display = {QRect(0, 0, 400, 300), QRect(0, 0, 100, 90)};
  SettingsComponent::Get().saved = {{"x", 0}, {"y", 0}, {"width", 250}, {"height", 150}};
  require(window.loadGeometryRect() == QRect(6, 29, 213, 120), "A work area below the minimum anchors the caption without inverted clamp bounds");
  std::cout << "Production default geometry fits small logical desktops and preserves valid saved windows\n";
}
