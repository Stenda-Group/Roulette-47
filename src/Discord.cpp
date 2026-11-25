#include "Discord.h"

#include <chrono>
#include <cstdint>
#include <vector>

#include <Windows.h>

#include <nlohmann/json.hpp>

#include "Diagnostic.h"

namespace {
    HANDLE Ouvrir() {
        for (int i = 0; i < 10; ++i) {
            const auto s_Nom = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            HANDLE h = CreateFileW(s_Nom.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) return h;
        }
        return INVALID_HANDLE_VALUE;
    }

    bool Ecrire(HANDLE h, uint32_t p_Code, const std::string& p_Json) {
        std::vector<uint8_t> s_Trame(8 + p_Json.size());
        const uint32_t s_Taille = static_cast<uint32_t>(p_Json.size());
        memcpy(s_Trame.data(), &p_Code, 4);
        memcpy(s_Trame.data() + 4, &s_Taille, 4);
        memcpy(s_Trame.data() + 8, p_Json.data(), p_Json.size());
        DWORD s_Ecrits = 0;
        return WriteFile(h, s_Trame.data(), static_cast<DWORD>(s_Trame.size()), &s_Ecrits, nullptr) && s_Ecrits == s_Trame.size();
    }

    // Lit les réponses en attente sans bloquer (Discord en envoie une à chaque
    // commande) ; faux si le tube est rompu.
    bool Vider(HANDLE h) {
        for (;;) {
            DWORD s_Dispo = 0;
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &s_Dispo, nullptr)) return false;
            if (s_Dispo == 0) return true;
            std::vector<uint8_t> s_Tampon(s_Dispo);
            DWORD s_Lus = 0;
            if (!ReadFile(h, s_Tampon.data(), s_Dispo, &s_Lus, nullptr)) return false;
            // Une erreur de Discord (application inconnue, activité refusée…) : au journal.
            const std::string s_Texte(s_Tampon.begin(), s_Tampon.begin() + s_Lus);
            if (s_Texte.find("\"evt\":\"ERROR\"") != std::string::npos || s_Texte.find("\"code\":4") != std::string::npos) {
                const auto p = s_Texte.find('{');
                Diag("Discord : réponse d'erreur : {}", p == std::string::npos ? s_Texte : s_Texte.substr(p, 300));
            }
            else if (s_Texte.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos) {
                static int s_Fois = 0;
                if (++s_Fois <= 3) {
                    const auto p = s_Texte.find('{');
                    Diag("Discord : réponse : {}", p == std::string::npos ? s_Texte : s_Texte.substr(p, 400));
                }
            }
            else if (s_Texte.find("\"evt\":\"READY\"") != std::string::npos) {
                Diag("Discord : prêt (application acceptée par le client).");
            }
        }
    }

    nlohmann::json Activite(const SPresence& p) {
        if (p.m_Details.empty() && p.m_Etat.empty()) return nullptr;
        nlohmann::json a = {
            { "assets", { { "large_image", "logo" }, { "large_text", "Roulette 47" } } },
        };
        if (!p.m_Details.empty()) a["details"] = p.m_Details.substr(0, 127);
        if (!p.m_Etat.empty()) a["state"] = p.m_Etat.substr(0, 127);
        if (p.m_Debut > 0) a["timestamps"] = { { "start", p.m_Debut } };
        return a;
    }
}

Discord::~Discord() {
    Arreter();
}

void Discord::Demarrer(const std::string& p_IdApplication) {
    if (p_IdApplication.empty() || m_Fil.joinable()) return;
    m_IdApplication = p_IdApplication;
    m_Arret = false;
    m_Fil = std::thread([this] { Boucle(); });
}

void Discord::Arreter() {
    m_Arret = true;
    if (m_Fil.joinable()) m_Fil.join();
}

void Discord::Montrer(const SPresence& p_Presence) {
    std::lock_guard s_Verrou(m_Verrou);
    if (p_Presence == m_Voulue) return;
    m_Voulue = p_Presence;
    m_Nouvelle = true;
}

void Discord::Boucle() {
    HANDLE h = INVALID_HANDLE_VALUE;
    bool s_Signale = false;
    auto s_ProchainEssai = std::chrono::steady_clock::now();
    unsigned s_Nonce = 0;
    // Une présence affichée est renvoyée toutes les 5 s : un autre mod du jeu
    // (DiscordRichPresence de ZHMModSDK) écrit celle du même processus, et
    // Discord montre la dernière reçue. Discord en admet 5 par 20 s.
    auto s_DernierEnvoi = std::chrono::steady_clock::now();

    while (!m_Arret) {
        if (h == INVALID_HANDLE_VALUE) {
            if (std::chrono::steady_clock::now() < s_ProchainEssai) {
                Sleep(250);
                continue;
            }
            s_ProchainEssai = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            h = Ouvrir();
            if (h == INVALID_HANDLE_VALUE) continue;

            const nlohmann::json s_Poignee = { { "v", 1 }, { "client_id", m_IdApplication } };
            if (!Ecrire(h, 0, s_Poignee.dump())) {
                CloseHandle(h);
                h = INVALID_HANDLE_VALUE;
                continue;
            }
            if (!s_Signale) {
                Diag("Discord : connecté au client.");
                s_Signale = true;
            }
            std::lock_guard s_Verrou(m_Verrou);
            m_Nouvelle = true; // renvoyer la présence après une (re)connexion
        }

        SPresence s_Presence;
        bool s_Envoyer = false;
        {
            std::lock_guard s_Verrou(m_Verrou);
            const bool s_Rappel = !(m_Voulue.m_Details.empty() && m_Voulue.m_Etat.empty()) &&
                                  std::chrono::steady_clock::now() - s_DernierEnvoi > std::chrono::seconds(5);
            if (m_Nouvelle || s_Rappel) {
                s_Presence = m_Voulue;
                m_Nouvelle = false;
                s_Envoyer = true;
            }
        }

        if (s_Envoyer) {
            s_DernierEnvoi = std::chrono::steady_clock::now();
            const nlohmann::json s_Commande = {
                { "cmd", "SET_ACTIVITY" },
                { "args", { { "pid", static_cast<int>(GetCurrentProcessId()) }, { "activity", Activite(s_Presence) } } },
                { "nonce", std::to_string(++s_Nonce) },
            };
            if (!Ecrire(h, 1, s_Commande.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))) {
                CloseHandle(h);
                h = INVALID_HANDLE_VALUE;
                std::lock_guard s_Verrou(m_Verrou);
                m_Nouvelle = true;
                continue;
            }
        }

        if (!Vider(h)) {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
            std::lock_guard s_Verrou(m_Verrou);
            m_Nouvelle = true;
            continue;
        }

        Sleep(500);
    }

    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}
