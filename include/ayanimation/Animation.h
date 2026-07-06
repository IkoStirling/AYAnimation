#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ayt::anim
{

// ===== TrackType — channel value semantics =====
enum class TrackType : uint8_t {
    Vector3,     // 3 floats per key (position / scale)
    Quaternion,  // 4 floats per key (rotation, w last)
    Float,       // 1 float per key
};

// ===== KeyframeTrack — one channel over time =====
//
// `times` are already normalized to seconds (loader PR will convert ticks → seconds).
// `values` is flat: for Vector3 the layout is [x0,y0,z0, x1,y1,z1, ...]; for
// Quaternion [x0,y0,z0,w0, x1,y1,z1,w1, ...]; for Float [v0, v1, ...].
// `property` matches AYResource's AnimTrack strings ("position" / "rotation" / "scale").
struct KeyframeTrack {
    std::string        nodeName;
    std::string        property;
    TrackType          type      = TrackType::Vector3;
    std::vector<float> times;
    std::vector<float> values;
};

// ===== Animation — single clip =====
class Animation {
public:
    void setName(const std::string& n)  { _name = n; }
    void setDuration(float s)           { _duration = s; }
    void setTicksPerSecond(float t)     { _tps = t; }
    void addTrack(KeyframeTrack t);

    const std::string&   getName() const           { return _name; }
    float                getDuration() const       { return _duration; }
    float                getTicksPerSecond() const { return _tps; }
    size_t               getTrackCount() const     { return _tracks.size(); }
    const KeyframeTrack& getTrack(size_t i) const  { return _tracks[i]; }

private:
    std::string                _name;
    float                      _duration = 0.0f;
    float                      _tps      = 30.0f;
    std::vector<KeyframeTrack> _tracks;
};

} // namespace ayt::anim