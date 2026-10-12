#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <optional>

namespace ApplicationScope
{
inline QString socketName(const QString& defaultName, bool portable, const QString& dataRoot)
{
  if (!portable) return defaultName;
  const QFileInfo profile(dataRoot);
  QString normalized = profile.canonicalFilePath();
  if (normalized.isEmpty()) normalized = profile.absoluteFilePath();
  normalized = QDir::cleanPath(QDir::fromNativeSeparators(normalized));
#ifdef Q_OS_WIN
  normalized = normalized.toLower();
#endif
  const QByteArray hash = QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex();
  return defaultName + '-' + QString::fromLatin1(hash);
}

inline std::optional<unsigned> devToolsPort(const QString& text)
{
  if (text.isEmpty()) return {};
  unsigned port = 0;
  for (const QChar character : text)
  {
    const auto digit = character.unicode();
    if (digit < '0' || digit > '9') return {};
    port = port * 10 + unsigned(digit - '0');
    if (port > 65535) return {};
  }
  if (port < 1024) return {};
  return port;
}

// Match WebEngine's environment argument grammar: ASCII spaces separate
// arguments outside double quotes; double quotes are removed, including quotes
// inside an argument. Single quotes and backslashes remain literal characters.
inline QStringList chromiumArguments(const QString& text)
{
  QStringList result;
  QString argument;
  bool quoted = false;
  bool started = false;
  for (const QChar character : text)
  {
    if (character == '"') { quoted = !quoted; started = true; }
    else if (character == ' ' && !quoted)
    {
      if (started) result.append(argument);
      argument.clear();
      started = false;
    }
    else { argument.append(character); started = true; }
  }
  if (!argument.isEmpty()) result.append(argument);
  return result;
}

inline QString chromiumSwitchName(const QString& argument)
{
  const int separator = argument.indexOf('=');
  QString name = (separator < 0 ? argument : argument.left(separator)).trimmed();
  if (name.startsWith("--")) name.remove(0, 2);
  else if (name.startsWith('-') || name.startsWith('/')) name.remove(0, 1);
  else return {};
  return name.toLower();
}

inline bool isRemoteDebuggingArgument(const QString& argument)
{
  const QString name = chromiumSwitchName(argument);
  return name == "remote-debugging-port" || name == "remote-debugging-address"
    || name == "remote-debugging-pipe";
}

inline QString encodeChromiumArguments(const QStringList& arguments)
{
  QStringList encoded;
  for (const QString& argument : arguments)
    encoded.append(argument.isEmpty() || argument.contains(' ')
      ? '"' + argument + '"' : argument);
  return encoded.join(' ');
}

inline QString withoutRemoteDebugging(const QString& text)
{
  const QStringList arguments = chromiumArguments(text);
  QStringList retained;
  for (qsizetype index = 0; index < arguments.size(); ++index)
  {
    const QString& argument = arguments[index];
    if (isRemoteDebuggingArgument(argument))
    {
      const int separator = argument.indexOf('=');
      if (separator < 0 || argument.mid(separator + 1).isEmpty())
      {
        if (index + 1 < arguments.size() && arguments[index + 1] == "=") ++index;
        if (index + 1 < arguments.size() && !arguments[index + 1].startsWith('-')
            && !arguments[index + 1].startsWith('/')) ++index;
      }
      continue;
    }
    retained.append(argument);
  }
  return encodeChromiumArguments(retained);
}

inline std::optional<bool> webEngineSoftwareRendering(bool disableGpu, bool gpuOptIn,
                                                      bool platformDefaultsToSoftware)
{
  if (disableGpu && gpuOptIn) return {};
  if (gpuOptIn) return false;
  return disableGpu || platformDefaultsToSoftware;
}

inline QStringList selectChromiumGpuMode(const QStringList& arguments, bool software, bool gpuOptIn)
{
  QStringList retained;
  for (qsizetype index = 0; index < arguments.size(); ++index)
  {
    const QString& argument = arguments[index];
    const QString name = chromiumSwitchName(argument);
    const bool backendValue = name == "use-gl" || name == "use-angle" || name == "use-vulkan"
      || name == "gpu-preferences";
    const bool incompatible = backendValue || name == "enable-gpu" || name == "enable-gpu-compositing"
      || name == "enable-gpu-rasterization" || name == "force-gpu-rasterization"
      || name == "enable-accelerated-2d-canvas" || name == "ignore-gpu-blocklist"
      || name == "disable-software-rasterizer";
    if ((software && incompatible) || ((software || gpuOptIn) && name == "disable-gpu"))
    {
      // Qt checks a custom --use-gl before --disable-gpu. Consume its value
      // too, using the same quoting grammar as WebEngine's environment parser.
      const int separator = argument.indexOf('=');
      if (backendValue && (separator < 0 || argument.mid(separator + 1).isEmpty()))
      {
        if (index + 1 < arguments.size() && arguments[index + 1] == "=") ++index;
        if (index + 1 < arguments.size() && !arguments[index + 1].startsWith('-')
            && !arguments[index + 1].startsWith('/')) ++index;
      }
      continue;
    }
    retained.append(argument);
  }
  if (software) retained.append("--disable-gpu");
  return retained;
}

inline QString chromiumLaunchFlags(const QString& inherited, const QStringList& defaults,
                                   bool disableGpu, bool gpuOptIn = false)
{
  QStringList arguments = chromiumArguments(withoutRemoteDebugging(inherited));
  for (const QString& flag : defaults)
  {
    if (flag.startsWith("--disable-features="))
    {
      // Chromium uses the last occurrence of a switch. Extend that effective
      // vector, retaining inherited feature names and field-trial parameters.
      qsizetype lastVector = -1;
      for (qsizetype index = 0; index < arguments.size(); ++index)
        if (arguments[index].startsWith("--disable-features=", Qt::CaseInsensitive)) lastVector = index;
      if (lastVector < 0) { arguments.append(flag); continue; }
      QString& vector = arguments[lastVector];
      for (const QString& feature : flag.mid(flag.indexOf('=') + 1).split(',', Qt::SkipEmptyParts))
      {
        bool present = false;
        for (const QString& entry : vector.mid(vector.indexOf('=') + 1).split(','))
          if (entry.section('<', 0, 0).section(':', 0, 0).trimmed() == feature) present = true;
        if (!present) vector += (vector.endsWith('=') ? QString() : QStringLiteral(",")) + feature;
      }
    }
    else if (!arguments.contains(flag)) arguments.append(flag);
  }
  const bool software = disableGpu || (!gpuOptIn && std::any_of(arguments.begin(), arguments.end(),
    [](const QString& argument) { return chromiumSwitchName(argument) == "disable-gpu"; }));
  return encodeChromiumArguments(selectChromiumGpuMode(arguments, software, gpuOptIn));
}
}
