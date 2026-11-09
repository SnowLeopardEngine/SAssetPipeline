#include "SAssetPipeline/AssetId.h"

#include <random>

namespace SnowLeopardEngine::AssetPipeline
{
    AssetId AssetId::Generate()
    {
        thread_local std::mt19937_64 engine {std::random_device {}() ^ (uint64_t(std::random_device {}()) << 32)};
        AssetId                      id;
        for (int i = 0; i < 16; i += 8)
        {
            const uint64_t value = engine();
            for (int k = 0; k < 8; ++k)
            {
                id.Bytes[i + k] = uint8_t(value >> (8 * k));
            }
        }
        id.Bytes[6] = uint8_t((id.Bytes[6] & 0x0F) | 0x40); // version 4
        id.Bytes[8] = uint8_t((id.Bytes[8] & 0x3F) | 0x80); // RFC 4122 variant
        return id;
    }

    std::optional<AssetId> AssetId::Parse(std::string_view text)
    {
        if (text.size() >= 2 && text.front() == '{' && text.back() == '}')
        {
            text = text.substr(1, text.size() - 2);
        }
        if (text.size() != 36)
        {
            return std::nullopt;
        }
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        AssetId id;
        size_t  byte = 0;
        for (size_t i = 0; i < text.size();)
        {
            if (i == 8 || i == 13 || i == 18 || i == 23)
            {
                if (text[i] != '-')
                    return std::nullopt;
                ++i;
                continue;
            }
            const int high = nibble(text[i]), low = nibble(text[i + 1]);
            if (high < 0 || low < 0)
                return std::nullopt;
            id.Bytes[byte++] = uint8_t((high << 4) | low);
            i += 2;
        }
        return id;
    }

    std::string AssetId::ToString() const
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string           text;
        text.reserve(36);
        for (size_t i = 0; i < 16; ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10)
                text += '-';
            text += kHex[Bytes[i] >> 4];
            text += kHex[Bytes[i] & 15];
        }
        return text;
    }

    bool AssetId::IsNil() const
    {
        for (const auto b : Bytes)
        {
            if (b != 0)
                return false;
        }
        return true;
    }
} // namespace SnowLeopardEngine::AssetPipeline
