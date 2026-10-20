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
  auto rect = window.loadGeometryRect();
  require(rect == QRect(0, 0, 995, 613), "DPR2/995x633 logical desktop must not request 1280x720 with a negative origin");
  require(rect.width() * 2 == 1990 && rect.height() * 2 == 1226, "DPR2 physical size remains bounded by the logical work area");
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
  std::cout << "Production default geometry fits small logical desktops and preserves valid saved windows\n";
}
