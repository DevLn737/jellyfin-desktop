#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <limits>
#include "diagnostics/DiagnosticsBuffer.h"
#include "utils/CachedRegexMatcher.h"
using namespace DesktopDiagnostics;

class DiagnosticsTest : public QObject {
  Q_OBJECT
private slots:
  void allowlistAndTypes() {
    Buffer buffer;
    QVERIFY(buffer.append(0, Event::SyncPlay, {{"event", 3}, {"driftMs", 12.5},
      {"url", "https://private/?api_key=secret"}, {"token", "secret"}, {"user", "secret"},
      {"positionMs", "secret"}, {"status", true}, {"playbackRate", std::numeric_limits<double>::infinity()}}));
    auto entries = buffer.snapshot(0);
    QCOMPARE(entries.size(), 1);
    auto object = QJsonDocument::fromJson(entries[0]).object();
    QCOMPARE(object.size(), 4); // atMs, kind, event, driftMs
    QCOMPARE(object.value("driftMs").toDouble(), 12.5);
    QVERIFY(!entries[0].contains("secret"));
    QVERIFY(!buffer.append(1, Event::Playback, {{"title", "private media"}}));
  }
  void boundedSize() {
    Buffer buffer(512);
    for (int i = 0; i < 1000; ++i) QVERIFY(buffer.append(i, Event::Command, {{"command", 3}, {"durationMs", 0.5}}));
    QVERIFY(buffer.bytes() <= 512);
    QVERIFY(buffer.dropped() > 0);
    auto entries = buffer.snapshot(1000);
    QVERIFY(entries.size() < 1000);
    QCOMPARE(QJsonDocument::fromJson(entries.last()).object().value("atMs").toInt(), 999);
  }
  void windowAndSession() {
    Buffer buffer;
    QVERIFY(buffer.append(0, Event::Gui, {{"delayMs", 0}}));
    QVERIFY(buffer.append(Buffer::WindowMs, Event::Gui, {{"delayMs", 5}}));
    QCOMPARE(buffer.snapshot(Buffer::WindowMs).size(), 2);
    QCOMPARE(buffer.snapshot(Buffer::WindowMs + 1).size(), 1);
    QVERIFY(!buffer.append(Buffer::SessionMs + 1, Event::Gui, {{"delayMs", 0}}));
    QVERIFY(!buffer.append(-1, Event::Gui, {{"delayMs", 0}}));
    buffer.clear();
    QCOMPARE(buffer.bytes(), 0);
    QVERIFY(buffer.snapshot(0).isEmpty());
    QCOMPARE(buffer.dropped(), quint64(0));
  }
  void regexMapping() {
    CachedRegexMatcher matcher(false);
    QVERIFY(matcher.addMatcher("^key:(.*)$", "%1"));
    QCOMPARE(matcher.match("key:play").first().toString(), "play");
    QVERIFY(matcher.match("no match").isEmpty());
    QVERIFY(matcher.addMatcher("^key:(.*)$", "pause"));
    // Replacement should invalidate previously cached matches as well.
    QCOMPARE(matcher.match("key:play").first().toString(), "pause");
  }
  void exportSnapshotSurvivesClearAndWrap() {
    Buffer buffer(512);
    for (int i = 0; i < 25; ++i) buffer.append(i, Event::Gui, {{"delayMs", i}});
    auto frozen = buffer.freeze(25);
    const auto expected = frozen.snapshot(25);
    buffer.clear();
    buffer.append(0, Event::Gui, {{"delayMs", 999}});
    QByteArray bytes;
    QBuffer output(&bytes);
    QVERIFY(output.open(QIODevice::WriteOnly));
    output.write("{\"records\":[");
    QVERIFY(frozen.writeRecords(output));
    output.write("]}");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QCOMPARE(document.object().value("records").toArray().size(), expected.size());
    QVERIFY(!bytes.contains("999"));
  }
};
QTEST_GUILESS_MAIN(DiagnosticsTest)
#include "DiagnosticsTest.moc"
