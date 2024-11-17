#include "stdafx.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>

#define spdlog_confparse(var) spdlog::info("Config Parse: {}: {}", #var, var)

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

// Fix details
std::string sFixName = "DAFix";
std::string sFixVersion = "0.0.1";
std::filesystem::path sFixPath;

// Ini
inipp::Ini<char> ini;
std::string sConfigFile = sFixName + ".ini";

// Logger
std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = sFixName + ".log";
std::filesystem::path sExePath;
std::string sExeName;

// Aspect ratio / FOV / HUD
std::pair DesktopDimensions = { 0,0 };
const float fPi = 3.1415926535f;
const float fNativeAspect = 1.777777791f;
float fAspectRatio;
float fAspectMultiplier;
float fHUDWidth;
float fHUDWidthOffset;
float fHUDHeight;
float fHUDHeightOffset;

// Ini variables
bool bBorderlessWindowed;
bool bDisablePillarboxing = true;
bool bFixAspect = true;
float fHUDScale = 0.00f;
float fFoliageDrawDistance = 1.50f;
float fObjectDrawDistance = 60.00f;
float fNPCDrawDistance = 60.00f;
int iShadowResolution = 1024;

// Variables
int iCurrentResX;
int iCurrentResY;
int iOldResX;
int iOldResY;

enum class Game {
    DA1,
    DA2,
    Unknown
};

struct GameInfo
{
    std::string GameTitle;
    std::string ExeName;
};

const std::map<Game, GameInfo> kGames = {
    {Game::DA1, {"Dragon Age: Origins", "DAOrigins.exe"}},
    {Game::DA2, {"Dragon Age II", "DragonAge2.exe"}},
};

const GameInfo* game = nullptr;
Game eGameType = Game::Unknown;

void Logging()
{
    // Get path to DLL
    WCHAR dllPath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
    sFixPath = dllPath;
    sFixPath = sFixPath.remove_filename();

    // Get game name and exe path
    WCHAR exePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(exeModule, exePath, MAX_PATH);
    sExePath = exePath;
    sExeName = sExePath.filename().string();
    sExePath = sExePath.remove_filename();

    // Spdlog initialisation
    try {
        logger = spdlog::basic_logger_st(sFixName.c_str(), sExePath.string() + sLogFile, true);
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::debug);

        spdlog::info("----------");
        spdlog::info("{:s} v{:s} loaded.", sFixName.c_str(), sFixVersion.c_str());
        spdlog::info("----------");
        spdlog::info("Log file: {}", sFixPath.string() + sLogFile);
        spdlog::info("----------");
        spdlog::info("Module Name: {0:s}", sExeName.c_str());
        spdlog::info("Module Path: {0:s}", sExePath.string());
        spdlog::info("Module Address: 0x{0:x}", (uintptr_t)exeModule);
        spdlog::info("Module Timestamp: {0:d}", Memory::ModuleTimestamp(exeModule));
        spdlog::info("----------");
    }
    catch (const spdlog::spdlog_ex& ex) {
        AllocConsole();
        FILE* dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "Log initialisation failed: " << ex.what() << std::endl;
        FreeLibraryAndExitThread(thisModule, 1);
    }  
}

void Configuration()
{
    // Inipp initialisation
    std::ifstream iniFile(sFixPath.string() + sConfigFile);
    if (!iniFile) {
        AllocConsole();
        FILE* dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "" << sFixName.c_str() << " v" << sFixVersion.c_str() << " loaded." << std::endl;
        std::cout << "ERROR: Could not locate config file." << std::endl;
        std::cout << "ERROR: Make sure " << sConfigFile.c_str() << " is located in " << sFixPath.string().c_str() << std::endl;
        spdlog::shutdown();
        FreeLibraryAndExitThread(thisModule, 1);
    }
    else {
        spdlog::info("Config file: {}", sFixPath.string() + sConfigFile);
        ini.parse(iniFile);
    }

    // Parse config
    ini.strip_trailing_comments();
    spdlog::info("----------");

    // Load settings from ini
    inipp::get_value(ini.sections["Borderless Windowed"], "Enabled", bBorderlessWindowed);
    inipp::get_value(ini.sections["Fix Aspect Ratio"], "Enabled", bFixAspect);
    inipp::get_value(ini.sections["Disable Pillarboxing"], "Enabled", bDisablePillarboxing);
    inipp::get_value(ini.sections["HUD Scale"], "Scale", fHUDScale);
    inipp::get_value(ini.sections["Draw Distances"], "Foliage", fFoliageDrawDistance);
    inipp::get_value(ini.sections["Draw Distances"], "NPC", fNPCDrawDistance);
    inipp::get_value(ini.sections["Draw Distances"], "Object", fObjectDrawDistance);
    inipp::get_value(ini.sections["Shadow Resolution"], "Resolution", iShadowResolution);

    // Log ini parse
    spdlog_confparse(bBorderlessWindowed);
    spdlog_confparse(bFixAspect);
    spdlog_confparse(bDisablePillarboxing);
    spdlog_confparse(fHUDScale);
    spdlog_confparse(fFoliageDrawDistance);
    spdlog_confparse(fNPCDrawDistance);
    spdlog_confparse(fObjectDrawDistance);
    spdlog_confparse(iShadowResolution);

    spdlog::info("----------");
}

bool DetectGame()
{
    for (const auto& [type, info] : kGames) {
        if (Util::stringcmp_caseless(info.ExeName, sExeName)) {
            spdlog::info("Detected game: {:s} ({:s})", info.GameTitle, sExeName);
            spdlog::info("----------");
            eGameType = type;
            game = &info;
            return true;
        }
    }

    spdlog::error("Failed to detect supported game, {:s} isn't supported by DAFix.", sExeName);
    return false;
}

void CalculateAspectRatio(bool bLog)
{
    if (iCurrentResX <= 0 || iCurrentResY <= 0)
        return;

    // Calculate aspect ratio
    fAspectRatio = (float)iCurrentResX / (float)iCurrentResY;
    fAspectMultiplier = fAspectRatio / fNativeAspect;

    // HUD 
    fHUDWidth = (float)iCurrentResY * fNativeAspect;
    fHUDHeight = (float)iCurrentResY;
    fHUDWidthOffset = (float)(iCurrentResX - fHUDWidth) / 2.00f;
    fHUDHeightOffset = 0.00f;
    if (fAspectRatio < fNativeAspect) {
        fHUDWidth = (float)iCurrentResX;
        fHUDHeight = (float)iCurrentResX / fNativeAspect;
        fHUDWidthOffset = 0.00f;
        fHUDHeightOffset = (float)(iCurrentResY - fHUDHeight) / 2.00f;
    }

    // Log details about current resolution
    if (bLog) {
        spdlog::info("----------");
        spdlog::info("Current Resolution: Resolution: {:d}x{:d}", iCurrentResX, iCurrentResY);
        spdlog::info("Current Resolution: fAspectRatio: {}", fAspectRatio);
        spdlog::info("Current Resolution: fAspectMultiplier: {}", fAspectMultiplier);
        spdlog::info("Current Resolution: fHUDWidth: {}", fHUDWidth);
        spdlog::info("Current Resolution: fHUDHeight: {}", fHUDHeight);
        spdlog::info("Current Resolution: fHUDWidthOffset: {}", fHUDWidthOffset);
        spdlog::info("Current Resolution: fHUDHeightOffset: {}", fHUDHeightOffset);
        spdlog::info("----------");
    }
}

void GameInit()
{
    // Wait up to 30s for the game to initialise
    std::uint8_t* GameInitScanResult = nullptr;
    for (int attempt = 1; attempt <= 150; ++attempt) {
        GameInitScanResult = Memory::PatternScan(exeModule, "D9 ?? ?? ?? D9 ?? ?? ?? ?? ?? 32 ?? 5E 8B ?? 5D C2 ?? ??");
        if (GameInitScanResult) {
            spdlog::info("Game initialisation complete.");
            spdlog::info("----------");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!GameInitScanResult) {
        spdlog::error("Failed to detect game initialisation.");
        spdlog::shutdown();
        FreeLibraryAndExitThread(thisModule, 1);
    }
}

void CurrentResolution()
{
    if (eGameType == Game::DA1 || eGameType == Game::DA2) {
        // DA1/DA2: Current Resolution
        std::uint8_t* CurrentResolutionScanResult = Memory::MultiPatternScan(exeModule, { "D9 ?? ?? ?? ?? ?? 85 ?? DB ?? ?? ?? ?? ?? ?? 7D ?? D8 ?? ?? ?? ?? ??", "DB ?? ?? ?? ?? ?? ?? 85 ?? 7D ?? D8 ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? ?? D9 ?? ?? ?? ?? ?? D9 ??" });
        if (CurrentResolutionScanResult) {
            spdlog::info("DA1/DA2: Current Resolution: Address is {:s}+{:x}", sExeName.c_str(), CurrentResolutionScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid CurrentResolutionMidHook{};
            CurrentResolutionMidHook = safetyhook::create_mid(CurrentResolutionScanResult,
                [](SafetyHookContext& ctx) {
                    int iResX = ctx.eax;
                    int iResY = ctx.ecx;
                    if (iResX != iCurrentResX || iResY != iCurrentResY) {
                        iCurrentResX = iResX;
                        iCurrentResY = iResY;
                        CalculateAspectRatio(true);
                    }
                });
        }
        else {
            spdlog::error("DA1/DA2: Current Resolution: Pattern scan failed.");
        }
    }
}

void WindowManagement()
{
    if (eGameType == Game::DA1 && bBorderlessWindowed) {
        // DA1: Borderless Windowed
        std::uint8_t* DA1_SetWindowLongWScanResult = Memory::PatternScan(exeModule, "74 ?? 8B ?? ?? ?? ?? ?? ?? ?? 50 FF ?? ?? ?? ?? ?? 5E C3");
        if (DA1_SetWindowLongWScanResult) {
            spdlog::info("DA1: Borderless: SetWindowLongW: Address is {:s}+{:x}", sExeName.c_str(), DA1_SetWindowLongWScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid DA1_SetWindowLongWMidHook{};
            DA1_SetWindowLongWMidHook = safetyhook::create_mid(DA1_SetWindowLongWScanResult,
                [](SafetyHookContext& ctx) {
                    if (ctx.esi + 0x168) {
                        // Get HWND
                        HWND hWnd = *reinterpret_cast<HWND*>(ctx.esi + 0x168);

                        // Get styles
                        LONG lStyle = GetWindowLongW(hWnd, GWL_STYLE);
                        LONG lExStyle = GetWindowLongW(hWnd, GWL_EXSTYLE);

                        // Apply borderless style
                        lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
                        lExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
                        SetWindowLongW(hWnd, GWL_STYLE, lStyle);
                        SetWindowLongW(hWnd, GWL_EXSTYLE, lExStyle);
                    }
                });
        }
        else {
            spdlog::error("DA1: Borderless: SetWindowLongW: Pattern scan failed.");
        }
    }
}

void AspectRatio()
{   
    if (eGameType == Game::DA1 && bFixAspect) {
        // DA1: Speedtree Culling
        std::uint8_t* DA1_SpeedtreeCullingScanResult = Memory::PatternScan(exeModule, "F6 ?? 05 7B ?? D9 ?? ?? ?? ?? ?? DE ?? D9 ?? ?? ?? ?? ??");
        if (DA1_SpeedtreeCullingScanResult) {
            spdlog::info("DA1: Aspect Ratio: Speedtree Culling: Address is {:s}+{:x}", sExeName.c_str(), DA1_SpeedtreeCullingScanResult - (std::uint8_t*)exeModule);
            Memory::PatchBytes(DA1_SpeedtreeCullingScanResult + 0x2, "\x00", 1);
            spdlog::info("DA1: Aspect Ratio: Speedtree Culling: Patched instruction.");
        }
        else {
            spdlog::error("DA1: Aspect Ratio: Speedtree Culling: Pattern scan failed.");
        }

        // DA1: Shadow Aspect Ratio
        std::uint8_t* DA1_ShadowAspectRatioScanResult = Memory::PatternScan(exeModule, "8B ?? 8B ?? ?? ?? ?? ?? 8B ?? FF ?? DC ?? ?? ?? ?? ?? D9 ?? ?? ?? D9 ?? ?? ?? E8 ?? ?? ?? ??");
        if (DA1_ShadowAspectRatioScanResult) {
            spdlog::info("DA1: Aspect Ratio: Shadows: Address is {:s}+{:x}", sExeName.c_str(), DA1_ShadowAspectRatioScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid DA1_ShadowAspectRatioMidHook{};
            DA1_ShadowAspectRatioMidHook = safetyhook::create_mid(DA1_ShadowAspectRatioScanResult,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio > fNativeAspect && ctx.esp)
                        *reinterpret_cast<float*>(ctx.esp + 0xC) = fNativeAspect;
                });
        }
        else {
            spdlog::error("DA1: Aspect Ratio: Shadows: Pattern scan failed.");
        }    
    }
    
    if (eGameType == Game::DA1 && bDisablePillarboxing) {
        // DA1: Dialog Pillarboxing
        std::uint8_t* DA1_PillarboxingScanResult = Memory::PatternScan(exeModule, "FF ?? 8B ?? ?? ?? ?? ?? 85 C0 74 ?? C6 ?? ?? 01");
        if (DA1_PillarboxingScanResult) {
            spdlog::info("DA1: Aspect Ratio: Dialog Pillarboxing: Address is {:s}+{:x}", sExeName.c_str(), DA1_PillarboxingScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid DA1_PillarboxingMidHook{};
            DA1_PillarboxingMidHook = safetyhook::create_mid(DA1_PillarboxingScanResult,
                [](SafetyHookContext& ctx) {
                    if (ctx.esp) {
                        *reinterpret_cast<int*>(ctx.esp + 0x0) = 0;             // Left
                        *reinterpret_cast<int*>(ctx.esp + 0x4) = 0;             // Right
                        *reinterpret_cast<int*>(ctx.esp + 0x8) = iCurrentResX;  // Width
                        *reinterpret_cast<int*>(ctx.esp + 0xC) = iCurrentResY;  // Height
                    }
                });
        }
        else {
            spdlog::error("DA1: Aspect Ratio: Dialog Pillarboxing: Pattern scan failed.");
        }
    }
    else if (eGameType == Game::DA2 && bDisablePillarboxing) {
        // DA2: Dialog Pillarboxing
        std::uint8_t* DA2_PillarboxingScanResult = Memory::PatternScan(exeModule, "89 ?? ?? ?? 89 ?? ?? ?? EB ?? DD ?? DE ?? DF ?? F6 ?? ?? 7A ??");
        if (DA2_PillarboxingScanResult) {
            spdlog::info("DA2: Aspect Ratio: Dialog Pillarboxing: Address is {:s}+{:x}", sExeName.c_str(), DA2_PillarboxingScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid DA2_PillarboxingMidHook{};
            DA2_PillarboxingMidHook = safetyhook::create_mid(DA2_PillarboxingScanResult,
                [](SafetyHookContext& ctx) {
                    ctx.ebx = 0;            // Left
                    ctx.ebp = 0;            // Right
                    ctx.ecx = iCurrentResX; // Width
                    ctx.edx = iCurrentResY; // Height
                });
        }
        else {
            spdlog::error("DA2: Aspect Ratio: Dialog Pillarboxing: Pattern scan failed.");
        }
    }   
}

void FOV()
{
    if ((eGameType == Game::DA1 || eGameType == Game::DA2) && bDisablePillarboxing) {
        // DA1/DA2: Dialog FOV
        std::uint8_t* DA1_DA2_DialogFOVScanResult = Memory::PatternScan(exeModule, "D9 ?? ?? ?? D9 ?? ?? ?? ?? ?? 32 ?? 5E 8B ?? 5D C2 ?? ??");
        if (DA1_DA2_DialogFOVScanResult) {
            spdlog::info("DA1/DA2: FOV: Dialog: Address is {:s}+{:x}", sExeName.c_str(), DA1_DA2_DialogFOVScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid DA1_DA2_DialogFOVMidHook{};
            DA1_DA2_DialogFOVMidHook = safetyhook::create_mid(DA1_DA2_DialogFOVScanResult,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio > fNativeAspect && ctx.esp)
                        *reinterpret_cast<float*>(ctx.esp + 0xC) = atanf(tanf(*reinterpret_cast<float*>(ctx.esp + 0xC) * (fPi / 360)) / fNativeAspect * fAspectRatio) * (360 / fPi);
                });
        }
        else {
            spdlog::error("DA1/DA2: FOV: Dialog: Pattern scan failed.");
        }
    }
}

void HUD()
{
    if (eGameType == Game::DA1 && fHUDScale >= 0.00f && fHUDScale <= 1.00f) {
        // DA1: HUD Scale
        std::uint8_t* DA1_HUDScaleScanResult = Memory::PatternScan(exeModule, "D9 ?? ?? ?? 8B ?? D9 ?? ?? ?? D9 ?? ?? ?? 8B ?? ?? 53");
        if (DA1_HUDScaleScanResult) {
            spdlog::info("DA1: HUD: HUD Scale: Address is {:s}+{:x}", sExeName.c_str(), DA1_HUDScaleScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid DA1_HUDScaleMidHook{};
            DA1_HUDScaleMidHook = safetyhook::create_mid(DA1_HUDScaleScanResult,
                [](SafetyHookContext& ctx) {
                    if (ctx.esp) {
                        if (fHUDScale == 0.00f) {
                            // Automatic HUD scale
                            if (fAspectRatio > 1.333333f && iCurrentResY > 768) {
                                *reinterpret_cast<float*>(ctx.esp + 0x08) = 768.00f / (float)iCurrentResY;
                            }
                            else if (fAspectRatio <= 1.33333f && iCurrentResX > 1024) {
                                *reinterpret_cast<float*>(ctx.esp + 0x08) = 1024.00f / (float)iCurrentResX;
                            }
                        }
                        else {
                            // Custom HUD scale
                            *reinterpret_cast<float*>(ctx.esp + 0x08) = fHUDScale;
                        }
                    }
                });
        }
        else {
            spdlog::error("DA1: HUD: HUD Scale: Pattern scan failed.");
        }       
    }
}

void Graphics()
{
    if (eGameType == Game::DA1 && fFoliageDrawDistance > 0.00f && fNPCDrawDistance > 0.00f && fObjectDrawDistance > 0.00f) {
        // DA1: Foliage & Object Draw Distance
        std::uint8_t* DA1_FoliageDrawDistanceScanResult = Memory::PatternScan(exeModule, "D9 ?? ?? ?? ?? ?? D9 ?? ?? ?? ?? ?? D9 ?? ?? ?? ?? ?? EB ?? D9 ?? ?? ?? ?? ?? D9 ?? ?? ?? ?? ??");
        std::uint8_t* DA1_ObjectDrawDistanceScanResult = Memory::PatternScan(exeModule, "D9 ?? ?? ?? ?? ?? DE ?? DF ?? F6 ?? ?? 74 ?? C6 ?? ?? ?? 00");
        if (DA1_FoliageDrawDistanceScanResult && DA1_ObjectDrawDistanceScanResult) {
            spdlog::info("DA1: Graphics: Draw Distance: Foliage: Scan address is {:s}+{:x}", sExeName.c_str(), DA1_FoliageDrawDistanceScanResult - (std::uint8_t*)exeModule);
            spdlog::info("DA1: Graphics: Draw Distance: Object: Scan address is {:s}+{:x}", sExeName.c_str(), DA1_ObjectDrawDistanceScanResult - (std::uint8_t*)exeModule);

            std::uint8_t* FoliageDrawDistance = (std::uint8_t*)*reinterpret_cast<std::uint32_t*>(DA1_FoliageDrawDistanceScanResult + 0x2);
            spdlog::info("DA1: Graphics: Draw Distance: Foliage: Address is {:s}+{:x}", sExeName.c_str(), FoliageDrawDistance - (std::uint8_t*)exeModule);
            std::uint8_t* NPCDrawDistance = (std::uint8_t*)*reinterpret_cast<std::uint32_t*>(DA1_FoliageDrawDistanceScanResult + 0xE);
            spdlog::info("DA1: Graphics: Draw Distance: NPC: Address is {:s}+{:x}", sExeName.c_str(), NPCDrawDistance - (std::uint8_t*)exeModule);

            std::uint8_t* ObjectDrawDistance = (std::uint8_t*)*reinterpret_cast<std::uint32_t*>(DA1_ObjectDrawDistanceScanResult + 0x2);
            spdlog::info("DA1: Graphics: Draw Distance: Object: Address is {:s}+{:x}", sExeName.c_str(), ObjectDrawDistance - (std::uint8_t*)exeModule);

            Memory::Write(FoliageDrawDistance, fFoliageDrawDistance);                   // Default very high = 1.5f
            Memory::Write(NPCDrawDistance, fNPCDrawDistance);                           // Default very high = 60.0f
            Memory::Write(ObjectDrawDistance, fObjectDrawDistance);                     // Default = 60.0f
        }
        else {
            spdlog::error("DA1: Graphics: Draw Distance: Pattern scan(s) failed.");
        }     
    }

    if (eGameType == Game::DA1 && iShadowResolution != 1024) {
        // DA1: Shadow Resolution
        std::uint8_t* DA1_ShadowResolutionScanResult = Memory::PatternScan(exeModule, "C7 ?? ?? ?? ?? ?? 00 04 00 00 56 57 E8 ?? ?? ?? ??");
        if (DA1_ShadowResolutionScanResult) {
            spdlog::info("DA1: Graphics: Shadow Resolution: Address is {:s}+{:x}", sExeName.c_str(), DA1_ShadowResolutionScanResult - (std::uint8_t*)exeModule);
            Memory::Write(DA1_ShadowResolutionScanResult + 0x6, iShadowResolution);      // Default very high = 1024
            spdlog::info("DA1: Graphics: Shadow Resolution: Patched instruction.");
        }
        else {
            spdlog::error("DA1: Graphics: Shadow Resolution: Pattern scan failed.");
        }
    }
}

DWORD __stdcall Main(void*)
{
    Logging();
    Configuration();
    if (DetectGame()) {
        GameInit();
        CurrentResolution();
        WindowManagement();
        AspectRatio();
        FOV();
        HUD();
        Graphics();
    }
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        thisModule = hModule;
        HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}