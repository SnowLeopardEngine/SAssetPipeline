#include "Importers.h"

// Internal linkage: a program that links this library may carry its own stb_image (the engine does).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <bc7enc.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace SnowLeopardEngine::AssetPipeline::Detail
{
    namespace
    {
        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        // Data maps by their usual names; everything else is colour.
        bool LooksLinear(const std::filesystem::path& source)
        {
            const auto name = Lower(source.stem().generic_string());
            for (const char* token : {"normal", "nrm", "rough", "metal", "ao", "occlusion", "height", "displace", "mask",
                                      "specular", "gloss", "noise", "perlin", "bump", "orm", "mra"})
            {
                if (name.find(token) != std::string::npos)
                    return true;
            }
            return name.ends_with("_n");
        }

        float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
        float LinearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

        // RGBA float level -> next level (2 x 2 box filter; an odd edge clamps).
        std::vector<float> Downsample(const std::vector<float>& source, uint32_t width, uint32_t height)
        {
            const uint32_t     w = std::max(width / 2, 1u), h = std::max(height / 2, 1u);
            std::vector<float> result(size_t(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y)
            {
                const uint32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
                for (uint32_t x = 0; x < w; ++x)
                {
                    const uint32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        const auto at = [&](uint32_t sx, uint32_t sy) { return source[(size_t(sy) * width + sx) * 4 + c]; };
                        result[(size_t(y) * w + x) * 4 + c] = (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1)) * 0.25f;
                    }
                }
            }
            return result;
        }

        uint16_t ToHalf(float value)
        {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            const uint32_t sign     = (bits >> 16) & 0x8000;
            const int32_t  exponent = int32_t((bits >> 23) & 0xFF) - 127 + 15;
            uint32_t       mantissa = bits & 0x7FFFFF;
            if (((bits >> 23) & 0xFF) == 0xFF) // inf / nan
                return uint16_t(sign | 0x7C00 | (mantissa != 0 ? 0x200 : 0));
            if (exponent >= 31) // too large: largest finite half
                return uint16_t(sign | 0x7BFF);
            if (exponent <= 0) // subnormal or zero
            {
                if (exponent < -10)
                    return uint16_t(sign);
                mantissa |= 0x800000;
                const int shift = 14 - exponent;
                uint32_t  half  = mantissa >> shift;
                if ((mantissa >> (shift - 1)) & 1) // round to nearest
                    ++half;
                return uint16_t(sign | half);
            }
            uint32_t half = sign | (uint32_t(exponent) << 10) | (mantissa >> 13);
            if (mantissa & 0x1000) // round to nearest (carries into the exponent correctly)
                ++half;
            return uint16_t(half);
        }

        // One RGBA8 level -> BC7 blocks. Partial blocks (levels below 4 texels) repeat their edge texels. Block rows
        // are spread over the hardware threads.
        std::vector<uint8_t> EncodeBC7(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, bool perceptual, uint32_t quality)
        {
            static std::once_flag init;
            std::call_once(init, [] { bc7enc_compress_block_init(); });

            bc7enc_compress_block_params params;
            bc7enc_compress_block_params_init(&params);
            if (!perceptual)
                bc7enc_compress_block_params_init_linear_weights(&params);
            params.m_uber_level = std::min(quality, uint32_t(BC7ENC_MAX_UBER_LEVEL));
            // Where alpha carries a shape (cut-out foliage, UI), the default perceptual weights (alpha 32 against red
            // 128) let the encoder trade alpha for colour and edges erode: weigh it like the strongest channel.
            bool translucent = false;
            for (size_t i = 3; i < rgba.size() && !translucent; i += 4)
                translucent = rgba[i] != 255;
            if (translucent)
            {
                params.m_weights[3] = std::max({params.m_weights[0], params.m_weights[1], params.m_weights[2]}) * 2;
                params.m_uber_level = std::max(params.m_uber_level, 2u);
            }

            const uint32_t        blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
            std::vector<uint8_t>  blocks(size_t(blocksX) * blocksY * 16);
            std::atomic<uint32_t> nextRow {0};
            auto worker = [&] {
                uint8_t texels[16 * 4];
                for (uint32_t by = nextRow++; by < blocksY; by = nextRow++)
                {
                    for (uint32_t bx = 0; bx < blocksX; ++bx)
                    {
                        for (uint32_t y = 0; y < 4; ++y)
                        {
                            const uint32_t sy = std::min(by * 4 + y, height - 1);
                            for (uint32_t x = 0; x < 4; ++x)
                            {
                                const uint32_t sx = std::min(bx * 4 + x, width - 1);
                                std::memcpy(texels + (y * 4 + x) * 4, rgba.data() + (size_t(sy) * width + sx) * 4, 4);
                            }
                        }
                        bc7enc_compress_block(blocks.data() + (size_t(by) * blocksX + bx) * 16, texels, &params);
                    }
                }
            };
            const uint32_t           count = std::clamp(std::thread::hardware_concurrency(), 1u, std::max(blocksY, 1u));
            std::vector<std::thread> threads;
            for (uint32_t i = 1; i < count; ++i)
                threads.emplace_back(worker);
            worker();
            for (auto& thread : threads)
                thread.join();
            return blocks;
        }
    } // namespace

    void TextureDefaults(const std::filesystem::path& source, std::map<std::string, std::string>& params)
    {
        const bool hdr = Lower(source.extension().generic_string()) == ".hdr";
        if (!hdr)
        {
            params["srgb"]        = LooksLinear(source) ? "false" : "true";
            params["mips"]        = "true";
            params["compression"] = "bc7"; // bc7 | none (none: exact texels, 4x the memory)
            params["quality"]     = "0";   // BC7 encoder effort, 0 (fast) .. 4 (best)
        }
        params["maxSize"] = "0"; // 0: keep the source size
    }

    bool ImportTexture(ImportContext& context, TextureAsset& asset)
    {
        const auto& settings = *context.Settings;
        const auto  path     = context.Source.generic_string();
        stbi_set_flip_vertically_on_load(false); // rows stay top to bottom, as in the file
        int width = 0, height = 0, channels = 0;

        if (stbi_is_hdr(path.c_str()))
        {
            // HDR (environment maps): linear half floats, one level (the renderer prefilters it itself).
            float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (pixels == nullptr)
            {
                context.Error = std::string("cannot decode: ") + stbi_failure_reason();
                return false;
            }
            asset.Width  = uint32_t(width);
            asset.Height = uint32_t(height);
            asset.Format = TextureFormat::RGBA16F;
            auto& level  = asset.Mips.emplace_back(size_t(width) * height * 8);
            auto* halves = reinterpret_cast<uint16_t*>(level.data());
            for (size_t i = 0; i < size_t(width) * height * 4; ++i)
                halves[i] = ToHalf(pixels[i]);
            stbi_image_free(pixels);
            return true;
        }

        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            context.Error = std::string("cannot decode: ") + stbi_failure_reason();
            return false;
        }
        const bool srgb = settings.GetBool("srgb", !LooksLinear(context.Source));
        const bool mips = settings.GetBool("mips", true);

        // Filtering happens on linear values: sRGB colour is decoded first, so mips do not darken (gamma-space
        // averaging, what the runtime loader did).
        std::array<float, 256> decode {};
        for (int i = 0; i < 256; ++i)
            decode[i] = srgb ? SrgbToLinear(i / 255.0f) : i / 255.0f;
        std::vector<float> level(size_t(width) * height * 4);
        for (size_t i = 0; i < level.size(); ++i)
            level[i] = (i % 4 == 3) ? pixels[i] / 255.0f : decode[pixels[i]];
        stbi_image_free(pixels);

        auto encode = [&](const std::vector<float>& values) {
            std::vector<uint8_t> bytes(values.size());
            for (size_t i = 0; i < values.size(); ++i)
            {
                const float v = (i % 4 == 3 || !srgb) ? values[i] : LinearToSrgb(values[i]);
                bytes[i]      = uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            }
            return bytes;
        };

        // maxSize: halve until the image fits (then the chain continues from there, if mips are on).
        const uint32_t maxSize = uint32_t(std::max(settings.GetInt("maxSize", 0), 0));
        uint32_t       w = uint32_t(width), h = uint32_t(height);
        while (maxSize > 0 && std::max(w, h) > maxSize && (w > 1 || h > 1))
        {
            level = Downsample(level, w, h);
            w     = std::max(w / 2, 1u);
            h     = std::max(h / 2, 1u);
        }
        asset.Width  = w;
        asset.Height = h;

        // BC7 needs whole blocks at the top level (D3D12 requires it; smaller mips pad themselves): other sizes, mostly
        // small UI images, stay uncompressed.
        const bool bc7 = settings.GetString("compression", "bc7") == "bc7";
        const bool fit = w % 4 == 0 && h % 4 == 0;
        asset.Format = bc7 && fit ? (srgb ? TextureFormat::BC7Srgb : TextureFormat::BC7) : (srgb ? TextureFormat::RGBA8Srgb : TextureFormat::RGBA8);
        const auto quality = uint32_t(std::clamp(settings.GetInt("quality", 0), 0, 4));

        while (true)
        {
            auto bytes = encode(level);
            asset.Mips.push_back(IsBlockCompressed(asset.Format) ? EncodeBC7(bytes, w, h, srgb, quality) : std::move(bytes));
            if (!mips || (w == 1 && h == 1))
                break;
            level = Downsample(level, w, h);
            w     = std::max(w / 2, 1u);
            h     = std::max(h / 2, 1u);
        }
        return true;
    }
} // namespace SnowLeopardEngine::AssetPipeline::Detail
