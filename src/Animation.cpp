#include <ayanimation/Animation.h>

namespace ayt::anim
{

void Animation::addTrack(KeyframeTrack t)
{
    _tracks.push_back(std::move(t));
}

} // namespace ayt::anim