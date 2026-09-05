#include "Image.h"
#pragma comment(lib, "windowscodecs.lib")
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include <objbase.h>

// =============================================================================
// D3D9 texture helper
// =============================================================================
static IDirect3DTexture9* MakeTex(IDirect3DDevice9* dev, const BYTE* bgra, UINT w, UINT h)
{
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &tex, nullptr)))
        return nullptr;

    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) { tex->Release(); return nullptr; }

    const BYTE* src = bgra;
    BYTE*       dst = (BYTE*)lr.pBits;
    for (UINT y = 0; y < h; y++)
    {
        memcpy(dst, src, w * 4);
        src += w * 4;
        dst += lr.Pitch;
    }
    tex->UnlockRect(0);
    return tex;
}

// =============================================================================
// HTTP downloader (WinHTTP)
// =============================================================================
static bool DownloadHTTP(const std::string& url, std::vector<BYTE>& outData,
                          std::string& errorMsg)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wurl(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl.data(), wlen);
    if (!wurl.empty() && wurl.back() == L'\0') wurl.pop_back();

    wchar_t host[512] = {}, path[2048] = {};
    URL_COMPONENTS uc = {};
    uc.dwStructSize     = sizeof(uc);
    uc.lpszHostName     = host; uc.dwHostNameLength = (DWORD)std::size(host);
    uc.lpszUrlPath      = path; uc.dwUrlPathLength  = (DWORD)std::size(path);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
    { errorMsg = "Invalid URL"; return false; }

    HINTERNET hSess = WinHttpOpen(L"TwinkImage/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) { errorMsg = "WinHTTP init failed"; return false; }

    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); errorMsg = "Connect failed"; return false; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET",
        (path[0] ? path : L"/"), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq)
    { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); errorMsg = "Open request failed"; return false; }

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr))
    {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
        errorMsg = "HTTP request failed"; return false;
    }

    DWORD avail = 0, read = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0)
    {
        size_t off = outData.size();
        outData.resize(off + avail);
        WinHttpReadData(hReq, outData.data() + off, avail, &read);
        outData.resize(off + read);
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);

    if (outData.empty()) { errorMsg = "Empty response"; return false; }
    return true;
}

// =============================================================================
// Shared WIC frame decoder
// =============================================================================
bool ImageModule::DecodeDecoder(ImageEntry& e, IDirect3DDevice9* dev,
                                 IWICImagingFactory* factory,
                                 IWICBitmapDecoder* decoder)
{
    UINT frameCount = 0;
    decoder->GetFrameCount(&frameCount);
    if (frameCount == 0) { e.errorMsg = "Image has no frames"; return false; }

    UINT canvasW = 0, canvasH = 0;
    bool isGif   = (frameCount > 1);

    if (isGif)
    {
        IWICMetadataQueryReader* gMeta = nullptr;
        if (SUCCEEDED(decoder->GetMetadataQueryReader(&gMeta)))
        {
            PROPVARIANT pv; PropVariantInit(&pv);
            if (SUCCEEDED(gMeta->GetMetadataByName(L"/logscrdesc/Width",  &pv)) && pv.vt == VT_UI2) canvasW = pv.uiVal;
            PropVariantClear(&pv);
            if (SUCCEEDED(gMeta->GetMetadataByName(L"/logscrdesc/Height", &pv)) && pv.vt == VT_UI2) canvasH = pv.uiVal;
            PropVariantClear(&pv);
            gMeta->Release();
        }
    }

    {
        IWICBitmapFrameDecode* f0 = nullptr;
        if (SUCCEEDED(decoder->GetFrame(0, &f0)))
        {
            UINT fw = 0, fh = 0; f0->GetSize(&fw, &fh);
            if (canvasW == 0) canvasW = fw;
            if (canvasH == 0) canvasH = fh;
            f0->Release();
        }
    }

    if (canvasW == 0 || canvasH == 0) { e.errorMsg = "Could not read image dimensions"; return false; }

    e.imgW = (int)canvasW;
    e.imgH = (int)canvasH;

    std::vector<BYTE> canvas(canvasW * canvasH * 4, 0);

    for (UINT fi = 0; fi < frameCount; fi++)
    {
        IWICBitmapFrameDecode* frame = nullptr;
        if (FAILED(decoder->GetFrame(fi, &frame))) continue;

        UINT offX = 0, offY = 0;
        unsigned int msDelay  = 100;
        BYTE         disposal = 0;

        if (isGif)
        {
            IWICMetadataQueryReader* meta = nullptr;
            if (SUCCEEDED(frame->GetMetadataQueryReader(&meta)))
            {
                PROPVARIANT pv; PropVariantInit(&pv);
                if (SUCCEEDED(meta->GetMetadataByName(L"/imgdesc/Left",      &pv)) && pv.vt == VT_UI2) offX = pv.uiVal;
                PropVariantClear(&pv);
                if (SUCCEEDED(meta->GetMetadataByName(L"/imgdesc/Top",       &pv)) && pv.vt == VT_UI2) offY = pv.uiVal;
                PropVariantClear(&pv);
                if (SUCCEEDED(meta->GetMetadataByName(L"/grctlext/Delay",    &pv)) && pv.vt == VT_UI2)
                    msDelay = (unsigned int)pv.uiVal * 10;
                PropVariantClear(&pv);
                if (SUCCEEDED(meta->GetMetadataByName(L"/grctlext/Disposal", &pv)) && pv.vt == VT_UI1)
                    disposal = pv.bVal;
                PropVariantClear(&pv);
                meta->Release();
            }
        }
        if (msDelay < 20) msDelay = 100;

        UINT fw = 0, fh = 0;
        frame->GetSize(&fw, &fh);

        IWICFormatConverter* conv = nullptr;
        factory->CreateFormatConverter(&conv);
        conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                         WICBitmapDitherTypeNone, nullptr, 0.0,
                         WICBitmapPaletteTypeCustom);

        std::vector<BYTE> px(fw * fh * 4);
        conv->CopyPixels(nullptr, fw * 4, (UINT)px.size(), px.data());
        conv->Release();
        frame->Release();

        for (UINT y = 0; y < fh && (offY + y) < canvasH; y++)
            for (UINT x = 0; x < fw && (offX + x) < canvasW; x++)
            {
                UINT si = (y * fw + x) * 4;
                UINT di = ((offY + y) * canvasW + (offX + x)) * 4;
                if (px[si + 3] > 0)
                { canvas[di] = px[si]; canvas[di+1] = px[si+1]; canvas[di+2] = px[si+2]; canvas[di+3] = px[si+3]; }
            }

        IDirect3DTexture9* tex = MakeTex(dev, canvas.data(), canvasW, canvasH);
        if (tex) e.frames.push_back({ tex, msDelay });

        if (disposal == 2)
            for (UINT y = 0; y < fh && (offY + y) < canvasH; y++)
                for (UINT x = 0; x < fw && (offX + x) < canvasW; x++)
                { UINT di = ((offY + y) * canvasW + (offX + x)) * 4; canvas[di] = canvas[di+1] = canvas[di+2] = canvas[di+3] = 0; }
    }
    return !e.frames.empty();
}

// =============================================================================
// WIC loader - from file path
// =============================================================================
bool ImageModule::LoadWIC(ImageEntry& e, IDirect3DDevice9* dev, const std::wstring& path)
{
    HRESULT comHr = CoInitialize(nullptr);
    bool    comOwn = (comHr == S_OK);

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, (void**)&factory)))
    { e.errorMsg = "WIC factory unavailable"; if (comOwn) CoUninitialize(); return false; }

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr,
               GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
    { e.errorMsg = "Cannot open file"; factory->Release(); if (comOwn) CoUninitialize(); return false; }

    bool ok = DecodeDecoder(e, dev, factory, decoder);
    decoder->Release(); factory->Release();
    if (comOwn) CoUninitialize();
    return ok;
}

// =============================================================================
// WIC loader - from memory (downloaded URL data)
// =============================================================================
bool ImageModule::LoadWICFromMemory(ImageEntry& e, IDirect3DDevice9* dev,
                                     const std::vector<BYTE>& data)
{
    HRESULT comHr = CoInitialize(nullptr);
    bool    comOwn = (comHr == S_OK);

    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!hg) { e.errorMsg = "Out of memory"; if (comOwn) CoUninitialize(); return false; }
    void* ptr = GlobalLock(hg);
    memcpy(ptr, data.data(), data.size());
    GlobalUnlock(hg);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hg, TRUE, &stream)))
    { GlobalFree(hg); e.errorMsg = "Stream creation failed"; if (comOwn) CoUninitialize(); return false; }

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, (void**)&factory)))
    { stream->Release(); e.errorMsg = "WIC factory unavailable"; if (comOwn) CoUninitialize(); return false; }

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(factory->CreateDecoderFromStream(stream, nullptr,
               WICDecodeMetadataCacheOnLoad, &decoder)))
    { factory->Release(); stream->Release(); e.errorMsg = "Cannot decode image data"; if (comOwn) CoUninitialize(); return false; }
    stream->Release();

    bool ok = DecodeDecoder(e, dev, factory, decoder);
    decoder->Release(); factory->Release();
    if (comOwn) CoUninitialize();
    return ok;
}

// =============================================================================
// TryLoad / UnloadEntry
// =============================================================================
void ImageModule::TryLoad(ImageEntry& e)
{
    UnloadEntry(e);
    e.errorMsg.clear();
    e.needsLoad = false;

    IDirect3DDevice9* dev = Twinkie->GetD3DDevice();
    if (!dev) { e.errorMsg = "No D3D device yet"; e.needsLoad = true; return; }

    std::string u8path(e.pathBuf);
    if (u8path.size() >= 2 && u8path.front() == '"' && u8path.back() == '"')
        u8path = u8path.substr(1, u8path.size() - 2);
    if (u8path.empty()) { e.errorMsg = "Path is empty"; return; }

    bool isUrl = (u8path.size() > 8 &&
                 (u8path.substr(0, 7) == "http://" || u8path.substr(0, 8) == "https://"));
    bool ok = false;

    if (isUrl)
    {
        e.errorMsg = "Downloading...";
        std::vector<BYTE> data;
        if (!DownloadHTTP(u8path, data, e.errorMsg)) return;
        e.errorMsg.clear();
        ok = LoadWICFromMemory(e, dev, data);
    }
    else
    {
        std::wstring wpath = Twinkie->UTF8ToWString(u8path);
        ok = LoadWIC(e, dev, wpath);
    }

    if (ok)
    {
        e.loaded       = true;
        e.curFrame     = 0;
        e.lastFrameTime = std::chrono::steady_clock::now();
    }
}

void ImageModule::UnloadEntry(ImageEntry& e)
{
    for (auto& f : e.frames) if (f.tex) e.pendingRelease.push_back(f.tex);
    e.frames.clear();
    e.loaded   = false;
    e.curFrame = 0;
    e.imgW = e.imgH = 0;
}

IDirect3DTexture9* ImageModule::CurrentTex(const ImageEntry& e) const
{
    if (e.frames.empty()) return nullptr;
    return e.frames[e.curFrame].tex;
}

// =============================================================================
// Destructor
// =============================================================================
ImageModule::~ImageModule()
{
    for (auto& e : m_Images)
    {
        for (auto* t : e.pendingRelease) if (t) t->Release();
        for (auto& f : e.frames)         if (f.tex) f.tex->Release();
    }
}

// =============================================================================
// RenderAnyways - flush pending releases, advance GIF frames, deferred loads,
//                 and draw overlay windows every frame (menu open or closed)
// =============================================================================
void ImageModule::RenderAnyways()
{
    using namespace ImGui;

    for (auto& e : m_Images)
    {
        // Flush textures released last frame
        for (auto* t : e.pendingRelease) if (t) t->Release();
        e.pendingRelease.clear();
    }

    if (!Enabled) return;

    for (int i = 0; i < (int)m_Images.size(); i++)
    {
        auto& e = m_Images[i];

        if (e.needsLoad) { TryLoad(e); continue; }

        // Advance GIF frame
        if (e.loaded && e.frames.size() > 1)
        {
            auto         now     = std::chrono::steady_clock::now();
            unsigned int elapsed = (unsigned int)std::chrono::duration_cast<
                                       std::chrono::milliseconds>(now - e.lastFrameTime).count();
            if (elapsed >= e.frames[e.curFrame].msDelay)
            {
                e.curFrame      = (e.curFrame + 1) % (int)e.frames.size();
                e.lastFrameTime = now;
            }
        }

        // Draw overlay window
        if (!e.loaded || !CurrentTex(e)) continue;

        ImVec2 dispSize = { e.imgW * e.scale, e.imgH * e.scale };

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
                               | ImGuiWindowFlags_AlwaysAutoResize
                               | ImGuiWindowFlags_NoCollapse
                               | ImGuiWindowFlags_NoFocusOnAppearing;

        // Use ### so both title variants share the same window position/state.
        // ## only appends to the hash; ### resets it so only the part after matters.
        std::string wndId = "###img_ovl_" + std::to_string(i);

        if (*UiRenderEnabled)
        {
            std::string title = "Image " + std::to_string(i + 1) + wndId;
            SetNextWindowBgAlpha(0.0f);
            if (Begin(title.c_str(), nullptr, flags))
                Image((ImTextureID)CurrentTex(e), dispSize);
            End();
        }
        else
        {
            flags |= ImGuiWindowFlags_NoTitleBar
                   | ImGuiWindowFlags_NoInputs
                   | ImGuiWindowFlags_NoMove;
            SetNextWindowBgAlpha(0.0f);
            if (Begin(wndId.c_str(), nullptr, flags))
                Image((ImTextureID)CurrentTex(e), dispSize);
            End();
        }
    }
}

// =============================================================================
// RenderMenuItem
// =============================================================================
void ImageModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_PICTURE_O " Image", "", Enabled))
        Enabled = !Enabled;
}

// =============================================================================
// RenderSettings - settings content (no Begin/End)
// =============================================================================
void ImageModule::RenderSettings()
{
    using namespace ImGui;

    int removeIdx = -1;

    for (int i = 0; i < (int)m_Images.size(); i++)
    {
        auto& e = m_Images[i];

        PushID(i);

        // Path input
        float removeW = CalcTextSize(ICON_FK_TIMES).x + GetStyle().FramePadding.x * 2.f + 4.f;
        SetNextItemWidth(GetContentRegionAvail().x - removeW);
        InputText("##path", e.pathBuf, ImageEntry::kPathBufSize);
        if (IsItemHovered())
            SetTooltip("Local file:  C:\\Users\\you\\Pictures\\cool.gif\n"
                       "URL:         https://example.com/image.png");
        SameLine();
        if (Button(ICON_FK_TIMES))
            removeIdx = i;

        // Load / Unload buttons + status
        if (Button(ICON_FK_PICTURE_O " Load"))  TryLoad(e);
        SameLine();
        if (Button(ICON_FK_TIMES " Unload")) { UnloadEntry(e); e.errorMsg.clear(); }
        SameLine();
        if (!e.errorMsg.empty())
            TextColored({ 1.f, 0.3f, 0.3f, 1.f }, "%s", e.errorMsg.c_str());
        else if (e.loaded)
            TextColored({ 0.3f, 1.f, 0.3f, 1.f }, "%dx%d (%d frame%s)",
                e.imgW, e.imgH, (int)e.frames.size(), e.frames.size() == 1 ? "" : "s");

        // Scale slider
        SetNextItemWidth(180);
        SliderFloat("Scale##sc", &e.scale, 0.1f, 5.0f, "%.2fx");
        if (IsItemHovered()) SetTooltip("Right-click to type an exact value.");

        Separator();
        PopID();
    }

    // Remove entry (deferred)
    if (removeIdx >= 0)
    {
        UnloadEntry(m_Images[removeIdx]);
        m_Images.erase(m_Images.begin() + removeIdx);
    }

    if (Button(ICON_FK_PLUS " Add Image"))
        m_Images.emplace_back();

    if (!m_Images.empty())
        TextDisabled("Drag overlay windows to reposition. F3 locks them.");
}

// =============================================================================
// Render - settings window only (overlays are drawn in RenderAnyways)
// =============================================================================
void ImageModule::Render()
{
}

// =============================================================================
// Settings
// =============================================================================
void ImageModule::SettingsInit(SettingMgr& Settings)
{
    Settings["Image"]["Enable"].GetAsBool(&Enabled);

    int count = 0;
    Settings["Image"]["Count"].GetAsInt(&count);
    if (count < 0) count = 0;
    if (count > 32) count = 32;

    m_Images.clear();
    for (int i = 0; i < count; i++)
    {
        std::string sec = "Image_" + std::to_string(i);
        ImageEntry e;
        std::string path;
        Settings[sec]["Path"].GetAsString(&path);
        if (!path.empty())
        {
            strncpy_s(e.pathBuf, path.c_str(), ImageEntry::kPathBufSize - 1);
            e.needsLoad = true;
        }
        Settings[sec]["Scale"].GetAsFloat(&e.scale);
        if (e.scale < 0.1f || e.scale > 10.f) e.scale = 1.0f;
        m_Images.push_back(std::move(e));
    }
}

void ImageModule::SettingsSave(SettingMgr& Settings)
{
    Settings["Image"]["Enable"].Set(Enabled);
    Settings["Image"]["Count"].Set((int)m_Images.size());
    for (int i = 0; i < (int)m_Images.size(); i++)
    {
        std::string sec = "Image_" + std::to_string(i);
        Settings[sec]["Path"].Set(std::string(m_Images[i].pathBuf));
        Settings[sec]["Scale"].Set(m_Images[i].scale);
    }
}
