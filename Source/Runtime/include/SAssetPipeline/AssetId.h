#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace SnowLeopardEngine::AssetPipeline
{
    // A stable asset identity: a random (version 4) UUID, written as "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (the same
    // text and byte order as the engine's stduuid ids, so either side can parse the other's).
    struct AssetId
    {
        std::array<uint8_t, 16> Bytes {};

        static AssetId                Generate();
        static std::optional<AssetId> Parse(std::string_view text);

        std::string ToString() const;
        bool        IsNil() const;

        auto operator<=>(const AssetId&) const = default;
    };
} // namespace SnowLeopardEngine::AssetPipeline

template<>
struct std::hash<SnowLeopardEngine::AssetPipeline::AssetId>
{
    size_t operator()(const SnowLeopardEngine::AssetPipeline::AssetId& id) const noexcept
    {
        uint64_t a = 0, b = 0;
        for (int i = 0; i < 8; ++i)
        {
            a = (a << 8) | id.Bytes[i];
            b = (b << 8) | id.Bytes[8 + i];
        }
        return size_t(a ^ (b * 0x9E3779B97F4A7C15ull));
    }
};
