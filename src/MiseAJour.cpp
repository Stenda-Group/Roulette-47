#include "MiseAJour.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <thread>
#include <vector>

#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <miniz.h>

#include "Diagnostic.h"
#include "Langue.h"

#ifndef ROULETTE_VERSION
#define ROULETTE_VERSION "0.0"
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
    constexpr const char* c_Releases = "https://api.github.com/repos/Stenda-Group/Roulette-47/releases/latest";
    constexpr const char* c_Pages = "https://github.com/Stenda-Group/Roulette-47/releases/";
    constexpr size_t c_TailleMax = 40u * 1024 * 1024;

    std::mutex g_Verrou;
    std::string g_VersionLocale = ROULETTE_VERSION;
    std::string g_Etat = "aucune";
    std::string g_Version, g_Page, g_Archive, g_Url, g_Somme, g_Erreur;
    std::function<void()> g_Changement;

    void Signaler() {
        std::function<void()> f;
        {
            std::lock_guard s_Verrou(g_Verrou);
            f = g_Changement;
        }
        if (f) f();
    }

    void Echouer(const std::string& p_Cle, const std::string& p_Detail) {
        Diag("Mise à jour : {}.", p_Detail);
        {
            std::lock_guard s_Verrou(g_Verrou);
            g_Etat = "erreur";
            g_Erreur = Tx(p_Cle);
        }
        Signaler();
    }

    std::wstring Large(const std::string& p) {
        if (p.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, p.data(), static_cast<int>(p.size()), nullptr, 0);
        std::wstring s(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, p.data(), static_cast<int>(p.size()), s.data(), n);
        return s;
    }

    // GET en HTTPS ; les redirections (GitHub -> objects.githubusercontent.com)
    // sont suivies par WinHTTP. Faux si le code n'est pas 200.
    bool Telecharger(const std::string& p_Url, std::string& p_Corps, DWORD& p_Code) {
        p_Corps.clear();
        p_Code = 0;
        const auto s_Url = Large(p_Url);

        URL_COMPONENTS u = { sizeof(u) };
        wchar_t s_Hote[256] = {}, s_Chemin[4096] = {};
        u.lpszHostName = s_Hote;
        u.dwHostNameLength = static_cast<DWORD>(std::size(s_Hote));
        u.lpszUrlPath = s_Chemin;
        u.dwUrlPathLength = static_cast<DWORD>(std::size(s_Chemin));
        if (!WinHttpCrackUrl(s_Url.c_str(), 0, 0, &u) || u.nScheme != INTERNET_SCHEME_HTTPS) return false;
        std::wstring s_Requete(s_Chemin, u.dwUrlPathLength);
        if (u.dwExtraInfoLength) s_Requete += std::wstring(u.lpszExtraInfo, u.dwExtraInfoLength);

        const auto s_Agent = Large(std::string("Roulette47/") + ROULETTE_VERSION);
        const HINTERNET s_Session = WinHttpOpen(s_Agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
        const HINTERNET s_Connexion = s_Session ? WinHttpConnect(s_Session, s_Hote, u.nPort, 0) : nullptr;
        const HINTERNET s_Demande = s_Connexion
            ? WinHttpOpenRequest(s_Connexion, L"GET", s_Requete.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
            : nullptr;

        bool s_Ok = false;
        if (s_Demande) {
            DWORD s_Delai = 20000;
            WinHttpSetOption(s_Demande, WINHTTP_OPTION_RECEIVE_TIMEOUT, &s_Delai, sizeof(s_Delai));
            const wchar_t* s_Entetes = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
            if (WinHttpSendRequest(s_Demande, s_Entetes, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(s_Demande, nullptr)) {
                DWORD s_Taille = sizeof(p_Code);
                WinHttpQueryHeaders(s_Demande, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                    &p_Code, &s_Taille, WINHTTP_NO_HEADER_INDEX);
                if (p_Code == 200) {
                    s_Ok = true;
                    DWORD s_Dispo = 0;
                    while (WinHttpQueryDataAvailable(s_Demande, &s_Dispo) && s_Dispo > 0) {
                        const size_t s_Debut = p_Corps.size();
                        if (s_Debut + s_Dispo > c_TailleMax) { s_Ok = false; break; }
                        p_Corps.resize(s_Debut + s_Dispo);
                        DWORD s_Lu = 0;
                        if (!WinHttpReadData(s_Demande, p_Corps.data() + s_Debut, s_Dispo, &s_Lu)) { s_Ok = false; break; }
                        p_Corps.resize(s_Debut + s_Lu);
                    }
                }
            }
        }

        if (s_Demande) WinHttpCloseHandle(s_Demande);
        if (s_Connexion) WinHttpCloseHandle(s_Connexion);
        if (s_Session) WinHttpCloseHandle(s_Session);
        return s_Ok && !p_Corps.empty();
    }

    std::string Sha256(const std::string& p_Donnees) {
        BCRYPT_ALG_HANDLE s_Algo = nullptr;
        BCRYPT_HASH_HANDLE s_Hache = nullptr;
        unsigned char s_Resultat[32] = {};
        bool s_Ok = BCryptOpenAlgorithmProvider(&s_Algo, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
                    BCryptCreateHash(s_Algo, &s_Hache, nullptr, 0, nullptr, 0, 0) == 0 &&
                    BCryptHashData(s_Hache, reinterpret_cast<PUCHAR>(const_cast<char*>(p_Donnees.data())),
                                   static_cast<ULONG>(p_Donnees.size()), 0) == 0 &&
                    BCryptFinishHash(s_Hache, s_Resultat, sizeof(s_Resultat), 0) == 0;
        if (s_Hache) BCryptDestroyHash(s_Hache);
        if (s_Algo) BCryptCloseAlgorithmProvider(s_Algo, 0);
        if (!s_Ok) return {};
        static const char* c_Hex = "0123456789abcdef";
        std::string s;
        for (const unsigned char c : s_Resultat) { s += c_Hex[c >> 4]; s += c_Hex[c & 15]; }
        return s;
    }

    // Le dossier de cette DLL, en Unicode (DossierDuMod() passe par la page de
    // code ANSI : un dossier de jeu accentué s'y perdrait).
    fs::path DossierMod() {
        HMODULE s_Module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&DossierMod), &s_Module);
        wchar_t s_Chemin[MAX_PATH] = {};
        GetModuleFileNameW(s_Module, s_Chemin, MAX_PATH);
        return fs::path(s_Chemin).parent_path();
    }
    fs::path DossierRetail() { return DossierMod().parent_path(); }

    bool Ecrire(const fs::path& p_Fichier, const void* p_Octets, size_t p_Taille) {
        std::error_code e;
        fs::create_directories(p_Fichier.parent_path(), e);
        std::ofstream s(p_Fichier, std::ios::binary | std::ios::trunc);
        s.write(static_cast<const char*>(p_Octets), static_cast<std::streamsize>(p_Taille));
        return static_cast<bool>(s);
    }

    // Pose une DLL peut-être chargée : l'actuelle part en « .ancien ».
    bool PoserDll(const fs::path& p_Cible, const void* p_Octets, size_t p_Taille) {
        std::error_code e;
        const fs::path s_Nouveau = p_Cible.wstring() + L".nouveau";
        const fs::path s_Ancien = p_Cible.wstring() + L".ancien";
        if (!Ecrire(s_Nouveau, p_Octets, p_Taille)) return false;

        if (fs::exists(p_Cible, e)) {
            // Un « .ancien » d'une installation précédente de la même partie est
            // encore chargé et ne s'efface pas : la cible, elle, ne l'est pas.
            fs::remove(s_Ancien, e);
            if (fs::exists(s_Ancien, e)) {
                if (!DeleteFileW(p_Cible.c_str())) return false;
            }
            else if (!MoveFileExW(p_Cible.c_str(), s_Ancien.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                return false;
            }
        }
        if (MoveFileExW(s_Nouveau.c_str(), p_Cible.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;

        // Échec : l'ancienne reprend sa place.
        MoveFileExW(s_Ancien.c_str(), p_Cible.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }

    void Installation() {
        std::string s_Url, s_Somme, s_Version, s_Archive;
        {
            std::lock_guard s_Verrou(g_Verrou);
            s_Url = g_Url;
            s_Somme = g_Somme;
            s_Version = g_Version;
            s_Archive = g_Archive;
        }

        std::string s_Zip;
        DWORD s_Code = 0;
        if (!Telecharger(s_Url, s_Zip, s_Code)) return Echouer("maj.reseau", "archive non téléchargée (HTTP " + std::to_string(s_Code) + ")");
        if (Sha256(s_Zip) != s_Somme) return Echouer("maj.somme", "SHA-256 de " + s_Archive + " différent de celui des notes");

        mz_zip_archive z = {};
        if (!mz_zip_reader_init_mem(&z, s_Zip.data(), s_Zip.size(), 0)) return Echouer("maj.archive", "archive illisible");

        struct SFichier { std::string m_Chemin; std::vector<char> m_Octets; };
        std::vector<SFichier> s_Fichiers;
        bool s_Ok = true;
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z) && s_Ok; ++i) {
            if (mz_zip_reader_is_file_a_directory(&z, i)) continue;
            char s_Nom[1024] = {};
            mz_zip_reader_get_filename(&z, i, s_Nom, sizeof(s_Nom));
            const auto s_Chemin = MiseAJour::CheminAccepte(s_Nom);
            if (s_Chemin.empty()) continue;
            size_t s_Taille = 0;
            void* p = mz_zip_reader_extract_to_heap(&z, i, &s_Taille, 0);
            if (!p) { s_Ok = false; break; }
            s_Fichiers.push_back({ s_Chemin, std::vector<char>(static_cast<char*>(p), static_cast<char*>(p) + s_Taille) });
            mz_free(p);
        }
        mz_zip_reader_end(&z);

        const auto s_EstDll = [](const std::string& c) { return c == "version.dll" || c == "Roulette47/Roulette47.dll"; };
        if (!s_Ok || std::ranges::none_of(s_Fichiers, [](const auto& f) { return f.m_Chemin == "Roulette47/Roulette47.dll"; })) {
            return Echouer("maj.archive", "archive incomplète");
        }

        // Le reste attend dans update/ ; la version attendue y est notée, pour
        // que seule la DLL de cette version le range.
        std::error_code e;
        const fs::path s_Attente = DossierMod() / L"update";
        fs::remove_all(s_Attente, e);
        for (const auto& f : s_Fichiers) {
            if (s_EstDll(f.m_Chemin)) continue;
            const auto s_Sous = f.m_Chemin.substr(std::string("Roulette47/").size());
            if (!Ecrire(s_Attente / fs::path(Large(s_Sous)), f.m_Octets.data(), f.m_Octets.size())) {
                fs::remove_all(s_Attente, e);
                return Echouer("maj.ecriture", "écriture impossible dans update/");
            }
        }
        if (!Ecrire(s_Attente / L"VERSION", s_Version.data(), s_Version.size())) {
            fs::remove_all(s_Attente, e);
            return Echouer("maj.ecriture", "écriture impossible dans update/");
        }

        // Les DLL en dernier : la mod, puis le chargeur.
        for (const char* s_Dll : { "Roulette47/Roulette47.dll", "version.dll" }) {
            const auto f = std::ranges::find(s_Fichiers, std::string(s_Dll), &SFichier::m_Chemin);
            if (f == s_Fichiers.end()) continue;
            if (!PoserDll(DossierRetail() / fs::path(Large(f->m_Chemin)), f->m_Octets.data(), f->m_Octets.size())) {
                if (std::string(s_Dll) == "Roulette47/Roulette47.dll") fs::remove_all(s_Attente, e);
                return Echouer("maj.ecriture", std::string("impossible de remplacer ") + s_Dll);
            }
        }

        Diag("Mise à jour : v{} installée ({} fichiers), active au prochain lancement du jeu.", s_Version, s_Fichiers.size());
        {
            std::lock_guard s_Verrou(g_Verrou);
            g_Etat = "installee";
        }
        Signaler();
    }

    void Verification() {
        std::string s_Corps;
        DWORD s_Code = 0;
        if (!Telecharger(c_Releases, s_Corps, s_Code)) {
            Diag("Mise à jour : dernière version non lue (HTTP {}).", s_Code);
            return;
        }
        const auto r = json::parse(s_Corps, nullptr, false);
        if (!r.is_object()) return;

        const auto s_Tag = r.value("tag_name", std::string());
        if (!MiseAJour::PlusRecente(s_Tag, MiseAJour::Version())) {
            Diag("Mise à jour : à jour (dernière version publiée : {}).", s_Tag.empty() ? std::string("?") : s_Tag);
            return;
        }

        const auto s_Version = s_Tag.substr(s_Tag.starts_with('v') ? 1 : 0);
        const auto s_Archive = "roulette47-v" + s_Version + ".zip";
        std::string s_Url;
        for (const auto& a : r.value("assets", json::array())) {
            if (a.value("name", std::string()) == s_Archive) s_Url = a.value("browser_download_url", std::string());
        }
        auto s_Page = r.value("html_url", std::string());
        if (!s_Page.starts_with(c_Pages)) s_Page = c_Pages;

        if (!s_Url.starts_with("https://github.com/Stenda-Group/Roulette-47/")) s_Url.clear();
        const auto s_Somme = MiseAJour::SommeDesNotes(r.value("body", std::string()), s_Archive);
        {
            std::lock_guard s_Verrou(g_Verrou);
            g_Version = s_Version;
            g_Page = s_Page;
            g_Archive = s_Archive;
            g_Url = s_Url;
            g_Somme = s_Somme;
            g_Etat = "disponible";
        }
        Diag("Mise à jour : v{} disponible (installée : v{}){}.", s_Version, MiseAJour::Version(),
             s_Url.empty() || s_Somme.empty() ? ", sans archive vérifiable : page seulement" : "");
        Signaler();
    }

    std::vector<int> Numeros(const std::string& p) {
        std::vector<int> v;
        int n = -1;
        for (const char c : p) {
            if (c >= '0' && c <= '9') n = (n < 0 ? 0 : n * 10) + (c - '0');
            else if (c == '.') { v.push_back(std::max(n, 0)); n = -1; }
            else if (c == 'v' && v.empty() && n < 0) continue;
            else break;
        }
        if (n >= 0) v.push_back(n);
        return v;
    }
}

namespace MiseAJour {
    const char* Version() { return g_VersionLocale.c_str(); }
    void ForcerVersion(const std::string& p_Version) { g_VersionLocale = p_Version; }

    bool PlusRecente(const std::string& p_Distante, const std::string& p_Locale) {
        auto a = Numeros(p_Distante), b = Numeros(p_Locale);
        if (a.empty() || b.empty()) return false;
        const size_t n = std::max(a.size(), b.size());
        a.resize(n, 0);
        b.resize(n, 0);
        return a > b;
    }

    std::string SommeDesNotes(const std::string& p_Notes, const std::string& p_Archive) {
        const auto i = p_Notes.find("`" + p_Archive + "`");
        if (i == std::string::npos) return {};
        std::smatch m;
        const std::string s_Suite = p_Notes.substr(i, 200);
        static const std::regex c_Somme("SHA-256 `([0-9a-f]{64})`");
        return std::regex_search(s_Suite, m, c_Somme) ? m[1].str() : std::string();
    }

    std::string CheminAccepte(const std::string& p_Nom) {
        std::string s = p_Nom;
        std::ranges::replace(s, '\\', '/');
        if (!s.starts_with("Retail/")) return {};
        s = s.substr(7);
        if (s.empty() || s.find(':') != std::string::npos || s.starts_with('/')) return {};
        for (size_t d = 0; d <= s.size();) {
            const size_t f = std::min(s.find('/', d), s.size());
            const auto p = s.substr(d, f - d);
            if (p.empty() || p == "." || p == "..") return {};
            d = f + 1;
        }
        if (s == "version.dll" || (s.starts_with("Roulette47/") && s.size() > 11)) return s;
        return {};
    }

    void AppliquerEnAttente() {
        std::error_code e;
        const fs::path s_Mod = DossierMod();
        const fs::path s_Attente = s_Mod / L"update";

        // Les DLL remplacées lors de la partie précédente ne sont plus chargées.
        fs::remove(s_Mod / L"Roulette47.dll.ancien", e);
        fs::remove(DossierRetail() / L"version.dll.ancien", e);

        if (!fs::is_directory(s_Attente, e)) return;

        std::string s_Version;
        {
            std::ifstream f(s_Attente / L"VERSION", std::ios::binary);
            std::getline(f, s_Version);
        }
        if (s_Version != Version()) {
            Diag("Mise à jour en attente (v{}) écartée : cette DLL est la v{}.", s_Version, Version());
            fs::remove_all(s_Attente, e);
            return;
        }

        int n = 0, s_Echecs = 0;
        for (auto it = fs::recursive_directory_iterator(s_Attente, e); it != fs::recursive_directory_iterator(); it.increment(e)) {
            if (e) break;
            if (!it->is_regular_file(e) || it->path().filename() == L"VERSION") continue;
            const auto s_Cible = s_Mod / fs::relative(it->path(), s_Attente, e);
            fs::create_directories(s_Cible.parent_path(), e);
            if (MoveFileExW(it->path().c_str(), s_Cible.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) ++n;
            else ++s_Echecs;
        }
        fs::remove_all(s_Attente, e);
        Diag("Mise à jour v{} rangée : {} fichier(s){}.", s_Version, n, s_Echecs ? ", " + std::to_string(s_Echecs) + " en échec" : std::string());
    }

    void Verifier(std::function<void()> p_Changement) {
        {
            std::lock_guard s_Verrou(g_Verrou);
            g_Changement = std::move(p_Changement);
        }
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            Verification();
        }).detach();
    }

    void Installer() {
        {
            std::lock_guard s_Verrou(g_Verrou);
            if (g_Etat != "disponible" && g_Etat != "erreur") return;
            if (g_Url.empty() || g_Somme.empty()) {
                g_Etat = "erreur";
                g_Erreur = Tx("maj.somme");
                return;
            }
            g_Etat = "telechargement";
            g_Erreur.clear();
        }
        Signaler();
        std::thread(Installation).detach();
    }

    void OuvrirPage() {
        std::string s_Page;
        {
            std::lock_guard s_Verrou(g_Verrou);
            s_Page = g_Page.empty() ? std::string(c_Pages) : g_Page;
        }
        ShellExecuteW(nullptr, L"open", Large(s_Page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    json Etat() {
        std::lock_guard s_Verrou(g_Verrou);
        return { { "etat", g_Etat }, { "version", g_Version }, { "actuelle", g_VersionLocale }, { "erreur", g_Erreur } };
    }
}
