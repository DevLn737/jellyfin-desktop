#include "DiscordActivity.h"
#include "DiscordIpc.h"
#include <QJsonDocument>
#include <QLocalServer>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <QtEndian>

namespace
{
QByteArray frame(quint32 opcode, const QByteArray& payload)
{
  QByteArray bytes(8, '\0');
  qToLittleEndian<quint32>(opcode, reinterpret_cast<uchar*>(bytes.data()));
  qToLittleEndian<quint32>(payload.size(), reinterpret_cast<uchar*>(bytes.data() + 4));
  return bytes + payload;
}

QByteArray jsonFrame(const QJsonObject& payload)
{
  return frame(1, QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

QJsonObject readJson(QLocalSocket* socket)
{
  // The fake peer and client share an event loop; never block on waitForReadyRead.
  QElapsedTimer timer;
  timer.start();
  while (socket->bytesAvailable() < 8 && timer.elapsed() < 10000)
    QTest::qWait(10);
  if (socket->bytesAvailable() < 8)
    return {};
  const auto header = socket->peek(8);
  const auto size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData() + 4));
  if (size > 65536)
    return {};
  while (socket->bytesAvailable() < 8 + size && timer.elapsed() < 10000)
    QTest::qWait(10);
  if (socket->bytesAvailable() < 8 + size)
    return {};
  socket->read(8);
  return QJsonDocument::fromJson(socket->read(size)).object();
}

void acknowledge(QLocalSocket* socket, const QJsonObject& request)
{
  socket->write(jsonFrame({{"cmd", "SET_ACTIVITY"}, {"nonce", request.value("nonce")}, {"data", QJsonObject()}}));
  socket->flush();
}

QVariantMap movie()
{
  return {{"Type", "Movie"}, {"Id", "abc123"}, {"Name", "Arrival"}, {"ProductionYear", 2016},
          {"ImageTags", QVariantMap{{"Primary", "etag123"}}}};
}

const QUrl Playback(QStringLiteral("https://user:password@media.example.org/jellyfin/Videos/abc123/stream.mkv?ApiKey=secret&token=secret#private"));
const QString AppId = QStringLiteral("1557831977250463766");
}

class DiscordPresenceTest : public QObject
{
  Q_OBJECT
private slots:
  void movieAndElapsedTimer()
  {
    const auto activity = DiscordActivity::build(movie(), Playback, 125000, true, false, 1800000000);
    QCOMPARE(activity.value("type").toInt(), 3);
    QCOMPARE(activity.value("details").toString(), QStringLiteral("Arrival"));
    QCOMPARE(activity.value("state").toString(), QStringLiteral("Фильм · 2016"));
    QCOMPARE(activity.value("timestamps").toObject().value("start").toDouble(), 1799999875.0);
    QVERIFY(!activity.value("timestamps").toObject().contains("end"));
    const auto assets = activity.value("assets").toObject();
    QVERIFY(assets.value("large_image").toString().contains("/jellyfin/Items/abc123/Images/Primary?"));
    QVERIFY(assets.value("small_image").toString().endsWith("/icon.png"));
    const auto json = QJsonDocument(activity).toJson();
    QVERIFY(!json.contains("secret"));
    QVERIFY(!json.contains("password"));
    QVERIFY(!json.contains("user:"));
  }

  void pauseSeekResumeAndBuffering()
  {
    auto activity = DiscordActivity::build(movie(), Playback, 125000, false, false, 1800000000);
    QCOMPARE(activity.value("timestamps").toObject().value("start").toDouble(), 1799999875.0);
    QCOMPARE(activity.value("state").toString(), QStringLiteral("Фильм · 2016 · Пауза · 2:05"));
    activity = DiscordActivity::build(movie(), Playback, 3600000, false, false, 1800000010);
    QVERIFY(activity.value("state").toString().endsWith("1:00:00"));
    activity = DiscordActivity::build(movie(), Playback, 3600000, true, false, 1800000100);
    QCOMPARE(activity.value("timestamps").toObject().value("start").toDouble(), 1799996500.0);
    activity = DiscordActivity::build(movie(), Playback, 3600000, false, true, 1800000100);
    QCOMPARE(activity.value("timestamps").toObject().value("start").toDouble(), 1799996500.0);
    QVERIFY(activity.value("state").toString().contains(QStringLiteral("Загрузка")));
  }

  void pauseRetainsTimerAcrossRefreshes()
  {
    const qint64 now = 1800000000;
    qint64 anchor = 0;
    auto item = movie();
    item["RunTimeTicks"] = qint64(101400000000LL);
    auto publish = [&](qint64 position, bool playing, bool buffering, qint64 time) {
      anchor = DiscordActivity::startTimestamp(anchor, position, playing, time);
      return DiscordActivity::build(item, Playback, position, playing, buffering, time, anchor);
    };
    const auto watching = publish(2172000, true, false, now);
    const auto timestamps = watching.value("timestamps");
    QCOMPARE(publish(2172000, false, false, now).value("timestamps"), timestamps);
    // A long pause, periodic refreshes and buffering must not create a new timer.
    for (qint64 elapsed = 15; elapsed <= 300; elapsed += 15)
    {
      const auto paused = publish(2172000, false, false, now + elapsed);
      QCOMPARE(paused.value("timestamps"), timestamps);
      QVERIFY(paused.value("state").toString().endsWith(QStringLiteral("Пауза · 36:12")));
    }
    QCOMPARE(publish(2172000, false, true, now + 300).value("timestamps"), timestamps);
    auto resumed = publish(2172000, true, false, now + 300);
    QCOMPARE(resumed.value("timestamps").toObject().value("start").toDouble(), double(now + 300 - 2172));
    QCOMPARE(publish(2182000, false, false, now + 310).value("timestamps"), resumed.value("timestamps"));
    // An explicit seek while paused rebases once, then remains stable.
    anchor = 0;
    const auto seek = publish(600000, false, false, now + 320);
    QCOMPARE(seek.value("timestamps").toObject().value("start").toDouble(), double(now + 320 - 600));
    QCOMPARE(publish(600000, false, false, now + 335).value("timestamps"), seek.value("timestamps"));
    // A new media item gets its own position, even if it starts buffering.
    anchor = 0;
    QCOMPARE(publish(90000, false, true, now + 400).value("timestamps").toObject().value("start").toDouble(),
             double(now + 400 - 90));
  }

  void nativeProgressBarAndMinimalCard()
  {
    auto item = movie();
    item["Name"] = QStringLiteral("Интерстеллар");
    item["ProductionYear"] = 2014;
    item["RunTimeTicks"] = qint64(101400000000LL); // 2:49:00
    auto activity = DiscordActivity::build(item, Playback, 5058000, true, false, 1800000000);
    QCOMPARE(activity.value("details").toString(), QStringLiteral("Интерстеллар"));
    QCOMPARE(activity.value("state").toString(), QStringLiteral("Фильм · 2014"));
    const auto timestamps = activity.value("timestamps").toObject();
    const double start = 1800000000.0 - 5058;
    QCOMPARE(timestamps.value("start").toDouble(), start);
    QCOMPARE(timestamps.value("end").toDouble(), start + 10140);
    QVERIFY(!activity.contains("buttons"));
    QVERIFY(activity.value("assets").toObject().value("small_text").toString().endsWith(QStringLiteral("Просмотр")));
    // Stream duration is only a fallback; transcoding must not shorten the full runtime.
    activity = DiscordActivity::build(item, Playback, 5058000, true, false, 1800000000, 0, 100000);
    QCOMPARE(activity.value("timestamps").toObject(), timestamps);
    item.remove("RunTimeTicks");
    activity = DiscordActivity::build(item, Playback, 5058000, true, false, 1800000000, 0, 10140000);
    QCOMPARE(activity.value("timestamps").toObject(), timestamps);
    // Unknown or invalid duration keeps the elapsed timer, without a fictitious end.
    for (qint64 ticks : {qint64(0), qint64(-1), qint64(9999)})
    {
      item["RunTimeTicks"] = ticks;
      activity = DiscordActivity::build(item, Playback, 5058000, true, false, 1800000000);
      QVERIFY(!activity.value("timestamps").toObject().contains("end"));
    }
  }

  void seriesPosterAndSpecials()
  {
    QVariantMap episode{{"Type", "Episode"}, {"Name", "Pilot"}, {"SeriesName", "A Series"},
      {"SeriesId", "series1"}, {"SeriesPrimaryImageTag", "seriesTag"}, {"Id", "episode1"},
      {"ParentIndexNumber", 0}, {"IndexNumber", 1}, {"IndexNumberEnd", 2}};
    auto activity = DiscordActivity::build(episode, Playback, 0, true, false, 1800000000);
    QCOMPARE(activity.value("details").toString(), QStringLiteral("A Series"));
    QCOMPARE(activity.value("state").toString(), QStringLiteral("Сериал · S00 E01–02"));
    QVERIFY(activity.value("assets").toObject().value("large_image").toString().contains("/Items/series1/"));
    episode.remove("ParentIndexNumber");
    episode.remove("IndexNumber");
    activity = DiscordActivity::build(episode, Playback, 0, true, false, 1800000000);
    QCOMPARE(activity.value("state").toString(), QStringLiteral("Сериал"));
  }

  void missingMetadataAndPrivatePosters()
  {
    QVERIFY(DiscordActivity::build({}, Playback, 0, true, false, 1800000000).isEmpty());
    auto item = movie();
    item["Type"] = "Audio";
    QVERIFY(DiscordActivity::build(item, Playback, 0, true, false, 1800000000).isEmpty());
    for (const auto& url : {"http://media.example.org/Videos/id/a", "https://192.168.0.1/Videos/id/a",
                            "https://localhost/Videos/id/a", "https://server.local/Videos/id/a",
                            "https://[::1]/Videos/id/a", "https://media.example.org/not-a-video"})
      QVERIFY2(DiscordActivity::posterUrl(movie(), QUrl(url)).isEmpty(), url);
    item = movie();
    item.remove("ImageTags");
    auto activity = DiscordActivity::build(item, Playback, 0, true, false, 1800000000);
    QVERIFY(activity.value("assets").toObject().value("large_image").toString().endsWith("/icon.png"));
    item["Name"] = QString::fromUtf8("🎬").repeated(100);
    activity = DiscordActivity::build(item, Playback, 0, true, false, 1800000000);
    const auto title = activity.value("details").toString();
    QVERIFY(title.toUtf8().size() <= 128);
    QVERIFY(!title.contains(QChar::ReplacementCharacter));
  }

  void ipcHandshakeFragmentsPingUpdateAndClear()
  {
    const QString prefix = "jmp-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + "-";
    QLocalServer server;
    QVERIFY(server.listen(prefix + "0"));
    DiscordIpc ipc(nullptr, prefix);
    const auto watching = DiscordActivity::build(movie(), Playback, 10000, true, false, 1800000000);
    ipc.setActivity(watching);
    ipc.configure(true, AppId);
    QTRY_VERIFY(server.hasPendingConnections());
    QScopedPointer<QLocalSocket> peer(server.nextPendingConnection());
    QCOMPARE(readJson(peer.data()).value("client_id").toString(), AppId);
    const auto ready = jsonFrame({{"cmd", "DISPATCH"}, {"evt", "READY"}});
    peer->write(ready.left(5));
    peer->flush();
    QTest::qWait(30);
    QCOMPARE(peer->bytesAvailable(), 0LL);
    peer->write(ready.mid(5));
    peer->flush();
    const auto first = readJson(peer.data());
    QCOMPARE(first.value("cmd").toString(), QStringLiteral("SET_ACTIVITY"));
    QCOMPARE(first.value("args").toObject().value("activity").toObject(), watching);
    QVERIFY(first.value("args").toObject().value("pid").toDouble() > 0);
    acknowledge(peer.data(), first);
    QTest::qWait(30);
    ipc.setActivity(watching);
    QTest::qWait(30);
    QCOMPARE(peer->bytesAvailable(), 0LL);

    peer->write(frame(3, "ping"));
    peer->flush();
    QTRY_COMPARE(peer->bytesAvailable(), 12LL);
    QCOMPARE(peer->readAll(), frame(4, "ping"));

    const auto paused = DiscordActivity::build(movie(), Playback, 20000, false, false, 1800000010);
    ipc.setActivity(paused);
    const auto pauseRequest = readJson(peer.data());
    QCOMPARE(pauseRequest.value("args").toObject().value("activity").toObject(), paused);
    // A stop while an update is in flight must win as soon as the ACK arrives.
    ipc.setActivity({});
    acknowledge(peer.data(), pauseRequest);
    const auto clear = readJson(peer.data());
    QVERIFY(clear.value("args").toObject().value("activity").isNull());
    acknowledge(peer.data(), clear);
    ipc.configure(false, AppId);
    QTRY_COMPARE(peer->state(), QLocalSocket::UnconnectedState);
  }

  void reconnectSendsLatestState()
  {
    const QString prefix = "jmp-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + "-";
    QLocalServer firstServer, nextServer;
    QVERIFY(firstServer.listen(prefix + "0"));
    QVERIFY(nextServer.listen(prefix + "1"));
    DiscordIpc ipc(nullptr, prefix);
    ipc.configure(true, AppId);
    QTRY_VERIFY(firstServer.hasPendingConnections());
    QScopedPointer<QLocalSocket> first(firstServer.nextPendingConnection());
    QVERIFY(!readJson(first.data()).isEmpty());
    first->write(jsonFrame({{"cmd", "DISPATCH"}, {"evt", "READY"}}));
    first->flush();
    const auto initial = readJson(first.data());
    acknowledge(first.data(), initial);
    first->abort();
    const auto latest = DiscordActivity::build(movie(), Playback, 70000, false, false, 1800000100);
    ipc.setActivity(latest);
    QTRY_VERIFY_WITH_TIMEOUT(nextServer.hasPendingConnections(), 20000);
    QScopedPointer<QLocalSocket> second(nextServer.nextPendingConnection());
    QCOMPARE(readJson(second.data()).value("client_id").toString(), AppId);
    second->write(jsonFrame({{"cmd", "DISPATCH"}, {"evt", "READY"}}));
    second->flush();
    QCOMPARE(readJson(second.data()).value("args").toObject().value("activity").toObject(), latest);
    ipc.shutdown();
  }

  void malformedFrameAndInvalidId()
  {
    const QString prefix = "jmp-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + "-";
    QLocalServer server;
    QVERIFY(server.listen(prefix + "0"));
    DiscordIpc ipc(nullptr, prefix);
    ipc.configure(true, "not-an-id");
    QTest::qWait(50);
    QVERIFY(!server.hasPendingConnections());
    ipc.configure(true, AppId);
    QTRY_VERIFY(server.hasPendingConnections());
    QScopedPointer<QLocalSocket> peer(server.nextPendingConnection());
    QVERIFY(!readJson(peer.data()).isEmpty());
    QByteArray invalid(8, '\0');
    qToLittleEndian<quint32>(1, reinterpret_cast<uchar*>(invalid.data()));
    qToLittleEndian<quint32>(65537, reinterpret_cast<uchar*>(invalid.data() + 4));
    peer->write(invalid);
    peer->flush();
    QTRY_COMPARE(peer->state(), QLocalSocket::UnconnectedState);
    ipc.shutdown();
  }
};

QTEST_GUILESS_MAIN(DiscordPresenceTest)
#include "DiscordPresenceTest.moc"
