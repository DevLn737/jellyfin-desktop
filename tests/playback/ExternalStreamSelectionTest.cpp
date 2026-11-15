#include <QCoreApplication>
#include <QDebug>
#include <cstdio>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include "ExternalAudioRequest.h"
struct AsyncCommand { std::uint64_t id; QStringList args; };
struct FakeMpv {
  QVariantList tracks;
  QVariantMap properties;
  std::vector<AsyncCommand> additions;
  std::vector<std::uint64_t> cancellations;
  QStringList removals;
};
namespace mpv { namespace qt {
class node_builder {
public:
  explicit node_builder(const QStringList& values) : args(values) {}
  const QStringList* node() const { return &args; }
private:
  QStringList args;
};
QVariant get_property(FakeMpv* mpv, const QString& key) {
  return key == "track-list" ? QVariant(mpv->tracks) : mpv->properties.value(key);
}
int set_property(FakeMpv* mpv, const QString& key, const QVariant& value) {
  mpv->properties[key] = value; return 0;
}
QVariant command(FakeMpv* mpv, const QStringList& args) {
  if (args[0] != "sub-remove" && args[0] != "audio-remove")
    throw std::runtime_error("External additions must never use the blocking command API");
  mpv->removals.push_back(args[1]);
  for (auto i = mpv->tracks.begin(); i != mpv->tracks.end(); )
    if (i->toMap().value("id").toString() == args[1]) i = mpv->tracks.erase(i);
    else ++i;
  return 0;
}
int get_error(const QVariant& result) { return result.toInt(); }
}}
static void mpv_abort_async_command(FakeMpv* mpv, std::uint64_t id) { mpv->cancellations.push_back(id); }
static int mpv_command_node_async(FakeMpv* mpv, std::uint64_t id, const QStringList* args) {
  mpv->additions.push_back({id, *args}); return 0;
}
static const char* mpv_error_string(int) { return "test failure"; }
class PlayerComponent {
public:
  enum class MediaType { Audio, Subtitle };
  FakeMpv* m_mpv;
  QString m_pendingExternalAudioStream, m_pendingExternalSubtitleStream;
  ExternalAudioRequest m_externalAudioRequest;
  ExternalStreamRequest m_externalSubtitleRequest {ExternalStreamRequest::SubtitleNamespace};
  explicit PlayerComponent(FakeMpv* mpv) : m_mpv(mpv) {}
  QVariantList findStreamsForURL(const QString& url);
  void reselectStream(const QString& streamSelection, MediaType target);
};
#include "StreamSelectionMethods.inc"
static void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static QVariantMap subtitle(int id, const QString& url) {
  return {{"id", id}, {"type", "sub"}, {"external", true}, {"external-filename", url}};
}
int main(int argc, char** argv) {
 QCoreApplication app(argc, argv);
 try {
  FakeMpv mpv;
  PlayerComponent player(&mpv);
  using Type = PlayerComponent::MediaType;
  player.reselectStream("#,https://fixture.invalid/one.srt", Type::Subtitle);
  require(mpv.additions.size() == 1 && mpv.additions[0].args[0] == "sub-add", "Submit subtitles asynchronously");
  const auto oldId = mpv.additions[0].id;
  player.reselectStream("#,https://fixture.invalid/one.srt", Type::Subtitle);
  require(mpv.additions.size() == 1, "Deduplicate an in-flight external subtitle");
  player.reselectStream("#,https://fixture.invalid/audio.mka", Type::Audio);
  const auto audioId = mpv.additions.back().id;
  require(audioId != oldId, "Audio and subtitles cannot share reply IDs");
  player.reselectStream("#,https://fixture.invalid/two.srt", Type::Subtitle);
  const auto latestId = mpv.additions.back().id;
  require(mpv.cancellations.size() == 1 && mpv.cancellations[0] == oldId, "Cancel only the superseded subtitle request");
  require(!player.m_externalSubtitleRequest.complete(oldId), "Ignore the old subtitle completion");
  mpv.tracks.push_back(subtitle(7, "https://fixture.invalid/one.srt"));
  player.reselectStream("#,https://fixture.invalid/two.srt", Type::Subtitle);
  require(mpv.removals.contains("7") && mpv.additions.size() == 3, "Remove a late stale demuxer without duplicating the latest load");
  mpv.tracks.push_back(subtitle(8, "https://fixture.invalid/two.srt"));
  player.reselectStream("#,https://fixture.invalid/two.srt", Type::Subtitle);
  require(mpv.properties.value("sid").toString() == "8", "Map the desired URL to its native subtitle track ID");
  player.reselectStream("", Type::Subtitle);
  require(mpv.properties.value("sid").toString() == "no" && mpv.tracks.isEmpty(), "Disable and remove external subtitles");
  require(player.m_externalAudioRequest.pending() == audioId, "Independent audio loading must survive subtitle changes");
  require(!player.m_externalSubtitleRequest.complete(latestId), "Disabled selection cannot be restored by a late reply");
  mpv.tracks.push_back(subtitle(9, "https://fixture.invalid/two.srt"));
  player.reselectStream("", Type::Subtitle);
  require(mpv.tracks.isEmpty() && mpv.properties.value("sid").toString() == "no", "Late completed tracks cannot re-enable disabled subtitles");
  return 0;
 } catch(const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
