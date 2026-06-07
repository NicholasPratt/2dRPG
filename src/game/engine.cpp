#include "game/engine.hpp"

#include "game/constants.hpp"
#include "game/project.hpp"

#include "stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

#include <SDL.h>
#include <GLFW/glfw3.h>
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <system_error>

namespace adventure::game {
namespace {

constexpr float kFixedStepSeconds = 1.0f / 60.0f;
constexpr float kPlayerSpeedPxPerSecond = 96.0f;
constexpr float kPlayerCollisionSizePx = 14.0f;
constexpr float kPlayerFallbackDrawSizePx = 32.0f;
constexpr float kScreenTransitionExitRatio = 0.30f;
constexpr float kMinHazardDamageInterval = 0.1f;
constexpr float kPlayerDamageInvulnerableSeconds = 0.65f;
constexpr float kMeleeActiveSeconds = 0.15f;
constexpr float kMeleeAttackSeconds = 0.28f;
constexpr float kRangedAttackSeconds = 0.22f;
constexpr float kAttackMoveSpeedScale = 0.55f;
// Melee swing timing: windup → active hit window → recovery (sums within kMeleeAttackSeconds).
constexpr float kMeleeWindupSeconds = 0.06f;
constexpr float kMeleeActiveWindowSeconds = 0.12f;
// Hit-reaction feel.
constexpr float kEnemyKnockbackBasePxPerSecond = 220.0f;  // scaled by (1 - knockbackResistance)
constexpr float kEnemyKnockbackDecayPerSecond = 6.0f;     // exponential decay rate
constexpr float kEnemyHitFlashSeconds = 0.12f;
constexpr float kPlayerHitFlashSeconds = 0.16f;
constexpr float kPlayerKnockbackPxPerSecond = 160.0f;
constexpr float kPlayerKnockbackDecayPerSecond = 8.0f;
constexpr float kItemPickupRadius = 12.0f;
constexpr float kProjectileHalfSize = 4.0f;
// Rebounding projectiles (e.g. slingshot stone): bounce, lose energy, then settle on the ground.
constexpr float kProjectileReboundRestitution = 0.55f;  // velocity retained per bounce
constexpr int kProjectileMaxBounces = 2;
constexpr float kProjectileMinReboundSpeed = 36.0f;     // below this, the projectile settles
constexpr float kProjectileSettleSeconds = 0.6f;        // grounded rest before despawn
constexpr float kEnemyDeathVisualSeconds = 0.35f;
constexpr float kSpeechTextScale = 1.5f;
constexpr float kDialogueTextScale = 1.8f;
constexpr int kDialogueVisibleLines = 4;
constexpr float kGamepadDeadZone = 0.35f;

std::size_t dialogueWrapChars(float screenWidth)
{
    constexpr float margin = 10.0f;
    constexpr float padX = 10.0f;
    return std::max<std::size_t>(24, static_cast<std::size_t>((screenWidth - margin * 2.0f - padX * 2.0f) / (6.0f * kDialogueTextScale)));
}

void setError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

std::filesystem::path assetPath(const std::filesystem::path& root, const std::filesystem::path& relative)
{
    return root / relative;
}

std::string currencyStateId(const ItemDef& def)
{
    if (def.targetId.empty()) {
        return "Money";
    }
    const bool numericTarget = std::all_of(def.targetId.begin(), def.targetId.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
    return numericTarget ? "Money" : def.targetId;
}

PathWaypoint catmullPoint(const EnemyPath& path, int segment, float t)
{
    const int count = static_cast<int>(path.waypoints.size());
    const auto at = [&path, count](int index) -> const PathWaypoint& {
        if (path.loop) {
            index %= count;
            if (index < 0) {
                index += count;
            }
            return path.waypoints[static_cast<std::size_t>(index)];
        }
        return path.waypoints[static_cast<std::size_t>(std::clamp(index, 0, count - 1))];
    };
    const PathWaypoint& p0 = at(segment - 1);
    const PathWaypoint& p1 = at(segment);
    const PathWaypoint& p2 = at(segment + 1);
    const PathWaypoint& p3 = at(segment + 2);
    const float t2 = t * t;
    const float t3 = t2 * t;
    return {
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
    };
}

float distance(PathWaypoint a, PathWaypoint b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

float approximatePathLength(const EnemyPath& path)
{
    if (path.waypoints.size() < 2) {
        return 0.0f;
    }
    float total = 0.0f;
    if (path.curveMode == PathCurveMode::Spline && path.waypoints.size() >= 3) {
        const int segments = path.loop ? static_cast<int>(path.waypoints.size()) : static_cast<int>(path.waypoints.size()) - 1;
        for (int s = 0; s < segments; ++s) {
            PathWaypoint prev = catmullPoint(path, s, 0.0f);
            for (int i = 1; i <= 12; ++i) {
                PathWaypoint next = catmullPoint(path, s, static_cast<float>(i) / 12.0f);
                total += distance(prev, next);
                prev = next;
            }
        }
        return total;
    }
    for (std::size_t i = 1; i < path.waypoints.size(); ++i) {
        total += distance(path.waypoints[i - 1], path.waypoints[i]);
    }
    if (path.loop) {
        total += distance(path.waypoints.back(), path.waypoints.front());
    }
    return total;
}

float pathDistanceToWaypoint(const EnemyPath& path, std::size_t waypointIndex)
{
    if (path.waypoints.empty() || waypointIndex == 0) {
        return 0.0f;
    }
    const std::size_t clampedIndex = std::min(waypointIndex, path.waypoints.size() - 1);
    float total = 0.0f;
    if (path.curveMode == PathCurveMode::Spline && path.waypoints.size() >= 3) {
        for (std::size_t segment = 0; segment < clampedIndex; ++segment) {
            PathWaypoint previous = catmullPoint(path, static_cast<int>(segment), 0.0f);
            for (int sample = 1; sample <= 12; ++sample) {
                const PathWaypoint next = catmullPoint(
                    path, static_cast<int>(segment), static_cast<float>(sample) / 12.0f);
                total += distance(previous, next);
                previous = next;
            }
        }
        return total;
    }
    for (std::size_t i = 1; i <= clampedIndex; ++i) {
        total += distance(path.waypoints[i - 1], path.waypoints[i]);
    }
    return total;
}

bool pathStartsHidden(const std::vector<PathWaypoint>& waypoints)
{
    return std::any_of(waypoints.begin(), waypoints.end(), [](const PathWaypoint& waypoint) {
        return waypoint.action == PathWaypointAction::Enter;
    });
}

std::string characterSpriteId(const std::filesystem::path& projectRoot, const std::string& characterId)
{
    if (characterId.empty()) {
        return {};
    }

    std::ifstream input(projectRoot / "assets/game/characters" / (characterId + ".adcharacter"));
    if (!input) {
        return {};
    }

    std::string key;
    input >> key;
    if (key != "ADCHARACTER") {
        return {};
    }
    int version = 0;
    input >> version;

    std::filesystem::path spriteReference;
    while (input >> key) {
        if (key == "sprite") {
            std::string value;
            input >> std::quoted(value);
            spriteReference = value;
        } else if (key == "end") {
            break;
        } else if (key == "name" || key == "bio") {
            std::string ignored;
            input >> std::quoted(ignored);
        } else if (key == "anim") {
            std::string ignoredA;
            std::string ignoredB;
            input >> std::quoted(ignoredA) >> std::quoted(ignoredB);
        } else if (key == "frame") {
            int ignoredIndex = 0;
            std::string ignoredState;
            std::string ignoredImage;
            input >> ignoredIndex >> std::quoted(ignoredState) >> std::quoted(ignoredImage);
        } else if (key == "playable" || key == "animations" || key == "frames") {
            std::size_t ignored = 0;
            input >> ignored;
        }
    }

    if (spriteReference.empty()) {
        return {};
    }

    SpriteMetadata metadata;
    const std::filesystem::path metadataPath = spriteReference.is_absolute() ? spriteReference : projectRoot / spriteReference;
    if (!loadSpriteMetadata(metadataPath, metadata, nullptr)) {
        return {};
    }
    return metadata.id;
}

std::array<std::uint8_t, 7> glyphRows(char c)
{
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(c)))) {
        case 'A': return {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
        case 'B': return {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
        case 'C': return {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
        case 'D': return {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
        case 'E': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
        case 'F': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
        case 'G': return {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f};
        case 'H': return {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
        case 'I': return {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e};
        case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c};
        case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
        case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
        case 'M': return {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
        case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
        case 'O': return {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
        case 'P': return {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
        case 'Q': return {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d};
        case 'R': return {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
        case 'S': return {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
        case 'T': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
        case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
        case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a};
        case 'X': return {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
        case 'Y': return {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
        case 'Z': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
        case '0': return {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
        case '1': return {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
        case '2': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
        case '3': return {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
        case '4': return {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
        case '5': return {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
        case '6': return {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e};
        case '7': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        case '8': return {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
        case '9': return {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e};
        case '.': return {0, 0, 0, 0, 0, 0x0c, 0x0c};
        case ',': return {0, 0, 0, 0, 0, 0x0c, 0x08};
        case '!': return {0x04, 0x04, 0x04, 0x04, 0x04, 0, 0x04};
        case '?': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0, 0x04};
        case ':': return {0, 0x0c, 0x0c, 0, 0x0c, 0x0c, 0};
        case '-': return {0, 0, 0, 0x1f, 0, 0, 0};
        case '\'': return {0x04, 0x04, 0x08, 0, 0, 0, 0};
        default: return {0, 0, 0, 0, 0, 0, 0};
    }
}

std::vector<std::string> wrapText(const std::string& text, std::size_t maxCharsPerLine, std::size_t maxLines)
{
    std::vector<std::string> lines;
    std::string current;
    std::string word;
    const auto canAddLine = [&]() {
        return maxLines == 0 || lines.size() < maxLines;
    };
    const auto pushCurrent = [&]() {
        if (!current.empty() && canAddLine()) {
            lines.push_back(current);
            current.clear();
        }
    };
    const auto flushWord = [&]() {
        if (word.empty() || !canAddLine()) {
            word.clear();
            return;
        }
        while (!word.empty() && canAddLine()) {
            const std::size_t available = current.empty()
                ? maxCharsPerLine
                : (maxCharsPerLine > current.size() + 1 ? maxCharsPerLine - current.size() - 1 : 0);
            if (word.size() <= available) {
                current += current.empty() ? word : " " + word;
                word.clear();
            } else if (current.empty()) {
                current = word.substr(0, maxCharsPerLine);
                word.erase(0, std::min(maxCharsPerLine, word.size()));
                pushCurrent();
            } else {
                pushCurrent();
            }
        }
        word.clear();
    };

    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            flushWord();
        } else {
            word.push_back(c);
        }
    }
    flushWord();
    if (!current.empty() && canAddLine()) {
        lines.push_back(current);
    }
    return lines;
}

PathWaypoint pointAtDistance(const EnemyPath& path, float targetDistance)
{
    if (path.waypoints.empty()) {
        return {};
    }
    if (path.waypoints.size() == 1) {
        return path.waypoints.front();
    }
    const float totalLength = approximatePathLength(path);
    if (totalLength <= 0.0f) {
        return path.waypoints.front();
    }
    if (path.loop) {
        targetDistance = std::fmod(targetDistance, totalLength);
        if (targetDistance < 0.0f) {
            targetDistance += totalLength;
        }
    } else {
        targetDistance = std::clamp(targetDistance, 0.0f, totalLength);
    }

    float walked = 0.0f;
    if (path.curveMode == PathCurveMode::Spline && path.waypoints.size() >= 3) {
        const int segments = path.loop ? static_cast<int>(path.waypoints.size()) : static_cast<int>(path.waypoints.size()) - 1;
        for (int s = 0; s < segments; ++s) {
            PathWaypoint prev = catmullPoint(path, s, 0.0f);
            for (int i = 1; i <= 12; ++i) {
                PathWaypoint next = catmullPoint(path, s, static_cast<float>(i) / 12.0f);
                const float segLen = distance(prev, next);
                if (walked + segLen >= targetDistance) {
                    const float t = segLen > 0.0f ? (targetDistance - walked) / segLen : 0.0f;
                    return {prev.x + (next.x - prev.x) * t, prev.y + (next.y - prev.y) * t};
                }
                walked += segLen;
                prev = next;
            }
        }
        return path.loop ? path.waypoints.front() : path.waypoints.back();
    }

    const int segments = path.loop ? static_cast<int>(path.waypoints.size()) : static_cast<int>(path.waypoints.size()) - 1;
    for (int i = 0; i < segments; ++i) {
        const PathWaypoint a = path.waypoints[static_cast<std::size_t>(i)];
        const PathWaypoint b = path.waypoints[static_cast<std::size_t>((i + 1) % static_cast<int>(path.waypoints.size()))];
        const float segLen = distance(a, b);
        if (walked + segLen >= targetDistance) {
            const float t = segLen > 0.0f ? (targetDistance - walked) / segLen : 0.0f;
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
        }
        walked += segLen;
    }
    return path.loop ? path.waypoints.front() : path.waypoints.back();
}

// Arc-distance along the path of the point nearest to (x, y). Used to resume patrol
// from wherever an enemy ended up after chasing, instead of snapping to a stale distance.
float nearestPathDistance(const EnemyPath& path, float x, float y, PathWaypoint& outPoint)
{
    const float length = approximatePathLength(path);
    if (length <= 0.0f || path.waypoints.empty()) {
        outPoint = path.waypoints.empty() ? PathWaypoint{} : path.waypoints.front();
        return 0.0f;
    }
    constexpr float kSampleStepPx = 3.0f;
    const int samples = std::max(2, static_cast<int>(length / kSampleStepPx));
    PathWaypoint bestPoint = pointAtDistance(path, 0.0f);
    float bestDistSq = (bestPoint.x - x) * (bestPoint.x - x) + (bestPoint.y - y) * (bestPoint.y - y);
    float bestD = 0.0f;
    for (int i = 1; i <= samples; ++i) {
        const float d = length * static_cast<float>(i) / static_cast<float>(samples);
        const PathWaypoint p = pointAtDistance(path, d);
        const float distSq = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestD = d;
            bestPoint = p;
        }
    }
    outPoint = bestPoint;
    return bestD;
}

// For linear paths: the waypoint an entity should head toward next given an arc-distance.
std::size_t waypointIndexForDistance(const EnemyPath& path, float targetDistance)
{
    const int n = static_cast<int>(path.waypoints.size());
    if (n < 2) {
        return 0;
    }
    const int segments = path.loop ? n : n - 1;
    float walked = 0.0f;
    for (int i = 0; i < segments; ++i) {
        const PathWaypoint& a = path.waypoints[static_cast<std::size_t>(i)];
        const PathWaypoint& b = path.waypoints[static_cast<std::size_t>((i + 1) % n)];
        const float segLen = distance(a, b);
        if (walked + segLen >= targetDistance) {
            return static_cast<std::size_t>((i + 1) % n);
        }
        walked += segLen;
    }
    return path.loop ? 0u : static_cast<std::size_t>(n - 1);
}

} // namespace

class Engine::MusicPlayer {
public:
    ~MusicPlayer()
    {
        stop();
        if (device_ != 0) {
            SDL_CloseAudioDevice(device_);
            device_ = 0;
        }
        if (audioInitialized_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
    }

    bool play(const std::filesystem::path& path, bool loop, std::string* errorMessage)
    {
        const std::string key = path.string() + (loop ? "#loop" : "#once");
        if (key == currentKey_ && !pcm_.empty()) {
            return true;
        }

        std::vector<std::uint8_t> decoded;
        SDL_AudioSpec decodedSpec{};
        if (!decodeOgg(path, decoded, decodedSpec, errorMessage)) {
            stop();
            return false;
        }

        if (!audioInitialized_) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                setError(errorMessage, std::string("Failed to initialize SDL audio: ") + SDL_GetError());
                return false;
            }
            audioInitialized_ = true;
        }

        decodedSpec.callback = &MusicPlayer::audioCallback;
        decodedSpec.userdata = this;

        if (device_ != 0 &&
            (deviceSpec_.freq != decodedSpec.freq || deviceSpec_.format != decodedSpec.format ||
                deviceSpec_.channels != decodedSpec.channels)) {
            SDL_CloseAudioDevice(device_);
            device_ = 0;
            deviceSpec_ = {};
            effectPcm_.clear();
            effectCursor_ = 0;
        }

        if (device_ == 0) {
            SDL_AudioSpec obtained{};
            device_ = SDL_OpenAudioDevice(nullptr, 0, &decodedSpec, &obtained, 0);
            if (device_ == 0) {
                setError(errorMessage, std::string("Failed to open SDL audio device: ") + SDL_GetError());
                return false;
            }
            deviceSpec_ = obtained;
        }

        SDL_LockAudioDevice(device_);
        pcm_ = std::move(decoded);
        cursor_ = 0;
        loop_ = loop;
        currentKey_ = key;
        SDL_UnlockAudioDevice(device_);
        SDL_PauseAudioDevice(device_, 0);
        return true;
    }

    bool playEffect(const std::filesystem::path& path, std::string* errorMessage)
    {
        std::vector<std::uint8_t> decoded;
        SDL_AudioSpec decodedSpec{};
        if (!decodeAudio(path, decoded, decodedSpec, errorMessage)) {
            return false;
        }
        if (!audioInitialized_) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                setError(errorMessage, std::string("Failed to initialize SDL audio: ") + SDL_GetError());
                return false;
            }
            audioInitialized_ = true;
        }
        if (device_ == 0) {
            decodedSpec.callback = &MusicPlayer::audioCallback;
            decodedSpec.userdata = this;
            SDL_AudioSpec obtained{};
            device_ = SDL_OpenAudioDevice(nullptr, 0, &decodedSpec, &obtained, 0);
            if (device_ == 0) {
                setError(errorMessage, std::string("Failed to open SDL audio device: ") + SDL_GetError());
                return false;
            }
            deviceSpec_ = obtained;
        } else if (deviceSpec_.freq != decodedSpec.freq || deviceSpec_.format != decodedSpec.format ||
            deviceSpec_.channels != decodedSpec.channels) {
            SDL_AudioCVT converter{};
            if (SDL_BuildAudioCVT(&converter, decodedSpec.format, decodedSpec.channels, decodedSpec.freq,
                    deviceSpec_.format, deviceSpec_.channels, deviceSpec_.freq) < 0) {
                setError(errorMessage, std::string("Failed to configure SFX conversion: ") + SDL_GetError());
                return false;
            }
            std::vector<std::uint8_t> converted(decoded.size() * static_cast<std::size_t>(converter.len_mult));
            std::memcpy(converted.data(), decoded.data(), decoded.size());
            converter.buf = converted.data();
            converter.len = static_cast<int>(decoded.size());
            if (SDL_ConvertAudio(&converter) != 0) {
                setError(errorMessage, std::string("Failed to convert SFX audio: ") + SDL_GetError());
                return false;
            }
            converted.resize(static_cast<std::size_t>(converter.len_cvt));
            decoded = std::move(converted);
        }

        SDL_LockAudioDevice(device_);
        effectPcm_ = std::move(decoded);
        effectCursor_ = 0;
        SDL_UnlockAudioDevice(device_);
        SDL_PauseAudioDevice(device_, 0);
        return true;
    }

    void stop()
    {
        currentKey_.clear();
        if (device_ == 0) {
            pcm_.clear();
            cursor_ = 0;
            return;
        }
        SDL_LockAudioDevice(device_);
        pcm_.clear();
        cursor_ = 0;
        loop_ = false;
        const bool effectPlaying = effectCursor_ < effectPcm_.size();
        SDL_UnlockAudioDevice(device_);
        if (!effectPlaying) {
            SDL_PauseAudioDevice(device_, 1);
        }
    }

private:
    SDL_AudioDeviceID device_ = 0;
    SDL_AudioSpec deviceSpec_{};
    std::vector<std::uint8_t> pcm_;
    std::size_t cursor_ = 0;
    std::vector<std::uint8_t> effectPcm_;
    std::size_t effectCursor_ = 0;
    bool loop_ = false;
    bool audioInitialized_ = false;
    std::string currentKey_;

    static bool decodeOgg(const std::filesystem::path& path, std::vector<std::uint8_t>& outPcm,
        SDL_AudioSpec& outSpec, std::string* errorMessage)
    {
        OggVorbis_File ogg{};
        if (ov_fopen(path.string().c_str(), &ogg) != 0) {
            setError(errorMessage, "Failed to open OGG music: " + path.string());
            return false;
        }

        vorbis_info* info = ov_info(&ogg, -1);
        if (info == nullptr || info->channels <= 0 || info->rate <= 0) {
            ov_clear(&ogg);
            setError(errorMessage, "Invalid OGG music stream: " + path.string());
            return false;
        }

        outSpec = {};
        outSpec.freq = static_cast<int>(info->rate);
        outSpec.format = AUDIO_S16SYS;
        outSpec.channels = static_cast<Uint8>(info->channels);
        outSpec.samples = 4096;

        int bitstream = 0;
        char buffer[8192];
        for (;;) {
            const long bytes = ov_read(&ogg, buffer, sizeof(buffer),
                SDL_BYTEORDER == SDL_BIG_ENDIAN ? 1 : 0, 2, 1, &bitstream);
            if (bytes == 0) {
                break;
            }
            if (bytes < 0) {
                ov_clear(&ogg);
                setError(errorMessage, "Failed while decoding OGG music: " + path.string());
                return false;
            }
            const auto* begin = reinterpret_cast<const std::uint8_t*>(buffer);
            outPcm.insert(outPcm.end(), begin, begin + bytes);
        }
        ov_clear(&ogg);

        if (outPcm.empty()) {
            setError(errorMessage, "OGG music has no decoded samples: " + path.string());
            return false;
        }
        return true;
    }

    static bool decodeAudio(const std::filesystem::path& path, std::vector<std::uint8_t>& outPcm,
        SDL_AudioSpec& outSpec, std::string* errorMessage)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".ogg") {
            return decodeOgg(path, outPcm, outSpec, errorMessage);
        }
        if (extension == ".wav") {
            Uint8* wavData = nullptr;
            Uint32 wavLength = 0;
            if (SDL_LoadWAV(path.string().c_str(), &outSpec, &wavData, &wavLength) == nullptr) {
                setError(errorMessage, "Failed to open WAV sound: " + path.string());
                return false;
            }
            outPcm.assign(wavData, wavData + wavLength);
            SDL_FreeWAV(wavData);
            return !outPcm.empty();
        }
        setError(errorMessage, "Unsupported sound format (expected .ogg or .wav): " + path.string());
        return false;
    }

    static void audioCallback(void* userdata, Uint8* stream, int len)
    {
        auto* player = static_cast<MusicPlayer*>(userdata);
        SDL_memset(stream, 0, len);
        if (player == nullptr) {
            return;
        }

        int written = 0;
        while (written < len && !player->pcm_.empty()) {
            const std::size_t remaining = player->pcm_.size() - player->cursor_;
            if (remaining == 0) {
                if (player->loop_) {
                    player->cursor_ = 0;
                    continue;
                }
                break;
            }

            const std::size_t chunk = std::min<std::size_t>(remaining, static_cast<std::size_t>(len - written));
            std::memcpy(stream + written, player->pcm_.data() + player->cursor_, chunk);
            player->cursor_ += chunk;
            written += static_cast<int>(chunk);
        }

        if (player->effectCursor_ < player->effectPcm_.size()) {
            const std::size_t remaining = player->effectPcm_.size() - player->effectCursor_;
            const std::size_t chunk = std::min<std::size_t>(remaining, static_cast<std::size_t>(len));
            SDL_MixAudioFormat(stream, player->effectPcm_.data() + player->effectCursor_,
                player->deviceSpec_.format, static_cast<Uint32>(chunk), SDL_MIX_MAXVOLUME);
            player->effectCursor_ += chunk;
            if (player->effectCursor_ >= player->effectPcm_.size()) {
                player->effectPcm_.clear();
                player->effectCursor_ = 0;
            }
        }
    }
};

Engine::Engine(std::filesystem::path projectRoot)
    : projectRoot_(std::move(projectRoot))
    , musicPlayer_(std::make_unique<MusicPlayer>())
{
}

Engine::~Engine()
{
    destroyTexture(floorTexture_);
    destroyTexture(wallTexture_);
    destroyTexture(prevFloorTexture_);
    destroyTexture(prevWallTexture_);
    destroyTexture(playerSprite_.texture);
    destroyTexture(font_.texture);
    for (auto& [id, sprite] : loadedSprites_) {
        destroyTexture(sprite.texture);
    }
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool Engine::initialize(const std::filesystem::path& chapterPath, std::string* errorMessage)
{
    if (!glfwInit()) {
        setError(errorMessage, "Failed to initialize GLFW.");
        return false;
    }

#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    window_ = glfwCreateWindow(kScreenTilesW * kTileSize * 2, kScreenTilesH * kTileSize * 2, "Adventure Runtime", nullptr, nullptr);
    if (window_ == nullptr) {
        setError(errorMessage, "Failed to create runtime window.");
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    std::string error;
    if (!loadChapter(chapterPath, chapter_, &error)) {
        setError(errorMessage, "Failed to load chapter: " + error);
        return false;
    }

    // Seed runtime state from project variable defaults, then merge in any saved progress.
    // Must happen before loadScreen so the start screen filters out already-defeated enemies.
    {
        GameProject project;
        if (loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr)) {
            for (const StateVariableDef& def : project.stateVariables) {
                switch (def.type) {
                    case StateVariableType::Integer: gameState_.setInt(def.id, def.defaultInt); break;
                    case StateVariableType::Boolean: gameState_.setBool(def.id, def.defaultBool); break;
                    case StateVariableType::Item: if (def.defaultBool) gameState_.giveItem(def.id); break;
                }
            }
        }
        // A normal launch starts a new game: state stays at the seeded defaults so
        // previously defeated enemies and quest progress do NOT carry over. Saved
        // progress is only merged in when explicitly continuing (and never for
        // fresh test launches).
        if (continueSave_ && !freshStart_) {
            const std::filesystem::path savePath = assetPath(projectRoot_, "assets/game/save.adstate");
            std::error_code ec;
            GameState saved;
            if (std::filesystem::exists(savePath, ec) && loadGameState(savePath, saved, nullptr)) {
                for (const auto& [id, value] : saved.ints()) gameState_.setInt(id, value);
                for (const auto& [id, value] : saved.bools()) gameState_.setBool(id, value);
                for (const std::string& id : saved.items()) gameState_.giveItem(id);
                for (const std::string& key : saved.defeatedEnemies()) gameState_.markEnemyDefeated(key);
            }
        }
    }

    // Pick the starting screen: an explicit override (editor test launch) or the chapter default.
    std::string startScreen = chapter_.startScreenId;
    if (!startScreenOverride_.empty() && findScreen(chapter_, startScreenOverride_) != nullptr) {
        startScreen = startScreenOverride_;
    }
    if (!loadScreen(startScreen, &error)) {
        setError(errorMessage, error);
        return false;
    }
    loadPlayableCharacter();
    loadWeapons();
    loadProjectFont();
    if (startPosX_ >= 0.0f && startPosY_ >= 0.0f && playerCanOccupy(startPosX_, startPosY_)) {
        playerX_ = startPosX_;
        playerY_ = startPosY_;
    } else {
        const float centerX = screenWidthPx() * 0.5f;
        const float centerY = screenHeightPx() * 0.5f;
        if (playerCanOccupy(centerX, centerY)) {
            playerX_ = centerX;
            playerY_ = centerY;
        } else {
            playerX_ = static_cast<float>(activeMap_.spawnX * kTileSize + kTileSize / 2);
            playerY_ = static_cast<float>(activeMap_.spawnY * kTileSize + kTileSize / 2);
        }
    }
    writeCheckpoint(startScreen, playerX_, playerY_);
    return true;
}

void Engine::run()
{
    double currentTime = glfwGetTime();
    double accumulator = 0.0;

    while (window_ != nullptr && !glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        const double newTime = glfwGetTime();
        const double frameTime = std::min(0.25, newTime - currentTime);
        currentTime = newTime;
        accumulator += frameTime;

        while (accumulator >= kFixedStepSeconds) {
            update(kFixedStepSeconds);
            accumulator -= kFixedStepSeconds;
        }

        render();
        glfwSwapBuffers(window_);
    }

    saveRuntimeState();  // persist progress on quit
}

bool Engine::loadScreen(const std::string& screenId, std::string* errorMessage)
{
    const ChapterScreen* screen = findScreen(chapter_, screenId);
    if (screen == nullptr) {
        setError(errorMessage, "Screen not found: " + screenId);
        return false;
    }

    TileMap map;
    std::string error;
    const std::filesystem::path mapPath = assetPath(projectRoot_, "assets/game/maps") / (screen->mapId + ".admap");
    if (!loadTileMap(mapPath, map, &error)) {
        setError(errorMessage, "Failed to load map for screen " + screenId + ": " + error);
        return false;
    }

    activeScreen_ = screen;
    activeMap_ = std::move(map);
    hazardCooldowns_.clear();
    destroyTexture(floorTexture_);
    destroyTexture(wallTexture_);

    const std::filesystem::path tilesetDir = assetPath(projectRoot_, "assets/game/tilesets");
    if (!loadTexture(tilesetDir / (screen->mapId + "_floor.png"), floorTexture_, nullptr)) {
        floorTexture_ = {};
    }
    if (!loadTexture(tilesetDir / (screen->mapId + "_wall.png"), wallTexture_, nullptr)) {
        wallTexture_ = {};
    }

    loadPathEntities();
    loadNpcEntities();
    loadItemEntities();
    projectiles_.clear();
    interactionState_ = InteractionState::None;
    interactingNpcIndex_ = -1;
    interactingDoorIndex_ = -1;
    interactingDoorIndex_ = -1;
    dialogueLineIndex_ = 0;
    loadAllSprites();
    updateScreenMusic();
    std::cout << "Loaded screen " << screen->id << " map " << activeMap_.id << "\n";
    return true;
}

void Engine::updateScreenMusic()
{
    if (musicPlayer_ == nullptr || activeScreen_ == nullptr) {
        return;
    }
    if (activeScreen_->musicPath.empty()) {
        musicPlayer_->stop();
        return;
    }

    const std::filesystem::path configuredPath(activeScreen_->musicPath);
    const std::filesystem::path musicPath = configuredPath.is_absolute() ? configuredPath : projectRoot_ / configuredPath;
    std::string error;
    if (!musicPlayer_->play(musicPath, activeScreen_->musicLoop, &error)) {
        std::cerr << "Failed to play screen music: " << error << "\n";
    }
}

bool Engine::loadTexture(const std::filesystem::path& path, Texture& texture, std::string* errorMessage)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (data == nullptr) {
        setError(errorMessage, "Failed to load texture: " + path.string());
        return false;
    }

    unsigned int glTexture = 0;
    glGenTextures(1, &glTexture);
    glBindTexture(GL_TEXTURE_2D, glTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    texture.id = glTexture;
    texture.width = width;
    texture.height = height;
    return true;
}

void Engine::destroyTexture(Texture& texture)
{
    if (texture.id != 0) {
        const unsigned int id = texture.id;
        glDeleteTextures(1, &id);
    }
    texture = {};
}

void Engine::loadWeapons()
{
    meleeWeapon_.reset();
    rangedWeapon_.reset();
    itemDefs_.clear();
    inventory_.clear();

    GameProject project;
    if (!loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr)) {
        return;
    }
    itemDefs_ = project.itemDefs;
    syncInventoryFromGameState();

    if (project.startingWeaponId.empty()) {
        return;
    }

    for (const WeaponDef& w : project.weaponDefs) {
        if (w.id == project.startingWeaponId) {
            if (w.type == WeaponType::Melee) {
                meleeWeapon_ = w;
                loadSpriteById(w.spriteId);
            } else {
                rangedWeapon_ = w;
                loadSpriteById(w.spriteId);
                loadSpriteById(w.ammoSpriteId);
            }
            break;
        }
    }
}

void Engine::syncInventoryFromGameState()
{
    for (const ItemDef& def : itemDefs_) {
        if (!def.id.empty() && gameState_.hasItem(def.id) && inventory_[def.id] <= 0) {
            inventory_[def.id] = 1;
        }
    }
}

std::string Engine::ammoItemIdForWeapon(const WeaponDef& weapon) const
{
    const std::string ammoType = weapon.ammoTypeId.empty() ? weapon.id : weapon.ammoTypeId;
    // Prefer an Ammo item definition whose id or target matches the ammo type;
    // its id is the inventory key. Otherwise fall back to the ammo type itself.
    for (const ItemDef& def : itemDefs_) {
        if (def.type == ItemDefType::Ammo && (def.id == ammoType || def.targetId == ammoType)) {
            return def.id;
        }
    }
    return ammoType;
}

int Engine::ammoCountForWeapon(const WeaponDef& weapon) const
{
    const auto it = inventory_.find(ammoItemIdForWeapon(weapon));
    return it != inventory_.end() ? it->second : 0;
}

void Engine::consumeAmmoForWeapon(const WeaponDef& weapon, int amount)
{
    (void)removeInventoryItem(ammoItemIdForWeapon(weapon), amount);
}

void Engine::loadProjectFont()
{
    destroyTexture(font_.texture);
    font_ = RuntimeFont{};
    font_.pixelHeight = 16.0f;
    font_.chars.resize(96);

    GameProject project;
    (void)loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr);
    std::filesystem::path fontPath = project.fontPath;
    if (fontPath.empty()) {
#ifdef ADVENTURE_SOURCE_ROOT
        fontPath = std::filesystem::path(ADVENTURE_SOURCE_ROOT) / "external/imgui/misc/fonts/Roboto-Medium.ttf";
#else
        fontPath = "external/imgui/misc/fonts/Roboto-Medium.ttf";
#endif
    } else if (!fontPath.is_absolute()) {
        fontPath = projectRoot_ / fontPath;
    }

    std::ifstream input(fontPath, std::ios::binary);
    if (!input) {
        return;
    }
    std::vector<unsigned char> fontBytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (fontBytes.empty()) {
        return;
    }

    constexpr int atlasW = 512;
    constexpr int atlasH = 512;
    std::vector<unsigned char> alpha(static_cast<std::size_t>(atlasW * atlasH), 0u);
    std::vector<stbtt_bakedchar> baked(96);
    const int bakeResult = stbtt_BakeFontBitmap(fontBytes.data(), 0, font_.pixelHeight,
        alpha.data(), atlasW, atlasH, 32, 96, baked.data());
    if (bakeResult <= 0) {
        return;
    }

    std::vector<unsigned char> rgba(static_cast<std::size_t>(atlasW * atlasH * 4), 255u);
    for (int i = 0; i < atlasW * atlasH; ++i) {
        rgba[static_cast<std::size_t>(i) * 4u + 3u] = alpha[static_cast<std::size_t>(i)];
    }

    unsigned int glTexture = 0;
    glGenTextures(1, &glTexture);
    glBindTexture(GL_TEXTURE_2D, glTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasW, atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    font_.texture.id = glTexture;
    font_.texture.width = atlasW;
    font_.texture.height = atlasH;
    font_.loaded = true;
    for (int i = 0; i < 96; ++i) {
        font_.chars[static_cast<std::size_t>(i)] = {
            baked[static_cast<std::size_t>(i)].x0,
            baked[static_cast<std::size_t>(i)].y0,
            baked[static_cast<std::size_t>(i)].x1,
            baked[static_cast<std::size_t>(i)].y1,
            baked[static_cast<std::size_t>(i)].xoff,
            baked[static_cast<std::size_t>(i)].yoff,
            baked[static_cast<std::size_t>(i)].xadvance,
        };
    }
}

void Engine::loadItemEntities()
{
    itemEntities_.clear();
    for (const MapItemPlacement& placement : activeMap_.items) {
        RuntimeItemEntity entity;
        entity.placement = placement;
        entity.collected = !placement.respawn &&
            gameState_.getBool(itemStateId(placement), false);
        itemEntities_.push_back(entity);
    }
}

std::string Engine::itemStateId(const MapItemPlacement& item) const
{
    const auto safeToken = [](std::string value) {
        for (char& c : value) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
                c = '_';
            }
        }
        return value.empty() ? std::string{"unknown"} : value;
    };
    return "item_collected." + safeToken(chapter_.id) + "." +
        safeToken(activeScreen_ == nullptr ? std::string{} : activeScreen_->id) + "." +
        safeToken(item.id);
}

void Engine::loadPathEntities()
{
    pathEntities_.clear();
    GameProject project;
    (void)loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr);

    auto typeForId = [&project](const std::string& id) -> const EnemyType* {
        for (const EnemyType& type : project.enemyTypes) {
            if (type.id == id) {
                return &type;
            }
        }
        return nullptr;
    };

    if (activeScreen_ != nullptr) {
        for (const EnemyPlacement& placement : activeScreen_->enemies) {
            if (placement.waypoints.empty()) {
                continue;
            }
            EnemyPath path;
            path.id = placement.id;
            path.enemyId = placement.typeId;
            path.mapId = activeScreen_->mapId;
            path.behavior = placement.behavior;
            path.curveMode = placement.curveMode;
            path.speed = placement.speedOverride;
            path.loop = placement.loop;
            path.respawn = placement.respawn;
            path.renderAboveWalls = placement.renderAboveWalls;
            path.waypoints = placement.waypoints;
            if (const EnemyType* type = typeForId(placement.typeId)) {
                path.spriteId = type->spriteId;
                path.combat.maxHealth = type->maxHealth;
                path.combat.contactDamage = type->contactDamage;
                path.combat.hitboxWidth = type->hitboxWidth;
                path.combat.hitboxHeight = type->hitboxHeight;
                path.combat.attackCooldownSeconds = type->attackCooldownSeconds;
                path.combat.attacks = type->attacks;
                path.combat.knockbackResistance = type->knockbackResistance;
                path.combat.hitstunSeconds = type->hitstunSeconds;
                path.combat.aggroRange = type->aggroRange;
                path.combat.killVariable = type->killVariable;
                path.combat.killAmount = type->killAmount;
                if (path.speed <= 0.0f) {
                    path.speed = type->speed;
                }
            }
            RuntimePathEntity entity;
            entity.path = std::move(path);
            entity.x = entity.path.waypoints.front().x;
            entity.y = entity.path.waypoints.front().y;
            entity.health = std::max(1, entity.path.combat.maxHealth);
            entity.waypointIndex = 0;
            entity.pathHidden = pathStartsHidden(entity.path.waypoints);
            entity.attackCooldowns.assign(entity.path.combat.attacks.size(), 0.0f);
            if (activeScreen_ != nullptr &&
                gameState_.isEnemyDefeated(activeScreen_->id + "/" + entity.path.id)) {
                continue;
            }
            applyEnemyWaypointAction(entity, entity.path.waypoints.front());
            pathEntities_.push_back(std::move(entity));
        }
    }

    const std::filesystem::path pathDir = assetPath(projectRoot_, "assets/game/paths");
    std::error_code ec;
    if (!std::filesystem::exists(pathDir, ec)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(pathDir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".adpath") {
            continue;
        }
        EnemyPath path;
        if (!loadEnemyPath(entry.path(), path, nullptr) || activeScreen_ == nullptr ||
            path.mapId != activeScreen_->mapId || path.waypoints.empty()) {
            continue;
        }
        RuntimePathEntity entity;
        entity.path = std::move(path);
        entity.x = entity.path.waypoints.front().x;
        entity.y = entity.path.waypoints.front().y;
        entity.health = std::max(1, entity.path.combat.maxHealth);
        entity.waypointIndex = 0;
        entity.pathHidden = pathStartsHidden(entity.path.waypoints);
        entity.attackCooldowns.assign(entity.path.combat.attacks.size(), 0.0f);
        if (activeScreen_ != nullptr &&
            gameState_.isEnemyDefeated(activeScreen_->id + "/" + entity.path.id)) {
            continue;
        }
        applyEnemyWaypointAction(entity, entity.path.waypoints.front());
        pathEntities_.push_back(std::move(entity));
    }
}

void Engine::loadSpriteById(const std::string& spriteId)
{
    if (spriteId.empty() || loadedSprites_.find(spriteId) != loadedSprites_.end()) {
        return;
    }
    const std::filesystem::path spriteDir = assetPath(projectRoot_, "assets/game/sprites");
    RuntimeSprite runtime;
    std::string error;
    const std::filesystem::path metadataPath = spriteDir / (spriteId + ".sprite.json");
    if (!loadSpriteMetadata(metadataPath, runtime.metadata, &error)) {
        loadedSprites_[spriteId] = std::move(runtime);
        return;
    }
    std::filesystem::path sourcePath = runtime.metadata.source;
    if (sourcePath.empty()) {
        sourcePath = std::filesystem::path("assets/raw/sprites") / (spriteId + "_sheet.png");
    }
    sourcePath = sourcePath.is_absolute() ? sourcePath : projectRoot_ / sourcePath;
    runtime.loaded = loadTexture(sourcePath, runtime.texture, nullptr);
    loadedSprites_[spriteId] = std::move(runtime);
}

void Engine::loadAllSprites()
{
    for (auto& [id, sprite] : loadedSprites_) {
        destroyTexture(sprite.texture);
    }
    loadedSprites_.clear();

    for (const MapObstacle& obstacle : activeMap_.obstacles) {
        loadSpriteById(obstacle.spriteId);
    }
    for (const MapDoorPlacement& door : activeMap_.doors) {
        loadSpriteById(door.spriteId);
    }
    for (const RuntimePathEntity& entity : pathEntities_) {
        loadSpriteById(entity.path.spriteId);
    }
    for (const RuntimeNpcEntity& npc : npcEntities_) {
        loadSpriteById(npc.spriteId);
    }
    for (const RuntimeItemEntity& item : itemEntities_) {
        loadSpriteById(item.placement.spriteId);
    }
    if (meleeWeapon_.has_value()) {
        loadSpriteById(meleeWeapon_->spriteId);
    }
    if (rangedWeapon_.has_value()) {
        loadSpriteById(rangedWeapon_->spriteId);
        loadSpriteById(rangedWeapon_->ammoSpriteId);
    }
    for (const ItemDef& item : itemDefs_) {
        loadSpriteById(item.spriteId);
    }
    if (activeScreen_ != nullptr) {
        for (const AnimatedTilePlacement& tile : activeScreen_->animatedTiles) {
            loadSpriteById(tile.spriteId);
        }
    }
}

void Engine::loadPlayableCharacter()
{
    destroyTexture(playerSprite_.texture);
    playerSprite_ = RuntimeSprite{};

    const std::filesystem::path characterDir = assetPath(projectRoot_, "assets/game/characters");
    std::error_code ec;
    if (!std::filesystem::exists(characterDir, ec)) {
        return;
    }

    auto loadCharacterSprite = [&](const std::filesystem::path& charPath) -> bool {
        std::ifstream input(charPath);
        if (!input) return false;

        std::string key;
        input >> key;
        if (key != "ADCHARACTER") return false;
        int version = 0;
        input >> version;

        bool playable = false;
        std::filesystem::path spritePath;
        while (input >> key) {
            if (key == "playable") {
                int v = 0;
                input >> v;
                playable = v != 0;
            } else if (key == "sprite") {
                std::string p;
                input >> std::quoted(p);
                spritePath = p;
            } else if (key == "end") {
                break;
            } else if (key == "name" || key == "bio") {
                std::string ignored;
                input >> std::quoted(ignored);
            } else if (key == "anim") {
                std::string a, b;
                input >> std::quoted(a) >> std::quoted(b);
            } else if (key == "frame") {
                int idx = 0;
                std::string st, img;
                input >> idx >> std::quoted(st) >> std::quoted(img);
            } else if (key == "animations" || key == "frames") {
                std::size_t ignored = 0;
                input >> ignored;
            }
        }

        if (!playable || spritePath.empty()) return false;

        const std::filesystem::path fullPath = spritePath.is_absolute() ? spritePath : projectRoot_ / spritePath;
        std::string error;
        if (!loadSpriteMetadata(fullPath, playerSprite_.metadata, &error)) return false;

        std::filesystem::path sourcePath = playerSprite_.metadata.source;
        if (sourcePath.empty()) {
            sourcePath = std::filesystem::path("assets/raw/sprites") / (playerSprite_.metadata.id + "_sheet.png");
        }
        sourcePath = sourcePath.is_absolute() ? sourcePath : projectRoot_ / sourcePath;
        playerSprite_.loaded = loadTexture(sourcePath, playerSprite_.texture, nullptr);
        return playerSprite_.loaded;
    };

    if (!chapter_.playableCharacterId.empty()) {
        if (loadCharacterSprite(characterDir / (chapter_.playableCharacterId + ".adcharacter"))) return;
    }

    GameProject project;
    if (loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr) &&
        !project.playableCharacterId.empty()) {
        if (loadCharacterSprite(characterDir / (project.playableCharacterId + ".adcharacter"))) return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(characterDir, ec)) {
        if (ec || !entry.is_regular_file(ec) || entry.path().extension() != ".adcharacter") continue;
        if (loadCharacterSprite(entry.path())) return;
    }
}

bool Engine::gamepadButtonDown(int button) const
{
    GLFWgamepadstate state{};
    if (!glfwJoystickIsGamepad(GLFW_JOYSTICK_1) ||
        !glfwGetGamepadState(GLFW_JOYSTICK_1, &state) ||
        button < 0 || button >= GLFW_GAMEPAD_BUTTON_LAST + 1) {
        return false;
    }
    return state.buttons[button] == GLFW_PRESS;
}

float Engine::gamepadAxis(int axis) const
{
    GLFWgamepadstate state{};
    if (!glfwJoystickIsGamepad(GLFW_JOYSTICK_1) ||
        !glfwGetGamepadState(GLFW_JOYSTICK_1, &state) ||
        axis < 0 || axis >= GLFW_GAMEPAD_AXIS_LAST + 1) {
        return 0.0f;
    }
    const float value = state.axes[axis];
    return std::abs(value) >= kGamepadDeadZone ? value : 0.0f;
}

bool Engine::inputDown(InputAction action) const
{
    switch (action) {
        case InputAction::Up:
            return glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS ||
                glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_DPAD_UP) ||
                gamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_Y) < 0.0f;
        case InputAction::Down:
            return glfwGetKey(window_, GLFW_KEY_DOWN) == GLFW_PRESS ||
                glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_DPAD_DOWN) ||
                gamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_Y) > 0.0f;
        case InputAction::Left:
            return glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS ||
                glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_DPAD_LEFT) ||
                gamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_X) < 0.0f;
        case InputAction::Right:
            return glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS ||
                glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT) ||
                gamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_X) > 0.0f;
        case InputAction::Interact:
            return glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS ||
                glfwGetKey(window_, GLFW_KEY_ENTER) == GLFW_PRESS ||
                glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_A);
        case InputAction::Melee:
            return glfwGetKey(window_, GLFW_KEY_Z) == GLFW_PRESS ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_X);
        case InputAction::Ranged:
            return glfwGetKey(window_, GLFW_KEY_X) == GLFW_PRESS ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_B);
        case InputAction::Inventory:
            return glfwGetKey(window_, GLFW_KEY_I) == GLFW_PRESS ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_Y) ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_START) ||
                gamepadButtonDown(GLFW_GAMEPAD_BUTTON_BACK);
        case InputAction::Exit:
            return glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    }
    return false;
}

void Engine::update(float dt)
{
    runtimeSeconds_ += dt;
    playerInvulnerableSeconds_ = std::max(0.0f, playerInvulnerableSeconds_ - dt);
    playerHitFlashSeconds_ = std::max(0.0f, playerHitFlashSeconds_ - dt);
    noticeSeconds_ = std::max(0.0f, noticeSeconds_ - dt);

    const bool inventoryInputDown = inputDown(InputAction::Inventory);
    if (inventoryInputDown && !inventoryInputWasDown_ &&
        interactionState_ != InteractionState::InDialogue &&
        interactionState_ != InteractionState::InShop) {
        inventoryVisible_ = !inventoryVisible_;
        inventoryUpWasDown_ = false;
        inventoryDownWasDown_ = false;
        inventoryUseWasDown_ = false;
    }
    inventoryInputWasDown_ = inventoryInputDown;

    if (transitionState_ == TransitionState::Sliding) {
        transitionTime_ += dt;
        if (transitionTime_ >= transitionDuration_) {
            transitionState_ = TransitionState::None;
            destroyTexture(prevFloorTexture_);
            destroyTexture(prevWallTexture_);
            playSoundEffect(pendingDoorCloseSoundPath_);
            pendingDoorCloseSoundPath_.clear();
        }
        return;
    }

    if (inventoryVisible_) {
        updateInventoryInput();
        if (playerActionType_ != "idle") {
            playerActionType_ = "idle";
            playerAnimSeconds_ = 0.0f;
        }
        playerIsMoving_ = false;
        return;
    }
    if (interactionState_ == InteractionState::InShop) {
        updateShopInput();
        playerIsMoving_ = false;
        return;
    }

    updatePlayer(dt);
    updateAttack(dt);
    updateProjectiles(dt);
    updateHazards(dt);
    updateEnemyCombat(dt);
    updateEnemyDeaths(dt);
    updatePaths(dt);
    updateNpcs(dt);
    updateDoors();
    updateInteraction();
    updateItemPickups();
}

bool Engine::isAmmoItemId(const std::string& id) const
{
    for (const ItemDef& def : itemDefs_) {
        if (def.id == id) {
            return def.type == ItemDefType::Ammo;
        }
    }
    return false;
}

bool Engine::isCurrencyItemId(const std::string& id) const
{
    for (const ItemDef& def : itemDefs_) {
        if (def.id == id) {
            return def.type == ItemDefType::Currency;
        }
    }
    return false;
}

bool Engine::hasInventoryItem(const std::string& itemId) const
{
    const auto it = inventory_.find(itemId);
    return it != inventory_.end() && it->second > 0;
}

void Engine::addInventoryItem(const std::string& itemId, int quantity)
{
    if (itemId.empty() || quantity <= 0) {
        return;
    }
    inventory_[itemId] += quantity;
    gameState_.giveItem(itemId);
}

bool Engine::removeInventoryItem(const std::string& itemId, int quantity)
{
    if (itemId.empty() || quantity <= 0) {
        return false;
    }
    auto it = inventory_.find(itemId);
    if (it == inventory_.end() || it->second < quantity) {
        return false;
    }
    it->second -= quantity;
    if (it->second <= 0) {
        inventory_.erase(it);
        gameState_.takeItem(itemId);
    } else {
        gameState_.giveItem(itemId);
    }
    return true;
}

std::vector<std::string> Engine::ammoInventoryIds() const
{
    std::vector<std::string> ids;
    for (const auto& [id, count] : inventory_) {
        if (count > 0 && isAmmoItemId(id)) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> Engine::sortedInventoryIds() const
{
    std::vector<std::string> ids;
    ids.reserve(inventory_.size());
    for (const auto& [id, count] : inventory_) {
        // Ammo and currency have dedicated inventory sections, not main rows.
        if (count > 0 && !isAmmoItemId(id) && !isCurrencyItemId(id)) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end(), [this](const std::string& a, const std::string& b) {
        const auto nameFor = [this](const std::string& id) -> std::string {
            for (const ItemDef& def : itemDefs_) {
                if (def.id == id) {
                    return def.name.empty() ? def.id : def.name;
                }
            }
            return id;
        };
        return nameFor(a) < nameFor(b);
    });
    return ids;
}

std::vector<std::string> Engine::sortedShopInventoryIds() const
{
    std::vector<std::string> ids;
    ids.reserve(inventory_.size());
    for (const auto& [id, count] : inventory_) {
        if (count > 0 && !isCurrencyItemId(id)) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end(), [this](const std::string& a, const std::string& b) {
        const auto nameFor = [this](const std::string& id) -> std::string {
            for (const ItemDef& def : itemDefs_) {
                if (def.id == id) {
                    return def.name.empty() ? def.id : def.name;
                }
            }
            return id;
        };
        return nameFor(a) < nameFor(b);
    });
    return ids;
}

void Engine::updateInventoryInput()
{
    const std::vector<std::string> ids = sortedInventoryIds();
    if (ids.empty()) {
        inventorySelection_ = 0;
        inventoryScroll_ = 0;
    } else {
        inventorySelection_ = std::clamp(inventorySelection_, 0, static_cast<int>(ids.size()) - 1);
        constexpr int kVisibleRows = 7;
        const bool upDown = inputDown(InputAction::Up);
        const bool downDown = inputDown(InputAction::Down);
        const bool useDown = inputDown(InputAction::Interact);

        if (upDown && !inventoryUpWasDown_) {
            inventorySelection_ = std::max(0, inventorySelection_ - 1);
        }
        if (downDown && !inventoryDownWasDown_) {
            inventorySelection_ = std::min(static_cast<int>(ids.size()) - 1, inventorySelection_ + 1);
        }
        if (useDown && !inventoryUseWasDown_) {
            useInventoryItem(ids[static_cast<std::size_t>(inventorySelection_)]);
        }

        inventoryUpWasDown_ = upDown;
        inventoryDownWasDown_ = downDown;
        inventoryUseWasDown_ = useDown;
        inventoryScroll_ = std::clamp(inventoryScroll_, 0, std::max(0, static_cast<int>(ids.size()) - kVisibleRows));
        if (inventorySelection_ < inventoryScroll_) {
            inventoryScroll_ = inventorySelection_;
        } else if (inventorySelection_ >= inventoryScroll_ + kVisibleRows) {
            inventoryScroll_ = inventorySelection_ - kVisibleRows + 1;
        }
    }
}

void Engine::useInventoryItem(const std::string& itemId)
{
    auto countIt = inventory_.find(itemId);
    if (countIt == inventory_.end() || countIt->second <= 0) {
        return;
    }

    const auto defIt = std::find_if(itemDefs_.begin(), itemDefs_.end(), [&itemId](const ItemDef& def) {
        return def.id == itemId;
    });
    if (defIt == itemDefs_.end()) {
        return;
    }

    const ItemDef& def = *defIt;
    bool consumed = false;
    const int value = std::max(1, def.value);
    switch (def.type) {
        case ItemDefType::Weapon: {
            GameProject project;
            if (loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr)) {
                const std::string& weaponId = def.targetId.empty() ? def.id : def.targetId;
                for (const WeaponDef& w : project.weaponDefs) {
                    if (w.id != weaponId) {
                        continue;
                    }
                    if (w.type == WeaponType::Melee) {
                        meleeWeapon_ = w;
                    } else {
                        rangedWeapon_ = w;
                    }
                    loadSpriteById(w.spriteId);
                    loadSpriteById(w.ammoSpriteId);
                    break;
                }
            }
            break;
        }
        case ItemDefType::Health:
            if (playerHealth_ < playerMaxHealth_) {
                playerHealth_ = std::min(playerMaxHealth_, playerHealth_ + value);
                consumed = true;
            }
            break;
        case ItemDefType::Mana:
            gameState_.addInt(def.targetId.empty() ? "Mana" : def.targetId, value);
            consumed = true;
            break;
        case ItemDefType::Ammo:
            // Ammo lives in the inventory and is consumed by firing the weapon,
            // so "using" it from the menu does nothing.
            break;
        case ItemDefType::Consumable:
            if (def.targetId == "Health" && playerHealth_ < playerMaxHealth_) {
                playerHealth_ = std::min(playerMaxHealth_, playerHealth_ + value);
                consumed = true;
            } else if (!def.targetId.empty()) {
                gameState_.addInt(def.targetId, value);
                consumed = true;
            }
            break;
        case ItemDefType::Currency:
        case ItemDefType::Key:
        case ItemDefType::Quest:
        case ItemDefType::Material:
        case ItemDefType::Equipment:
        case ItemDefType::Custom:
            break;
    }

    if (consumed) {
        (void)removeInventoryItem(itemId, 1);
        const int count = static_cast<int>(sortedInventoryIds().size());
        inventorySelection_ = std::clamp(inventorySelection_, 0, std::max(0, count - 1));
    }
}

void Engine::updatePlayer(float dt)
{
    if (interactionState_ == InteractionState::InDialogue) {
        if (playerActionType_ != "idle") {
            playerActionType_ = "idle";
            playerAnimSeconds_ = 0.0f;
        }
        playerIsMoving_ = false;
        return;
    }

    float dx = 0.0f;
    float dy = 0.0f;
    if (inputDown(InputAction::Left)) { dx -= 1.0f; }
    if (inputDown(InputAction::Right)) { dx += 1.0f; }
    if (inputDown(InputAction::Up)) { dy -= 1.0f; }
    if (inputDown(InputAction::Down)) { dy += 1.0f; }
    if (inputDown(InputAction::Exit)) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }

    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0f) {
        dx /= len;
        dy /= len;
        playerFacingX_ = dx;
        playerFacingY_ = dy;
    }

    if (!playerActionLocksBaseMotion()) {
        setPlayerActionState(len > 0.0f ? PlayerActionState::Walk : PlayerActionState::Idle);
    }
    const std::string newAction = playerActionName();
    if (newAction != playerActionType_) {
        playerActionType_ = newAction;
        playerAnimSeconds_ = 0.0f;
    }

    playerAnimSeconds_ += dt;
    playerIsMoving_ = (len > 0.0f);

    const float speedScale = playerActionLocksBaseMotion() ? kAttackMoveSpeedScale : 1.0f;
    const float newX = playerX_ + dx * kPlayerSpeedPxPerSecond * speedScale * dt;
    const float newY = playerY_ + dy * kPlayerSpeedPxPerSecond * speedScale * dt;
    if (playerCanOccupy(newX, playerY_)) {
        playerX_ = newX;
    }
    if (playerCanOccupy(playerX_, newY)) {
        playerY_ = newY;
    }

    // Apply and decay knockback from a recent hit (collision-checked per axis).
    if (playerKnockbackVx_ != 0.0f || playerKnockbackVy_ != 0.0f) {
        const float kx = playerX_ + playerKnockbackVx_ * dt;
        const float ky = playerY_ + playerKnockbackVy_ * dt;
        if (playerCanOccupy(kx, playerY_)) { playerX_ = kx; } else { playerKnockbackVx_ = 0.0f; }
        if (playerCanOccupy(playerX_, ky)) { playerY_ = ky; } else { playerKnockbackVy_ = 0.0f; }
        const float decay = std::exp(-kPlayerKnockbackDecayPerSecond * dt);
        playerKnockbackVx_ *= decay;
        playerKnockbackVy_ *= decay;
        if (std::abs(playerKnockbackVx_) < 1.0f) playerKnockbackVx_ = 0.0f;
        if (std::abs(playerKnockbackVy_) < 1.0f) playerKnockbackVy_ = 0.0f;
    }

    const float width = screenWidthPx();
    const float height = screenHeightPx();
    if (activeScreen_ == nullptr) {
        return;
    }

    const float half = kPlayerCollisionSizePx * 0.5f;
    const float exitDistance = kPlayerCollisionSizePx * kScreenTransitionExitRatio;
    const bool crossedWest = playerX_ - half <= -exitDistance;
    const bool crossedEast = playerX_ + half >= width + exitDistance;
    const bool crossedNorth = playerY_ - half <= -exitDistance;
    const bool crossedSouth = playerY_ + half >= height + exitDistance;

    bool transitioned = false;
    if (crossedWest && !activeScreen_->links.west.empty()) {
        transitioned = beginScreenTransition(activeScreen_->links.west, width - half, playerY_, -width, 0.0f);
    } else if (crossedEast && !activeScreen_->links.east.empty()) {
        transitioned = beginScreenTransition(activeScreen_->links.east, half, playerY_, width, 0.0f);
    } else if (crossedNorth && !activeScreen_->links.north.empty()) {
        transitioned = beginScreenTransition(activeScreen_->links.north, playerX_, height - half, 0.0f, -height);
    } else if (crossedSouth && !activeScreen_->links.south.empty()) {
        transitioned = beginScreenTransition(activeScreen_->links.south, playerX_, half, 0.0f, height);
    }

    if (transitioned) {
        return;
    }

    const bool canLeaveWest = !activeScreen_->links.west.empty();
    const bool canLeaveEast = !activeScreen_->links.east.empty();
    const bool canLeaveNorth = !activeScreen_->links.north.empty();
    const bool canLeaveSouth = !activeScreen_->links.south.empty();
    if (!canLeaveWest || crossedWest) {
        playerX_ = std::max(playerX_, half);
    }
    if (!canLeaveEast || crossedEast) {
        playerX_ = std::min(playerX_, width - half);
    }
    if (!canLeaveNorth || crossedNorth) {
        playerY_ = std::max(playerY_, half);
    }
    if (!canLeaveSouth || crossedSouth) {
        playerY_ = std::min(playerY_, height - half);
    }
}

void Engine::updateAttack(float dt)
{
    if (interactionState_ == InteractionState::InDialogue) {
        return;
    }
    meleeCooldownSeconds_ = std::max(0.0f, meleeCooldownSeconds_ - dt);
    rangedCooldownSeconds_ = std::max(0.0f, rangedCooldownSeconds_ - dt);
    meleeActiveSeconds_ = std::max(0.0f, meleeActiveSeconds_ - dt);

    if (playerActionSeconds_ > 0.0f) {
        playerActionSeconds_ = std::max(0.0f, playerActionSeconds_ - dt);
        if (playerActionSeconds_ <= 0.0f &&
            (playerActionState_ == PlayerActionState::MeleeAttack ||
                playerActionState_ == PlayerActionState::RangedAttack ||
                playerActionState_ == PlayerActionState::Hurt)) {
            setPlayerActionState(playerIsMoving_ ? PlayerActionState::Walk : PlayerActionState::Idle);
        }
    }

    const bool meleeInputDown = inputDown(InputAction::Melee);
    const bool rangedInputDown = inputDown(InputAction::Ranged);
    const bool meleePressed = meleeInputDown && !meleeInputWasDown_;
    const bool rangedPressed = rangedInputDown && !rangedInputWasDown_;
    meleeInputWasDown_ = meleeInputDown;
    rangedInputWasDown_ = rangedInputDown;

    if (meleeWeapon_.has_value() && meleePressed && meleeCooldownSeconds_ <= 0.0f &&
        !playerActionLocksBaseMotion()) {
        setPlayerActionState(PlayerActionState::MeleeAttack, kMeleeAttackSeconds);
        meleeCooldownSeconds_ = meleeWeapon_->attackCooldown;
        meleeActiveSeconds_ = kMeleeActiveSeconds;
        meleeElapsedSeconds_ = 0.0f;
        meleeHitEnemies_.clear();
    }

    // Active-frames hit window: the swing only connects between windup and recovery.
    // Each enemy is hit at most once per swing (tracked in meleeHitEnemies_).
    if (playerActionState_ == PlayerActionState::MeleeAttack) {
        meleeElapsedSeconds_ += dt;
        if (meleeElapsedSeconds_ >= kMeleeWindupSeconds &&
            meleeElapsedSeconds_ <= kMeleeWindupSeconds + kMeleeActiveWindowSeconds) {
            checkMeleeHits();
        }
    }

    if (rangedWeapon_.has_value() && rangedPressed && rangedCooldownSeconds_ <= 0.0f &&
        !playerActionLocksBaseMotion()) {
        const int available = ammoCountForWeapon(*rangedWeapon_);
        if (available >= rangedWeapon_->ammoPerShot) {
            setPlayerActionState(PlayerActionState::RangedAttack, kRangedAttackSeconds);
            consumeAmmoForWeapon(*rangedWeapon_, rangedWeapon_->ammoPerShot);
            rangedCooldownSeconds_ = rangedWeapon_->attackCooldown;
            RuntimeProjectile proj;
            const float offset = kPlayerCollisionSizePx * 0.5f + 2.0f;
            proj.x = playerX_ + playerFacingX_ * offset;
            proj.y = playerY_ + playerFacingY_ * offset;
            proj.vx = playerFacingX_ * rangedWeapon_->projectileSpeed;
            proj.vy = playerFacingY_ * rangedWeapon_->projectileSpeed;
            proj.maxDistance = rangedWeapon_->range;
            proj.damage = rangedWeapon_->damage;
            proj.spriteId = rangedWeapon_->ammoSpriteId.empty() ? rangedWeapon_->spriteId : rangedWeapon_->ammoSpriteId;
            proj.fromEnemy = false;
            proj.wallBehavior = rangedWeapon_->wallBehavior;
            proj.bouncesRemaining = kProjectileMaxBounces;
            projectiles_.push_back(proj);
        }
    }
}

void Engine::setPlayerActionState(PlayerActionState state, float durationSeconds)
{
    if (playerActionState_ == state && playerActionSeconds_ == durationSeconds) {
        return;
    }
    playerActionState_ = state;
    playerActionSeconds_ = std::max(0.0f, durationSeconds);

    const std::string actionName = playerActionName();
    if (playerActionType_ != actionName) {
        playerActionType_ = actionName;
        playerAnimSeconds_ = 0.0f;
    }
}

bool Engine::playerActionLocksBaseMotion() const
{
    return playerActionState_ == PlayerActionState::MeleeAttack ||
        playerActionState_ == PlayerActionState::RangedAttack ||
        playerActionState_ == PlayerActionState::Hurt ||
        playerActionState_ == PlayerActionState::Dead;
}

std::string Engine::playerActionName() const
{
    switch (playerActionState_) {
        case PlayerActionState::Walk:
            return "walk";
        case PlayerActionState::MeleeAttack:
            return "attack_1";
        case PlayerActionState::RangedAttack:
            return "cast";
        case PlayerActionState::Hurt:
            return "hit_react";
        case PlayerActionState::Dead:
            return "death";
        case PlayerActionState::Idle:
        default:
            return "idle";
    }
}

void Engine::checkMeleeHits()
{
    if (!meleeWeapon_.has_value()) {
        return;
    }
    const float reach = meleeWeapon_->range;
    for (RuntimePathEntity& entity : pathEntities_) {
        if (entity.pathHidden || entity.health <= 0 || entity.deathSeconds >= 0.0f) {
            continue;
        }
        if (meleeHitEnemies_.count(entity.path.id) > 0) {
            continue;  // already hit by this swing
        }
        const float ex = entity.x - playerX_;
        const float ey = entity.y - playerY_;
        const float dist = std::sqrt(ex * ex + ey * ey);
        if (dist > reach) {
            continue;
        }
        const float dot = (dist > 0.0f) ? (ex * playerFacingX_ + ey * playerFacingY_) / dist : 1.0f;
        if (dot < 0.3f) {
            continue;
        }
        meleeHitEnemies_.insert(entity.path.id);
        // Knockback away from the player (fall back to player facing if co-located).
        const float dirX = (dist > 0.0f) ? ex / dist : playerFacingX_;
        const float dirY = (dist > 0.0f) ? ey / dist : playerFacingY_;
        applyEnemyHit(entity, meleeWeapon_->damage, dirX, dirY);
    }
}

void Engine::applyEnemyHit(RuntimePathEntity& entity, int damage, float dirX, float dirY)
{
    if (entity.health <= 0 || entity.deathSeconds >= 0.0f) {
        return;
    }
    entity.health = std::max(0, entity.health - damage);
    entity.hitFlashSeconds = kEnemyHitFlashSeconds;

    const bool lethal = entity.health <= 0;
    const float resist = std::clamp(entity.path.combat.knockbackResistance, 0.0f, 1.0f);
    const float kb = kEnemyKnockbackBasePxPerSecond * (1.0f - resist);
    entity.knockbackVx = dirX * kb;
    entity.knockbackVy = dirY * kb;

    if (lethal) {
        entity.deathSeconds = 0.0f;
        if (entity.animState != "dead") {
            entity.animState = "dead";
            entity.animSeconds = 0.0f;
        }
    } else {
        entity.hitstunSeconds = std::max(0.0f, entity.path.combat.hitstunSeconds);
        if (entity.hitstunSeconds > 0.0f && entity.animState != "hurt") {
            entity.animState = "hurt";
            entity.animSeconds = 0.0f;
        }
    }
}

void Engine::updateProjectiles(float dt)
{
    const float playerHalf = kPlayerCollisionSizePx * 0.5f;
    for (RuntimeProjectile& proj : projectiles_) {
        if (proj.dead) {
            continue;
        }

        // Settled (grounded) projectiles are inert — rest briefly, then despawn.
        if (proj.settleSeconds >= 0.0f) {
            proj.settleSeconds += dt;
            if (proj.settleSeconds >= kProjectileSettleSeconds) {
                proj.dead = true;
            }
            continue;
        }

        const float step = std::sqrt(proj.vx * proj.vx + proj.vy * proj.vy) * dt;
        proj.distanceTraveled += step;

        // Move with per-axis wall collision so we know which face was struck.
        bool hitWall = false;
        const float nx = proj.x + proj.vx * dt;
        const float ny = proj.y + proj.vy * dt;
        if (solidAtPixel(nx, proj.y)) {
            hitWall = true;
            proj.vx = -proj.vx * kProjectileReboundRestitution;
        } else {
            proj.x = nx;
        }
        if (solidAtPixel(proj.x, ny)) {
            hitWall = true;
            proj.vy = -proj.vy * kProjectileReboundRestitution;
        } else {
            proj.y = ny;
        }

        if (hitWall) {
            if (proj.wallBehavior != ProjectileWallBehavior::Rebound) {
                proj.dead = true;  // arrow/bullet: vanish on impact
                continue;
            }
            // Rebounding stone: count the bounce, settle once it's spent or too slow.
            --proj.bouncesRemaining;
            const float speed = std::sqrt(proj.vx * proj.vx + proj.vy * proj.vy);
            if (proj.bouncesRemaining < 0 || speed < kProjectileMinReboundSpeed) {
                proj.vx = 0.0f;
                proj.vy = 0.0f;
                proj.settleSeconds = 0.0f;  // fall to the ground and rest
                continue;
            }
        }

        // Spent flight distance: rebounders settle, others expire.
        if (proj.distanceTraveled >= proj.maxDistance) {
            if (proj.wallBehavior == ProjectileWallBehavior::Rebound) {
                proj.vx = 0.0f;
                proj.vy = 0.0f;
                proj.settleSeconds = 0.0f;
            } else {
                proj.dead = true;
            }
            continue;
        }

        // Damage resolution depends on which team fired the projectile.
        const float vlen = std::sqrt(proj.vx * proj.vx + proj.vy * proj.vy);
        const float dirX = (vlen > 0.0f) ? proj.vx / vlen : 0.0f;
        const float dirY = (vlen > 0.0f) ? proj.vy / vlen : 0.0f;

        if (proj.fromEnemy) {
            if (proj.x + kProjectileHalfSize > playerX_ - playerHalf &&
                proj.x - kProjectileHalfSize < playerX_ + playerHalf &&
                proj.y + kProjectileHalfSize > playerY_ - playerHalf &&
                proj.y - kProjectileHalfSize < playerY_ + playerHalf) {
                damagePlayer(proj.damage, proj.x, proj.y);
                proj.dead = true;
            }
            continue;
        }

        for (RuntimePathEntity& entity : pathEntities_) {
            if (entity.pathHidden || entity.health <= 0 || entity.deathSeconds >= 0.0f) {
                continue;
            }
            const float halfW = entity.path.combat.hitboxWidth * 0.5f;
            const float halfH = entity.path.combat.hitboxHeight * 0.5f;
            if (proj.x + kProjectileHalfSize > entity.x - halfW &&
                proj.x - kProjectileHalfSize < entity.x + halfW &&
                proj.y + kProjectileHalfSize > entity.y - halfH &&
                proj.y - kProjectileHalfSize < entity.y + halfH) {
                applyEnemyHit(entity, proj.damage, dirX, dirY);
                proj.dead = true;
                break;
            }
        }
    }

    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(), [](const RuntimeProjectile& p) { return p.dead; }),
        projectiles_.end());
}

void Engine::updateItemPickups()
{
    for (RuntimeItemEntity& item : itemEntities_) {
        if (item.collected) {
            continue;
        }
        if (playerOverlapsItem(item)) {
            collectItem(item);
        }
    }
}

bool Engine::playerOverlapsItem(const RuntimeItemEntity& item) const
{
    const float dx = item.placement.x - playerX_;
    const float dy = item.placement.y - playerY_;
    return std::sqrt(dx * dx + dy * dy) <= kItemPickupRadius;
}

bool Engine::playerOverlapsDoor(const MapDoorPlacement& door) const
{
    return playerOverlapsDoorAt(door, playerX_, playerY_);
}

bool Engine::playerOverlapsDoorAt(const MapDoorPlacement& door, float playerX, float playerY) const
{
    const float x = static_cast<float>(door.x * kTileSize);
    const float y = static_cast<float>(door.y * kTileSize);
    const float w = static_cast<float>(std::max(1, door.widthTiles) * kTileSize);
    const float h = static_cast<float>(std::max(1, door.heightTiles) * kTileSize);
    const float half = kPlayerCollisionSizePx * 0.5f;
    return playerX + half >= x &&
        playerX - half <= x + w &&
        playerY + half >= y &&
        playerY - half <= y + h;
}

bool Engine::doorBlocksPlayerAt(const MapDoorPlacement& door, float x, float y) const
{
    if (doorIsUnlocked(door) || !playerOverlapsDoorAt(door, x, y)) {
        return false;
    }
    if (!playerOverlapsDoor(door)) {
        return true;
    }

    // Let old saves or edited spawn points escape a blocking door, but never move farther into it.
    const float centerX = static_cast<float>(door.x * kTileSize) +
        static_cast<float>(std::max(1, door.widthTiles) * kTileSize) * 0.5f;
    const float centerY = static_cast<float>(door.y * kTileSize) +
        static_cast<float>(std::max(1, door.heightTiles) * kTileSize) * 0.5f;
    const float currentDx = playerX_ - centerX;
    const float currentDy = playerY_ - centerY;
    const float nextDx = x - centerX;
    const float nextDy = y - centerY;
    return nextDx * nextDx + nextDy * nextDy <= currentDx * currentDx + currentDy * currentDy;
}

bool Engine::playerCanInteractWithDoor(const MapDoorPlacement& door) const
{
    if (doorIsUnlocked(door) && door.targetScreenId.empty()) {
        return false;
    }
    const float x = static_cast<float>(door.x * kTileSize);
    const float y = static_cast<float>(door.y * kTileSize);
    const float w = static_cast<float>(std::max(1, door.widthTiles) * kTileSize);
    const float h = static_cast<float>(std::max(1, door.heightTiles) * kTileSize);
    const float closestX = std::clamp(playerX_, x, x + w);
    const float closestY = std::clamp(playerY_, y, y + h);
    const float dx = playerX_ - closestX;
    const float dy = playerY_ - closestY;
    return playerOverlapsDoor(door) || std::sqrt(dx * dx + dy * dy) <= kItemPickupRadius;
}

int Engine::nearestInteractableDoor() const
{
    int nearest = -1;
    float nearestDist = 99999.0f;
    for (int i = 0; i < static_cast<int>(activeMap_.doors.size()); ++i) {
        const MapDoorPlacement& door = activeMap_.doors[static_cast<std::size_t>(i)];
        if (!playerCanInteractWithDoor(door)) {
            continue;
        }
        const float cx = static_cast<float>((door.x + std::max(1, door.widthTiles) * 0.5f) * kTileSize);
        const float cy = static_cast<float>((door.y + std::max(1, door.heightTiles) * 0.5f) * kTileSize);
        const float dx = playerX_ - cx;
        const float dy = playerY_ - cy;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < nearestDist) {
            nearest = i;
            nearestDist = dist;
        }
    }
    return nearest;
}

void Engine::showNotice(const std::string& text, float seconds)
{
    noticeText_ = text;
    noticeSeconds_ = std::max(0.0f, seconds);
}

bool Engine::activateDoor(const MapDoorPlacement& door)
{
    if (door.lockMode == DoorLockMode::Locked) {
        playSoundEffect(door.lockedSoundPath);
        showNotice("Locked");
        return false;
    }
    if (door.lockMode == DoorLockMode::RequiresItem) {
        if (!doorIsUnlocked(door) &&
            (door.requiredItemId.empty() || !hasInventoryItem(door.requiredItemId))) {
            playSoundEffect(door.lockedSoundPath);
            showNotice(door.requiredItemId.empty() ? "Needs a key" : "Needs " + door.requiredItemId);
            return false;
        }
        if (door.targetScreenId.empty()) {
            gameState_.setBool(doorStateId(door, "unlocked"), true);
            playSoundEffect(door.openSoundPath);
            if (door.consumeKey) {
                (void)removeInventoryItem(door.requiredItemId, 1);
            }
            saveRuntimeState();
            showNotice("Unlocked");
            endInteraction();
            return true;
        }
    }
    if (door.targetScreenId.empty()) {
        showNotice("No destination");
        return false;
    }

    const float spawnX = static_cast<float>(door.targetTileX * kTileSize + kTileSize / 2);
    const float spawnY = static_cast<float>(door.targetTileY * kTileSize + kTileSize / 2);
    float fromX = 0.0f;
    float fromY = 0.0f;
    if (std::abs(playerFacingX_) >= std::abs(playerFacingY_)) {
        fromX = playerFacingX_ >= 0.0f ? screenWidthPx() : -screenWidthPx();
    } else {
        fromY = playerFacingY_ >= 0.0f ? screenHeightPx() : -screenHeightPx();
    }

    if (!beginScreenTransition(door.targetScreenId, spawnX, spawnY, fromX, fromY)) {
        showNotice("Door is blocked");
        return false;
    }
    playSoundEffect(door.openSoundPath);
    pendingDoorCloseSoundPath_ = door.closeSoundPath;

    if (door.lockMode == DoorLockMode::RequiresItem && door.consumeKey) {
        (void)removeInventoryItem(door.requiredItemId, 1);
        saveRuntimeState();
    }
    endInteraction();
    return true;
}

std::string Engine::doorStateId(const MapDoorPlacement& door, const char* state) const
{
    const auto safeToken = [](std::string value) {
        for (char& c : value) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
                c = '_';
            }
        }
        return value.empty() ? std::string{"unknown"} : value;
    };
    return "door_" + safeToken(state == nullptr ? std::string{} : std::string{state}) + "." +
        safeToken(chapter_.id) + "." +
        safeToken(activeScreen_ == nullptr ? std::string{} : activeScreen_->id) + "." +
        safeToken(door.id);
}

bool Engine::doorIsUnlocked(const MapDoorPlacement& door) const
{
    return door.lockMode == DoorLockMode::FreeUse ||
        gameState_.getBool(doorStateId(door, "unlocked"), false);
}

bool Engine::doorIsHidden(const MapDoorPlacement& door) const
{
    return gameState_.getBool(doorStateId(door, "hidden"), false);
}

void Engine::updateDoors()
{
    for (const MapDoorPlacement& door : activeMap_.doors) {
        if (door.lockMode != DoorLockMode::RequiresItem || !doorIsUnlocked(door) ||
            !door.targetScreenId.empty() ||
            !door.openingAnimation.empty() || doorIsHidden(door) ||
            !playerOverlapsDoor(door)) {
            continue;
        }
        gameState_.setBool(doorStateId(door, "hidden"), true);
        saveRuntimeState();
    }
}

void Engine::playSoundEffect(const std::string& configuredPath)
{
    if (configuredPath.empty() || musicPlayer_ == nullptr) {
        return;
    }
    const std::filesystem::path path(configuredPath);
    const std::filesystem::path fullPath = path.is_absolute() ? path : projectRoot_ / path;
    std::string error;
    if (!musicPlayer_->playEffect(fullPath, &error)) {
        std::cerr << "Failed to play sound effect: " << error << "\n";
    }
}

void Engine::collectItem(RuntimeItemEntity& item)
{
    item.collected = true;
    switch (item.placement.pickupType) {
        case ItemPickupType::Weapon: {
            GameProject project;
            if (loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr)) {
                for (const WeaponDef& w : project.weaponDefs) {
                    if (w.id == item.placement.targetId) {
                        if (w.type == WeaponType::Melee) {
                            meleeWeapon_ = w;
                        } else {
                            rangedWeapon_ = w;
                        }
                        addInventoryItem(w.id, 1);
                        loadSpriteById(w.spriteId);
                        loadSpriteById(w.ammoSpriteId);
                        break;
                    }
                }
            }
            break;
        }
        case ItemPickupType::Ammo: {
            const std::string& ammoType = item.placement.targetId;
            if (!ammoType.empty()) {
                // Store ammo in the inventory under the matching ammo item id so it
                // appears in the Ammo section and can be fired.
                std::string invKey = ammoType;
                for (const ItemDef& def : itemDefs_) {
                    if (def.type == ItemDefType::Ammo && (def.id == ammoType || def.targetId == ammoType)) {
                        invKey = def.id;
                        break;
                    }
                }
                addInventoryItem(invKey, std::max(1, item.placement.quantity));
            }
            break;
        }
        case ItemPickupType::Health:
            playerHealth_ = std::min(playerMaxHealth_, playerHealth_ + item.placement.quantity);
            break;
        case ItemPickupType::ProjectItem: {
            const auto defIt = std::find_if(itemDefs_.begin(), itemDefs_.end(), [&item](const ItemDef& def) {
                return def.id == item.placement.targetId;
            });
            const int amount = std::max(1, item.placement.quantity);
            if (defIt == itemDefs_.end()) {
                if (!item.placement.targetId.empty()) {
                    addInventoryItem(item.placement.targetId, amount);
                }
                break;
            }

            const ItemDef& def = *defIt;
            addInventoryItem(def.id, def.stackable ? amount : 1);
            switch (def.type) {
                case ItemDefType::Weapon: {
                    GameProject project;
                    if (loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr)) {
                        const std::string& weaponId = def.targetId.empty() ? def.id : def.targetId;
                        for (const WeaponDef& w : project.weaponDefs) {
                            if (w.id != weaponId) {
                                continue;
                            }
                            if (w.type == WeaponType::Melee) {
                                meleeWeapon_ = w;
                            } else {
                                rangedWeapon_ = w;
                            }
                            loadSpriteById(w.spriteId);
                            loadSpriteById(w.ammoSpriteId);
                            break;
                        }
                    }
                    break;
                }
                case ItemDefType::Ammo:
                    // Already added to the inventory above; firing consumes it.
                    break;
                case ItemDefType::Health:
                    break;
                case ItemDefType::Currency:
                    gameState_.addInt(currencyStateId(def), amount * std::max(1, def.value));
                    break;
                case ItemDefType::Mana:
                case ItemDefType::Key:
                case ItemDefType::Quest:
                case ItemDefType::Consumable:
                case ItemDefType::Material:
                case ItemDefType::Equipment:
                case ItemDefType::Custom:
                    break;
            }
            loadSpriteById(def.spriteId);
            break;
        }
    }
    if (!item.placement.respawn) {
        gameState_.setBool(itemStateId(item.placement), true);
        saveRuntimeState();
    }
}

void Engine::loadNpcEntities()
{
    npcEntities_.clear();
    if (activeScreen_ == nullptr) {
        return;
    }

    GameProject project;
    (void)loadGameProject(assetPath(projectRoot_, "assets/game/project.adgame"), project, nullptr);

    auto typeForId = [&project](const std::string& id) -> const NpcTypeDef* {
        for (const NpcTypeDef& npc : project.npcTypes) {
            if (npc.id == id) {
                return &npc;
            }
        }
        return nullptr;
    };

    for (const NpcPlacement& placement : activeScreen_->npcs) {
        RuntimeNpcEntity entity;
        entity.placement = placement;
        entity.x = placement.x;
        entity.y = placement.y;
        static constexpr float kFacingX[] = {0.0f, 0.0f, 1.0f, -1.0f};
        static constexpr float kFacingY[] = {1.0f, -1.0f, 0.0f, 0.0f};
        const int facing = std::clamp(placement.facing, 0, 3);
        entity.facingX = kFacingX[facing];
        entity.facingY = kFacingY[facing];
        entity.graphId = placement.graphOverride;
        if (const NpcTypeDef* type = typeForId(placement.typeId)) {
            entity.interactionMode = type->defaultInteraction;
            entity.shopInventory = type->shopInventory;
            if (!placement.shopInventoryOverride.empty()) {
                entity.shopInventory = placement.shopInventoryOverride;
            }
            entity.spriteId = type->spriteId;
            if (entity.graphId.empty()) {
                entity.graphId = type->defaultGraphId;
            }
            if (entity.spriteId.empty()) {
                entity.spriteId = characterSpriteId(projectRoot_, type->characterId);
            }
            if (placement.speedOverride <= 0.0f) {
                entity.placement.speedOverride = type->defaultSpeed;
            }
            if (placement.movementOverride == NpcMovementMode::Stationary &&
                type->defaultMovement != NpcMovementMode::Stationary) {
                entity.placement.movementOverride = type->defaultMovement;
            }
            if (placement.dialogueOverride.empty()) {
                entity.dialogue = type->defaultDialogue;
            } else {
                entity.dialogue = placement.dialogueOverride;
            }
        } else if (!placement.dialogueOverride.empty()) {
            entity.dialogue = placement.dialogueOverride;
        }
        if (!placement.waypoints.empty()) {
            entity.x = placement.waypoints.front().x;
            entity.y = placement.waypoints.front().y;
            entity.waypointIndex = 0;
            entity.pathHidden = pathStartsHidden(placement.waypoints);
            applyNpcWaypointAction(entity, placement.waypoints.front());
        }
        if (!entity.graphId.empty()) {
            std::filesystem::path graphPath = assetPath(projectRoot_, "assets/game/dialogue") / chapter_.id / (entity.graphId + ".addialogue");
            if (!std::filesystem::exists(graphPath)) {
                graphPath = assetPath(projectRoot_, "assets/game/dialogue") / (entity.graphId + ".addialogue");
            }
            entity.hasGraph = loadDialogueGraph(graphPath, entity.graph, nullptr);
        }
        npcEntities_.push_back(std::move(entity));
    }
}

void Engine::applyEnemyWaypointAction(RuntimePathEntity& entity, const PathWaypoint& waypoint)
{
    entity.atWaypoint = true;
    entity.waitRemainingSeconds = std::max(0.0f, waypoint.waitSeconds);
    if (!waypoint.animState.empty() && entity.animState != waypoint.animState) {
        entity.animState = waypoint.animState;
        entity.animSeconds = 0.0f;
    }
    if (waypoint.facing >= 0) {
        static constexpr float kFacingX[] = {0.0f, 0.0f, 1.0f, -1.0f};
        static constexpr float kFacingY[] = {1.0f, -1.0f, 0.0f, 0.0f};
        const int facing = std::clamp(waypoint.facing, 0, 3);
        entity.facingX = kFacingX[facing];
        entity.facingY = kFacingY[facing];
    }
    switch (waypoint.action) {
        case PathWaypointAction::Enter:
            entity.pathHidden = false;
            break;
        case PathWaypointAction::Speak:
            entity.speechText = waypoint.speechText;
            entity.speechRemainingSeconds = std::max(0.0f, waypoint.speechDurationSeconds);
            entity.waitRemainingSeconds = std::max(entity.waitRemainingSeconds, entity.speechRemainingSeconds);
            break;
        case PathWaypointAction::Leave:
            entity.pathHidden = true;
            entity.pathFinished = true;
            break;
        case PathWaypointAction::None:
            break;
    }
}

void Engine::applyNpcWaypointAction(RuntimeNpcEntity& npc, const PathWaypoint& waypoint)
{
    npc.atWaypoint = true;
    npc.waitRemainingSeconds = std::max(0.0f, waypoint.waitSeconds);
    npc.actionType = waypoint.animState.empty() ? "idle" : waypoint.animState;
    if (waypoint.facing >= 0) {
        static constexpr float kFacingX[] = {0.0f, 0.0f, 1.0f, -1.0f};
        static constexpr float kFacingY[] = {1.0f, -1.0f, 0.0f, 0.0f};
        const int facing = std::clamp(waypoint.facing, 0, 3);
        npc.facingX = kFacingX[facing];
        npc.facingY = kFacingY[facing];
    }
    switch (waypoint.action) {
        case PathWaypointAction::Enter:
            npc.pathHidden = false;
            break;
        case PathWaypointAction::Speak:
            npc.speechText = waypoint.speechText;
            npc.speechRemainingSeconds = std::max(0.0f, waypoint.speechDurationSeconds);
            npc.waitRemainingSeconds = std::max(npc.waitRemainingSeconds, npc.speechRemainingSeconds);
            break;
        case PathWaypointAction::Leave:
            npc.pathHidden = true;
            npc.pathFinished = true;
            break;
        case PathWaypointAction::None:
            break;
    }
}

void Engine::updateNpcs(float dt)
{
    updateNpcAwareness();
    for (RuntimeNpcEntity& npc : npcEntities_) {
        npc.speechRemainingSeconds = std::max(0.0f, npc.speechRemainingSeconds - dt);
        if (npc.hidden) {
            continue;
        }
        npc.animSeconds += dt;
        if (npc.followingPlayer && interactionState_ != InteractionState::InDialogue) {
            const float dx = playerX_ - npc.x;
            const float dy = playerY_ - npc.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 28.0f) {
                npc.facingX = dx / dist;
                npc.facingY = dy / dist;
                npc.x += npc.facingX * 56.0f * dt;
                npc.y += npc.facingY * 56.0f * dt;
                npc.actionType = "walk";
            } else {
                npc.actionType = "idle";
            }
            continue;
        }
        if (npc.pathFinished) {
            npc.actionType = "idle";
            continue;
        }
        if (npc.playerInAwareness) {
            npc.actionType = "idle";
            continue;
        }
        if (npc.placement.movementOverride != NpcMovementMode::Patrol || npc.placement.waypoints.size() < 2) {
            npc.actionType = "idle";
            continue;
        }

        if (npc.atWaypoint) {
            if (npc.waitRemainingSeconds > 0.0f) {
                npc.waitRemainingSeconds -= dt;
                continue;
            }
            npc.atWaypoint = false;
            if (npc.waypointIndex + 1 < npc.placement.waypoints.size()) {
                ++npc.waypointIndex;
            } else if (npc.placement.loop) {
                npc.waypointIndex = 0;
                npc.pathDistance = 0.0f;
            } else {
                npc.pathFinished = true;
                npc.actionType = "idle";
                continue;
            }
        }

        npc.actionType = "walk";
        const PathWaypoint& target = npc.placement.waypoints[npc.waypointIndex];
        if (npc.placement.curveMode == PathCurveMode::Spline && npc.placement.waypoints.size() >= 3) {
            EnemyPath path;
            path.curveMode = npc.placement.curveMode;
            path.loop = npc.placement.loop;
            path.waypoints = npc.placement.waypoints;
            const float length = approximatePathLength(path);
            float targetDistance = pathDistanceToWaypoint(path, npc.waypointIndex);
            if (npc.placement.loop && npc.waypointIndex == 0 && npc.pathDistance > 0.0f) {
                targetDistance = length;
            }
            const float speed = target.speedOverride > 0.0f ? target.speedOverride
                : (npc.placement.speedOverride > 0.0f ? npc.placement.speedOverride : 32.0f);
            const float previousDistance = npc.pathDistance;
            npc.pathDistance = std::min(targetDistance, npc.pathDistance + speed * dt);
            const PathWaypoint point = pointAtDistance(path, npc.pathDistance >= length ? 0.0f : npc.pathDistance);
            const PathWaypoint previous = pointAtDistance(path, previousDistance >= length ? 0.0f : previousDistance);
            const float dx = point.x - previous.x;
            const float dy = point.y - previous.y;
            const float directionLength = std::sqrt(dx * dx + dy * dy);
            if (directionLength > 0.001f) {
                npc.facingX = dx / directionLength;
                npc.facingY = dy / directionLength;
            }
            npc.x = point.x;
            npc.y = point.y;
            if (npc.pathDistance >= targetDistance - 0.001f) {
                npc.x = target.x;
                npc.y = target.y;
                applyNpcWaypointAction(npc, target);
            }
            continue;
        }
        const float dx = target.x - npc.x;
        const float dy = target.y - npc.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= 1.0f) {
            npc.x = target.x;
            npc.y = target.y;
            applyNpcWaypointAction(npc, target);
            continue;
        }
        const float segSpeed = target.speedOverride > 0.0f ? target.speedOverride
            : (npc.placement.speedOverride > 0.0f ? npc.placement.speedOverride : 32.0f);
        const float step = std::min(dist, segSpeed * dt);
        npc.x += dx / dist * step;
        npc.y += dy / dist * step;
        npc.facingX = dx / dist;
        npc.facingY = dy / dist;
    }
}

void Engine::updateNpcAwareness()
{
    for (RuntimeNpcEntity& npc : npcEntities_) {
        if (npc.hidden || npc.pathHidden) {
            npc.playerInAwareness = false;
            continue;
        }
        const float dx = playerX_ - npc.x;
        const float dy = playerY_ - npc.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        npc.playerInAwareness = dist <= npc.placement.awarenessRadius;
    }
}

const DialogueNode* Engine::dialogueNodeById(const RuntimeNpcEntity& npc, const std::string& nodeId) const
{
    for (const DialogueNode& node : npc.graph.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

bool Engine::dialogueConditionPasses(const DialogueCondition& condition) const
{
    switch (condition.type) {
        case DialogueConditionType::Always:
            return true;
        case DialogueConditionType::IntCompare: {
            const int value = gameState_.getInt(condition.variableId, 0);
            switch (condition.op) {
                case DialogueCompareOp::Equal: return value == condition.intValue;
                case DialogueCompareOp::NotEqual: return value != condition.intValue;
                case DialogueCompareOp::Less: return value < condition.intValue;
                case DialogueCompareOp::LessOrEqual: return value <= condition.intValue;
                case DialogueCompareOp::Greater: return value > condition.intValue;
                case DialogueCompareOp::GreaterOrEqual: return value >= condition.intValue;
            }
            return false;
        }
        case DialogueConditionType::BoolEquals:
            return gameState_.getBool(condition.variableId, false) == condition.boolValue;
        case DialogueConditionType::HasItem:
            return gameState_.hasItem(condition.variableId) == condition.boolValue;
        case DialogueConditionType::HasMoney:
            return gameState_.getInt("Money", 0) >= condition.intValue;
    }
    return false;
}

void Engine::executeDialogueActions(const std::vector<DialogueAction>& actions, RuntimeNpcEntity& npc)
{
    for (const DialogueAction& action : actions) {
        switch (action.type) {
            case DialogueActionType::SetInt:
                gameState_.setInt(action.targetId, action.intValue);
                break;
            case DialogueActionType::AddInt:
                gameState_.addInt(action.targetId, action.intValue);
                break;
            case DialogueActionType::SetBool:
                gameState_.setBool(action.targetId, action.boolValue);
                break;
            case DialogueActionType::GiveItem:
                addInventoryItem(action.targetId, std::max(1, action.intValue));
                break;
            case DialogueActionType::TakeItem:
                if (!removeInventoryItem(action.targetId, std::max(1, action.intValue))) {
                    gameState_.takeItem(action.targetId);
                }
                break;
            case DialogueActionType::GiveMoney:
                gameState_.addInt("Money", action.intValue);
                break;
            case DialogueActionType::TakeMoney:
                gameState_.setInt("Money", std::max(0, gameState_.getInt("Money", 0) - action.intValue));
                break;
            case DialogueActionType::HealPlayer:
                playerHealth_ = std::min(playerMaxHealth_, playerHealth_ + std::max(0, action.intValue));
                break;
            case DialogueActionType::DamagePlayer:
                damagePlayer(action.intValue);
                break;
            case DialogueActionType::MoveNpc:
                npc.x = action.x;
                npc.y = action.y;
                npc.placement.x = action.x;
                npc.placement.y = action.y;
                break;
            case DialogueActionType::HideNpc:
                npc.hidden = true;
                break;
            case DialogueActionType::ShowNpc:
                npc.hidden = false;
                break;
            case DialogueActionType::FollowPlayer:
                npc.followingPlayer = true;
                break;
            case DialogueActionType::StopFollowingPlayer:
                npc.followingPlayer = false;
                break;
            case DialogueActionType::SetNpcAnimation:
                npc.actionType = action.textValue.empty() ? "idle" : action.textValue;
                break;
            case DialogueActionType::StartQuest:
                gameState_.setBool(action.targetId.empty() ? action.textValue : action.targetId, true);
                break;
            case DialogueActionType::CompleteQuest:
                gameState_.setBool(action.targetId.empty() ? action.textValue : action.targetId, true);
                break;
        }
    }
}

void Engine::endInteraction()
{
    interactionState_ = InteractionState::None;
    interactingNpcIndex_ = -1;
    dialogueLineIndex_ = 0;
    dialogueScrollLine_ = 0;
    dialogueGraphNodeId_.clear();
    dialogueGraphLine_ = {};
    dialogueGraphChoices_.clear();
    dialogueChoiceIndex_ = 0;
    shopPanel_ = 0;
    shopSelection_ = 0;
    shopScroll_[0] = 0;
    shopScroll_[1] = 0;
    shopBuyActive_ = false;
    shopBuyIndex_ = -1;
    shopBuyQuantity_ = 0;
}

int Engine::maxShopBuyQuantity(const RuntimeNpcEntity& npc, int index) const
{
    if (index < 0 || index >= static_cast<int>(npc.shopInventory.size())) {
        return 0;
    }
    const ShopItemDef& item = npc.shopInventory[static_cast<std::size_t>(index)];
    if (item.itemId.empty() || (!item.unlimited && item.quantity <= 0)) {
        return 0;
    }

    int maxQuantity = item.unlimited ? 99 : std::max(0, item.quantity);
    const int price = std::max(0, item.buyPrice);
    if (price > 0) {
        maxQuantity = std::min(maxQuantity, gameState_.getInt("Money", 0) / price);
    }
    return std::max(0, maxQuantity);
}

void Engine::beginShopPurchase(const RuntimeNpcEntity& npc, int index)
{
    const int maxQuantity = maxShopBuyQuantity(npc, index);
    if (maxQuantity <= 0) {
        return;
    }
    shopBuyActive_ = true;
    shopBuyIndex_ = index;
    shopBuyQuantity_ = std::min(1, maxQuantity);
}

void Engine::buyShopItem(RuntimeNpcEntity& npc, int index, int quantity)
{
    if (index < 0 || index >= static_cast<int>(npc.shopInventory.size())) {
        return;
    }
    ShopItemDef& item = npc.shopInventory[static_cast<std::size_t>(index)];
    const int maxQuantity = maxShopBuyQuantity(npc, index);
    quantity = std::clamp(quantity, 0, maxQuantity);
    if (quantity <= 0) {
        return;
    }

    const int price = std::max(0, item.buyPrice);
    const int money = gameState_.getInt("Money", 0);
    const int total = price * quantity;
    if (money < total) {
        return;
    }
    gameState_.setInt("Money", money - total);
    addInventoryItem(item.itemId, quantity);
    if (!item.unlimited) {
        item.quantity = std::max(0, item.quantity - quantity);
    }
}

void Engine::sellInventoryItem(const std::string& itemId)
{
    auto countIt = inventory_.find(itemId);
    if (countIt == inventory_.end() || countIt->second <= 0) {
        return;
    }
    int price = 1;
    if (interactingNpcIndex_ >= 0 && interactingNpcIndex_ < static_cast<int>(npcEntities_.size())) {
        const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
        const auto shopIt = std::find_if(npc.shopInventory.begin(), npc.shopInventory.end(), [&itemId](const ShopItemDef& item) {
            return item.itemId == itemId;
        });
        if (shopIt != npc.shopInventory.end()) {
            price = std::max(0, shopIt->sellPrice);
            ShopItemDef& stock = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)].shopInventory[static_cast<std::size_t>(std::distance(npc.shopInventory.begin(), shopIt))];
            if (!stock.unlimited) {
                stock.quantity += 1;
            }
        } else {
            const auto itemIt = std::find_if(itemDefs_.begin(), itemDefs_.end(), [&itemId](const ItemDef& item) {
                return item.id == itemId;
            });
            if (itemIt != itemDefs_.end()) {
                price = std::max(1, itemIt->value / 2);
            }
        }
    }
    if (removeInventoryItem(itemId, 1)) {
        gameState_.addInt("Money", price);
    }
}

void Engine::updateShopInput()
{
    if (interactingNpcIndex_ < 0 || interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
        endInteraction();
        return;
    }
    RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
    const std::vector<std::string> playerItems = sortedShopInventoryIds();
    constexpr int kVisibleRows = 16;
    const int activeCount = shopPanel_ == 0 ? static_cast<int>(npc.shopInventory.size()) : static_cast<int>(playerItems.size());
    shopSelection_ = activeCount <= 0 ? 0 : std::clamp(shopSelection_, 0, activeCount - 1);

    const bool upDown = inputDown(InputAction::Up);
    const bool downDown = inputDown(InputAction::Down);
    const bool leftDown = inputDown(InputAction::Left);
    const bool rightDown = inputDown(InputAction::Right);
    const bool useDown = inputDown(InputAction::Interact);
    const bool exitDown = inputDown(InputAction::Inventory);

    if (shopBuyActive_) {
        const int maxQuantity = maxShopBuyQuantity(npc, shopBuyIndex_);
        if (maxQuantity <= 0) {
            shopBuyActive_ = false;
            shopBuyIndex_ = -1;
            shopBuyQuantity_ = 0;
        } else {
            shopBuyQuantity_ = std::clamp(shopBuyQuantity_, 0, maxQuantity);
            if (leftDown && !shopLeftWasDown_) {
                shopBuyQuantity_ = std::max(0, shopBuyQuantity_ - 1);
            }
            if (rightDown && !shopRightWasDown_) {
                shopBuyQuantity_ = std::min(maxQuantity, shopBuyQuantity_ + 1);
            }
            if (useDown && !shopUseWasDown_) {
                buyShopItem(npc, shopBuyIndex_, shopBuyQuantity_);
                shopBuyActive_ = false;
                shopBuyIndex_ = -1;
                shopBuyQuantity_ = 0;
            }
            if (exitDown && !shopExitWasDown_) {
                shopBuyActive_ = false;
                shopBuyIndex_ = -1;
                shopBuyQuantity_ = 0;
            }
        }

        shopUpWasDown_ = upDown;
        shopDownWasDown_ = downDown;
        shopLeftWasDown_ = leftDown;
        shopRightWasDown_ = rightDown;
        shopUseWasDown_ = useDown;
        shopExitWasDown_ = exitDown;
        return;
    }

    if (upDown && !shopUpWasDown_ && activeCount > 0) {
        shopSelection_ = std::max(0, shopSelection_ - 1);
    }
    if (downDown && !shopDownWasDown_ && activeCount > 0) {
        shopSelection_ = std::min(activeCount - 1, shopSelection_ + 1);
    }
    if ((leftDown && !shopLeftWasDown_) || (rightDown && !shopRightWasDown_)) {
        shopPanel_ = shopPanel_ == 0 ? 1 : 0;
        shopSelection_ = 0;
    }
    if (useDown && !shopUseWasDown_) {
        if (shopPanel_ == 0) {
            beginShopPurchase(npc, shopSelection_);
        } else if (shopSelection_ >= 0 && shopSelection_ < static_cast<int>(playerItems.size())) {
            sellInventoryItem(playerItems[static_cast<std::size_t>(shopSelection_)]);
        }
    }
    if (exitDown && !shopExitWasDown_) {
        endInteraction();
    }
    shopUpWasDown_ = upDown;
    shopDownWasDown_ = downDown;
    shopLeftWasDown_ = leftDown;
    shopRightWasDown_ = rightDown;
    shopUseWasDown_ = useDown;
    shopExitWasDown_ = exitDown;
    shopScroll_[shopPanel_] = std::clamp(shopScroll_[shopPanel_], 0, std::max(0, activeCount - kVisibleRows));
    if (shopSelection_ < shopScroll_[shopPanel_]) {
        shopScroll_[shopPanel_] = shopSelection_;
    } else if (shopSelection_ >= shopScroll_[shopPanel_] + kVisibleRows) {
        shopScroll_[shopPanel_] = shopSelection_ - kVisibleRows + 1;
    }
}

void Engine::startDialogueGraph(const RuntimeNpcEntity& npc)
{
    dialogueGraphNodeId_ = npc.graph.startNodeId.empty() ? "start" : npc.graph.startNodeId;
    dialogueGraphLine_ = {};
    dialogueGraphChoices_.clear();
    dialogueChoiceIndex_ = 0;
    dialogueScrollLine_ = 0;
    advanceDialogueGraph();
}

void Engine::advanceDialogueGraph()
{
    if (interactingNpcIndex_ < 0 || interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
        endInteraction();
        return;
    }

    RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
    for (int guard = 0; guard < 128; ++guard) {
        const DialogueNode* node = dialogueNodeById(npc, dialogueGraphNodeId_);
        if (node == nullptr) {
            endInteraction();
            return;
        }

        switch (node->type) {
            case DialogueNodeType::Start:
                dialogueGraphNodeId_ = node->nextNodeId;
                break;
            case DialogueNodeType::Condition:
                dialogueGraphNodeId_ = dialogueConditionPasses(node->condition) ? node->nextNodeId : node->falseNodeId;
                break;
            case DialogueNodeType::Action:
                executeDialogueActions(node->actions, npc);
                dialogueGraphNodeId_ = node->nextNodeId;
                break;
            case DialogueNodeType::Dialogue:
            case DialogueNodeType::Choice: {
                dialogueGraphLine_ = {node->speaker, node->text};
                dialogueGraphChoices_.clear();
                for (const DialogueChoice& choice : node->choices) {
                    if (dialogueConditionPasses(choice.condition)) {
                        dialogueGraphChoices_.push_back(choice);
                    }
                }
                dialogueChoiceIndex_ = std::clamp(dialogueChoiceIndex_, 0, std::max(0, static_cast<int>(dialogueGraphChoices_.size()) - 1));
                dialogueScrollLine_ = 0;
                return;
            }
            case DialogueNodeType::End:
                endInteraction();
                return;
        }

        if (dialogueGraphNodeId_.empty()) {
            endInteraction();
            return;
        }
    }
    endInteraction();
}

void Engine::confirmDialogueGraph()
{
    if (interactingNpcIndex_ < 0 || interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
        endInteraction();
        return;
    }
    const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
    const DialogueNode* node = dialogueNodeById(npc, dialogueGraphNodeId_);
    if (node == nullptr) {
        endInteraction();
        return;
    }

    if (!dialogueGraphChoices_.empty()) {
        const int choiceIndex = std::clamp(dialogueChoiceIndex_, 0, static_cast<int>(dialogueGraphChoices_.size()) - 1);
        dialogueGraphNodeId_ = dialogueGraphChoices_[static_cast<std::size_t>(choiceIndex)].targetNodeId;
    } else {
        dialogueGraphNodeId_ = node->nextNodeId;
    }
    advanceDialogueGraph();
}

void Engine::updateInteraction()
{
    const bool interactDown = inputDown(InputAction::Interact);
    const bool interactPressed = interactDown && !interactInputWasDown_;
    interactInputWasDown_ = interactDown;
    const bool scrollUpDown = inputDown(InputAction::Up);
    const bool scrollDownDown = inputDown(InputAction::Down);
    const bool scrollUpPressed = scrollUpDown && !dialogueScrollUpWasDown_;
    const bool scrollDownPressed = scrollDownDown && !dialogueScrollDownWasDown_;
    dialogueScrollUpWasDown_ = scrollUpDown;
    dialogueScrollDownWasDown_ = scrollDownDown;

    switch (interactionState_) {
        case InteractionState::None: {
            int nearest = -1;
            float nearestDist = 99999.0f;
            for (int i = 0; i < static_cast<int>(npcEntities_.size()); ++i) {
                const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(i)];
                if (npc.hidden || npc.pathHidden ||
                    (npc.interactionMode != NpcInteractionMode::Shop && !npc.hasGraph && npc.dialogue.empty())) {
                    continue;
                }
                const float dx = playerX_ - npc.x;
                const float dy = playerY_ - npc.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist <= npc.placement.interactionRadius && dist < nearestDist) {
                    nearest = i;
                    nearestDist = dist;
                }
            }
            if (nearest >= 0) {
                interactionState_ = InteractionState::PromptVisible;
                interactingNpcIndex_ = nearest;
                interactingDoorIndex_ = -1;
                break;
            }
            const int nearestDoor = nearestInteractableDoor();
            if (nearestDoor >= 0) {
                interactionState_ = InteractionState::PromptVisible;
                interactingNpcIndex_ = -1;
                interactingDoorIndex_ = nearestDoor;
            }
            break;
        }
        case InteractionState::PromptVisible: {
            if (interactingDoorIndex_ >= 0) {
                if (interactingDoorIndex_ >= static_cast<int>(activeMap_.doors.size())) {
                    interactionState_ = InteractionState::None;
                    break;
                }
                const MapDoorPlacement& door = activeMap_.doors[static_cast<std::size_t>(interactingDoorIndex_)];
                if (!playerCanInteractWithDoor(door)) {
                    endInteraction();
                    break;
                }
                if (interactPressed) {
                    const bool opened = activateDoor(door);
                    if (!opened && interactionState_ == InteractionState::PromptVisible) {
                        interactionState_ = InteractionState::None;
                    }
                }
                break;
            }
            if (interactingNpcIndex_ >= 0) {
                if (interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
                    interactionState_ = InteractionState::None;
                    break;
                }
                const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
                const float dx = playerX_ - npc.x;
                const float dy = playerY_ - npc.y;
                if (std::sqrt(dx * dx + dy * dy) > npc.placement.interactionRadius) {
                    endInteraction();
                    break;
                }
                if (interactPressed) {
                    if (npc.interactionMode == NpcInteractionMode::Shop) {
                        interactionState_ = InteractionState::InShop;
                        shopPanel_ = 0;
                        shopSelection_ = 0;
                        shopScroll_[0] = 0;
                        shopScroll_[1] = 0;
                        shopUpWasDown_ = shopDownWasDown_ = shopLeftWasDown_ = shopRightWasDown_ = false;
                        shopUseWasDown_ = shopExitWasDown_ = false;
                        break;
                    }
                    interactionState_ = InteractionState::InDialogue;
                    dialogueLineIndex_ = 0;
                    dialogueScrollLine_ = 0;
                    if (npc.hasGraph) {
                        startDialogueGraph(npc);
                    }
                }
                break;
            }
            interactionState_ = InteractionState::None;
            break;
        }
        case InteractionState::InShop:
            updateShopInput();
            break;
        case InteractionState::InDialogue: {
            if (interactingNpcIndex_ < 0 ||
                interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
                endInteraction();
                break;
            }
            RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
            if (npc.hasGraph) {
                if (!dialogueGraphChoices_.empty() && (scrollUpPressed || scrollDownPressed)) {
                    const int maxChoice = static_cast<int>(dialogueGraphChoices_.size()) - 1;
                    dialogueChoiceIndex_ = std::clamp(dialogueChoiceIndex_ + (scrollDownPressed ? 1 : -1), 0, maxChoice);
                } else if (interactPressed) {
                    confirmDialogueGraph();
                } else if (scrollUpPressed || scrollDownPressed) {
                    const int wrappedLineCount = static_cast<int>(wrapText(dialogueGraphLine_.text, dialogueWrapChars(screenWidthPx()), 0).size());
                    const int maxScroll = std::max(0, wrappedLineCount - kDialogueVisibleLines);
                    dialogueScrollLine_ = std::clamp(dialogueScrollLine_ + (scrollDownPressed ? 1 : -1), 0, maxScroll);
                }
                break;
            }
            if (interactPressed) {
                ++dialogueLineIndex_;
                dialogueScrollLine_ = 0;
                if (dialogueLineIndex_ >= static_cast<int>(npc.dialogue.size())) {
                    endInteraction();
                }
            } else if (scrollUpPressed || scrollDownPressed) {
                const DialogueLine& line = npc.dialogue[static_cast<std::size_t>(dialogueLineIndex_)];
                const int wrappedLineCount = static_cast<int>(wrapText(line.text, dialogueWrapChars(screenWidthPx()), 0).size());
                const int maxScroll = std::max(0, wrappedLineCount - kDialogueVisibleLines);
                dialogueScrollLine_ = std::clamp(dialogueScrollLine_ + (scrollDownPressed ? 1 : -1), 0, maxScroll);
            }
            break;
        }
    }
}

void Engine::renderNpcs() const
{
    for (const RuntimeNpcEntity& npc : npcEntities_) {
        if (npc.hidden || npc.pathHidden) {
            continue;
        }
        auto spriteIt = loadedSprites_.find(npc.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            bool flipH = false;
            const SpriteFrameDef* frame = spriteFrameForNpc(spriteIt->second, npc, flipH);
            if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                const float drawW = static_cast<float>(frame->width);
                const float drawH = static_cast<float>(frame->height);
                renderTextureRegion(spriteIt->second.texture, npc.x - drawW * 0.5f, npc.y - drawH * 0.5f, drawW, drawH,
                    flipH ? u1 : u0, v0, flipH ? u0 : u1, v1);
                continue;
            }
        }
        renderFilledRect(npc.x - 6.0f, npc.y - 6.0f, 12.0f, 12.0f, 0.10f, 0.75f, 0.72f, 0.90f);
    }
}

void Engine::renderDoors() const
{
    for (const MapDoorPlacement& door : activeMap_.doors) {
        if (doorIsHidden(door)) {
            continue;
        }
        const float x = static_cast<float>(door.x * kTileSize);
        const float y = static_cast<float>(door.y * kTileSize);
        const float w = static_cast<float>(std::max(1, door.widthTiles) * kTileSize);
        const float h = static_cast<float>(std::max(1, door.heightTiles) * kTileSize);

        auto spriteIt = loadedSprites_.find(door.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
            if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                renderTextureRegion(spriteIt->second.texture, x, y, w, h, u0, v0, u1, v1);
                continue;
            }
        }

        if (door.lockMode == DoorLockMode::Locked) {
            renderFilledRect(x, y, w, h, 0.72f, 0.12f, 0.10f, 0.38f);
        } else if (door.lockMode == DoorLockMode::RequiresItem && !doorIsUnlocked(door)) {
            renderFilledRect(x, y, w, h, 0.15f, 0.34f, 0.86f, 0.34f);
        } else {
            renderFilledRect(x, y, w, h, 0.10f, 0.62f, 0.32f, 0.30f);
        }
    }
}

void Engine::renderInteractionPrompt() const
{
    if (interactionState_ != InteractionState::PromptVisible) {
        return;
    }
    const float pulse = 0.55f + 0.45f * std::sin(runtimeSeconds_ * 5.0f);
    const float size = 6.0f;
    if (interactingDoorIndex_ >= 0 && interactingDoorIndex_ < static_cast<int>(activeMap_.doors.size())) {
        const MapDoorPlacement& door = activeMap_.doors[static_cast<std::size_t>(interactingDoorIndex_)];
        const float x = static_cast<float>(door.x * kTileSize);
        const float y = static_cast<float>(door.y * kTileSize);
        const float w = static_cast<float>(std::max(1, door.widthTiles) * kTileSize);
        renderFilledRect(x + w * 0.5f - size * 0.5f, y - 8.0f, size, size, 0.25f, 0.90f, 0.35f, pulse);
        return;
    }
    if (interactingNpcIndex_ < 0 || interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
        return;
    }
    const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
    renderFilledRect(npc.x - size * 0.5f, npc.y - 22.0f, size, size, 0.25f, 0.90f, 0.35f, pulse);
}

void Engine::renderNotice() const
{
    if (noticeSeconds_ <= 0.0f || noticeText_.empty()) {
        return;
    }
    const float scale = 1.2f;
    const float padX = 10.0f;
    const float padY = 7.0f;
    const float w = std::min(screenWidthPx() - 16.0f, textWidth(noticeText_, scale) + padX * 2.0f);
    const float h = 26.0f;
    const float x = (screenWidthPx() - w) * 0.5f;
    const float y = 12.0f;
    const float alpha = std::min(1.0f, noticeSeconds_ / 0.35f);
    renderFilledRect(x, y, w, h, 0.03f, 0.04f, 0.05f, 0.88f * alpha);
    renderText(noticeText_, x + padX, y + padY, scale, 0.94f, 0.95f, 0.90f, alpha);
}

void Engine::renderSpeechBubble() const
{
    if ((interactionState_ != InteractionState::PromptVisible && interactionState_ != InteractionState::InDialogue) ||
        interactingNpcIndex_ < 0 ||
        interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
        return;
    }
    const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
    const DialogueLine* dialogue = nullptr;
    if (interactionState_ == InteractionState::InDialogue && npc.hasGraph) {
        dialogue = &dialogueGraphLine_;
    } else {
        const int lineIndex = interactionState_ == InteractionState::InDialogue ? dialogueLineIndex_ : 0;
        if (lineIndex >= 0 && lineIndex < static_cast<int>(npc.dialogue.size())) {
            dialogue = &npc.dialogue[static_cast<std::size_t>(lineIndex)];
        }
    }
    if (dialogue == nullptr || dialogue->text.empty()) {
        return;
    }

    std::vector<std::string> lines = wrapText(dialogue->text, 24, 3);
    if (lines.empty()) {
        return;
    }

    float maxTextW = 0.0f;
    for (const std::string& line : lines) {
        maxTextW = std::max(maxTextW, textWidth(line, kSpeechTextScale));
    }
    const float padX = 8.0f;
    const float padY = 6.0f;
    const float lineH = 10.0f * kSpeechTextScale;
    const float bubbleW = std::min(screenWidthPx() - 12.0f, maxTextW + padX * 2.0f);
    const float bubbleH = static_cast<float>(lines.size()) * lineH + padY * 2.0f;
    const float x = std::clamp(npc.x - bubbleW * 0.5f, 6.0f, screenWidthPx() - bubbleW - 6.0f);
    const float y = std::max(6.0f, npc.y - 48.0f - bubbleH);

    renderFilledRect(x + 1.0f, y + 1.0f, bubbleW, bubbleH, 0.0f, 0.0f, 0.0f, 0.35f);
    renderFilledRect(x, y, bubbleW, bubbleH, 0.98f, 0.98f, 0.92f, 0.95f);
    renderFilledRect(npc.x - 4.0f, y + bubbleH - 1.0f, 8.0f, 7.0f, 0.98f, 0.98f, 0.92f, 0.95f);

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        renderText(lines[static_cast<std::size_t>(i)], x + padX, y + padY + static_cast<float>(i) * lineH,
            kSpeechTextScale, 0.08f, 0.08f, 0.09f, 1.0f);
    }
}

void Engine::renderPathSpeechBubbles() const
{
    const auto renderBubble = [this](float actorX, float actorY, const std::string& text) {
        if (text.empty()) {
            return;
        }
        const std::vector<std::string> lines = wrapText(text, 28, 3);
        if (lines.empty()) {
            return;
        }
        constexpr float scale = 1.0f;
        constexpr float lineHeight = 12.0f;
        constexpr float padX = 7.0f;
        constexpr float padY = 5.0f;
        float maxTextWidth = 0.0f;
        for (const std::string& line : lines) {
            maxTextWidth = std::max(maxTextWidth, textWidth(line, scale));
        }
        const float bubbleWidth = std::min(screenWidthPx() - 12.0f, maxTextWidth + padX * 2.0f);
        const float bubbleHeight = static_cast<float>(lines.size()) * lineHeight + padY * 2.0f;
        const float x = std::clamp(actorX - bubbleWidth * 0.5f, 6.0f, screenWidthPx() - bubbleWidth - 6.0f);
        const float y = std::max(6.0f, actorY - 32.0f - bubbleHeight);
        renderFilledRect(x + 1.0f, y + 1.0f, bubbleWidth, bubbleHeight, 0.0f, 0.0f, 0.0f, 0.35f);
        renderFilledRect(x, y, bubbleWidth, bubbleHeight, 0.98f, 0.98f, 0.92f, 0.95f);
        renderFilledRect(actorX - 4.0f, y + bubbleHeight - 1.0f, 8.0f, 7.0f, 0.98f, 0.98f, 0.92f, 0.95f);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            renderText(lines[i], x + padX, y + padY + static_cast<float>(i) * lineHeight,
                scale, 0.10f, 0.12f, 0.14f, 1.0f);
        }
    };

    for (const RuntimePathEntity& entity : pathEntities_) {
        if (!entity.pathHidden && entity.speechRemainingSeconds > 0.0f) {
            renderBubble(entity.x, entity.y, entity.speechText);
        }
    }
    for (const RuntimeNpcEntity& npc : npcEntities_) {
        if (!npc.hidden && !npc.pathHidden && npc.speechRemainingSeconds > 0.0f) {
            renderBubble(npc.x, npc.y, npc.speechText);
        }
    }
}

void Engine::renderDialogueBox() const
{
    if (interactionState_ != InteractionState::InDialogue || interactingNpcIndex_ < 0 ||
        interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
        return;
    }
    const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
    const bool graphDialogue = npc.hasGraph;
    if (!graphDialogue && (dialogueLineIndex_ < 0 || dialogueLineIndex_ >= static_cast<int>(npc.dialogue.size()))) {
        return;
    }
    const DialogueLine& dialogue = graphDialogue ? dialogueGraphLine_ : npc.dialogue[static_cast<std::size_t>(dialogueLineIndex_)];

    const float sw = screenWidthPx();
    const float sh = screenHeightPx();
    const float boxH = graphDialogue && !dialogueGraphChoices_.empty() ? 124.0f : 96.0f;
    const float margin = 10.0f;
    const float boxY = sh - boxH - margin;
    const float padX = 10.0f;
    const float padY = 8.0f;
    const float lineH = 11.0f * kDialogueTextScale;
    const std::size_t maxChars = dialogueWrapChars(sw);
    const std::vector<std::string> lines = wrapText(dialogue.text, maxChars, 0);
    const int visibleTextLines = graphDialogue && !dialogueGraphChoices_.empty() ? 2 : kDialogueVisibleLines;
    const int maxScroll = std::max(0, static_cast<int>(lines.size()) - visibleTextLines);
    const int firstLine = std::clamp(dialogueScrollLine_, 0, maxScroll);

    renderFilledRect(margin, boxY, sw - margin * 2.0f, boxH, 0.04f, 0.05f, 0.07f, 0.92f);
    renderFilledRect(margin, boxY, sw - margin * 2.0f, 2.0f, 0.30f, 0.70f, 0.90f, 0.80f);

    float textY = boxY + padY;
    if (!dialogue.speaker.empty()) {
        renderText(dialogue.speaker, margin + padX, textY, kDialogueTextScale, 0.30f, 0.78f, 0.95f, 1.0f);
        textY += lineH;
    }

    for (int i = 0; i < visibleTextLines; ++i) {
        const int lineIndex = firstLine + i;
        if (lineIndex >= static_cast<int>(lines.size())) {
            break;
        }
        renderText(lines[static_cast<std::size_t>(lineIndex)], margin + padX, textY + static_cast<float>(i) * lineH,
            kDialogueTextScale, 0.86f, 0.88f, 0.90f, 1.0f);
    }

    if (graphDialogue && !dialogueGraphChoices_.empty()) {
        const float choiceY = boxY + boxH - 46.0f;
        const int firstChoice = std::clamp(dialogueChoiceIndex_ - 1, 0, std::max(0, static_cast<int>(dialogueGraphChoices_.size()) - 3));
        const int lastChoice = std::min(static_cast<int>(dialogueGraphChoices_.size()), firstChoice + 3);
        for (int i = firstChoice; i < lastChoice; ++i) {
            const float y = choiceY + static_cast<float>(i - firstChoice) * 14.0f;
            const bool selected = i == dialogueChoiceIndex_;
            if (selected) {
                renderFilledRect(margin + 6.0f, y - 1.0f, sw - margin * 2.0f - 20.0f, 12.0f, 0.18f, 0.32f, 0.42f, 0.88f);
            }
            renderText(selected ? "> " + dialogueGraphChoices_[static_cast<std::size_t>(i)].text
                                : "  " + dialogueGraphChoices_[static_cast<std::size_t>(i)].text,
                margin + padX, y, 1.25f, 0.92f, 0.94f, 0.96f, 1.0f);
        }
    }

    if (maxScroll > 0) {
        const float trackX = sw - margin - 9.0f;
        const float trackY = boxY + 10.0f;
        const float trackH = boxH - 26.0f;
        const float thumbH = std::max(10.0f, trackH * (static_cast<float>(visibleTextLines) / static_cast<float>(lines.size())));
        const float thumbY = trackY + (trackH - thumbH) * (static_cast<float>(firstLine) / static_cast<float>(maxScroll));
        renderFilledRect(trackX, trackY, 3.0f, trackH, 0.25f, 0.28f, 0.32f, 0.75f);
        renderFilledRect(trackX - 1.0f, thumbY, 5.0f, thumbH, 0.70f, 0.76f, 0.84f, 0.90f);
        if (firstLine > 0) {
            renderText("^", trackX - 4.0f, boxY + 1.0f, 1.0f, 0.80f, 0.84f, 0.88f, 0.95f);
        }
        if (firstLine < maxScroll) {
            renderText("v", trackX - 4.0f, boxY + boxH - 14.0f, 1.0f, 0.80f, 0.84f, 0.88f, 0.95f);
        }
    }

    if (graphDialogue) {
        return;
    }

    const int total = static_cast<int>(npc.dialogue.size());
    for (int i = 0; i < total; ++i) {
        const float dotX = sw - margin - 8.0f - static_cast<float>(total - 1 - i) * 8.0f;
        const float dotY = boxY + boxH - 12.0f;
        const bool current = i == dialogueLineIndex_;
        renderFilledRect(dotX, dotY, 4.0f, 4.0f,
            current ? 0.90f : 0.40f, current ? 0.90f : 0.40f, current ? 0.90f : 0.40f,
            current ? 0.95f : 0.50f);
    }
}

void Engine::updatePaths(float dt)
{
    for (RuntimePathEntity& entity : pathEntities_) {
        entity.speechRemainingSeconds = std::max(0.0f, entity.speechRemainingSeconds - dt);
        if (entity.deathSeconds >= 0.0f) {
            continue;  // dying entities are frozen; updateEnemyDeaths handles removal
        }
        // Decay hit-reaction timers for all living entities.
        entity.hitFlashSeconds = std::max(0.0f, entity.hitFlashSeconds - dt);
        entity.hitstunSeconds = std::max(0.0f, entity.hitstunSeconds - dt);

        // Apply and decay knockback displacement (collision-checked), even while stationary.
        if (entity.knockbackVx != 0.0f || entity.knockbackVy != 0.0f) {
            const float nx = entity.x + entity.knockbackVx * dt;
            const float ny = entity.y + entity.knockbackVy * dt;
            if (!solidAtPixel(nx, entity.y)) { entity.x = nx; } else { entity.knockbackVx = 0.0f; }
            if (!solidAtPixel(entity.x, ny)) { entity.y = ny; } else { entity.knockbackVy = 0.0f; }
            const float decay = std::exp(-kEnemyKnockbackDecayPerSecond * dt);
            entity.knockbackVx *= decay;
            entity.knockbackVy *= decay;
            if (std::abs(entity.knockbackVx) < 1.0f) entity.knockbackVx = 0.0f;
            if (std::abs(entity.knockbackVy) < 1.0f) entity.knockbackVy = 0.0f;
        }

        // Staggered enemies don't move or chase while in hitstun.
        if (entity.hitstunSeconds > 0.0f) {
            continue;
        }

        // Aggro: chase the player directly when within range (overrides waypoint following).
        const float aggroRange = entity.path.combat.aggroRange;
        if (!entity.pathHidden && aggroRange > 0.0f) {
            const float dxp = playerX_ - entity.x;
            const float dyp = playerY_ - entity.y;
            const float distP = std::sqrt(dxp * dxp + dyp * dyp);
            const bool wasAggro = entity.aggroActive;
            if (!entity.aggroActive && distP <= aggroRange) {
                entity.aggroActive = true;
            } else if (entity.aggroActive && distP > aggroRange * 1.3f) {  // hysteresis
                entity.aggroActive = false;
            }
            if (entity.aggroActive) {
                if (entity.animState.find("attack") == std::string::npos && entity.animState != "walk") {
                    entity.animState = "walk";
                    entity.animSeconds = 0.0f;
                }
                const float speed = entity.path.speed > 0.0f ? entity.path.speed : 64.0f;
                if (distP > 1.0f) {
                    const float step = std::min(distP, speed * dt);
                    const float nx = entity.x + dxp / distP * step;
                    const float ny = entity.y + dyp / distP * step;
                    if (!solidAtPixel(nx, entity.y)) entity.x = nx;
                    if (!solidAtPixel(entity.x, ny)) entity.y = ny;
                    entity.facingX = dxp / distP;
                    entity.facingY = dyp / distP;
                }
                continue;  // skip waypoint movement while chasing
            }
            // Lost aggro this frame: head back to the nearest point on the path, not a stale one.
            if (wasAggro && !entity.path.waypoints.empty()) {
                PathWaypoint nearest;
                entity.resumePathDistance = nearestPathDistance(entity.path, entity.x, entity.y, nearest);
                entity.returningToPath = true;
            }
        }

        // Walk back to the nearest path point after a chase, then resume normal patrol from there.
        if (entity.returningToPath) {
            if (entity.path.waypoints.empty()) {
                entity.returningToPath = false;
            } else {
                const PathWaypoint target = pointAtDistance(entity.path, entity.resumePathDistance);
                const float dx = target.x - entity.x;
                const float dy = target.y - entity.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float speed = entity.path.speed > 0.0f ? entity.path.speed : 64.0f;
                if (entity.animState.find("attack") == std::string::npos && entity.animState != "walk") {
                    entity.animState = "walk";
                    entity.animSeconds = 0.0f;
                }
                if (dist <= std::max(1.0f, speed * dt)) {
                    // Arrived: sync patrol bookkeeping to the nearest point and resume this frame.
                    entity.x = target.x;
                    entity.y = target.y;
                    entity.pathDistance = entity.resumePathDistance;
                    entity.waypointIndex = waypointIndexForDistance(entity.path, entity.resumePathDistance);
                    entity.atWaypoint = false;
                    entity.returningToPath = false;
                } else {
                    const float step = speed * dt;
                    const float nx = entity.x + dx / dist * step;
                    const float ny = entity.y + dy / dist * step;
                    if (!solidAtPixel(nx, entity.y)) entity.x = nx;
                    if (!solidAtPixel(entity.x, ny)) entity.y = ny;
                    entity.facingX = dx / dist;
                    entity.facingY = dy / dist;
                    continue;  // still returning to the path
                }
            }
        }

        if (entity.pathFinished || entity.path.behavior == PathBehavior::Idle || entity.path.waypoints.empty()) {
            if (entity.animState != "idle" && entity.animState.find("attack") == std::string::npos) {
                entity.animState = "idle";
                entity.animSeconds = 0.0f;
            }
            continue;
        }
        if (entity.atWaypoint) {
            if (entity.waitRemainingSeconds <= 0.0f &&
                entity.animState != "idle" && entity.animState.find("attack") == std::string::npos) {
                entity.animState = "idle";
                entity.animSeconds = 0.0f;
            }
            if (entity.waitRemainingSeconds > 0.0f) {
                entity.waitRemainingSeconds -= dt;
                continue;
            }
            entity.atWaypoint = false;
            if (entity.waypointIndex + 1 < entity.path.waypoints.size()) {
                // Departing waypoint 0 to begin a new lap: restart the arc-length
                // accumulator now that the closing segment has been travelled.
                if (entity.path.loop && entity.waypointIndex == 0 && entity.pathDistance > 0.0f) {
                    entity.pathDistance = 0.0f;
                }
                ++entity.waypointIndex;
            } else if (entity.path.loop) {
                // Wrap onto the closing segment (last -> first). Keep pathDistance so
                // the spline branch travels the closing arc at speed instead of
                // snapping the enemy straight back to the start.
                entity.waypointIndex = 0;
            } else {
                entity.pathFinished = true;
                continue;
            }
        }

        // Set walk state when moving (but don't override attack states)
        if (entity.animState.find("attack") == std::string::npos) {
            if (entity.animState != "walk") {
                entity.animState = "walk";
                entity.animSeconds = 0.0f;
            }
        }

        if (entity.path.curveMode == PathCurveMode::Spline && entity.path.waypoints.size() >= 3) {
            const float length = approximatePathLength(entity.path);
            if (length <= 0.0f) {
                continue;
            }
            const PathWaypoint& target = entity.path.waypoints[entity.waypointIndex];
            float targetDistance = pathDistanceToWaypoint(entity.path, entity.waypointIndex);
            if (entity.path.loop && entity.waypointIndex == 0 && entity.pathDistance > 0.0f) {
                targetDistance = length;
            }
            const float prevDistance = entity.pathDistance;
            const float segSpeed = target.speedOverride > 0.0f ? target.speedOverride : entity.path.speed;
            entity.pathDistance = std::min(targetDistance, entity.pathDistance + segSpeed * dt);
            // Derive facing direction from spline tangent (sample slightly ahead)
            constexpr float kTangentDelta = 2.0f;
            const float d1 = std::min(prevDistance, length);
            const float d2 = std::min(prevDistance + kTangentDelta, length);
            if (d2 > d1) {
                const PathWaypoint p1 = pointAtDistance(entity.path, d1);
                const PathWaypoint p2 = pointAtDistance(entity.path, d2);
                const float tdx = p2.x - p1.x;
                const float tdy = p2.y - p1.y;
                const float tlen = std::sqrt(tdx * tdx + tdy * tdy);
                if (tlen > 0.001f) {
                    entity.facingX = tdx / tlen;
                    entity.facingY = tdy / tlen;
                }
            }
            const PathWaypoint point = pointAtDistance(
                entity.path, entity.pathDistance >= length ? 0.0f : entity.pathDistance);
            entity.x = point.x;
            entity.y = point.y;
            if (entity.pathDistance >= targetDistance - 0.001f) {
                entity.x = target.x;
                entity.y = target.y;
                applyEnemyWaypointAction(entity, target);
            }
            continue;
        }
        const PathWaypoint& target = entity.path.waypoints[entity.waypointIndex];
        const float dx = target.x - entity.x;
        const float dy = target.y - entity.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= 1.0f) {
            entity.x = target.x;
            entity.y = target.y;
            applyEnemyWaypointAction(entity, target);
            continue;
        }
        const float segSpeed = target.speedOverride > 0.0f ? target.speedOverride : entity.path.speed;
        const float step = std::min(dist, segSpeed * dt);
        entity.x += dx / dist * step;
        entity.y += dy / dist * step;
        entity.facingX = dx / dist;
        entity.facingY = dy / dist;
    }
}

void Engine::updateHazards(float dt)
{
    // Tick down each obstacle's independent damage-rate timer.
    for (auto& [id, cooldown] : hazardCooldowns_) {
        cooldown = std::max(0.0f, cooldown - dt);
    }

    for (const MapObstacle& obstacle : activeMap_.obstacles) {
        if (obstacle.damage <= 0 || !obstacleIsActive(obstacle) || !playerOverlapsObstacle(obstacle)) {
            continue;
        }
        float& cooldown = hazardCooldowns_[obstacle.id];
        if (cooldown > 0.0f) {
            continue;
        }
        damagePlayer(obstacle.damage);
        cooldown = std::max(kMinHazardDamageInterval, obstacle.damageIntervalSeconds);
    }
}

void Engine::updateEnemyCombat(float dt)
{
    for (RuntimePathEntity& entity : pathEntities_) {
        // Advance animation timer for all entities
        entity.animSeconds += dt;

        entity.contactCooldownSeconds = std::max(0.0f, entity.contactCooldownSeconds - dt);

        // Tick per-attack cooldowns (resize if needed — e.g., first frame after load)
        if (entity.attackCooldowns.size() < entity.path.combat.attacks.size()) {
            entity.attackCooldowns.resize(entity.path.combat.attacks.size(), 0.0f);
        }
        for (float& cd : entity.attackCooldowns) {
            cd = std::max(0.0f, cd - dt);
        }

        // Staggered enemies cannot attack while in hitstun.
        if (entity.pathHidden || entity.health <= 0 ||
            entity.deathSeconds >= 0.0f || entity.hitstunSeconds > 0.0f) continue;

        // Compute vector from entity to player
        const float ex = playerX_ - entity.x;
        const float ey = playerY_ - entity.y;
        const float distToPlayer = std::sqrt(ex * ex + ey * ey);

        // Contact damage (overlap)
        if (entity.path.combat.contactDamage > 0 && entity.contactCooldownSeconds <= 0.0f && playerOverlapsEnemy(entity)) {
            damagePlayer(entity.path.combat.contactDamage, entity.x, entity.y);
            entity.contactCooldownSeconds = entity.path.combat.attackCooldownSeconds;
        }

        // Active attacks
        for (std::size_t ai = 0; ai < entity.path.combat.attacks.size(); ++ai) {
            const EnemyAttackDef& atk = entity.path.combat.attacks[ai];
            if (entity.attackCooldowns[ai] > 0.0f) continue;

            if (atk.type == EnemyAttackType::Melee) {
                if (distToPlayer <= atk.range) {
                    damagePlayer(atk.damage, entity.x, entity.y);
                    entity.attackCooldowns[ai] = atk.cooldown;
                    if (!atk.animState.empty() && entity.animState != atk.animState) {
                        entity.animState = atk.animState;
                        entity.animSeconds = 0.0f;
                    }
                }
            } else if (atk.type == EnemyAttackType::Ranged) {
                if (distToPlayer <= atk.range && distToPlayer > 0.0f) {
                    RuntimeProjectile proj;
                    proj.x = entity.x;
                    proj.y = entity.y;
                    proj.vx = (ex / distToPlayer) * atk.projectileSpeed;
                    proj.vy = (ey / distToPlayer) * atk.projectileSpeed;
                    proj.maxDistance = atk.range;
                    proj.damage = atk.damage;
                    proj.spriteId = atk.ammoSpriteId;
                    proj.fromEnemy = true;
                    proj.wallBehavior = ProjectileWallBehavior::Break;
                    projectiles_.push_back(proj);
                    entity.attackCooldowns[ai] = atk.cooldown;
                    if (!atk.animState.empty() && entity.animState != atk.animState) {
                        entity.animState = atk.animState;
                        entity.animSeconds = 0.0f;
                    }
                }
            }
        }
    }
}

void Engine::updateEnemyDeaths(float dt)
{
    bool anyRemoved = false;
    for (RuntimePathEntity& entity : pathEntities_) {
        if (entity.deathSeconds < 0.0f) {
            continue;
        }
        entity.deathSeconds += dt;
        if (entity.deathSeconds >= kEnemyDeathVisualSeconds && activeScreen_ != nullptr) {
            // Quest hook fires on every kill, regardless of respawn/persistence.
            if (!entity.path.combat.killVariable.empty()) {
                gameState_.addInt(entity.path.combat.killVariable, entity.path.combat.killAmount);
            }
            // Persist the defeat only when the enemy is not configured to respawn.
            if (!entity.path.respawn && !activeScreen_->respawnEnemies) {
                recordEnemyDefeated(activeScreen_->id, entity);
            }
            anyRemoved = true;
        }
    }
    if (anyRemoved) {
        pathEntities_.erase(
            std::remove_if(pathEntities_.begin(), pathEntities_.end(),
                [](const RuntimePathEntity& e) {
                    return e.deathSeconds >= kEnemyDeathVisualSeconds;
                }),
            pathEntities_.end());
    }
}

void Engine::saveRuntimeState() const
{
    if (freshStart_) {
        return;  // test launches must not clobber the player's real save
    }
    const std::filesystem::path savePath = assetPath(projectRoot_, "assets/game/save.adstate");
    (void)saveGameState(savePath, gameState_, nullptr);
}

void Engine::writeCheckpoint(const std::string& screenId, float x, float y) const
{
    // Lightweight "last entered screen" marker the editor reads for test launches.
    // Written in all modes (including fresh) so the test button follows the latest play.
    const std::filesystem::path path = assetPath(projectRoot_, "assets/game/test_checkpoint");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (out) {
        out << "screen " << screenId << "\n";
        out << "pos " << x << ' ' << y << "\n";
    }
}

void Engine::recordEnemyDefeated(const std::string& screenId, const RuntimePathEntity& entity)
{
    gameState_.markEnemyDefeated(screenId + "/" + entity.path.id);
}

bool Engine::beginScreenTransition(const std::string& targetScreenId, float spawnX, float spawnY, float fromX, float fromY)
{
    const ChapterScreen* targetScreen = findScreen(chapter_, targetScreenId);
    if (targetScreen == nullptr) {
        std::cerr << "Screen not found: " << targetScreenId << "\n";
        return false;
    }

    TileMap targetMap;
    std::string error;
    const std::filesystem::path mapPath = assetPath(projectRoot_, "assets/game/maps") / (targetScreen->mapId + ".admap");
    if (!loadTileMap(mapPath, targetMap, &error)) {
        std::cerr << "Failed to load map for screen " << targetScreenId << ": " << error << "\n";
        return false;
    }

    const float half = kPlayerCollisionSizePx * 0.5f;
    const float targetWidth = static_cast<float>(targetMap.width * kTileSize);
    const float targetHeight = static_cast<float>(targetMap.height * kTileSize);
    spawnX = std::clamp(spawnX, half, targetWidth - half);
    spawnY = std::clamp(spawnY, half, targetHeight - half);
    if (!playerCanOccupyInMap(targetMap, spawnX, spawnY)) {
        return false;
    }

    destroyTexture(prevFloorTexture_);
    destroyTexture(prevWallTexture_);
    prevFloorTexture_ = floorTexture_;
    prevWallTexture_ = wallTexture_;
    floorTexture_ = {};
    wallTexture_ = {};

    if (!loadScreen(targetScreenId, &error)) {
        std::cerr << error << "\n";
        return false;
    }
    playerX_ = spawnX;
    playerY_ = spawnY;
    transitionState_ = TransitionState::Sliding;
    transitionTime_ = 0.0f;
    transitionFromX_ = fromX;
    transitionFromY_ = fromY;
    transitionToX_ = 0.0f;
    transitionToY_ = 0.0f;
    saveRuntimeState();  // persist progress (defeated enemies, quest state) on each screen change
    writeCheckpoint(targetScreenId, spawnX, spawnY);  // record last-entered screen for test launches
    return true;
}

bool Engine::playerCanOccupy(float x, float y) const
{
    const float half = kPlayerCollisionSizePx * 0.5f;
    const bool mapIsClear = !solidAtPixel(x - half, y - half) &&
        !solidAtPixel(x + half, y - half) &&
        !solidAtPixel(x - half, y + half) &&
        !solidAtPixel(x + half, y + half);
    if (!mapIsClear) {
        return false;
    }
    return std::none_of(activeMap_.doors.begin(), activeMap_.doors.end(),
        [this, x, y](const MapDoorPlacement& door) {
            return doorBlocksPlayerAt(door, x, y);
        });
}

bool Engine::solidAtPixel(float x, float y) const
{
    return solidAtPixelInMap(activeMap_, x, y);
}

bool Engine::playerCanOccupyInMap(const TileMap& map, float x, float y) const
{
    const float half = kPlayerCollisionSizePx * 0.5f;
    return !solidAtPixelInMap(map, x - half, y - half) &&
        !solidAtPixelInMap(map, x + half, y - half) &&
        !solidAtPixelInMap(map, x - half, y + half) &&
        !solidAtPixelInMap(map, x + half, y + half);
}

bool Engine::solidAtPixelInMap(const TileMap& map, float x, float y) const
{
    const float width = static_cast<float>(map.width * kTileSize);
    const float height = static_cast<float>(map.height * kTileSize);
    if (x < 0.0f || y < 0.0f || x >= width || y >= height) {
        return false;
    }
    const int tileX = std::clamp(static_cast<int>(x) / kTileSize, 0, map.width - 1);
    const int tileY = std::clamp(static_cast<int>(y) / kTileSize, 0, map.height - 1);
    const std::size_t index = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(tileX);
    return map.layers[1][index] != 0u;
}

bool Engine::obstacleIsActive(const MapObstacle& obstacle) const
{
    if (obstacle.type != ObstacleType::TimedSpike) {
        return true;
    }
    const float cycle = std::max(0.05f, obstacle.activeSeconds + obstacle.inactiveSeconds);
    const float t = std::fmod(runtimeSeconds_ + obstacle.phaseSeconds, cycle);
    return t < obstacle.activeSeconds;
}

bool Engine::playerOverlapsObstacle(const MapObstacle& obstacle) const
{
    const float half = kPlayerCollisionSizePx * 0.5f;
    const float playerMinX = playerX_ - half;
    const float playerMinY = playerY_ - half;
    const float playerMaxX = playerX_ + half;
    const float playerMaxY = playerY_ + half;
    const float obstacleMinX = static_cast<float>(obstacle.x * kTileSize);
    const float obstacleMinY = static_cast<float>(obstacle.y * kTileSize);
    const float obstacleMaxX = static_cast<float>((obstacle.x + obstacle.width) * kTileSize);
    const float obstacleMaxY = static_cast<float>((obstacle.y + obstacle.height) * kTileSize);
    return playerMaxX > obstacleMinX && playerMaxY > obstacleMinY &&
        playerMinX < obstacleMaxX && playerMinY < obstacleMaxY;
}

bool Engine::playerOverlapsEnemy(const RuntimePathEntity& entity) const
{
    const float half = kPlayerCollisionSizePx * 0.5f;
    const float playerMinX = playerX_ - half;
    const float playerMinY = playerY_ - half;
    const float playerMaxX = playerX_ + half;
    const float playerMaxY = playerY_ + half;
    const float enemyHalfW = entity.path.combat.hitboxWidth * 0.5f;
    const float enemyHalfH = entity.path.combat.hitboxHeight * 0.5f;
    const float enemyMinX = entity.x - enemyHalfW;
    const float enemyMinY = entity.y - enemyHalfH;
    const float enemyMaxX = entity.x + enemyHalfW;
    const float enemyMaxY = entity.y + enemyHalfH;
    return playerMaxX > enemyMinX && playerMaxY > enemyMinY &&
        playerMinX < enemyMaxX && playerMinY < enemyMaxY;
}

void Engine::damagePlayer(int amount)
{
    // No directional source (hazards, scripted damage): no knockback.
    damagePlayer(amount, playerX_, playerY_);
}

void Engine::damagePlayer(int amount, float sourceX, float sourceY)
{
    if (amount <= 0 || playerInvulnerableSeconds_ > 0.0f) {
        return;
    }
    playerHealth_ = std::max(0, playerHealth_ - amount);
    playerInvulnerableSeconds_ = kPlayerDamageInvulnerableSeconds;
    playerHitFlashSeconds_ = kPlayerHitFlashSeconds;

    // Knockback away from the damage source (skipped when source == player position).
    const float dx = playerX_ - sourceX;
    const float dy = playerY_ - sourceY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > 0.001f) {
        playerKnockbackVx_ = dx / dist * kPlayerKnockbackPxPerSecond;
        playerKnockbackVy_ = dy / dist * kPlayerKnockbackPxPerSecond;
    }

    if (playerHealth_ <= 0) {
        setPlayerActionState(PlayerActionState::Dead);
        respawnPlayerAtMapSpawn();
        return;
    }
    setPlayerActionState(PlayerActionState::Hurt, 0.22f);
}

void Engine::respawnPlayerAtMapSpawn()
{
    playerX_ = static_cast<float>(activeMap_.spawnX * kTileSize + kTileSize / 2);
    playerY_ = static_cast<float>(activeMap_.spawnY * kTileSize + kTileSize / 2);
    playerHealth_ = playerMaxHealth_;
    playerInvulnerableSeconds_ = kPlayerDamageInvulnerableSeconds;
    playerKnockbackVx_ = 0.0f;
    playerKnockbackVy_ = 0.0f;
    setPlayerActionState(PlayerActionState::Idle);
}

float Engine::screenWidthPx() const
{
    return static_cast<float>(activeMap_.width * kTileSize);
}

float Engine::screenHeightPx() const
{
    return static_cast<float>(activeMap_.height * kTileSize);
}

void Engine::render()
{
    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(window_, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(0.03f, 0.035f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, screenWidthPx(), screenHeightPx(), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float tx = 0.0f;
    float ty = 0.0f;
    float transitionT = 0.0f;
    if (transitionState_ == TransitionState::Sliding) {
        transitionT = std::clamp(transitionTime_ / transitionDuration_, 0.0f, 1.0f);
        tx = transitionFromX_ + (transitionToX_ - transitionFromX_) * transitionT;
        ty = transitionFromY_ + (transitionToY_ - transitionFromY_) * transitionT;
    }

    if (transitionState_ == TransitionState::Sliding) {
        const float prevTx = -transitionFromX_ * transitionT;
        const float prevTy = -transitionFromY_ * transitionT;
        glPushMatrix();
        glTranslatef(prevTx, prevTy, 0.0f);
        if (prevFloorTexture_.id != 0) {
            renderTexture(prevFloorTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
        } else {
            renderFilledRect(0.0f, 0.0f, screenWidthPx(), screenHeightPx(), 0.12f, 0.18f, 0.20f, 1.0f);
        }
        if (prevWallTexture_.id != 0) {
            renderTexture(prevWallTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
        }
        glPopMatrix();
    }

    glPushMatrix();
    glTranslatef(tx, ty, 0.0f);

    if (floorTexture_.id != 0) {
        renderTexture(floorTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
    } else {
        renderFilledRect(0.0f, 0.0f, screenWidthPx(), screenHeightPx(), 0.12f, 0.18f, 0.20f, 1.0f);
    }

    // Floor-layer animated tiles sit above the floor but below entities/player.
    renderAnimatedTiles(0);
    renderDoors();

    for (int y = 0; y < activeMap_.height; ++y) {
        for (int x = 0; x < activeMap_.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(activeMap_.width) + static_cast<std::size_t>(x);
            if (activeMap_.layers[1][index] != 0u) {
                renderFilledRect(static_cast<float>(x * kTileSize), static_cast<float>(y * kTileSize),
                    static_cast<float>(kTileSize), static_cast<float>(kTileSize), 0.95f, 0.82f, 0.20f, 0.22f);
            }
        }
    }

    for (const RuntimePathEntity& entity : pathEntities_) {
        if (!entity.pathHidden && !entity.path.renderAboveWalls) {
            renderEnemyEntity(entity);
        }
    }

    for (const MapObstacle& obstacle : activeMap_.obstacles) {
        if (!obstacleIsActive(obstacle)) {
            continue;
        }

        const float x = static_cast<float>(obstacle.x * kTileSize);
        const float y = static_cast<float>(obstacle.y * kTileSize);
        const float w = static_cast<float>(obstacle.width * kTileSize);
        const float h = static_cast<float>(obstacle.height * kTileSize);

        auto spriteIt = loadedSprites_.find(obstacle.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
            if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                renderTextureRegion(spriteIt->second.texture, x, y, w, h, u0, v0, u1, v1);
            }
        }
    }

    renderItems();
    renderNpcs();
    renderInteractionPrompt();

    bool flipH = false;
    const SpriteFrameDef* pf = playerSpriteFrame(flipH);
    if (pf != nullptr && playerSprite_.texture.id != 0 &&
        playerSprite_.texture.width > 0 && playerSprite_.texture.height > 0) {
        const float tw = static_cast<float>(playerSprite_.texture.width);
        const float th = static_cast<float>(playerSprite_.texture.height);
        const float u0 = static_cast<float>(pf->x) / tw;
        const float v0 = static_cast<float>(pf->y) / th;
        const float u1 = static_cast<float>(pf->x + pf->width) / tw;
        const float v1 = static_cast<float>(pf->y + pf->height) / th;
        const float drawW = static_cast<float>(pf->width);
        const float drawH = static_cast<float>(pf->height);
        const bool hitFlash = playerHitFlashSeconds_ > 0.0f;
        renderTextureRegion(playerSprite_.texture,
            playerX_ - drawW * 0.5f, playerY_ - drawH * 0.5f, drawW, drawH,
            flipH ? u1 : u0, v0, flipH ? u0 : u1, v1,
            1.0f, hitFlash ? 0.18f : 1.0f, hitFlash ? 0.18f : 1.0f);
    } else {
        const bool hitFlash = playerHitFlashSeconds_ > 0.0f;
        renderFilledRect(playerX_ - kPlayerFallbackDrawSizePx * 0.5f, playerY_ - kPlayerFallbackDrawSizePx * 0.5f,
            kPlayerFallbackDrawSizePx, kPlayerFallbackDrawSizePx,
            hitFlash ? 1.0f : 0.20f, hitFlash ? 0.18f : 0.62f, hitFlash ? 0.18f : 1.0f, 1.0f);
    }

    renderMeleeFlash();
    renderProjectiles();

    if (wallTexture_.id != 0) {
        renderTexture(wallTexture_, 0.0f, 0.0f, screenWidthPx(), screenHeightPx());
    }

    // Overlay-layer animated tiles draw above the walls (player walks behind them).
    renderAnimatedTiles(1);

    for (const RuntimePathEntity& entity : pathEntities_) {
        if (!entity.pathHidden && entity.path.renderAboveWalls) {
            renderEnemyEntity(entity);
        }
    }

    for (const RuntimePathEntity& entity : pathEntities_) {
        if (entity.pathHidden || entity.path.combat.maxHealth <= 1 || entity.deathSeconds >= 0.0f) {
            continue;
        }
        const float barW = std::max(8.0f, entity.path.combat.hitboxWidth);
        const float barH = 2.0f;
        const float pct = std::clamp(static_cast<float>(entity.health) / static_cast<float>(entity.path.combat.maxHealth), 0.0f, 1.0f);
        renderFilledRect(entity.x - barW * 0.5f, entity.y - entity.path.combat.hitboxHeight * 0.5f - 5.0f, barW, barH, 0.08f, 0.08f, 0.08f, 0.85f);
        renderFilledRect(entity.x - barW * 0.5f, entity.y - entity.path.combat.hitboxHeight * 0.5f - 5.0f, barW * pct, barH, 0.95f, 0.20f, 0.16f, 0.95f);
    }

    renderPathSpeechBubbles();
    renderSpeechBubble();

    renderHud();
    renderInventory();

    glPopMatrix();

    renderNotice();
    renderDialogueBox();
    renderShopMenu();
}

void Engine::renderTexture(const Texture& texture, float x, float y, float width, float height) const
{
    renderTextureRegion(texture, x, y, width, height, 0.0f, 0.0f, 1.0f, 1.0f);
}

void Engine::renderEnemyEntity(const RuntimePathEntity& entity) const
{
    const bool dying = entity.deathSeconds >= 0.0f;
    const float deathAlpha = dying
        ? 1.0f - std::clamp(entity.deathSeconds / kEnemyDeathVisualSeconds, 0.0f, 1.0f)
        : 1.0f;

    auto spriteIt = loadedSprites_.find(entity.path.spriteId);
    if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
        bool flipH = false;
        const SpriteFrameDef* frame = spriteFrameForEntity(spriteIt->second, entity, flipH);
        if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
            const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
            const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
            const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
            const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
            const float drawW = static_cast<float>(frame->width);
            const float drawH = static_cast<float>(frame->height);
            renderTextureRegion(spriteIt->second.texture, entity.x - drawW * 0.5f, entity.y - drawH * 0.5f, drawW, drawH,
                flipH ? u1 : u0, v0, flipH ? u0 : u1, v1);
            if (dying) {
                renderFilledRect(entity.x - drawW * 0.5f, entity.y - drawH * 0.5f,
                    drawW, drawH, 1.0f, 1.0f, 1.0f, deathAlpha * 0.6f);
            } else if (entity.hitFlashSeconds > 0.0f) {
                const float flash = std::clamp(entity.hitFlashSeconds / kEnemyHitFlashSeconds, 0.0f, 1.0f);
                renderFilledRect(entity.x - drawW * 0.5f, entity.y - drawH * 0.5f,
                    drawW, drawH, 1.0f, 1.0f, 1.0f, flash * 0.7f);
            }
            return;
        }
    }
    renderFilledRect(entity.x - 4.0f, entity.y - 4.0f, 8.0f, 8.0f, 0.90f, 0.18f, 0.14f, dying ? deathAlpha : 1.0f);
}

void Engine::renderTextureRegion(const Texture& texture, float x, float y, float width, float height,
    float u0, float v0, float u1, float v1, float r, float g, float b, float a) const
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u1, v0); glVertex2f(x + width, y);
    glTexCoord2f(u1, v1); glVertex2f(x + width, y + height);
    glTexCoord2f(u0, v1); glVertex2f(x, y + height);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

std::string Engine::directionFromFacing(float fx, float fy)
{
    const float angle = std::atan2(fy, fx) * (180.0f / 3.14159265358979f);
    if (angle > -22.5f  && angle <=  22.5f) return "E";
    if (angle >  22.5f  && angle <=  67.5f) return "SE";
    if (angle >  67.5f  && angle <= 112.5f) return "S";
    if (angle > 112.5f  && angle <= 157.5f) return "SW";
    if (angle >  157.5f || angle <= -157.5f) return "W";
    if (angle > -157.5f && angle <= -112.5f) return "NW";
    if (angle > -112.5f && angle <=  -67.5f) return "N";
    return "NE";
}

const SpriteFrameDef* Engine::playerSpriteFrame(bool& flipHorizontal) const
{
    flipHorizontal = false;
    if (playerSprite_.metadata.frames.empty()) return nullptr;

    const std::string& action = playerActionType_;
    const std::string dir = directionFromFacing(playerFacingX_, playerFacingY_);

    // Mirror pair: flip East frames when facing these directions
    const std::string mirrorOf = (dir == "W") ? "E" : (dir == "NW") ? "NE" : (dir == "SW") ? "SE" : "";

    auto collectByDir = [&](const std::string& actionName, const std::string& targetDir) -> std::vector<const SpriteFrameDef*> {
        std::vector<const SpriteFrameDef*> out;
        for (const SpriteFrameDef& frame : playerSprite_.metadata.frames) {
            if (frame.type == actionName && (frame.direction.empty() || frame.direction == targetDir)) {
                out.push_back(&frame);
            }
        }
        return out;
    };

    std::vector<const SpriteFrameDef*> candidates = collectByDir(action, dir);

    if (candidates.empty() && !mirrorOf.empty()) {
        candidates = collectByDir(action, mirrorOf);
        if (!candidates.empty()) flipHorizontal = true;
    }

    // Fall back to any frame of this action type
    if (candidates.empty()) {
        for (const SpriteFrameDef& frame : playerSprite_.metadata.frames) {
            if (frame.type == action) candidates.push_back(&frame);
        }
    }

    if (candidates.empty() && action != "idle") {
        candidates = collectByDir("idle", dir);
        if (candidates.empty() && !mirrorOf.empty()) {
            candidates = collectByDir("idle", mirrorOf);
            if (!candidates.empty()) flipHorizontal = true;
        }
        if (candidates.empty()) {
            for (const SpriteFrameDef& frame : playerSprite_.metadata.frames) {
                if (frame.type == "idle") candidates.push_back(&frame);
            }
        }
    }

    if (candidates.empty()) return &playerSprite_.metadata.frames.front();

    int totalMs = 0;
    for (const SpriteFrameDef* f : candidates) totalMs += std::max(1, f->durationMs);

    int t = static_cast<int>(std::fmod(playerAnimSeconds_ * 1000.0f, static_cast<float>(totalMs)));
    for (const SpriteFrameDef* f : candidates) {
        t -= std::max(1, f->durationMs);
        if (t < 0) return f;
    }
    return candidates.back();
}

const SpriteFrameDef* Engine::spriteFrame(const RuntimeSprite& sprite) const
{
    if (sprite.metadata.frames.empty()) {
        return nullptr;
    }
    const int totalMs = std::accumulate(sprite.metadata.frames.begin(), sprite.metadata.frames.end(), 0, [](int total, const SpriteFrameDef& frame) {
        return total + std::max(1, frame.durationMs);
    });
    if (totalMs <= 0) {
        return &sprite.metadata.frames.front();
    }
    int t = static_cast<int>(std::fmod(runtimeSeconds_ * 1000.0f, static_cast<float>(totalMs)));
    for (const SpriteFrameDef& frame : sprite.metadata.frames) {
        t -= std::max(1, frame.durationMs);
        if (t < 0) {
            return &frame;
        }
    }
    return &sprite.metadata.frames.back();
}

const SpriteFrameDef* Engine::spriteFrameForEntity(const RuntimeSprite& sprite, const RuntimePathEntity& entity, bool& flipHorizontal) const
{
    flipHorizontal = false;
    if (sprite.metadata.frames.empty()) return nullptr;

    const std::string& action = entity.animState;
    const std::string dir = directionFromFacing(entity.facingX, entity.facingY);
    const std::string mirrorOf = (dir == "W") ? "E" : (dir == "NW") ? "NE" : (dir == "SW") ? "SE" : "";

    auto collectByDir = [&](const std::string& actionName, const std::string& targetDir) -> std::vector<const SpriteFrameDef*> {
        std::vector<const SpriteFrameDef*> out;
        for (const SpriteFrameDef& f : sprite.metadata.frames) {
            if (f.type == actionName && (f.direction.empty() || f.direction == targetDir)) {
                out.push_back(&f);
            }
        }
        return out;
    };

    std::vector<const SpriteFrameDef*> candidates = collectByDir(action, dir);
    if (candidates.empty() && !mirrorOf.empty()) {
        candidates = collectByDir(action, mirrorOf);
        if (!candidates.empty()) flipHorizontal = true;
    }
    // Fall back to any frame of this action (no direction filter)
    if (candidates.empty()) {
        for (const SpriteFrameDef& f : sprite.metadata.frames) {
            if (f.type == action) candidates.push_back(&f);
        }
    }
    // Fall back to "idle" direction-aware, then any idle
    if (candidates.empty() && action != "idle") {
        flipHorizontal = false;
        candidates = collectByDir("idle", dir);
        if (candidates.empty() && !mirrorOf.empty()) {
            candidates = collectByDir("idle", mirrorOf);
            if (!candidates.empty()) flipHorizontal = true;
        }
        if (candidates.empty()) {
            for (const SpriteFrameDef& f : sprite.metadata.frames) {
                if (f.type == "idle") candidates.push_back(&f);
            }
        }
    }
    // Last resort: any frame
    if (candidates.empty()) {
        for (const SpriteFrameDef& f : sprite.metadata.frames) candidates.push_back(&f);
    }
    if (candidates.empty()) return &sprite.metadata.frames.front();

    int totalMs = 0;
    for (const SpriteFrameDef* f : candidates) totalMs += std::max(1, f->durationMs);
    if (totalMs <= 0) return candidates.front();

    int t = static_cast<int>(std::fmod(entity.animSeconds * 1000.0f, static_cast<float>(totalMs)));
    for (const SpriteFrameDef* f : candidates) {
        t -= std::max(1, f->durationMs);
        if (t < 0) return f;
    }
    return candidates.back();
}

const SpriteFrameDef* Engine::spriteFrameForNpc(const RuntimeSprite& sprite, const RuntimeNpcEntity& npc, bool& flipHorizontal) const
{
    flipHorizontal = false;
    if (sprite.metadata.frames.empty()) return nullptr;

    const std::string& action = npc.actionType;
    const std::string dir = directionFromFacing(npc.facingX, npc.facingY);
    const std::string mirrorOf = (dir == "W") ? "E" : (dir == "NW") ? "NE" : (dir == "SW") ? "SE" : "";

    auto collectByDir = [&](const std::string& actionName, const std::string& targetDir) -> std::vector<const SpriteFrameDef*> {
        std::vector<const SpriteFrameDef*> out;
        for (const SpriteFrameDef& f : sprite.metadata.frames) {
            if (f.type == actionName && (f.direction.empty() || f.direction == targetDir)) {
                out.push_back(&f);
            }
        }
        return out;
    };

    std::vector<const SpriteFrameDef*> candidates = collectByDir(action, dir);
    if (candidates.empty() && !mirrorOf.empty()) {
        candidates = collectByDir(action, mirrorOf);
        if (!candidates.empty()) flipHorizontal = true;
    }
    if (candidates.empty()) {
        for (const SpriteFrameDef& f : sprite.metadata.frames) {
            if (f.type == action) candidates.push_back(&f);
        }
    }
    if (candidates.empty() && action != "idle") {
        flipHorizontal = false;
        candidates = collectByDir("idle", dir);
        if (candidates.empty() && !mirrorOf.empty()) {
            candidates = collectByDir("idle", mirrorOf);
            if (!candidates.empty()) flipHorizontal = true;
        }
        if (candidates.empty()) {
            for (const SpriteFrameDef& f : sprite.metadata.frames) {
                if (f.type == "idle") candidates.push_back(&f);
            }
        }
    }
    if (candidates.empty()) {
        for (const SpriteFrameDef& f : sprite.metadata.frames) candidates.push_back(&f);
    }
    if (candidates.empty()) return &sprite.metadata.frames.front();

    int totalMs = 0;
    for (const SpriteFrameDef* f : candidates) totalMs += std::max(1, f->durationMs);
    if (totalMs <= 0) return candidates.front();

    int t = static_cast<int>(std::fmod(npc.animSeconds * 1000.0f, static_cast<float>(totalMs)));
    for (const SpriteFrameDef* f : candidates) {
        t -= std::max(1, f->durationMs);
        if (t < 0) return f;
    }
    return candidates.back();
}

void Engine::renderFilledRect(float x, float y, float width, float height, float r, float g, float b, float a) const
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

float Engine::textWidth(const std::string& text, float scale) const
{
    if (font_.loaded) {
        float width = 0.0f;
        for (unsigned char c : text) {
            if (c < 32 || c > 127) {
                width += 4.0f * scale;
                continue;
            }
            width += font_.chars[static_cast<std::size_t>(c - 32)].xadvance * scale;
        }
        return width;
    }
    return static_cast<float>(text.size()) * 6.0f * scale;
}

void Engine::renderText(const std::string& text, float x, float y, float scale, float r, float g, float b, float a) const
{
    if (font_.loaded && font_.texture.id != 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, font_.texture.id);
        glColor4f(r, g, b, a);
        glBegin(GL_QUADS);
        float penX = x;
        for (unsigned char c : text) {
            if (c < 32 || c > 127) {
                penX += 4.0f * scale;
                continue;
            }
            const RuntimeFont::BakedChar& ch = font_.chars[static_cast<std::size_t>(c - 32)];
            const float x0 = penX + ch.xoff * scale;
            const float y0 = y + ch.yoff * scale + font_.pixelHeight * scale;
            const float x1 = x0 + static_cast<float>(ch.x1 - ch.x0) * scale;
            const float y1 = y0 + static_cast<float>(ch.y1 - ch.y0) * scale;
            const float u0 = static_cast<float>(ch.x0) / static_cast<float>(font_.texture.width);
            const float v0 = static_cast<float>(ch.y0) / static_cast<float>(font_.texture.height);
            const float u1 = static_cast<float>(ch.x1) / static_cast<float>(font_.texture.width);
            const float v1 = static_cast<float>(ch.y1) / static_cast<float>(font_.texture.height);
            glTexCoord2f(u0, v0); glVertex2f(x0, y0);
            glTexCoord2f(u1, v0); glVertex2f(x1, y0);
            glTexCoord2f(u1, v1); glVertex2f(x1, y1);
            glTexCoord2f(u0, v1); glVertex2f(x0, y1);
            penX += ch.xadvance * scale;
        }
        glEnd();
        glDisable(GL_TEXTURE_2D);
        return;
    }

    float penX = x;
    for (char c : text) {
        const std::array<std::uint8_t, 7> rows = glyphRows(c);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[static_cast<std::size_t>(row)] & (1u << (4 - col))) != 0u) {
                    renderFilledRect(penX + static_cast<float>(col) * scale,
                        y + static_cast<float>(row) * scale,
                        scale, scale, r, g, b, a);
                }
            }
        }
        penX += 6.0f * scale;
    }
}

void Engine::renderAnimatedTiles(int layer) const
{
    if (activeScreen_ == nullptr) {
        return;
    }
    for (const AnimatedTilePlacement& tile : activeScreen_->animatedTiles) {
        if (tile.layer != layer) {
            continue;
        }
        auto spriteIt = loadedSprites_.find(tile.spriteId);
        if (spriteIt == loadedSprites_.end() || !spriteIt->second.loaded || spriteIt->second.texture.id == 0) {
            continue;
        }
        const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
        if (frame == nullptr) {
            continue;
        }
        const Texture& tex = spriteIt->second.texture;
        const float u0 = static_cast<float>(frame->x) / static_cast<float>(tex.width);
        const float v0 = static_cast<float>(frame->y) / static_cast<float>(tex.height);
        const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(tex.width);
        const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(tex.height);
        const float x = static_cast<float>(tile.cellX * kTileSize);
        const float y = static_cast<float>(tile.cellY * kTileSize);
        renderTextureRegion(tex, x, y, static_cast<float>(frame->width), static_cast<float>(frame->height), u0, v0, u1, v1);
    }
}

void Engine::renderItems() const
{
    for (const RuntimeItemEntity& item : itemEntities_) {
        if (item.collected) {
            continue;
        }
        const float x = item.placement.x;
        const float y = item.placement.y;
        const float r = 7.0f;
        float cr = 1.0f, cg = 1.0f, cb = 0.2f;
        if (item.placement.pickupType == ItemPickupType::Ammo) { cr = 0.2f; cg = 0.8f; cb = 1.0f; }
        else if (item.placement.pickupType == ItemPickupType::Health) { cr = 0.2f; cg = 0.9f; cb = 0.3f; }

        auto spriteIt = loadedSprites_.find(item.placement.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
            if (frame != nullptr) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                renderTextureRegion(spriteIt->second.texture, x - r, y - r, r * 2.0f, r * 2.0f, u0, v0, u1, v1);
                continue;
            }
        }

        // Diamond fallback
        glDisable(GL_TEXTURE_2D);
        glColor4f(cr, cg, cb, 0.92f);
        glBegin(GL_QUADS);
        glVertex2f(x,       y - r);
        glVertex2f(x + r,   y);
        glVertex2f(x,       y + r);
        glVertex2f(x - r,   y);
        glEnd();
    }
}

void Engine::renderProjectiles() const
{
    for (const RuntimeProjectile& proj : projectiles_) {
        auto spriteIt = loadedSprites_.find(proj.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
            if (frame != nullptr) {
                const float drawW = static_cast<float>(frame->width);
                const float drawH = static_cast<float>(frame->height);
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                renderTextureRegion(spriteIt->second.texture, proj.x - drawW * 0.5f, proj.y - drawH * 0.5f, drawW, drawH, u0, v0, u1, v1);
                continue;
            }
        }
        // Settling projectiles fade out as they rest on the ground.
        const float alpha = proj.settleSeconds >= 0.0f
            ? std::clamp(1.0f - proj.settleSeconds / kProjectileSettleSeconds, 0.0f, 1.0f) * 0.95f
            : 0.95f;
        renderFilledRect(proj.x - kProjectileHalfSize, proj.y - kProjectileHalfSize,
            kProjectileHalfSize * 2.0f, kProjectileHalfSize * 2.0f, 1.0f, 0.85f, 0.1f, alpha);
    }
}

void Engine::renderMeleeFlash() const
{
    if (meleeActiveSeconds_ <= 0.0f || !meleeWeapon_.has_value()) {
        return;
    }
    const float reach = meleeWeapon_->range;
    const float hw = reach * 0.5f;
    const float cx = playerX_ + playerFacingX_ * (kPlayerCollisionSizePx * 0.5f + hw);
    const float cy = playerY_ + playerFacingY_ * (kPlayerCollisionSizePx * 0.5f + hw);
    const float alpha = std::clamp(meleeActiveSeconds_ / kMeleeActiveSeconds, 0.0f, 1.0f) * 0.55f;
    renderFilledRect(cx - hw, cy - hw, reach, reach, 1.0f, 0.95f, 0.2f, alpha);
}

void Engine::renderHud() const
{
    const float heartW = 8.0f;
    for (int i = 0; i < playerMaxHealth_; ++i) {
        const bool filled = i < playerHealth_;
        renderFilledRect(8.0f + static_cast<float>(i) * (heartW + 2.0f), 8.0f, heartW, 6.0f,
            filled ? 0.90f : 0.16f, filled ? 0.08f : 0.08f, filled ? 0.12f : 0.09f, 0.95f);
    }

    float hudY = 18.0f;

    if (meleeWeapon_.has_value()) {
        glDisable(GL_TEXTURE_2D);
        glColor4f(1.0f, 0.9f, 0.3f, 0.85f);
        glBegin(GL_QUADS);
        glVertex2f(8.0f,  hudY);
        glVertex2f(18.0f, hudY);
        glVertex2f(18.0f, hudY + 6.0f);
        glVertex2f(8.0f,  hudY + 6.0f);
        glEnd();
        hudY += 9.0f;
    }

    if (rangedWeapon_.has_value()) {
        const int ammoCount = ammoCountForWeapon(*rangedWeapon_);
        const float barMax = 20.0f;
        const float barW = std::min(static_cast<float>(ammoCount), barMax) * 2.0f;
        renderFilledRect(8.0f, hudY, 40.0f, 4.0f, 0.08f, 0.08f, 0.08f, 0.6f);
        renderFilledRect(8.0f, hudY, barW, 4.0f, 0.2f, 0.8f, 1.0f, 0.9f);
    }
}

void Engine::renderInventory() const
{
    if (!inventoryVisible_) {
        return;
    }

    struct InventoryEntry {
        std::string id;
        std::string name;
        std::string spriteId;
        int count = 0;
        bool stackable = true;
    };

    std::vector<InventoryEntry> entries;
    entries.reserve(inventory_.size());
    for (const std::string& id : sortedInventoryIds()) {
        const auto countIt = inventory_.find(id);
        if (countIt == inventory_.end()) {
            continue;
        }
        const int count = countIt->second;
        if (count <= 0) {
            continue;
        }
        InventoryEntry entry;
        entry.id = id;
        entry.name = id;
        entry.count = count;
        for (const ItemDef& def : itemDefs_) {
            if (def.id == id) {
                entry.name = def.name.empty() ? def.id : def.name;
                entry.spriteId = def.spriteId;
                entry.stackable = def.stackable;
                break;
            }
        }
        entries.push_back(std::move(entry));
    }

    constexpr float panelW = 168.0f;
    constexpr float pad = 8.0f;
    constexpr float icon = 18.0f;
    constexpr float rowH = 24.0f;
    constexpr float moneyHeaderH = 22.0f;
    const float panelX = screenWidthPx() - panelW - 8.0f;
    const float panelY = 8.0f;
    const std::size_t visibleRowCount = std::min<std::size_t>(entries.size(), 7);
    const float visibleRows = static_cast<float>(visibleRowCount);
    const float listY = panelY + 28.0f + moneyHeaderH;
    const float panelH = 28.0f + moneyHeaderH + std::max(1.0f, visibleRows) * rowH + pad;

    const std::vector<std::string> ammoIds = ammoInventoryIds();

    renderFilledRect(panelX, panelY, panelW, panelH, 0.03f, 0.04f, 0.06f, 0.88f);
    renderFilledRect(panelX, panelY, panelW, 1.0f, 0.55f, 0.72f, 0.92f, 0.90f);
    renderText("INVENTORY", panelX + pad, panelY + 4.0f, 1.0f, 0.92f, 0.95f, 0.98f, 1.0f);
    renderFilledRect(panelX + 4.0f, panelY + 27.0f, panelW - 8.0f, moneyHeaderH - 3.0f, 0.10f, 0.11f, 0.13f, 0.82f);
    renderText("MONEY", panelX + pad, panelY + 30.0f, 0.85f, 0.76f, 0.80f, 0.86f, 1.0f);
    const std::string moneyAmount = std::to_string(gameState_.getInt("Money", 0));
    renderText(moneyAmount, panelX + panelW - pad - textWidth(moneyAmount, 0.95f), panelY + 29.0f, 0.95f,
        1.0f, 0.90f, 0.42f, 1.0f);

    if (entries.empty()) {
        renderText("Empty", panelX + pad, listY + 2.0f, 1.0f, 0.68f, 0.72f, 0.78f, 1.0f);
    }

    for (std::size_t i = 0; i < visibleRowCount; ++i) {
        const std::size_t entryIndex = static_cast<std::size_t>(inventoryScroll_) + i;
        if (entryIndex >= entries.size()) {
            break;
        }
        const InventoryEntry& entry = entries[entryIndex];
        const float y = listY + static_cast<float>(i) * rowH;
        const float iconX = panelX + pad;
        const float iconY = y + 2.0f;
        const bool selected = static_cast<int>(entryIndex) == inventorySelection_;

        if (selected) {
            renderFilledRect(panelX + 4.0f, y, panelW - 8.0f, rowH - 2.0f, 0.18f, 0.32f, 0.42f, 0.88f);
        }

        bool renderedIcon = false;
        const auto spriteIt = loadedSprites_.find(entry.spriteId);
        if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
            const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
            if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
                const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                renderTextureRegion(spriteIt->second.texture, iconX, iconY, icon, icon, u0, v0, u1, v1);
                renderedIcon = true;
            }
        }
        if (!renderedIcon) {
            renderFilledRect(iconX, iconY, icon, icon, 0.40f, 0.48f, 0.58f, 0.95f);
        }

        std::string label = entry.name;
        constexpr float labelScale = 0.85f;
        const float labelMaxW = 82.0f;
        while (!label.empty() && textWidth(label, labelScale) > labelMaxW) {
            label.pop_back();
        }
        if (label.empty()) {
            label = entry.id.substr(0, std::min<std::size_t>(entry.id.size(), 8));
        }
        renderText(label, panelX + pad + icon + 6.0f, y + 4.0f, labelScale,
            selected ? 0.98f : 0.86f, selected ? 1.0f : 0.89f, selected ? 1.0f : 0.94f, 1.0f);

        if (entry.stackable || entry.count != 1) {
            const std::string count = std::to_string(entry.count);
            renderText(count, panelX + panelW - pad - textWidth(count, 1.0f), y + 4.0f, 1.0f,
                1.0f, 0.90f, 0.42f, 1.0f);
        }
    }

    if (entries.size() > visibleRowCount) {
        if (inventoryScroll_ > 0) {
            renderText("^", panelX + panelW - pad - 8.0f, panelY + 13.0f, 1.0f, 0.72f, 0.78f, 0.86f, 1.0f);
        }
        if (inventoryScroll_ + static_cast<int>(visibleRowCount) < static_cast<int>(entries.size())) {
            renderText("v", panelX + panelW - pad - 8.0f, panelY + panelH - 16.0f, 1.0f, 0.72f, 0.78f, 0.86f, 1.0f);
        }
    }

    // AMMO section: ammo lives in the inventory but is shown separately and is
    // consumed by firing ranged weapons (not "used" from the menu).
    if (!ammoIds.empty()) {
        const float ammoY = panelY + panelH + 6.0f;
        const float ammoH = 28.0f + static_cast<float>(ammoIds.size()) * rowH + pad;
        renderFilledRect(panelX, ammoY, panelW, ammoH, 0.03f, 0.04f, 0.06f, 0.88f);
        renderFilledRect(panelX, ammoY, panelW, 1.0f, 0.20f, 0.80f, 1.0f, 0.90f);
        renderText("AMMO", panelX + pad, ammoY + 4.0f, 1.0f, 0.85f, 0.94f, 1.0f, 1.0f);

        for (std::size_t i = 0; i < ammoIds.size(); ++i) {
            const std::string& id = ammoIds[i];
            const auto countIt = inventory_.find(id);
            const int count = countIt == inventory_.end() ? 0 : countIt->second;

            std::string name = id;
            std::string spriteId;
            for (const ItemDef& def : itemDefs_) {
                if (def.id == id) {
                    name = def.name.empty() ? def.id : def.name;
                    spriteId = def.spriteId;
                    break;
                }
            }

            const float y = ammoY + 28.0f + static_cast<float>(i) * rowH;
            const float iconX = panelX + pad;
            const float iconY = y + 2.0f;

            bool renderedIcon = false;
            const auto spriteIt = loadedSprites_.find(spriteId);
            if (spriteIt != loadedSprites_.end() && spriteIt->second.loaded && spriteIt->second.texture.id != 0) {
                const SpriteFrameDef* frame = spriteFrame(spriteIt->second);
                if (frame != nullptr && spriteIt->second.texture.width > 0 && spriteIt->second.texture.height > 0) {
                    const float u0 = static_cast<float>(frame->x) / static_cast<float>(spriteIt->second.texture.width);
                    const float v0 = static_cast<float>(frame->y) / static_cast<float>(spriteIt->second.texture.height);
                    const float u1 = static_cast<float>(frame->x + frame->width) / static_cast<float>(spriteIt->second.texture.width);
                    const float v1 = static_cast<float>(frame->y + frame->height) / static_cast<float>(spriteIt->second.texture.height);
                    renderTextureRegion(spriteIt->second.texture, iconX, iconY, icon, icon, u0, v0, u1, v1);
                    renderedIcon = true;
                }
            }
            if (!renderedIcon) {
                renderFilledRect(iconX, iconY, icon, icon, 0.20f, 0.55f, 0.70f, 0.95f);
            }

            std::string label = name;
            constexpr float labelScale = 0.85f;
            const float labelMaxW = 82.0f;
            while (!label.empty() && textWidth(label, labelScale) > labelMaxW) {
                label.pop_back();
            }
            renderText(label, panelX + pad + icon + 6.0f, y + 4.0f, labelScale, 0.86f, 0.89f, 0.94f, 1.0f);

            const std::string countStr = std::to_string(count);
            renderText(countStr, panelX + panelW - pad - textWidth(countStr, 1.0f), y + 4.0f, 1.0f,
                0.2f, 0.8f, 1.0f, 1.0f);
        }
    }
}

void Engine::renderShopMenu() const
{
    if (interactionState_ != InteractionState::InShop ||
        interactingNpcIndex_ < 0 ||
        interactingNpcIndex_ >= static_cast<int>(npcEntities_.size())) {
        return;
    }
    const RuntimeNpcEntity& npc = npcEntities_[static_cast<std::size_t>(interactingNpcIndex_)];
    const std::vector<std::string> playerItems = sortedShopInventoryIds();

    const float sw = screenWidthPx();
    const float sh = screenHeightPx();
    const float margin = 18.0f;
    const float panelW = (sw - margin * 3.0f) * 0.5f;
    const float panelH = sh - margin * 2.0f;
    const float y = margin;
    const float shopX = margin;
    const float playerX = margin * 2.0f + panelW;
    constexpr float rowH = 20.0f;
    constexpr int visibleRows = 16;

    renderFilledRect(0.0f, 0.0f, sw, sh, 0.0f, 0.0f, 0.0f, 0.48f);
    renderFilledRect(shopX, y, panelW, panelH, 0.04f, 0.05f, 0.07f, 0.94f);
    renderFilledRect(playerX, y, panelW, panelH, 0.04f, 0.05f, 0.07f, 0.94f);
    renderFilledRect(shopPanel_ == 0 ? shopX : playerX, y, panelW, 2.0f, 0.30f, 0.70f, 0.90f, 0.95f);

    renderText("SHOP", shopX + 8.0f, y + 7.0f, 1.2f, 0.92f, 0.95f, 0.98f, 1.0f);
    renderText("PLAYER", playerX + 8.0f, y + 7.0f, 1.2f, 0.92f, 0.95f, 0.98f, 1.0f);
    renderFilledRect(playerX + 6.0f, y + 31.0f, panelW - 12.0f, 21.0f, 0.10f, 0.11f, 0.13f, 0.82f);
    renderText("MONEY", playerX + 10.0f, y + 34.0f, 0.9f, 0.76f, 0.80f, 0.86f, 1.0f);
    const std::string money = std::to_string(gameState_.getInt("Money", 0));
    renderText(money, playerX + panelW - 10.0f - textWidth(money, 1.0f), y + 33.0f, 1.0f, 1.0f, 0.90f, 0.42f, 1.0f);

    auto itemName = [this](const std::string& id) {
        for (const ItemDef& item : itemDefs_) {
            if (item.id == id) {
                return item.name.empty() ? item.id : item.name;
            }
        }
        return id;
    };

    const float listY = y + 60.0f;
    const int shopFirst = std::clamp(shopScroll_[0], 0, std::max(0, static_cast<int>(npc.shopInventory.size()) - visibleRows));
    for (int i = 0; i < std::min<int>(visibleRows, static_cast<int>(npc.shopInventory.size()) - shopFirst); ++i) {
        const int itemIndex = shopFirst + i;
        const ShopItemDef& item = npc.shopInventory[static_cast<std::size_t>(itemIndex)];
        const float rowY = listY + static_cast<float>(i) * rowH;
        const bool selected = shopPanel_ == 0 && itemIndex == shopSelection_;
        if (selected) {
            renderFilledRect(shopX + 5.0f, rowY - 2.0f, panelW - 10.0f, rowH - 2.0f, 0.18f, 0.32f, 0.42f, 0.88f);
        }
        std::string label = itemName(item.itemId);
        if (!item.unlimited) {
            label += " x" + std::to_string(item.quantity);
        }
        renderText(label, shopX + 9.0f, rowY, 0.95f, 0.88f, 0.91f, 0.95f, 1.0f);
        const std::string price = std::to_string(item.buyPrice);
        renderText(price, shopX + panelW - 9.0f - textWidth(price, 0.95f), rowY, 0.95f, 1.0f, 0.90f, 0.42f, 1.0f);
    }
    if (npc.shopInventory.empty()) {
        renderText("No stock", shopX + 9.0f, listY + 2.0f, 1.0f, 0.68f, 0.72f, 0.78f, 1.0f);
    }

    const int playerFirst = std::clamp(shopScroll_[1], 0, std::max(0, static_cast<int>(playerItems.size()) - visibleRows));
    for (int i = 0; i < std::min<int>(visibleRows, static_cast<int>(playerItems.size()) - playerFirst); ++i) {
        const int itemIndex = playerFirst + i;
        const std::string& itemId = playerItems[static_cast<std::size_t>(itemIndex)];
        const auto countIt = inventory_.find(itemId);
        const int count = countIt == inventory_.end() ? 0 : countIt->second;
        const float rowY = listY + static_cast<float>(i) * rowH;
        const bool selected = shopPanel_ == 1 && itemIndex == shopSelection_;
        if (selected) {
            renderFilledRect(playerX + 5.0f, rowY - 2.0f, panelW - 10.0f, rowH - 2.0f, 0.18f, 0.32f, 0.42f, 0.88f);
        }
        renderText(itemName(itemId) + " x" + std::to_string(count), playerX + 9.0f, rowY, 0.95f, 0.88f, 0.91f, 0.95f, 1.0f);
        int sellPrice = 1;
        const auto shopIt = std::find_if(npc.shopInventory.begin(), npc.shopInventory.end(), [&itemId](const ShopItemDef& item) {
            return item.itemId == itemId;
        });
        if (shopIt != npc.shopInventory.end()) {
            sellPrice = shopIt->sellPrice;
        }
        const std::string price = std::to_string(sellPrice);
        renderText(price, playerX + panelW - 9.0f - textWidth(price, 0.95f), rowY, 0.95f, 1.0f, 0.90f, 0.42f, 1.0f);
    }
    if (playerItems.empty()) {
        renderText("Inventory empty", playerX + 9.0f, listY + 2.0f, 1.0f, 0.68f, 0.72f, 0.78f, 1.0f);
    }
    if (shopFirst > 0) {
        renderText("^", shopX + panelW - 16.0f, y + 48.0f, 1.0f, 0.72f, 0.78f, 0.86f, 1.0f);
    }
    if (shopFirst + visibleRows < static_cast<int>(npc.shopInventory.size())) {
        renderText("v", shopX + panelW - 16.0f, y + panelH - 18.0f, 1.0f, 0.72f, 0.78f, 0.86f, 1.0f);
    }
    if (playerFirst > 0) {
        renderText("^", playerX + panelW - 16.0f, y + 48.0f, 1.0f, 0.72f, 0.78f, 0.86f, 1.0f);
    }
    if (playerFirst + visibleRows < static_cast<int>(playerItems.size())) {
        renderText("v", playerX + panelW - 16.0f, y + panelH - 18.0f, 1.0f, 0.72f, 0.78f, 0.86f, 1.0f);
    }

    if (shopBuyActive_ && shopBuyIndex_ >= 0 && shopBuyIndex_ < static_cast<int>(npc.shopInventory.size())) {
        const ShopItemDef& item = npc.shopInventory[static_cast<std::size_t>(shopBuyIndex_)];
        const int price = std::max(0, item.buyPrice);
        const int maxQuantity = maxShopBuyQuantity(npc, shopBuyIndex_);
        const int quantity = std::clamp(shopBuyQuantity_, 0, maxQuantity);
        const int total = price * quantity;
        const int remainingMoney = std::max(0, gameState_.getInt("Money", 0) - total);
        const float boxW = std::min(312.0f, sw - margin * 2.0f);
        const float boxH = 128.0f;
        const float boxX = (sw - boxW) * 0.5f;
        const float boxY = (sh - boxH) * 0.5f;

        renderFilledRect(boxX, boxY, boxW, boxH, 0.03f, 0.04f, 0.06f, 0.98f);
        renderFilledRect(boxX, boxY, boxW, 2.0f, 0.55f, 0.72f, 0.92f, 0.95f);

        std::string title = "Buy " + itemName(item.itemId);
        constexpr float titleScale = 1.05f;
        const float titleMaxW = boxW - 20.0f;
        while (!title.empty() && textWidth(title, titleScale) > titleMaxW) {
            title.pop_back();
        }
        renderText(title, boxX + 10.0f, boxY + 10.0f, titleScale, 0.92f, 0.95f, 0.98f, 1.0f);

        const std::string amount = "< " + std::to_string(quantity) + " >";
        renderText("AMOUNT", boxX + 14.0f, boxY + 42.0f, 0.9f, 0.76f, 0.80f, 0.86f, 1.0f);
        renderText(amount, boxX + boxW - 14.0f - textWidth(amount, 1.1f), boxY + 39.0f, 1.1f,
            1.0f, 0.90f, 0.42f, 1.0f);

        const std::string totalText = "Total " + std::to_string(total);
        renderText(totalText, boxX + 14.0f, boxY + 68.0f, 0.95f, 0.86f, 0.89f, 0.94f, 1.0f);
        const std::string moneyText = "Money " + std::to_string(remainingMoney);
        renderText(moneyText, boxX + boxW - 14.0f - textWidth(moneyText, 0.95f), boxY + 68.0f, 0.95f,
            1.0f, 0.90f, 0.42f, 1.0f);

        const std::string stockText = item.unlimited ? "Stock unlimited" : "Stock " + std::to_string(item.quantity);
        renderText(stockText, boxX + 14.0f, boxY + 94.0f, 0.9f, 0.68f, 0.72f, 0.78f, 1.0f);
        const std::string maxText = "Max " + std::to_string(maxQuantity);
        renderText(maxText, boxX + boxW - 14.0f - textWidth(maxText, 0.9f), boxY + 94.0f, 0.9f,
            0.68f, 0.72f, 0.78f, 1.0f);
    }
}

} // namespace adventure::game
