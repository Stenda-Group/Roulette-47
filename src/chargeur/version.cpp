// Chargeur de Roulette 47 : se fait passer pour version.dll, que HITMAN3.exe
// charge au démarrage depuis son propre dossier (Retail) avant celle de Windows.
//
// - Toutes les fonctions de version.dll sont renvoyées à la vraie
//   (C:\Windows\System32\version.dll) : le jeu ne voit aucune différence.
// - Au chargement, il lance Roulette47\Roulette47.dll, à côté de lui.
//
// Pourquoi version.dll : les autres chargeurs de mods du jeu passent souvent
// par dinput8.dll ; les deux cohabitent, chacun lance ses mods sans gêner l'autre.

#include <Windows.h>

#define RENVOI(Nom, Ordinal) \
    __pragma(comment(linker, "/export:" #Nom "=C:\\Windows\\System32\\version." #Nom ",@" #Ordinal))

RENVOI(GetFileVersionInfoA, 1)
RENVOI(GetFileVersionInfoByHandle, 2)
RENVOI(GetFileVersionInfoExA, 3)
RENVOI(GetFileVersionInfoExW, 4)
RENVOI(GetFileVersionInfoSizeA, 5)
RENVOI(GetFileVersionInfoSizeExA, 6)
RENVOI(GetFileVersionInfoSizeExW, 7)
RENVOI(GetFileVersionInfoSizeW, 8)
RENVOI(GetFileVersionInfoW, 9)
RENVOI(VerFindFileA, 10)
RENVOI(VerFindFileW, 11)
RENVOI(VerInstallFileA, 12)
RENVOI(VerInstallFileW, 13)
RENVOI(VerLanguageNameA, 14)
RENVOI(VerLanguageNameW, 15)
RENVOI(VerQueryValueA, 16)
RENVOI(VerQueryValueW, 17)

namespace {
    DWORD WINAPI Charger(LPVOID p_Module) {
        wchar_t s_Chemin[MAX_PATH] = {};
        GetModuleFileNameW(static_cast<HMODULE>(p_Module), s_Chemin, MAX_PATH);

        // ...\Retail\version.dll -> ...\Retail\Roulette47\Roulette47.dll
        wchar_t* s_Barre = wcsrchr(s_Chemin, L'\\');
        if (!s_Barre) return 0;
        wcscpy_s(s_Barre + 1, MAX_PATH - (s_Barre + 1 - s_Chemin), L"Roulette47\\Roulette47.dll");

        if (GetFileAttributesW(s_Chemin) != INVALID_FILE_ATTRIBUTES) {
            LoadLibraryW(s_Chemin);
        }

        return 0;
    }

    bool EstLeJeu() {
        wchar_t s_Exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, s_Exe, MAX_PATH);
        const wchar_t* s_Nom = wcsrchr(s_Exe, L'\\');
        return s_Nom && _wcsicmp(s_Nom + 1, L"HITMAN3.exe") == 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE p_Module, DWORD p_Raison, LPVOID) {
    if (p_Raison == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(p_Module);

        // Pas de LoadLibrary sous le verrou du chargeur : un thread s'en charge.
        if (EstLeJeu()) {
            if (const HANDLE s_Thread = CreateThread(nullptr, 0, Charger, p_Module, 0, nullptr)) {
                CloseHandle(s_Thread);
            }
        }
    }

    return TRUE;
}
