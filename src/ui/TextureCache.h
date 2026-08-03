#pragma once
#include <imgui.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace dfts::ui {

// Downloads remote images (stream and category thumbnails), decodes them with
// the Windows Imaging Component and uploads them as OpenGL textures.
//
// get() never blocks: it returns nullptr while a fetch is in flight and the
// texture on a later frame. Uploads happen on the render thread inside
// processCompleted(), which the frame loop calls once per frame.
class TextureCache {
public:
    static TextureCache& get();
    ~TextureCache();

    // Returns a texture id for `url`, or nullptr if it is not ready yet.
    // Kicks off a background fetch the first time a url is seen.
    ImTextureID texture(const std::string& url, int* outWidth = nullptr,
                        int* outHeight = nullptr);

    // Loads a local image file. Reads and uploads synchronously on first call,
    // then serves the cached texture; intended for small bundled assets.
    ImTextureID textureFromFile(const std::string& path, int* outWidth = nullptr,
                                int* outHeight = nullptr);

    // Uploads whatever finished decoding since the last frame. Render thread only.
    void processCompleted();

    // Drops every texture; used when the user signs out.
    void clear();

    size_t pendingCount() const;

private:
    struct Entry {
        unsigned int textureId = 0;
        int          width     = 0;
        int          height    = 0;
        bool         failed    = false;
    };
    struct Decoded {
        std::string          url;
        std::vector<uint8_t> pixels;   // RGBA8
        int                  width  = 0;
        int                  height = 0;
        bool                 failed = false;
    };

    TextureCache() = default;

    void fetch(const std::string& url);

    mutable std::mutex               m_mutex;
    std::unordered_map<std::string, Entry> m_entries;
    std::unordered_map<std::string, bool>  m_inFlight;
    std::vector<Decoded>             m_completed;
    std::atomic<int>                 m_activeFetches{0};

    // Twitch card art is small; a hard ceiling keeps VRAM bounded on long sessions.
    static constexpr size_t kMaxEntries   = 512;
    static constexpr int    kMaxConcurrent = 6;
};

// Decodes an encoded image (JPEG/PNG/etc.) into RGBA8 using WIC.
// Returns false if the bytes are not a supported image.
bool decodeImage(const std::vector<uint8_t>& encoded, std::vector<uint8_t>& rgbaOut,
                 int& width, int& height);

} // namespace dfts::ui
