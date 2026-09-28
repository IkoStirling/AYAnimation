#include <AYAnimationEditor/AnimationClipboard.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace ayt::anim::editor {
namespace {
using namespace ayt::resource;
std::size_t width(AnimTrackType type) {
    switch (type) {
    case AnimTrackType::Float: return 1;
    case AnimTrackType::Vector3: return 3;
    case AnimTrackType::Quaternion: return 4;
    }
    return 0;
}
bool finite(const std::vector<float>& values) {
    return std::all_of(values.begin(), values.end(), [](float x) { return std::isfinite(x); });
}
bool validTrack(const AnimTrack& track, double rate) {
    const auto size = width(track.valueType) * track.times.size();
    if (!width(track.valueType) || track.values.size() != size || !finite(track.times)
        || !finite(track.values) || (!track.inTangents.empty() && track.inTangents.size() != size)
        || (!track.outTangents.empty() && track.outTangents.size() != size)
        || !finite(track.inTangents) || !finite(track.outTangents)) return false;
    for (std::size_t i = 1; i < track.times.size(); ++i)
        if ((track.times[i] - track.times[i-1]) / rate < 1e-6) return false;
    return true;
}
}
std::optional<AnimationClipboard> copyAnimationKeys(const IAnimation& source,
    const std::vector<AnimationKeyReference>& keys, std::string* error) {
    const auto fail = [&]() -> std::optional<AnimationClipboard> {
        if (error) *error = "Invalid clipboard key selection or source data.";
        return {};
    };
    const auto clip = copyAnimationForAuthoring(source);
    if (keys.empty() || !std::isfinite(clip.duration) || clip.duration < 0
        || !std::isfinite(clip.ticksPerSecond)) return fail();
    const double rate = clip.ticksPerSecond > 0 ? clip.ticksPerSecond : 1;
    double first = clip.duration, last = 0;
    std::set<std::tuple<bool, std::size_t, std::size_t>> unique;
    for (const auto& key : keys) {
        if (!unique.emplace(key.notify, key.notify ? 0 : key.track, key.key).second) return fail();
        if (key.notify ? key.key >= clip.notifies.size()
            : key.track >= clip.tracks.size()) return fail();
        if (!key.notify && (key.key >= clip.tracks[key.track].times.size()
            || !validTrack(clip.tracks[key.track], rate))) return fail();
        const double time = key.notify ? clip.notifies[key.key].time : clip.tracks[key.track].times[key.key] / rate;
        if (!std::isfinite(time) || time < 0 || time > clip.duration) return fail();
        if (key.notify && (clip.notifies[key.key].name.empty() || !std::isfinite(clip.notifies[key.key].payload))) return fail();
        first = std::min(first, time); last = std::max(last, time);
    }
    AnimationClipboard result;
    result.spanSeconds = last - first;
    std::vector<std::size_t> trackIds;
    for (const auto& key : keys) {
        if (key.notify) {
            const auto& notify = clip.notifies[key.key];
            result.notifies.push_back({notify.name, notify.time - first, notify.payload});
            continue;
        }
        const auto& track = clip.tracks[key.track];
        auto found = std::find(trackIds.begin(), trackIds.end(), key.track);
        if (found == trackIds.end()) {
            trackIds.push_back(key.track);
            result.tracks.push_back({track.nodeName, track.property, track.valueType,
                track.blendMode, track.interpolation, {}});
            found = trackIds.end() - 1;
        }
        AnimationClipboardKey item;
        item.offsetSeconds = track.times[key.key] / rate - first;
        const auto count = width(track.valueType);
        for (auto pair : {std::pair{&item.values, &track.values},
                          std::pair{&item.inTangents, &track.inTangents},
                          std::pair{&item.outTangents, &track.outTangents}})
            if (!pair.second->empty()) pair.first->assign(
                pair.second->begin() + key.key * count,
                pair.second->begin() + (key.key + 1) * count);
        result.tracks[static_cast<std::size_t>(found - trackIds.begin())].keys.push_back(std::move(item));
    }
    if (error) error->clear();
    return result;
}

AnimationKeyEdit pasteAnimationKeys(const IAnimation& target,
    const AnimationClipboard& clipboard, double startSeconds) {
    auto clip = copyAnimationForAuthoring(target);
    const auto fail = [](const char* error) { return AnimationKeyEdit{{}, {}, error}; };
    if (clipboard.empty() || !std::isfinite(startSeconds) || !std::isfinite(clip.duration)
        || clip.duration < 0 || !std::isfinite(clip.ticksPerSecond))
        return fail("Invalid clipboard or target duration.");
    const double rate = clip.ticksPerSecond > 0 ? clip.ticksPerSecond : 1;
    const auto timeFor = [&](double offset) {
        return std::isfinite(offset) && offset >= 0 && startSeconds + offset >= 0
            && startSeconds + offset <= clip.duration;
    };
    std::vector<AnimationKeyReference> selected;
    for (const auto& group : clipboard.tracks) {
        std::size_t match = clip.tracks.size();
        for (std::size_t i = 0; i < clip.tracks.size(); ++i) {
            const auto& track = clip.tracks[i];
            if (track.nodeName != group.nodeName || track.property != group.property) continue;
            if (match != clip.tracks.size()) return fail("Ambiguous target track.");
            if (track.valueType != group.valueType || track.blendMode != group.blendMode
                || track.interpolation != group.interpolation) return fail("Incompatible target track.");
            match = i;
        }
        if (match == clip.tracks.size() || group.keys.empty()) return fail("Missing target track.");
        auto& track = clip.tracks[match];
        if (!validTrack(track, rate)) return fail("Invalid target track.");
        const auto count = width(track.valueType);
        for (const auto& key : group.keys) {
            if (!timeFor(key.offsetSeconds) || key.values.size() != count || !finite(key.values)
                || (!key.inTangents.empty() && key.inTangents.size() != count)
                || (!key.outTangents.empty() && key.outTangents.size() != count)
                || !finite(key.inTangents) || !finite(key.outTangents))
                return fail("Invalid clipboard key.");
            const float tick = static_cast<float>((startSeconds + key.offsetSeconds) * rate);
            if (track.valueType == AnimTrackType::Quaternion) {
                double norm = 0;
                for (float value : key.values) norm += static_cast<double>(value) * value;
                if (norm <= 1e-12) return fail("Degenerate quaternion clipboard key.");
            }
            if (!std::isfinite(tick)) return fail("Key time overflow.");
            if (std::any_of(track.times.begin(), track.times.end(), [&](float t) {
                return std::fabs((t - tick) / rate) < 1e-6;
            })) return fail("Paste would overwrite a key.");
            const auto index = static_cast<std::size_t>(std::lower_bound(
                track.times.begin(), track.times.end(), tick) - track.times.begin());
            const auto oldCount = track.times.size();
            track.times.insert(track.times.begin() + index, tick);
            track.values.insert(track.values.begin() + index * count, key.values.begin(), key.values.end());
            for (auto pair : {std::pair{&track.inTangents, &key.inTangents},
                              std::pair{&track.outTangents, &key.outTangents}}) {
                if (pair.first->empty() && pair.second->empty()) continue;
                if (pair.first->empty()) pair.first->resize(oldCount * count, 0);
                const auto values = pair.second->empty() ? std::vector<float>(count, 0) : *pair.second;
                pair.first->insert(pair.first->begin() + index * count, values.begin(), values.end());
            }
            for (auto& ref : selected)
                if (!ref.notify && ref.track == match && ref.key >= index) ++ref.key;
            selected.push_back({match, index, false});
        }
    }
    for (const auto& notify : clipboard.notifies) {
        if (!timeFor(notify.offsetSeconds) || notify.name.empty() || !std::isfinite(notify.payload))
            return fail("Invalid clipboard Notify.");
        const auto time = static_cast<float>(startSeconds + notify.offsetSeconds);
        if (std::any_of(clip.notifies.begin(), clip.notifies.end(), [&](const auto& n) {
            return n.name == notify.name && std::fabs(n.time - time) < 1e-6;
        })) return fail("Paste would duplicate an identical Notify.");
        const auto index = static_cast<std::size_t>(std::upper_bound(clip.notifies.begin(), clip.notifies.end(),
            time, [](float t, const auto& n) { return t < n.time; }) - clip.notifies.begin());
        clip.notifies.insert(clip.notifies.begin() + index, {notify.name, time, notify.payload});
        for (auto& ref : selected) if (ref.notify && ref.key >= index) ++ref.key;
        selected.push_back({0, index, true});
    }
    return {buildAuthoredAnimation(clip), std::move(selected), {}};
}
}
