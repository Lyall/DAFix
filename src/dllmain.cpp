#include "stdafx.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>

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

    // spdlog initialisation
    {
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
}

void Configuration()
{
    // inipp initialisation
    std::ifstream iniFile(sFixPath.string() + sConfigFile);
    if (!iniFile) {
        AllocConsole();
        FILE* dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "" << sFixName.c_str() << " v" << sFixVersion.c_str() << " loaded." << std::endl;
        std::cout << "ERROR: Could not locate config file." << std::endl;
        std::cout << "ERROR: Make sure " << sConfigFile.c_str() << " is located in " << sFixPath.string().c_str() << std::endl;
        FreeLibraryAndExitThread(thisModule, 1);
        return;
    }
    else {
        spdlog::info("Config file: {}", sFixPath.string() + sConfigFile);
        ini.parse(iniFile);
    }

    // Parse config
    ini.strip_trailing_comments();
    spdlog::info("----------");

    inipp::get_value(ini.sections["Fix Aspect Ratio"], "Enabled", bFixAspect);
    spdlog::info("Config Parse: bFixAspect: {}", bFixAspect);

    inipp::get_value(ini.sections["Disable Pillarboxing"], "Enabled", bDisablePillarboxing);
    spdlog::info("Config Parse: bDisablePillarboxing: {}", bDisablePillarboxing);

    inipp::get_value(ini.sections["HUD Scale"], "Scale", fHUDScale);
    spdlog::info("Config Parse: fHUDScale: {}", fHUDScale);

    inipp::get_value(ini.sections["Draw Distances"], "Foliage", fFoliageDrawDistance);
    spdlog::info("Config Parse: fFoliageDrawDistance: {}", fFoliageDrawDistance);
    inipp::get_value(ini.sections["Draw Distances"], "NPC", fNPCDrawDistance);
    spdlog::info("Config Parse: fNPCDrawDistance: {}", fNPCDrawDistance);
    inipp::get_value(ini.sections["Draw Distances"], "Object", fObjectDrawDistance);
    spdlog::info("Config Parse: fObjectDrawDistance: {}", fObjectDrawDistance);

    inipp::get_value(ini.sections["Shadow Resolution"], "Resolution", iShadowResolution);
    spdlog::info("Config Parse: iShadowResolution: {}", iShadowResolution);

    spdlog::info("----------");
}

bool DetectGame()
{
    for (const auto& [type, info] : kGames)
    {
        if (Util::stringcmp_caseless(info.ExeName, sExeName))
        {
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

void AspectRatio()
{   
    if (bFixAspect) {
        if (eGameType == Game::DA1) {
            // Speedtree Culling
            std::uint8_t* SpeedtreeCullingScanResult = Memory::PatternScan(exeModule, "F6 ?? 05 7B ?? D9 ?? ?? ?? ?? ?? DE ?? D9 ?? ?? ?? ?? ??");
            if (SpeedtreeCullingScanResult) {
                spdlog::info("DA1: Aspect Ratio: Speedtree Culling: Address is {:s}+{:x}", sExeName.c_str(), SpeedtreeCullingScanResult - (std::uint8_t*)exeModule);
                Memory::PatchBytes(SpeedtreeCullingScanResult + 0x2, "\x00", 1);
                spdlog::info("DA1: Aspect Ratio: Speedtree Culling: Patched instruction.");
            }
            else {
                spdlog::error("DA1: Aspect Ratio: Speedtree Culling: Pattern scan failed.");
            }

            // Shadow Aspect Ratio
            std::uint8_t* ShadowAspectRatioScanResult = Memory::PatternScan(exeModule, "8B ?? 8B ?? ?? ?? ?? ?? 8B ?? FF ?? DC ?? ?? ?? ?? ?? D9 ?? ?? ?? D9 ?? ?? ?? E8 ?? ?? ?? ??");
            if (ShadowAspectRatioScanResult) {
                spdlog::info("DA1: Aspect Ratio: Shadows: Address is {:s}+{:x}", sExeName.c_str(), ShadowAspectRatioScanResult - (std::uint8_t*)exeModule);
                static SafetyHookMid ShadowAspectRatioMidHook{};
                ShadowAspectRatioMidHook = safetyhook::create_mid(ShadowAspectRatioScanResult,
                    [](SafetyHookContext& ctx) {
                        if (fAspectRatio > fNativeAspect && ctx.esp)
                            *reinterpret_cast<float*>(ctx.esp + 0xC) = fNativeAspect;
                    });
            }
            else {
                spdlog::error("DA1: Aspect Ratio: Shadows: Pattern scan failed.");
            }
        }
    }
    
    if (bDisablePillarboxing) {
        if (eGameType == Game::DA1) {
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
        else if (eGameType == Game::DA2) {
            std::uint8_t* DA2_PillarboxingScanResult = Memory::PatternScan(exeModule, "F6 ?? 05 7A ?? 8B ?? ?? ?? ?? ?? 85 C0 74 ?? C6 ?? ?? 01");
            if (DA2_PillarboxingScanResult) {
                spdlog::info("DA2: Aspect Ratio: Dialog Pillarboxing: Address is {:s}+{:x}", sExeName.c_str(), DA2_PillarboxingScanResult - (std::uint8_t*)exeModule);
                static SafetyHookMid DA2_PillarboxingMidHook{};
                DA2_PillarboxingMidHook = safetyhook::create_mid(DA2_PillarboxingScanResult,
                    [](SafetyHookContext& ctx) {
                        if (ctx.esi) {
                            *reinterpret_cast<int*>(ctx.esi + 0x38) = 0;            // Left
                            *reinterpret_cast<int*>(ctx.esi + 0x3C) = 0;            // Right
                            *reinterpret_cast<int*>(ctx.esi + 0x40) = iCurrentResX; // Width
                            *reinterpret_cast<int*>(ctx.esi + 0x44) = iCurrentResY; // Height
                        }
                    });
            }
            else {
                spdlog::error("DA2: Aspect Ratio: Dialog Pillarboxing: Pattern scan failed.");
            }
        }
    }
}

void FOV()
{
    if ((eGameType == Game::DA1 || eGameType == Game::DA2) && bDisablePillarboxing) {
        // DA1/DA2: Dialog FOV
        std::uint8_t* DialogFOVScanResult = Memory::PatternScan(exeModule, "D9 ?? ?? ?? D9 ?? ?? ?? ?? ?? 32 ?? 5E 8B ?? 5D C2 ?? ??");
        if (DialogFOVScanResult) {
            spdlog::info("DA1/DA2: FOV: Dialog: Address is {:s}+{:x}", sExeName.c_str(), DialogFOVScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid DialogFOVMidHook{};
            DialogFOVMidHook = safetyhook::create_mid(DialogFOVScanResult,
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
    if (fHUDScale >= 0.00f && fHUDScale <= 1.00f) {
        if (eGameType == Game::DA1) {
            // HUD Scale
            std::uint8_t* HUDScaleScanResult = Memory::PatternScan(exeModule, "D9 ?? ?? ?? 8B ?? D9 ?? ?? ?? D9 ?? ?? ?? 8B ?? ?? 53");
            if (HUDScaleScanResult) {
                spdlog::info("DA1: HUD: HUD Scale: Address is {:s}+{:x}", sExeName.c_str(), HUDScaleScanResult - (std::uint8_t*)exeModule);
                static SafetyHookMid HUDScaleMidHook{};
                HUDScaleMidHook = safetyhook::create_mid(HUDScaleScanResult,
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
}

void Graphics()
{
    if (fFoliageDrawDistance > 0.00f && fNPCDrawDistance > 0.00f && fObjectDrawDistance > 0.00f) {
        if (eGameType == Game::DA1) {
            // Foliage Draw Distance
            std::uint8_t* FoliageDrawDistanceScanResult = Memory::PatternScan(exeModule, "D9 ?? ?? ?? ?? ?? D9 ?? ?? ?? ?? ?? D9 ?? ?? ?? ?? ?? EB ?? D9 ?? ?? ?? ?? ?? D9 ?? ?? ?? ?? ??");
            std::uint8_t* ObjectDrawDistanceScanResult = Memory::PatternScan(exeModule, "D9 ?? ?? ?? ?? ?? DE ?? DF ?? F6 ?? ?? 74 ?? C6 ?? ?? ?? 00");
            if (FoliageDrawDistanceScanResult && ObjectDrawDistanceScanResult) {
                spdlog::info("Graphics: Draw Distance: Foliage: Scan address is {:s}+{:x}", sExeName.c_str(), FoliageDrawDistanceScanResult - (std::uint8_t*)exeModule);
                spdlog::info("Graphics: Draw Distance: Object: Scan address is {:s}+{:x}", sExeName.c_str(), ObjectDrawDistanceScanResult - (std::uint8_t*)exeModule);

                std::uint8_t* FoliageDrawDistance = (std::uint8_t*)*reinterpret_cast<std::uint32_t*>(FoliageDrawDistanceScanResult + 0x2);
                spdlog::info("Graphics: Draw Distance: Foliage: Address is {:s}+{:x}", sExeName.c_str(), FoliageDrawDistance - (std::uint8_t*)exeModule);
                std::uint8_t* NPCDrawDistance = (std::uint8_t*)*reinterpret_cast<std::uint32_t*>(FoliageDrawDistanceScanResult + 0xE);
                spdlog::info("Graphics: Draw Distance: NPC: Address is {:s}+{:x}", sExeName.c_str(), NPCDrawDistance - (std::uint8_t*)exeModule);

                std::uint8_t* ObjectDrawDistance = (std::uint8_t*)*reinterpret_cast<std::uint32_t*>(ObjectDrawDistanceScanResult + 0x2);
                spdlog::info("Graphics: Draw Distance: Object: Address is {:s}+{:x}", sExeName.c_str(), ObjectDrawDistance - (std::uint8_t*)exeModule);

                Memory::Write(FoliageDrawDistance, fFoliageDrawDistance);                   // Default very high = 1.5f
                Memory::Write(NPCDrawDistance, fNPCDrawDistance);                           // Default very high = 60.0f
                Memory::Write(ObjectDrawDistance, fObjectDrawDistance);                     // Default = 60.0f
            }
            else {
                spdlog::error("Graphics: Draw Distance: Pattern scan(s) failed.");
            }
        }
    }

    if (iShadowResolution != 1024) {
        if (eGameType == Game::DA1) {
            // Shadow Resolution
            std::uint8_t* ShadowResolutionScanResult = Memory::PatternScan(exeModule, "C7 ?? ?? ?? ?? ?? 00 04 00 00 56 57 E8 ?? ?? ?? ??");
            if (ShadowResolutionScanResult) {
                spdlog::info("Graphics: Shadow Resolution: Address is {:s}+{:x}", sExeName.c_str(), ShadowResolutionScanResult - (std::uint8_t*)exeModule);
                Memory::Write(ShadowResolutionScanResult + 0x6, iShadowResolution);      // Default very high = 1024
                spdlog::info("Graphics: Shadow Resolution: Patched instruction.");
            }
            else {
                spdlog::error("Graphics: Shadow Resolution: Pattern scan failed.");
            }
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