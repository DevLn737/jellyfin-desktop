#include "DiscordActivity.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QUrlQuery>

namespace
{
const QString Logo = QStringLiteral("https://raw.githubusercontent.com/jellyfin/jellyfin-media-player/v1.12.0/resources/images/icon.png");

QString clipped(QString text)
{
  text = text.simplified();
  // Discord limits these fields to 128 UTF-8 bytes. Do not split a code point.
  while (text.toUtf8().size() > 128)
  {
    if (text.at(text.size() - 1).isLowSurrogate())
      text.chop(2);
    else
      text.chop(1);
  }
  return text;
}

QString clockText(qint64 ms)
{
  const qint64 seconds = qMax<qint64>(0, ms) / 1000;
  return seconds >= 3600
    ? QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
    : QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
}

QString DiscordActivity::posterUrl(const QVariantMap& item, const QUrl& playbackUrl)
{
  // Never share a playback URL, authentication query, credentials or private host.
  // Discord's image proxy can only retrieve publicly accessible images.
  const QString host = playbackUrl.host().toLower();
  QHostAddress address;
  if (playbackUrl.scheme() != QStringLiteral("https") || host.isEmpty() ||
      address.setAddress(host) || !host.contains('.') || host.endsWith(".localhost") ||
      host.endsWith(".local") || host.endsWith(".lan") || host.endsWith(".internal"))
    return {};

  const bool episode = item.value("Type").toString() == QStringLiteral("Episode");
  QString id = episode ? item.value("SeriesId").toString() : item.value("Id").toString();
  QString tag = episode ? item.value("SeriesPrimaryImageTag").toString()
                        : item.value("ImageTags").toMap().value("Primary").toString();
  if (id.isEmpty() || tag.isEmpty())
    return {};
  const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9-]+$"));
  if (!identifier.match(id).hasMatch() || !identifier.match(tag).hasMatch())
    return {};

  // Preserve reverse-proxy subpaths, including /jellyfin, without trusting queries.
  const QString path = playbackUrl.path();
  const int videoPath = path.indexOf(QStringLiteral("/Videos/"), 0, Qt::CaseInsensitive);
  if (videoPath < 0)
    return {};
  QUrl result = playbackUrl.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
  result.setPath(path.left(videoPath) + QStringLiteral("/Items/%1/Images/Primary").arg(id));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("tag"), tag);
  query.addQueryItem(QStringLiteral("maxHeight"), QStringLiteral("512"));
  query.addQueryItem(QStringLiteral("quality"), QStringLiteral("90"));
  result.setQuery(query);
  const QString url = result.toString(QUrl::FullyEncoded);
  return url.size() <= 256 ? url : QString();
}

QJsonObject DiscordActivity::build(const QVariantMap& item, const QUrl& playbackUrl,
                                   qint64 positionMs, bool playing, bool buffering, qint64 nowSeconds)
{
  const QString type = item.value("Type").toString();
  if (type != QStringLiteral("Movie") && type != QStringLiteral("Episode"))
    return {};

  const bool episode = type == QStringLiteral("Episode");
  QString title = episode ? item.value("SeriesName").toString() : item.value("Name").toString();
  if (title.trimmed().isEmpty())
    title = item.value("Name").toString();
  if (title.trimmed().isEmpty())
    return {};

  QStringList state;
  if (episode)
  {
    if (item.contains("ParentIndexNumber") && !item.value("ParentIndexNumber").isNull())
      state << QStringLiteral("S%1").arg(item.value("ParentIndexNumber").toInt(), 2, 10, QLatin1Char('0'));
    if (item.contains("IndexNumber") && !item.value("IndexNumber").isNull())
    {
      QString number = QStringLiteral("E%1").arg(item.value("IndexNumber").toInt(), 2, 10, QLatin1Char('0'));
      if (item.value("IndexNumberEnd").toInt() > item.value("IndexNumber").toInt())
        number += QStringLiteral("\u2013%1").arg(item.value("IndexNumberEnd").toInt(), 2, 10, QLatin1Char('0'));
      state << number;
    }
  }
  else if (item.value("ProductionYear").toInt() > 0)
    state << QString::number(item.value("ProductionYear").toInt());

  const QString status = buffering ? QStringLiteral("Buffering")
    : playing ? QStringLiteral("Watching") : QStringLiteral("Paused");
  QString stateText = state.join(QLatin1Char(' '));
  if (!stateText.isEmpty())
    stateText += QStringLiteral(" \u00b7 ");
  stateText += status;
  if (!playing)
    stateText += QStringLiteral(" \u00b7 ") + clockText(positionMs);

  const QString poster = posterUrl(item, playbackUrl);
  QJsonObject assets{{"large_image", poster.isEmpty() ? Logo : poster},
                     {"large_text", clipped(title)}, {"small_image", Logo},
                     {"small_text", QStringLiteral("Jellyfin Desktop")}};
  QJsonObject activity{{"type", 3}, {"details", clipped(title)},
                       {"state", clipped(stateText)}, {"assets", assets}, {"instance", false}};
  if (playing)
  {
    // Start only means elapsed time. An end timestamp switches Discord to remaining time.
    const qint64 start = qMax<qint64>(1, nowSeconds - qMax<qint64>(0, positionMs) / 1000);
    activity.insert("timestamps", QJsonObject{{"start", static_cast<double>(start)}});
  }
  return activity;
}
