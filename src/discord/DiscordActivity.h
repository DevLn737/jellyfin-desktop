#pragma once

#include <QJsonObject>
#include <QUrl>
#include <QVariantMap>

namespace DiscordActivity
{
// Pure payload builder, shared by the Windows component and its tests.
QJsonObject build(const QVariantMap& item, const QUrl& playbackUrl,
                  qint64 positionMs, bool playing, bool buffering, qint64 nowSeconds);
QString posterUrl(const QVariantMap& item, const QUrl& playbackUrl);
}
