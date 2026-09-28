#include <AYAnimationEditor/AnimationAuthoring.h>
#include <AYAnimationEditor/AnimationTimeTransform.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace ayt::anim::editor {
namespace {
using namespace ayt::resource;
constexpr double kTimeEpsilon = 1e-6;

std::size_t width(AnimTrackType type) {
    switch (type) {
    case AnimTrackType::Float: return 1;
    case AnimTrackType::Vector3: return 3;
    case AnimTrackType::Quaternion: return 4;
    }
    return 0;
}

bool validSelection(const AuthoredAnimation& clip, const std::vector<AnimationKeyReference>& keys) {
    if (keys.empty() || !std::isfinite(clip.duration) || clip.duration < 0
        || !std::isfinite(clip.ticksPerSecond)) return false;
    std::set<std::tuple<bool, std::size_t, std::size_t>> unique;
    std::set<std::size_t> validatedTracks;
    bool validatedNotifies = false;
    const double rate = clip.ticksPerSecond > 0 ? clip.ticksPerSecond : 1;
    const auto finite = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float x) { return std::isfinite(x); });
    };
    for (const auto& key : keys) {
        if (!unique.emplace(key.notify, key.notify ? 0 : key.track, key.key).second) return false;
        if (key.notify) {
            if (key.key >= clip.notifies.size()) return false;
            if (!validatedNotifies) {
                for (const auto& notify : clip.notifies)
                    if (!std::isfinite(notify.time) || !std::isfinite(notify.payload)
                        || notify.time < 0 || notify.time > clip.duration) return false;
                validatedNotifies = true;
            }
            continue;
        }
        if (key.track >= clip.tracks.size()) return false;
        const auto& track = clip.tracks[key.track];
        const auto count = width(track.valueType) * track.times.size();
        if (!width(track.valueType) || key.key >= track.times.size() || track.values.size() != count
            || (!track.inTangents.empty() && track.inTangents.size() != count)
            || (!track.outTangents.empty() && track.outTangents.size() != count)) return false;
        if (validatedTracks.insert(key.track).second) {
            if (!finite(track.times) || !finite(track.values)
                || !finite(track.inTangents) || !finite(track.outTangents)) return false;
            for (std::size_t i = 0; i < track.times.size(); ++i)
                if (track.times[i] < 0 || track.times[i] / rate > clip.duration
                    || (i && (track.times[i] - track.times[i-1]) / rate < kTimeEpsilon)) return false;
        }
    }
    return true;
}

std::vector<std::size_t> reorderTrack(AnimTrack& track, double rate) {
    std::vector<std::size_t> order(track.times.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return track.times[a] < track.times[b]; });
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (!std::isfinite(track.times[order[i]])
            || (i && std::fabs((track.times[order[i]] - track.times[order[i-1]]) / rate) < kTimeEpsilon)) return {};
    }
    auto original = std::move(track);
    track = original;
    const auto count = width(track.valueType);
    for (std::size_t i = 0; i < order.size(); ++i) {
        track.times[i] = original.times[order[i]];
        for (auto* values : {&track.values, &track.inTangents, &track.outTangents}) {
            if (values->empty()) continue;
            const auto& source = values == &track.values ? original.values
                : values == &track.inTangents ? original.inTangents : original.outTangents;
            std::copy_n(source.begin() + order[i] * count, count, values->begin() + i * count);
        }
    }
    return order;
}

std::vector<std::size_t> reorderNotifies(std::vector<AnimNotifyMarker>& notifies) {
    std::vector<std::size_t> order(notifies.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return notifies[a].time < notifies[b].time; });
    auto original = notifies;
    for (std::size_t i = 0; i < order.size(); ++i) notifies[i] = original[order[i]];
    return order;
}

std::vector<std::size_t> inverseOrder(const std::vector<std::size_t>& order) {
    std::vector<std::size_t> inverse(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) inverse[order[i]] = i;
    return inverse;
}
} // namespace

AuthoredAnimation copyAnimationForAuthoring(const ayt::resource::IAnimation& source) {
    AuthoredAnimation result;
    result.name = source.getName() ? source.getName() : "";
    result.duration = source.getDuration();
    result.ticksPerSecond = source.getTicksPerSecond();
    if (const auto* concrete = dynamic_cast<const ayt::resource::Animation*>(&source)) result.guid = concrete->getGuid();
    for (uint32_t i = 0; i < source.getTrackCount(); ++i) {
        ayt::resource::AnimTrack track;
        track.nodeName = source.getTrackNodeName(i) ? source.getTrackNodeName(i) : "";
        track.property = source.getTrackProperty(i) ? source.getTrackProperty(i) : "";
        track.valueType = source.getTrackType(i);
        track.blendMode = source.getTrackBlendMode(i);
        track.interpolation = source.getTrackInterpolation(i);
        const auto count = source.getTrackKeyframeCount(i);
        if (const auto* times = source.getTrackTimes(i)) track.times.assign(times, times + count);
        const auto values = count * width(track.valueType);
        if (const auto* data = source.getTrackValues(i)) track.values.assign(data, data + values);
        if (const auto* data = source.getTrackInTangents(i)) track.inTangents.assign(data, data + values);
        if (const auto* data = source.getTrackOutTangents(i)) track.outTangents.assign(data, data + values);
        result.tracks.push_back(std::move(track));
    }
    for (uint32_t i = 0; i < source.getNotifyCount(); ++i)
        result.notifies.push_back({source.getNotifyName(i) ? source.getNotifyName(i) : "", source.getNotifyTime(i), source.getNotifyPayload(i)});
    return result;
}

std::shared_ptr<ayt::resource::Animation> buildAuthoredAnimation(const AuthoredAnimation& source) {
    auto result = std::make_shared<ayt::resource::Animation>();
    result->setName(source.name);
    result->setDuration(source.duration);
    result->setTicksPerSecond(source.ticksPerSecond);
    result->setGuid(source.guid);
    for (const auto& track : source.tracks) result->addTrack(track);
    for (const auto& notify : source.notifies) result->addNotify(notify);
    return result;
}

AnimationKeyEdit translateAnimationKeys(const ayt::resource::IAnimation& source,
    const std::vector<AnimationKeyReference>& keys, double deltaSeconds, std::size_t component, float deltaValue) {
    auto clip = copyAnimationForAuthoring(source);
    if (!validSelection(clip, keys) || !std::isfinite(deltaSeconds) || !std::isfinite(deltaValue))
        return {{}, {}, "Invalid animation key selection/translation."};
    const double rate = clip.ticksPerSecond > 0 ? clip.ticksPerSecond : 1;
    double first = clip.duration, last = 0;
    for (const auto& key : keys) {
        const double time = key.notify ? clip.notifies[key.key].time : clip.tracks[key.track].times[key.key] / rate;
        if (!std::isfinite(time) || time < 0 || time > clip.duration
            || (key.notify && deltaValue != 0)
            || (!key.notify && component >= width(clip.tracks[key.track].valueType)))
            return {{}, {}, "Incompatible animation component/time."};
        first = std::min(first, time);
        last = std::max(last, time);
    }
    const double delta = std::clamp(deltaSeconds, -first, clip.duration - last);
    if (delta == 0 && deltaValue == 0) return {{}, {}, "No animation change."};
    std::map<std::size_t, std::vector<std::size_t>> remaps;
    bool notifyChanged = false;
    for (const auto& key : keys) {
        if (key.notify) {
            clip.notifies[key.key].time += static_cast<float>(delta);
            notifyChanged = true;
            continue;
        }
        auto& track = clip.tracks[key.track];
        const auto count = width(track.valueType);
        track.times[key.key] = static_cast<float>((track.times[key.key] / rate + delta) * rate);
        if (deltaValue != 0) {
            auto* values = track.values.data() + key.key * count;
            values[component] += deltaValue;
            for (std::size_t i = 0; i < count; ++i) if (!std::isfinite(values[i])) return {{}, {}, "Non-finite key value."};
            if (track.valueType == ayt::resource::AnimTrackType::Quaternion) {
                double norm = 0;
                for (std::size_t i = 0; i < count; ++i) norm += static_cast<double>(values[i]) * values[i];
                if (norm <= 1e-12) return {{}, {}, "Degenerate quaternion."};
                for (std::size_t i = 0; i < count; ++i) values[i] = static_cast<float>(values[i] / std::sqrt(norm));
            }
        }
        remaps.try_emplace(key.track);
    }
    for (auto& [track, order] : remaps) {
        order = reorderTrack(clip.tracks[track], rate);
        if (order.empty()) return {{}, {}, "Key times overlap within a track."};
        order = inverseOrder(order);
    }
    const auto notifyOrder = notifyChanged ? inverseOrder(reorderNotifies(clip.notifies)) : std::vector<std::size_t>{};
    auto updated = keys;
    for (auto& key : updated) {
        const auto& order = key.notify ? notifyOrder : remaps.at(key.track);
        key.key = order[key.key];
    }
    return {buildAuthoredAnimation(clip), std::move(updated), {}};
}

AnimationKeyEdit removeAnimationKeys(const ayt::resource::IAnimation& source,
    const std::vector<AnimationKeyReference>& keys) {
    auto clip = copyAnimationForAuthoring(source);
    if (!validSelection(clip, keys)) return {{}, {}, "Invalid animation key selection."};
    auto selected = keys;
    for (auto& key : selected) if (key.notify) key.track = 0;
    std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
        return std::tie(a.notify, a.track, a.key) > std::tie(b.notify, b.track, b.key);
    });
    for (const auto& key : selected) {
        if (key.notify) { clip.notifies.erase(clip.notifies.begin() + key.key); continue; }
        auto& track = clip.tracks[key.track];
        const auto count = width(track.valueType);
        track.times.erase(track.times.begin() + key.key);
        for (auto* values : {&track.values, &track.inTangents, &track.outTangents})
            if (!values->empty()) values->erase(values->begin() + key.key * count, values->begin() + (key.key + 1) * count);
    }
    return {buildAuthoredAnimation(clip), {}, {}};
}
AnimationKeyEdit retimeAnimationKeys(const ayt::resource::IAnimation& source,
    const std::vector<AnimationKeyReference>& keys, double anchorSeconds, double scale) {
    auto clip = copyAnimationForAuthoring(source);
    if (!validSelection(clip, keys) || !std::isfinite(anchorSeconds)
        || !std::isfinite(scale) || scale == 0 || scale == 1)
        return {{}, {}, "Invalid/no-op animation time transform."};
    const double rate = clip.ticksPerSecond > 0 ? clip.ticksPerSecond : 1;
    std::map<std::size_t, std::vector<std::size_t>> remaps;
    bool notifyChanged = false;
    for (const auto& key : keys) {
        const double oldTime = key.notify ? clip.notifies[key.key].time
            : clip.tracks[key.track].times[key.key] / rate;
        const double time = anchorSeconds + (oldTime - anchorSeconds) * scale;
        if (!std::isfinite(oldTime) || oldTime < 0 || oldTime > clip.duration
            || !std::isfinite(time) || time < 0 || time > clip.duration)
            return {{}, {}, "Time transform leaves the clip duration."};
        if (key.notify) {
            clip.notifies[key.key].time = static_cast<float>(time);
            notifyChanged = true;
            continue;
        }
        auto& track = clip.tracks[key.track];
        if (scale < 0 && track.interpolation == AnimInterpolation::Step)
            return {{}, {}, "Step reversal requires right-hold, which this format cannot represent."};
        track.times[key.key] = static_cast<float>(time * rate);
        const auto count = width(track.valueType);
        if (!track.inTangents.empty() || !track.outTangents.empty()) {
            if (track.inTangents.empty()) track.inTangents.resize(track.values.size(), 0);
            if (track.outTangents.empty()) track.outTangents.resize(track.values.size(), 0);
            for (std::size_t c = 0; c < count; ++c) {
                const auto i = key.key * count + c;
                const auto incoming = track.inTangents[i], outgoing = track.outTangents[i];
                track.inTangents[i] = static_cast<float>((scale < 0 ? outgoing : incoming) / scale);
                track.outTangents[i] = static_cast<float>((scale < 0 ? incoming : outgoing) / scale);
                if (!std::isfinite(track.inTangents[i]) || !std::isfinite(track.outTangents[i]))
                    return {{}, {}, "Time transform overflows tangent slopes."};
            }
        }
        remaps.try_emplace(key.track);
    }
    for (auto& [track, order] : remaps) {
        order = reorderTrack(clip.tracks[track], rate);
        if (order.empty()) return {{}, {}, "Time transform overlaps keys."};
        order = inverseOrder(order);
    }
    const auto notifyOrder = notifyChanged ? inverseOrder(reorderNotifies(clip.notifies)) : std::vector<std::size_t>{};
    auto updated = keys;
    for (auto& key : updated) {
        const auto& order = key.notify ? notifyOrder : remaps.at(key.track);
        key.key = order[key.key];
    }
    return {buildAuthoredAnimation(clip), std::move(updated), {}};
}
} // namespace ayt::anim::editor
