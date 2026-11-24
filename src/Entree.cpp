// Point d'entrée de Roulette.dll, chargée par version.dll (src/chargeur).

#include <Windows.h>

#include "Roulette.h"

namespace {
    DWORD WINAPI Demarrer(LPVOID) {
        Roulette::Instance().Demarrer();
        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE p_Module, DWORD p_Raison, LPVOID) {
    if (p_Raison == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(p_Module);

        // Jamais de travail sous le verrou du chargeur : un thread démarre le mod.
        if (const HANDLE s_Thread = CreateThread(nullptr, 0, Demarrer, nullptr, 0, nullptr)) {
            CloseHandle(s_Thread);
        }
    }

    return TRUE;
}
