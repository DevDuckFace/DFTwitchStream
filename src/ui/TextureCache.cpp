#include "ui/TextureCache.h"
#include "core/Http.h"
#include "core/Log.h"
#include "core/Util.h"

#include <windows.h>
#include <wincodec.h>

#include <GLFW/glfw3.h>   // pulls in <GL/gl.h>

// The Windows SDK's gl.h stops at OpenGL 1.1, but every GL 3.3 context we
// create supports this enum; the loader just never declares it.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <fstream>
#include <iterator>
#include <thread>

namespace dfts::ui {

namespace {

// One WIC factory per process, created lazily on the first decode.
IWICImagingFactory* wicFactory() {
    static IWICImagingFactory* factory = []() -> IWICImagingFactory* {
        // Each worker thread initialises COM itself; the factory is
        // free-threaded so sharing the pointer is safe.
        IWICImagingFactory* created = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&created));
        if (FAILED(hr)) {
            LOG_E("cannot create the WIC imaging factory");
            return nullptr;
        }
        return created;
    }();
    return factory;
}

// Small RAII wrapper so the decode path can bail out at any step.
template <typename T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T*  operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

} // namespace

bool decodeImage(const std::vector<uint8_t>& encoded, std::vector<uint8_t>& rgbaOut,
                 int& width, int& height) {
    if (encoded.empty()) return false;

    // WIC calls must happen on a COM-initialised thread.
    HRESULT comInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(comInit);

    bool ok = false;
    do {
        IWICImagingFactory* factory = wicFactory();
        if (!factory) break;

        ComPtr<IWICStream> stream;
        if (FAILED(factory->CreateStream(&stream))) break;
        if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()),
                                                (DWORD)encoded.size()))) break;

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromStream(stream.p, nullptr,
                                                    WICDecodeMetadataCacheOnLoad, &decoder))) break;

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) break;

        // Normalise whatever pixel format the source uses to straight RGBA8.
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter))) break;
        if (FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppRGBA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom))) break;

        UINT w = 0, h = 0;
        if (FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0) break;
        if (w > 4096 || h > 4096) break;   // thumbnails are never this large

        rgbaOut.resize((size_t)w * h * 4);
        if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)rgbaOut.size(), rgbaOut.data())))
            break;

        width  = (int)w;
        height = (int)h;
        ok     = true;
    } while (false);

    if (needUninit) CoUninitialize();
    return ok;
}

TextureCache& TextureCache::get() {
    static TextureCache instance;
    return instance;
}

TextureCache::~TextureCache() { clear(); }

size_t TextureCache::pendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inFlight.size();
}

ImTextureID TextureCache::texture(const std::string& url, int* outWidth, int* outHeight) {
    if (url.empty()) return (ImTextureID)0;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto it = m_entries.find(url); it != m_entries.end()) {
            if (it->second.failed || it->second.textureId == 0) return (ImTextureID)0;
            if (outWidth)  *outWidth  = it->second.width;
            if (outHeight) *outHeight = it->second.height;
            return (ImTextureID)(intptr_t)it->second.textureId;
        }
        if (m_inFlight.count(url)) return (ImTextureID)0;

        // Throttle so a screenful of new cards does not open 40 connections.
        if (m_activeFetches.load() >= kMaxConcurrent) return (ImTextureID)0;
        m_inFlight[url] = true;
    }

    fetch(url);
    return (ImTextureID)0;
}

ImTextureID TextureCache::textureFromFile(const std::string& path, int* outWidth,
                                          int* outHeight) {
    if (path.empty()) return (ImTextureID)0;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto it = m_entries.find(path); it != m_entries.end()) {
            if (it->second.failed || it->second.textureId == 0) return (ImTextureID)0;
            if (outWidth)  *outWidth  = it->second.width;
            if (outHeight) *outHeight = it->second.height;
            return (ImTextureID)(intptr_t)it->second.textureId;
        }
    }

    Decoded decoded;
    decoded.url = path;

    std::ifstream file(util::widen(path), std::ios::binary);
    if (file) {
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        if (!decodeImage(bytes, decoded.pixels, decoded.width, decoded.height))
            decoded.failed = true;
    } else {
        decoded.failed = true;
        LOG_W("cannot open image " + path);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_completed.push_back(std::move(decoded));
    }
    // Upload right away so callers get the texture on the very next frame.
    processCompleted();

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(path);
    if (it == m_entries.end() || it->second.failed) return (ImTextureID)0;
    if (outWidth)  *outWidth  = it->second.width;
    if (outHeight) *outHeight = it->second.height;
    return (ImTextureID)(intptr_t)it->second.textureId;
}

void TextureCache::fetch(const std::string& url) {
    ++m_activeFetches;
    std::thread([this, url]() {
        Decoded result;
        result.url = url;

        http::Response response = http::get(url, {{"Referer", "https://www.twitch.tv/"}});
        if (response.ok() && !response.body.empty()) {
            std::vector<uint8_t> bytes(response.body.begin(), response.body.end());
            if (!decodeImage(bytes, result.pixels, result.width, result.height))
                result.failed = true;
        } else {
            result.failed = true;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_completed.push_back(std::move(result));
            m_inFlight.erase(url);
        }
        --m_activeFetches;
    }).detach();
}

void TextureCache::processCompleted() {
    std::vector<Decoded> ready;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ready.swap(m_completed);
    }
    if (ready.empty()) return;

    for (Decoded& item : ready) {
        Entry entry;
        if (item.failed || item.pixels.empty()) {
            entry.failed = true;
        } else {
            GLuint id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, item.width, item.height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, item.pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            entry.textureId = id;
            entry.width     = item.width;
            entry.height    = item.height;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        // Evicting the whole cache is crude, but it only triggers after
        // hundreds of distinct thumbnails in one session.
        if (m_entries.size() >= kMaxEntries) {
            for (auto& [key, value] : m_entries)
                if (value.textureId) glDeleteTextures(1, &value.textureId);
            m_entries.clear();
        }
        m_entries[item.url] = entry;
    }
}

void TextureCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [key, value] : m_entries)
        if (value.textureId) glDeleteTextures(1, &value.textureId);
    m_entries.clear();
    m_completed.clear();
}

} // namespace dfts::ui
