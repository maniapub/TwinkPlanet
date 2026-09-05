#define NOMINMAX
#pragma execution_character_set("utf-8")
#include "Blahaj.h"
#include <ShlObj.h>
#include <objbase.h>
#include "../../TwinkToast/TwinkToast.h"
#pragma comment(lib, "windowscodecs.lib")

// =============================================================================
// D3D9 texture helper
// =============================================================================
static IDirect3DTexture9* BlahajMakeTex(IDirect3DDevice9* dev,
                                         const BYTE* bgra, UINT w, UINT h)
{
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &tex, nullptr)))
        return nullptr;

    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) { tex->Release(); return nullptr; }

    const BYTE* src = bgra;
    BYTE*       dst = (BYTE*)lr.pBits;
    for (UINT y = 0; y < h; y++) {
        memcpy(dst, src, w * 4);
        src += w * 4;
        dst += lr.Pitch;
    }
    tex->UnlockRect(0);
    return tex;
}

// =============================================================================
// Destructor
// =============================================================================
BlahajModule::~BlahajModule()
{
    for (auto* t : m_PendingRelease) if (t) t->Release();
    for (auto& f : m_Frames)         if (f.tex) f.tex->Release();
}

// =============================================================================
// WIC GIF decoder
// =============================================================================
bool BlahajModule::DecodeWIC(IDirect3DDevice9* dev,
                              IWICImagingFactory* factory,
                              IWICBitmapDecoder* decoder)
{
    UINT frameCount = 0;
    decoder->GetFrameCount(&frameCount);
    if (frameCount == 0) return false;

    UINT canvasW = 0, canvasH = 0;
    bool isGif   = (frameCount > 1);

    if (isGif) {
        IWICMetadataQueryReader* gMeta = nullptr;
        if (SUCCEEDED(decoder->GetMetadataQueryReader(&gMeta))) {
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
        if (SUCCEEDED(decoder->GetFrame(0, &f0))) {
            UINT fw = 0, fh = 0; f0->GetSize(&fw, &fh);
            if (canvasW == 0) canvasW = fw;
            if (canvasH == 0) canvasH = fh;
            f0->Release();
        }
    }

    if (canvasW == 0 || canvasH == 0) return false;

    m_ImgW       = (int)canvasW;
    m_ImgH       = (int)canvasH;
    m_AspectRatio = (float)canvasW / (float)canvasH;

    std::vector<BYTE> canvas(canvasW * canvasH * 4, 0);

    for (UINT fi = 0; fi < frameCount; fi++) {
        IWICBitmapFrameDecode* frame = nullptr;
        if (FAILED(decoder->GetFrame(fi, &frame))) continue;

        UINT offX = 0, offY = 0;
        unsigned int msDelay = 100;
        BYTE disposal = 0;

        if (isGif) {
            IWICMetadataQueryReader* meta = nullptr;
            if (SUCCEEDED(frame->GetMetadataQueryReader(&meta))) {
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

        // disposal=0 (unspecified) or disposal=2: clear the frame region before
        // painting so old frame data doesn't bleed through.
        // disposal=1 (do not dispose): leave canvas as-is (delta compositing).
        if (disposal != 1) {
            for (UINT y = 0; y < fh && (offY + y) < canvasH; y++)
                for (UINT x = 0; x < fw && (offX + x) < canvasW; x++) {
                    UINT di = ((offY + y) * canvasW + (offX + x)) * 4;
                    canvas[di] = canvas[di+1] = canvas[di+2] = canvas[di+3] = 0;
                }
        }

        for (UINT y = 0; y < fh && (offY + y) < canvasH; y++)
            for (UINT x = 0; x < fw && (offX + x) < canvasW; x++) {
                UINT si = (y * fw + x) * 4;
                UINT di = ((offY + y) * canvasW + (offX + x)) * 4;
                if (px[si + 3] > 0) {
                    canvas[di]   = px[si];
                    canvas[di+1] = px[si+1];
                    canvas[di+2] = px[si+2];
                    canvas[di+3] = px[si+3];
                }
            }

        IDirect3DTexture9* tex = BlahajMakeTex(dev, canvas.data(), canvasW, canvasH);
        if (tex) m_Frames.push_back({ tex, msDelay });
    }
    return !m_Frames.empty();
}

// =============================================================================
// Load shark.gif from Documents\TwinkPlanet\media\shark.gif
// =============================================================================
void BlahajModule::Load(IDirect3DDevice9* dev)
{
    m_NeedsLoad = false;
    m_ErrorMsg.clear();

    HRESULT hr  = CoInitialize(nullptr);
    bool comOwn = (hr == S_OK);

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, (void**)&factory)))
    {
        m_ErrorMsg = "WIC unavailable";
        Logger->PrintError("[Blahaj] WIC unavailable");
        TwinkToast::Get().Push("[Blahaj] WIC unavailable", 5.f, { 1.f, 0.4f, 0.4f, 1.f });
        if (comOwn) CoUninitialize();
        return;
    }

    IWICBitmapDecoder* decoder = nullptr;
    char docsPath[MAX_PATH] = {};
    SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docsPath);
    std::string u8path = std::string(docsPath) + "\\TwinkPlanet\\media\\shark.gif";
    int wn = MultiByteToWideChar(CP_ACP, 0, u8path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wn - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, u8path.c_str(), -1, wpath.data(), wn);

    if (FAILED(factory->CreateDecoderFromFilename(wpath.c_str(), nullptr,
               GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
    {
        m_ErrorMsg = "shark.gif not found";
        Logger->PrintErrorArgs("[Blahaj] shark.gif missing at {}", u8path);
        TwinkToast::Get().Push("[Blahaj] shark.gif missing from Documents\\TwinkPlanet\\media\\",
                               8.f, { 1.f, 0.6f, 0.2f, 1.f });
        factory->Release();
        if (comOwn) CoUninitialize();
        return;
    }

    bool ok = DecodeWIC(dev, factory, decoder);
    decoder->Release();
    factory->Release();
    if (comOwn) CoUninitialize();

    if (ok) {
        m_Loaded        = true;
        m_CurFrame      = 0;
        m_LastFrameTime = std::chrono::steady_clock::now();
    } else {
        m_ErrorMsg = "Failed to decode shark.gif";
        Logger->PrintError("[Blahaj] Failed to decode shark.gif");
        TwinkToast::Get().Push("[Blahaj] Failed to decode shark.gif", 5.f, { 1.f, 0.4f, 0.4f, 1.f });
    }
}

// =============================================================================
// RenderAnyways - always runs, even when the main menu is hidden
// =============================================================================

// Aspect-ratio size constraint (called synchronously inside Begin)
struct BlahajARData { float ratio; int* resizeAxis; };
static void BlahajARConstraint(ImGuiSizeCallbackData* d)
{
    auto*  data = (BlahajARData*)d->UserData;
    float  ar   = data->ratio;
    int&   axis = *data->resizeAxis;

    // Reset lock when no drag is in progress
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f))
        axis = 0;

    // Lock the axis on first significant movement of a drag
    if (axis == 0)
    {
        float dx = fabsf(d->DesiredSize.x - d->CurrentSize.x);
        float dy = fabsf(d->DesiredSize.y - d->CurrentSize.y);
        if (dx > 1.f || dy > 1.f)
            axis = (dx >= dy) ? 1 : 2;
    }

    if (axis == 2)
        d->DesiredSize.x = d->DesiredSize.y * ar;  // height-primary
    else
        d->DesiredSize.y = d->DesiredSize.x / ar;  // width-primary (default)
}

void BlahajModule::RenderAnyways()
{
    // Flush textures queued for release last frame
    for (auto* t : m_PendingRelease) if (t) t->Release();
    m_PendingRelease.clear();

    if (!Enabled) return;

    // Sync instance vector length to count
    while ((int)m_Instances.size() < m_Count) m_Instances.push_back({});
    while ((int)m_Instances.size() > m_Count) m_Instances.pop_back();

    // Lazy-load on first enabled frame
    if (m_NeedsLoad) {
        IDirect3DDevice9* dev = Twinkie->GetD3DDevice();
        if (dev) Load(dev);
    }

    // If loading was attempted and failed, show a small error window
    if (!m_Loaded && !m_NeedsLoad) {
        ImGui::SetNextWindowSize(ImVec2(360.f, 60.f), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.05f, 0.05f, 0.92f));
        constexpr ImGuiWindowFlags kErrFlags =
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin("Blahaj##blahaj_err", nullptr, kErrFlags)) {
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "shark.gif not found!");
            ImGui::TextWrapped("Place it at: Documents\\TwinkPlanet\\media\\shark.gif");
        }
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    if (m_Frames.empty()) return;

    // Advance GIF animation (shared frame index across all instances)
    {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = (unsigned int)std::chrono::duration_cast<
                           std::chrono::milliseconds>(now - m_LastFrameTime).count();
        if (elapsed >= m_Frames[m_CurFrame].msDelay) {
            m_CurFrame      = (m_CurFrame + 1) % (int)m_Frames.size();
            m_LastFrameTime = now;
        }
    }

    bool menuVisible = *UiRenderEnabled;

    for (int i = 0; i < m_Count; i++)
    {
        BlahajInst& inst = m_Instances[i];

        // arData must stay alive through ImGui::Begin (which fires the callback)
        BlahajARData arData = { m_AspectRatio, &inst.resizeAxis };

        if (menuVisible) {
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(40.f, 40.f),
                ImVec2(4000.f, 4000.f),
                BlahajARConstraint,
                &arData
            );
        }

        if (!inst.sizeInit && m_Loaded) {
            float ar = (m_AspectRatio > 0.f) ? m_AspectRatio : 1.f;
            float h  = (inst.sizeH > 0.f) ? inst.sizeH : inst.sizeW / ar;
            ImGui::SetNextWindowSize(ImVec2(inst.sizeW, h), ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(inst.posX, inst.posY), ImGuiCond_Always);
            inst.sizeInit = true;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::SetNextWindowBgAlpha(0.f);

        ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar         |
            ImGuiWindowFlags_NoScrollbar        |
            ImGuiWindowFlags_NoScrollWithMouse  |
            ImGuiWindowFlags_NoCollapse         |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;

        if (!menuVisible)
            kFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoInputs;

        char wndId[32];
        snprintf(wndId, sizeof(wndId), "Blahaj##blahaj_%d", i);

        if (!ImGui::Begin(wndId, nullptr, kFlags)) {
            ImGui::End();
            ImGui::PopStyleVar(2);
            continue;
        }

        ImVec2 wsize = ImGui::GetWindowSize();
        ImVec2 wpos  = ImGui::GetWindowPos();

        // Track current pos/size so they get saved
        inst.sizeW = wsize.x;
        inst.sizeH = wsize.y;
        inst.posX  = wpos.x;
        inst.posY  = wpos.y;

        // InvisibleButton covers most of the window for drag-to-move.
        // Leave the bottom-right 14x14 px corner free so ImGui's resize grip works.
        if (menuVisible) {
            const float grip  = 14.f;
            ImVec2      btnSz = ImVec2(wsize.x - grip, wsize.y - grip);
            if (btnSz.x > 0.f && btnSz.y > 0.f) {
                ImGui::SetCursorPos(ImVec2(0.f, 0.f));
                ImGui::InvisibleButton("##drag", btnSz);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    ImGui::SetWindowPos(ImVec2(wpos.x + delta.x, wpos.y + delta.y));
                }
            }
        }

        ImGui::SetCursorPos(ImVec2(0.f, 0.f));
        ImGui::Image((ImTextureID)m_Frames[m_CurFrame].tex, wsize);

        // Instance number badge in bottom-left, only when menu is open
        if (menuVisible && m_Count > 1) {
            char badge[8];
            snprintf(badge, sizeof(badge), "%d", i + 1);
            ImVec2 ts      = ImGui::CalcTextSize(badge);
            float  pad     = 3.f;
            ImVec2 badgeMin = ImVec2(wpos.x + pad, wpos.y + wsize.y - ts.y - pad);
            ImVec2 badgeMax = ImVec2(badgeMin.x + ts.x + pad * 2.f, badgeMin.y + ts.y + pad);
            ImGui::GetWindowDrawList()->AddRectFilled(badgeMin, badgeMax, IM_COL32(0,0,0,160), 3.f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(badgeMin.x + pad, badgeMin.y + pad * 0.5f), IM_COL32(255,255,255,255), badge);
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

// =============================================================================
// RenderMenuItem
// =============================================================================
void BlahajModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_HEART " Blahaj", "", Enabled))
        Enabled = !Enabled;
}

// =============================================================================
// RenderSettings
// =============================================================================
void BlahajModule::RenderSettings()
{
    ImGui::Text("Blahajs: %d", m_Count);
    ImGui::SameLine();
    if (ImGui::Button("+##blahaj_add")) {
        m_Count++;
        m_Instances.push_back({});
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_Count <= 1);
    if (ImGui::Button("-##blahaj_rem")) {
        m_Count--;
        if (!m_Instances.empty()) m_Instances.pop_back();
    }
    ImGui::EndDisabled();
}

// =============================================================================
// Settings
// =============================================================================
void BlahajModule::SettingsInit(SettingMgr& Settings)
{
    Settings["Blahaj"]["Enable"].GetAsBool(&Enabled);
    Settings["Blahaj"]["Count"].GetAsInt(&m_Count);
    if (m_Count < 1) m_Count = 1;

    m_Instances.clear();
    for (int i = 0; i < m_Count; i++) {
        std::string sec = "Blahaj_" + std::to_string(i);
        BlahajInst inst;
        Settings[sec]["PosX"].GetAsFloat(&inst.posX);
        Settings[sec]["PosY"].GetAsFloat(&inst.posY);
        Settings[sec]["SizeW"].GetAsFloat(&inst.sizeW);
        Settings[sec]["SizeH"].GetAsFloat(&inst.sizeH);
        if (inst.sizeW < 40.f) inst.sizeW = 200.f;
        m_Instances.push_back(inst);
    }
}

void BlahajModule::SettingsSave(SettingMgr& Settings)
{
    Settings["Blahaj"]["Enable"].Set(Enabled);
    Settings["Blahaj"]["Count"].Set(m_Count);
    for (int i = 0; i < (int)m_Instances.size(); i++) {
        std::string sec = "Blahaj_" + std::to_string(i);
        Settings[sec]["PosX"].Set(m_Instances[i].posX);
        Settings[sec]["PosY"].Set(m_Instances[i].posY);
        Settings[sec]["SizeW"].Set(m_Instances[i].sizeW);
        Settings[sec]["SizeH"].Set(m_Instances[i].sizeH);
    }
}
