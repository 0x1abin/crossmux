#pragma once
#include <WeReadClient.h>
class GfxRenderer;
namespace weread_auto {
// Queue only after the local reader position has been saved successfully.
void queue(const char* bookId, WeReadClient::ProgressSyncInput input);
// Called after outgoing readers are destroyed and outside RenderLock.
void drain(GfxRenderer& renderer);
}  // namespace weread_auto
