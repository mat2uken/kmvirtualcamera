#pragma once
#include "../receiver/native_media.h"
#include <cstdint>
#include <string>

namespace km::host {
// Stage 6 (W6-4/W6-6): the HOST draws the known moving test video (7-segment frame
// counter plus a triangle-wave rectangle, stage-5 style) that is pushed through the
// sink. The extension no longer generates video itself; it relays this and shows
// standby black when the producer is gone. A fresh buffer is created per frame
// because enqueued frames stay alive in the sink queue until the extension consumes
// them - reusing one buffer would corrupt frames already queued.
km::mac::PixelBuffer MakeGeneratedFrame(uint64_t frameIndex, std::string& error);
} // namespace km::host
