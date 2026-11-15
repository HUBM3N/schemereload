// schemereload: inject into TF2 (x64). hud_reloadscheme then ALSO rereads:
//   - every loaded scheme (ClientScheme.res, SourceScheme.res + their #base files): colors, fonts, borders
//   - every loaded .vtf / .vmt under tf\custom\<any>\materials (hud images, bitmap font pages)
//   - localization files (chat_english.txt, tf_english.txt ...)
//   then reapplies EVERY panel (main menu, GameUI too), not just the hud
// No MinHook: the ConCommand's callback pointer is swapped for ours.
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ICvar VEngineCvar004: FindVar 12, GetCommands 16 -> FindCommand 14
constexpr int kCvFindCommand = 14;
// ConCommand (tier1/convar.h): name +0x18, callback +0x30, bitfield byte +0x40 (bit 1 = new-style callback)
constexpr size_t kCmdName = 0x18, kCmdCallback = 0x30, kCmdBits = 0x40;
// vgui2 CSchemeManager VGUI_Scheme010: 8 GetIScheme, 12 LoadSchemeFromFileEx, scheme count +0x18
constexpr int kSmGetIScheme = 8, kSmLoadSchemeFromFileEx = 12;
constexpr size_t kSmCount = 0x18;
// vgui2 CScheme: name[256] +8, tag[64] +0x108, body (data, borders, fonts, colors, sizing) +0x148..0x208
constexpr size_t kScName = 0x8, kScTag = 0x108, kScBody = 0x148, kScEnd = 0x208;
constexpr int kScGetSizingPanel = 12;   // IScheme
// vguimatsurface VGUI_Surface031: 38 GetScreenSize, 111 OnScreenSizeChanged (= resolution change:
// font caches rebuilt, OnScreenSizeChanged posted to every panel)
constexpr int kSurfGetScreenSize = 38, kSurfOnScreenSizeChanged = 111;
// materialsystem VMaterialSystem082
constexpr int kMsFindMaterial = 71, kMsIsMaterialLoaded = 72, kMsFindTexture = 79, kMsIsTextureLoaded = 80;
constexpr int kMsLock = 102, kMsUnlock = 103;
// ITexture 13 Download (no rect = reread from disk) 15 IsError, IMaterial 37 Refresh (vmt reparse) 42 IsErrorMaterial
constexpr int kTexDownload = 13, kTexIsError = 15, kMatRefresh = 37, kMatIsError = 42;
// vgui2 VGUI_Localize005: 14 ReloadLocalizationFiles
constexpr int kLocReload = 14;

using Callback = void (*)(const void* args);   // FnCommandCallback_t(const CCommand&)

Callback g_orig = nullptr;
char*    g_cmd  = nullptr;
void*    g_sm = nullptr, *g_ms = nullptr, *g_loc = nullptr, *g_surf = nullptr;
void (*g_msg)(const char*, ...) = nullptr;     // tier0 Msg = game console

template <class R = void, class... A> R VCall(void* self, int idx, A... a)
{
    return (*reinterpret_cast<R(***)(void*, A...)>(self))[idx](self, a...);
}

void* Iface(const char* dll, const char* name)
{
    const HMODULE m = GetModuleHandleA(dll);
    const auto ci = m ? reinterpret_cast<void* (*)(const char*, int*)>(GetProcAddress(m, "CreateInterface")) : nullptr;
    return ci ? ci(name, nullptr) : nullptr;
}

void Log(const char* fmt, ...)
{
    char b[512];
    va_list v;
    va_start(v, fmt);
    vsnprintf(b, sizeof b, fmt, v);
    va_end(v);
    if (!g_msg) if (const HMODULE t = GetModuleHandleA("tier0.dll")) g_msg = reinterpret_cast<decltype(g_msg)>(GetProcAddress(t, "Msg"));
    if (g_msg) g_msg("[schemereload] %s\n", b);
}

// vgui2 hands back the scheme it already holds for a name and never rereads the file. So hide the
// old scheme's name + tag, load the same file again (fresh scheme), put the name back and swap the
// two CScheme bodies: same handle, fresh fonts / colors / borders. The old body stays alive (renamed)
// for panels still holding its fonts / borders. Raw memory only, so SEH; -1 = faulted (layout
// changed by a TF2 update).
int ReloadSchemes()
{
    static int retired = 0;
    int done = 0;
    __try
    {
        char* sm = static_cast<char*>(g_sm);
        const int count = *reinterpret_cast<int*>(sm + kSmCount);
        for (int h = 1; h < count && h < 1024; ++h)   // 0 = null scheme; fresh loads land past count
        {
            char* a = VCall<char*>(g_sm, kSmGetIScheme, (unsigned long)h);
            if (!a || !a[kScName] || !strncmp(a + kScName, "sr_old_", 7)) continue;
            char name[256], tag[64];
            memcpy(name, a + kScName, sizeof name);
            memcpy(tag, a + kScTag, sizeof tag);
            name[255] = tag[63] = 0;
            strcpy_s(a + kScName, 256, "sr_tmp");
            strcpy_s(a + kScTag, 64, "sr_tmp");
            const int before = *reinterpret_cast<int*>(sm + kSmCount);
            unsigned long hb = 0;
            __try { hb = VCall<unsigned long>(g_sm, kSmLoadSchemeFromFileEx, VCall<uintptr_t>(a, kScGetSizingPanel), (const char*)name, (const char*)tag); }
            __except (EXCEPTION_EXECUTE_HANDLER) { hb = 0; }
            memcpy(a + kScName, name, sizeof name);   // name back before anything can look it up
            memcpy(a + kScTag, tag, sizeof tag);
            // handle below `before` = no fresh load (file missing, or vgui2 matched another scheme)
            char* b = hb >= (unsigned long)before ? VCall<char*>(g_sm, kSmGetIScheme, hb) : nullptr;
            if (!b || b == a) { Log("  %s (%s): NOT reloaded", name, tag); continue; }
            const bool same = *reinterpret_cast<void**>(a) == *reinterpret_cast<void**>(b);   // same class
            if (same)
            {
                char tmp[kScEnd - kScBody];
                memcpy(tmp, a + kScBody, sizeof tmp);
                memcpy(a + kScBody, b + kScBody, sizeof tmp);
                memcpy(b + kScBody, tmp, sizeof tmp);
                ++done;
            }
            // ponytail: retired bodies never freed (vgui2 has no scheme unload) and their fonts get
            // rebuilt on every font reset, so each reload is a bit slower; restart TF2 after a few
            // hundred. Renamed so no name / tag lookup lands on them.
            snprintf(b + kScName, 256, "sr_old_%d", retired);
            snprintf(b + kScTag, 64, "sr_old_%d", retired++);
            Log("  %s (%s): %s", name, tag, same ? "reloaded" : "NOT reloaded (class mismatch)");
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return done;
}

// only what is already loaded is reread, so walking every addon is cheap
int RefreshMaterials()
{
    std::vector<std::string> tex, mat;
    try
    {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);   // <tf2>\tf_win64.exe
        for (const auto& addon : fs::directory_iterator(fs::path(exe).parent_path() / "tf" / "custom"))
        {
            const fs::path root = addon.path() / "materials";
            if (!fs::is_directory(root)) continue;   // also skips addon .vpk files
            for (const auto& f : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied))
            {
                std::string ext = f.path().extension().string(), name = f.path().lexically_relative(root).replace_extension().generic_string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                for (char& c : name) c = (char)tolower((unsigned char)c);   // material names: lowercase, '/', no ext
                if (ext == ".vtf") tex.push_back(name);
                else if (ext == ".vmt") mat.push_back(name);
            }
        }
    }
    catch (...) {}   // unreadable path / non-ascii name: keep what was collected
    int n = 0;
    for (const auto& t : tex)   // Download locks the material system itself
    {
        if (!VCall<bool>(g_ms, kMsIsTextureLoaded, t.c_str())) continue;
        void* tx = VCall<void*>(g_ms, kMsFindTexture, t.c_str(), "VGUI", false, 0);
        if (tx && !VCall<bool>(tx, kTexIsError)) { VCall(tx, kTexDownload, (void*)nullptr, 0); ++n; }
    }
    void* lock = VCall<void*>(g_ms, kMsLock);   // Refresh does not: hold the render thread off
    for (const auto& m : mat)
    {
        if (!VCall<bool>(g_ms, kMsIsMaterialLoaded, m.c_str())) continue;
        void* mt = VCall<void*>(g_ms, kMsFindMaterial, m.c_str(), "Other textures", false, (const char*)nullptr);
        if (mt && !VCall<bool>(mt, kMatIsError)) { VCall(mt, kMatRefresh); ++n; }
    }
    VCall(g_ms, kMsUnlock, lock);
    return n;
}

// runs on the main thread, like every console command
void Hooked(const void* args)
{
    const DWORD t0 = GetTickCount();
    g_orig(args);   // first: flushes the KeyValues file cache (else stale #base files)
    const int schemes = g_sm ? ReloadSchemes() : -1;
    const int mats = g_ms ? RefreshMaterials() : -1;
    if (g_loc) VCall(g_loc, kLocReload);   // before the reapply: labels resolve #tokens on apply
    // Stock hud_reloadscheme only reapplies the hud. Main menu, dashboard and GameUI hang off other
    // vgui branches and would keep the old fonts / colors. A resolution change to the same size
    // reapplies every panel and rebuilds every font; TF2 runs it on each video settings Apply.
    int w = 0, h = 0;
    if (g_surf) VCall(g_surf, kSurfGetScreenSize, &w, &h);
    if (w > 0 && h > 0) VCall(g_surf, kSurfOnScreenSizeChanged, w, h);
    else g_orig(args);   // no surface: at least the hud
    Log("%d schemes, %d materials, strings reloaded, %s (%lu ms)", schemes, mats,
        w > 0 ? "all panels reapplied" : "hud only reapplied", GetTickCount() - t0);
}

char* FindReloadCmd()
{
    void* cvar = Iface("vstdlib.dll", "VEngineCvar004");
    if (!cvar) return nullptr;
    __try
    {
        char* c = VCall<char*>(cvar, kCvFindCommand, "hud_reloadscheme");
        return c && !strcmp(*reinterpret_cast<const char**>(c + kCmdName), "hud_reloadscheme") ? c : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

DWORD WINAPI Init(LPVOID)
{
    char* cmd;
    for (int i = 0; !(cmd = FindReloadCmd()); ++i)   // client.dll registers it at boot: inject any time
    {
        if (i == 240) Log("hud_reloadscheme not found after 60 s, still waiting");
        Sleep(250);
    }
    if (!(cmd[kCmdBits] & 2)) { Log("hud_reloadscheme uses an old-style callback, NOT hooked"); return 0; }
    g_sm  = Iface("vgui2.dll", "VGUI_Scheme010");
    g_ms  = Iface("materialsystem.dll", "VMaterialSystem082");
    g_loc = Iface("vgui2.dll", "VGUI_Localize005");
    if (!(g_surf = Iface("vguimatsurface.dll", "VGUI_Surface031"))) g_surf = Iface("vguimatsurface.dll", "VGUI_Surface030");
    g_orig = *reinterpret_cast<Callback*>(cmd + kCmdCallback);
    g_cmd = cmd;
    *reinterpret_cast<Callback*>(cmd + kCmdCallback) = Hooked;   // aligned 8-byte store: atomic vs main thread
    Log("loaded (schemes %s, materials %s, strings %s, panels %s). hud_reloadscheme now reloads everything.",
        g_sm ? "ok" : "MISSING", g_ms ? "ok" : "MISSING", g_loc ? "ok" : "MISSING", g_surf ? "ok" : "MISSING");
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        if (HANDLE t = CreateThread(nullptr, 0, Init, nullptr, 0, nullptr)) CloseHandle(t);
    }
    else if (reason == DLL_PROCESS_DETACH && !reserved && g_cmd)   // FreeLibrary (not game exit): put it back
        *reinterpret_cast<Callback*>(g_cmd + kCmdCallback) = g_orig;
    return TRUE;
}
