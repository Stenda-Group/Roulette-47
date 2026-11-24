#include "Images.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

#include <Windows.h>
#include <winhttp.h>

#include "Diagnostic.h"
#include "Glacier.h"
#include "Json.h"

namespace {
    std::wstring Large(const std::string& p_Texte) {
        if (p_Texte.empty()) return {};
        const int s_Taille = MultiByteToWideChar(CP_UTF8, 0, p_Texte.data(), static_cast<int>(p_Texte.size()), nullptr, 0);
        std::wstring s_Large(s_Taille, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, p_Texte.data(), static_cast<int>(p_Texte.size()), s_Large.data(), s_Taille);
        return s_Large;
    }
}

Images::Images() :
    m_DossierCache(DossierDuMod() + "\\cache") {
    std::error_code s_Erreur;
    std::filesystem::create_directories(m_DossierCache, s_Erreur);

    m_Thread = std::thread(&Images::Boucle, this);
}

Images::~Images() {
    {
        std::lock_guard s_Verrou(m_Verrou);
        m_Arret = true;
    }

    m_Signal.notify_all();

    if (m_Thread.joinable()) {
        m_Thread.join();
    }
}

void Images::RelireService() {
    if (!Glacier::Globales::EnLigne) {
        return;
    }

    std::string s_Url = Glacier::AdresseImages();
    std::string s_Signature = Glacier::SignatureImages();

    std::lock_guard s_Verrou(m_Verrou);

    if (s_Url != m_UrlService) {
        // L'adresse n'est pas un secret ; la signature, si : seule sa longueur est notée.
        Diag("Service d'images : '{}' (signature : {} caractères).", s_Url, s_Signature.size());
    }

    m_UrlService = std::move(s_Url);
    m_Signature = std::move(s_Signature);

    if (!m_UrlService.empty()) {
        m_Signal.notify_all();
    }
}

std::string Images::FichierLocal(const std::string& p_Chemin) const {
    // « images/actors/x.jpg » → « cache\images\actors\x.jpg » ; on refuse toute
    // remontée de dossier, le chemin venant des données du jeu.
    std::string s_Relatif = p_Chemin;

    for (auto& c : s_Relatif) {
        if (c == '/') c = '\\';
    }

    if (s_Relatif.find("..") != std::string::npos || s_Relatif.find(':') != std::string::npos) {
        return {};
    }

    return m_DossierCache + "\\" + s_Relatif;
}

void Images::QuandArrivee(std::function<void()> p_Rappel) {
    std::lock_guard s_Verrou(m_Verrou);
    m_QuandArrivee = std::move(p_Rappel);
}

void Images::Demander(const std::string& p_Chemin, const std::string& p_Repli) {
    if (p_Chemin.empty()) {
        return;
    }

    std::lock_guard s_Verrou(m_Verrou);

    if (!p_Repli.empty() && p_Repli != p_Chemin) {
        m_Replis[p_Chemin] = p_Repli;
    }

    const auto [s_It, s_Nouvelle] = m_Images.try_emplace(p_Chemin, EEtat::Attente);

    if (!s_Nouvelle) {
        return;
    }

    const auto s_Fichier = FichierLocal(p_Chemin);

    if (s_Fichier.empty()) {
        s_It->second = EEtat::Echec;
    }
    else if (std::filesystem::exists(s_Fichier)) {
        s_It->second = EEtat::Fichier;
    }
    else {
        m_File.push_back(p_Chemin);
        m_Signal.notify_all();
    }
}

void Images::Boucle() {
    while (true) {
        std::string s_Chemin;

        {
            std::unique_lock s_Verrou(m_Verrou);
            m_Signal.wait(s_Verrou, [&] { return m_Arret || (!m_File.empty() && !m_UrlService.empty()); });

            if (m_Arret) {
                return;
            }

            s_Chemin = m_File.front();
            m_File.pop_front();
        }

        const auto s_Fichier = FichierLocal(s_Chemin);
        // Le service d'IOI distingue majuscules et minuscules, et le dépôt écrit
        // parfois « images/actors/Ancestral_Alexa_Carlisle.jpg » pour un fichier
        // rangé en minuscules (404 à Dartmoor, 22/11/2026) : second essai.
        // Puis la vignette de repli (« Tile »), elle aussi des deux façons. Tout
        // est rangé sous le chemin d'origine : la page n'en sait rien.
        std::string s_Repli;

        {
            std::lock_guard s_Verrou(m_Verrou);
            if (const auto s_It = m_Replis.find(s_Chemin); s_It != m_Replis.end()) s_Repli = s_It->second;
        }

        const auto Minuscules = [](std::string p) {
            for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return p;
        };

        bool s_Ok = false;

        for (const auto& s_Essai : { s_Chemin, Minuscules(s_Chemin), s_Repli, Minuscules(s_Repli) }) {
            if (s_Essai.empty()) continue;
            if ((s_Ok = Telecharger(s_Essai, s_Fichier))) {
                if (s_Essai != s_Chemin) Diag("Image {} trouvée sous {}", s_Chemin, s_Essai);
                break;
            }
        }

        if (!s_Ok) Diag("Image introuvable sur le service : {}", s_Chemin);

        std::function<void()> s_Rappel;

        {
            std::lock_guard s_Verrou(m_Verrou);
            m_Images[s_Chemin] = s_Ok ? EEtat::Fichier : EEtat::Echec;
            s_Rappel = m_QuandArrivee;
        }

        if (s_Ok && s_Rappel) {
            s_Rappel();
        }
    }
}

bool Images::Telecharger(const std::string& p_Chemin, const std::string& p_Fichier) {
    std::string s_Url, s_Signature;

    {
        std::lock_guard s_Verrou(m_Verrou);
        s_Url = m_UrlService;
        s_Signature = m_Signature;
    }

    if (!s_Url.empty() && s_Url.back() != '/') s_Url += '/';
    s_Url += p_Chemin;

    if (!s_Signature.empty()) {
        s_Url += (s_Signature.front() == '?' || s_Signature.front() == '&') ? s_Signature : "?" + s_Signature;
    }

    const auto s_UrlLarge = Large(s_Url);

    URL_COMPONENTS s_Parties = { sizeof(s_Parties) };
    wchar_t s_Hote[256] = {}, s_Chemin[4096] = {};
    s_Parties.lpszHostName = s_Hote;
    s_Parties.dwHostNameLength = std::size(s_Hote);
    s_Parties.lpszUrlPath = s_Chemin;
    s_Parties.dwUrlPathLength = std::size(s_Chemin);

    if (!WinHttpCrackUrl(s_UrlLarge.c_str(), 0, 0, &s_Parties)) {
        Diag("Adresse d'image invalide pour {}", p_Chemin);
        return false;
    }

    // Le chemin rendu par WinHttpCrackUrl s'arrête avant la requête : on la recolle.
    std::wstring s_Requete = std::wstring(s_Chemin, s_Parties.dwUrlPathLength);

    if (s_Parties.dwExtraInfoLength > 0) {
        s_Requete += std::wstring(s_Parties.lpszExtraInfo, s_Parties.dwExtraInfoLength);
    }

    bool s_Ok = false;
    std::vector<char> s_Contenu;

    const HINTERNET s_Session = WinHttpOpen(L"Roulette47/0.13", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    const HINTERNET s_Connexion = s_Session ? WinHttpConnect(s_Session, s_Hote, s_Parties.nPort, 0) : nullptr;
    const HINTERNET s_Demande = s_Connexion
        ? WinHttpOpenRequest(
            s_Connexion, L"GET", s_Requete.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            s_Parties.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0
        )
        : nullptr;

    if (s_Demande &&
        WinHttpSendRequest(s_Demande, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(s_Demande, nullptr)) {
        DWORD s_Code = 0, s_TailleCode = sizeof(s_Code);
        WinHttpQueryHeaders(
            s_Demande, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &s_Code,
            &s_TailleCode, WINHTTP_NO_HEADER_INDEX
        );

        if (s_Code == 200) {
            DWORD s_Dispo = 0;

            while (WinHttpQueryDataAvailable(s_Demande, &s_Dispo) && s_Dispo > 0) {
                const size_t s_Debut = s_Contenu.size();
                s_Contenu.resize(s_Debut + s_Dispo);
                DWORD s_Lu = 0;
                WinHttpReadData(s_Demande, s_Contenu.data() + s_Debut, s_Dispo, &s_Lu);
                s_Contenu.resize(s_Debut + s_Lu);
            }

            s_Ok = !s_Contenu.empty();
        }
        else {
            if (s_Code != 404) Diag("Image {} : HTTP {}", p_Chemin, s_Code);
        }
    }

    if (s_Demande) WinHttpCloseHandle(s_Demande);
    if (s_Connexion) WinHttpCloseHandle(s_Connexion);
    if (s_Session) WinHttpCloseHandle(s_Session);

    if (!s_Ok) {
        return false;
    }

    // Écrit à côté puis renommé : une image à moitié écrite ne doit jamais
    // passer pour une image du cache.
    std::error_code s_Erreur;
    std::filesystem::create_directories(std::filesystem::path(p_Fichier).parent_path(), s_Erreur);

    const auto s_Temporaire = p_Fichier + ".part";

    {
        std::ofstream s_Sortie(s_Temporaire, std::ios::binary);
        s_Sortie.write(s_Contenu.data(), static_cast<std::streamsize>(s_Contenu.size()));

        if (!s_Sortie) {
            return false;
        }
    }

    std::filesystem::rename(s_Temporaire, p_Fichier, s_Erreur);
    return !s_Erreur;
}
