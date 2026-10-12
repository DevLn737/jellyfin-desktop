#include "PlayerQuickItem.h"
#include "PlayerComponent.h"

// Keep the application component adapter separate from native rendering. The
// renderer never polls client events or reaches back into a component singleton.
void PlayerQuickItem::initMpv(PlayerComponent* player)
{
  initMpvHandle(player->getMpvHandle());
  setVideoRectangle(player->videoRectangle());
  connect(player, &PlayerComponent::windowVisible, this, &PlayerQuickItem::setVideoVisible);
  connect(player, &PlayerComponent::videoPlaybackActive, this, &PlayerQuickItem::setPlaybackActive);
  connect(player, &PlayerComponent::onVideoRecangleChanged, this, [this, player] {
    setVideoRectangle(player->videoRectangle());
  });
}
