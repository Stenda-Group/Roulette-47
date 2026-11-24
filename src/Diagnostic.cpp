#include "Diagnostic.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <Windows.h>

std::string DossierDuMod() {
    HMODULE s_Module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&DossierDuMod), &s_Module
    );

    wchar_t s_Chemin[MAX_PATH] = {};
    GetModuleFileNameW(s_Module, s_Chemin, MAX_PATH);

    return std::filesystem::path(s_Chemin).parent_path().string();
}

void Diagnostiquer(const std::string& p_Ligne) {
    static std::mutex s_Verrou;
    static std::ofstream s_Fichier;

    std::lock_guard s_Garde(s_Verrou);

    if (!s_Fichier.is_open()) {
        s_Fichier.open(std::filesystem::path(DossierDuMod()) / "diagnostic.log", std::ios::trunc);
    }

    const auto s_Heure = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    s_Fichier << std::format("{:%H:%M:%S} ", std::chrono::zoned_time { std::chrono::current_zone(), s_Heure })
              << p_Ligne << '\n';
    s_Fichier.flush();
}
