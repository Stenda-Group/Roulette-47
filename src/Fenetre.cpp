#include "Fenetre.h"

#include <string>

#include <dcomp.h>
#include <windowsx.h>
#include <wrl.h>

#include <WebView2.h>
#include <shellapi.h>
#include <urlmon.h>

#include "Diagnostic.h"
#include "Langue.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
    constexpr UINT WM_ROULETTE_POUSSER = WM_APP + 1;
    constexpr UINT WM_ROULETTE_OUVRIR = WM_APP + 2;
    constexpr UINT WM_ROULETTE_ROMPRE = WM_APP + 3;

    // Contrat rompu : la fiche laisse libre la gauche du jeu (le menu pause).
    constexpr double c_PartLibreRompu = 0.36;
    constexpr UINT_PTR c_Minuterie = 1;

    // Marges de l'encart par rapport au coin haut droit du jeu (pixels CSS).
    constexpr int c_EncartHaut = 140;
    constexpr int c_EncartDroite = 24;

    std::wstring Large(const std::string& p_Texte) {
        if (p_Texte.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, p_Texte.data(), static_cast<int>(p_Texte.size()), nullptr, 0);
        std::wstring s(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, p_Texte.data(), static_cast<int>(p_Texte.size()), s.data(), n);
        return s;
    }

    std::string Etroit(const wchar_t* p_Texte) {
        if (!p_Texte || !*p_Texte) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, p_Texte, -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, p_Texte, -1, s.data(), n, nullptr, nullptr);
        return s;
    }

    // État de la fenêtre, propre au thread de l'interface.
    struct SEtat {
        Fenetre::SParametres* m_Parametres = nullptr;
        std::atomic<bool>* m_Ouverte = nullptr;
        std::atomic<bool>* m_Rompu = nullptr;
        int m_AvantRouvrir = 0; // contrat rompu, fiche refermée : images avant de la rouvrir

        HWND m_Hwnd = nullptr;
        ComPtr<IDCompositionDevice> m_Composition;
        ComPtr<IDCompositionTarget> m_Cible;
        ComPtr<IDCompositionVisual> m_Visuel;
        ComPtr<ICoreWebView2Environment3> m_Environnement;
        ComPtr<ICoreWebView2CompositionController> m_Composite;
        ComPtr<ICoreWebView2Controller> m_Controleur;
        ComPtr<ICoreWebView2> m_Vue;

        bool m_PagePrete = false;
        bool m_ToucheEnfoncee = false;
        bool m_SourisSuivie = false;
        HCURSOR m_Curseur = LoadCursorW(nullptr, IDC_ARROW);
        SIZE m_Encart {}; // en pixels CSS, 0 = rien à montrer
        RECT m_DernierRect {};
        bool m_Visible = false;
        bool m_FocusDonne = false;
    };

    SEtat* g_Etat = nullptr;

    double Echelle(HWND p_Fenetre) {
        const UINT s_Dpi = p_Fenetre ? GetDpiForWindow(p_Fenetre) : GetDpiForSystem();
        return s_Dpi ? s_Dpi / 96.0 : 1.0;
    }

    void EnvoyerEtat(SEtat& e) {
        if (!e.m_Vue || !e.m_PagePrete || !e.m_Parametres->m_Etat) return;
        const auto s_Json = e.m_Parametres->m_Etat(*e.m_Ouverte);
        e.m_Vue->PostWebMessageAsJson(Large(s_Json).c_str());
    }

    // Place (ou cache) la fenêtre selon l'état : fiche, encart, rien.
    void Placer(SEtat& e) {
        const HWND s_Jeu = e.m_Parametres->m_FenetreDuJeu ? e.m_Parametres->m_FenetreDuJeu() : nullptr;
        const HWND s_Devant = GetForegroundWindow();
        const bool s_ModeEssai = !e.m_Parametres->m_FenetreDuJeu;
        const bool s_JeuDevant = s_ModeEssai || (s_Jeu && (s_Devant == s_Jeu || s_Devant == e.m_Hwnd)) ;

        RECT s_Zone {};

        if (s_Jeu) {
            GetClientRect(s_Jeu, &s_Zone);
            MapWindowPoints(s_Jeu, nullptr, reinterpret_cast<POINT*>(&s_Zone), 2);
        }
        else {
            s_Zone = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        }

        const bool s_Minimise = s_Jeu && IsIconic(s_Jeu);
        const bool s_Ouverte = *e.m_Ouverte;
        bool s_Montrer = s_JeuDevant && !s_Minimise && (s_Ouverte || (e.m_Encart.cx > 0 && e.m_Encart.cy > 0));

        RECT s_Rect = s_Zone;

        if (s_Ouverte && *e.m_Rompu) {
            s_Rect.left = s_Zone.left + static_cast<LONG>((s_Zone.right - s_Zone.left) * c_PartLibreRompu);
        }

        if (!s_Ouverte) {
            const double k = Echelle(s_Jeu ? s_Jeu : e.m_Hwnd);
            const int l = static_cast<int>(e.m_Encart.cx * k + 0.5);
            const int h = static_cast<int>(e.m_Encart.cy * k + 0.5);
            s_Rect.right = s_Zone.right - static_cast<int>(c_EncartDroite * k);
            s_Rect.left = s_Rect.right - l;
            s_Rect.top = s_Zone.top + static_cast<int>(c_EncartHaut * k);
            s_Rect.bottom = s_Rect.top + h;
        }

        if (!s_Montrer) {
            if (e.m_Visible) {
                ShowWindow(e.m_Hwnd, SW_HIDE);
                e.m_Visible = false;
            }
            return;
        }

        if (!EqualRect(&s_Rect, &e.m_DernierRect) || !e.m_Visible) {
            e.m_DernierRect = s_Rect;
            SetWindowPos(
                e.m_Hwnd, HWND_TOPMOST, s_Rect.left, s_Rect.top, s_Rect.right - s_Rect.left, s_Rect.bottom - s_Rect.top,
                SWP_NOACTIVATE | SWP_SHOWWINDOW
            );
            e.m_Visible = true;
        }
    }

    // ---------------------------------------------------------------- Pas de pause
    //
    // Quand la fiche prend le focus, le jeu reçoit WM_ACTIVATEAPP (FALSE) et se
    // met en pause (essai v0.3.2 : menu pause ouvert derrière la fiche). On
    // avale ce message — mais seulement pendant que la
    // fiche est ouverte, en s'intercalant sur la boîte de messages de la fenêtre
    // du jeu (SetWindowLongPtr : aucun code du jeu n'est modifié).

    std::atomic<bool> g_GarderJeuActif = false;
    WNDPROC g_ProcJeu = nullptr;
    HWND g_FenetreInterceptee = nullptr;

    LRESULT CALLBACK ProcJeu(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_ACTIVATEAPP && wp == FALSE && g_GarderJeuActif) {
            return 0;
        }

        // La musique du menu se coupait à l'ouverture de la fiche (Julien,
        // 23/11/2026) : le jeu l'éteint aussi sur WM_ACTIVATE / WM_KILLFOCUS.
        // Avalés de même tant que la fiche est ouverte ; à sa fermeture, le jeu
        // repasse devant et reçoit l'activation normalement.
        if (g_GarderJeuActif && ((msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) || msg == WM_KILLFOCUS)) {
            return 0;
        }

        return CallWindowProcW(g_ProcJeu, hwnd, msg, wp, lp);
    }

    void IntercepterJeu(HWND p_Jeu) {
        if (!p_Jeu || p_Jeu == g_FenetreInterceptee) return;

        const auto s_Ancienne = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(p_Jeu, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ProcJeu))
        );

        if (s_Ancienne && s_Ancienne != &ProcJeu) {
            g_ProcJeu = s_Ancienne;
            g_FenetreInterceptee = p_Jeu;
            Diag("Interface : fenêtre du jeu interceptée (pas de pause à l'ouverture de la fiche).");
        }
    }

    // Bascule entre « fiche » (prend souris et clavier) et « encart » (transparent aux clics).
    // p_PerteDeFocus : on ferme parce qu'une autre application est passée devant.
    void Basculer(SEtat& e, bool p_Ouvrir, bool p_PerteDeFocus = false) {
        if (*e.m_Ouverte == p_Ouvrir) return;

        *e.m_Ouverte = p_Ouvrir;

        LONG_PTR s_Ex = GetWindowLongPtrW(e.m_Hwnd, GWL_EXSTYLE);
        s_Ex = p_Ouvrir ? (s_Ex & ~WS_EX_NOACTIVATE) : (s_Ex | WS_EX_NOACTIVATE);
        SetWindowLongPtrW(e.m_Hwnd, GWL_EXSTYLE, s_Ex);

        Placer(e);
        EnvoyerEtat(e);

        const HWND s_Jeu = e.m_Parametres->m_FenetreDuJeu ? e.m_Parametres->m_FenetreDuJeu() : nullptr;

        if (p_Ouvrir) {
            IntercepterJeu(s_Jeu);
            // Contrat rompu : on laisse le jeu se mettre en pause (son menu pause
            // est le chemin vers « Recommencer »).
            g_GarderJeuActif = g_FenetreInterceptee != nullptr && !*e.m_Rompu;

            // Le jeu bride le curseur à sa fenêtre : on le libère le temps de la fiche.
            ClipCursor(nullptr);
            SetForegroundWindow(e.m_Hwnd);
            SetFocus(e.m_Hwnd);
            if (e.m_Controleur) e.m_Controleur->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            e.m_FocusDonne = true;
        }
        else {
            if (s_Jeu && !p_PerteDeFocus && GetForegroundWindow() == e.m_Hwnd) SetForegroundWindow(s_Jeu);
            g_GarderJeuActif = false;

            // Alt+Tab pendant la fiche : le jeu n'a jamais su qu'il perdait le
            // focus (message avalé) ; on le lui dit, sinon il garderait la souris.
            if (p_PerteDeFocus && s_Jeu && g_FenetreInterceptee == s_Jeu) {
                PostMessageW(s_Jeu, WM_ACTIVATEAPP, FALSE, 0);
            }

            e.m_FocusDonne = false;
        }
    }

    void TraiterMessagePage(SEtat& e, const std::string& p_Json) {
        // Ceux qui concernent la fenêtre elle-même ; tout part ensuite au mod.
        if (p_Json.find("\"action\":\"prete\"") != std::string::npos) {
            e.m_PagePrete = true;
            EnvoyerEtat(e);
        }
        else if (p_Json.find("\"action\":\"fermer\"") != std::string::npos) {
            if (!*e.m_Rompu) Basculer(e, false);
        }
        else if (p_Json.find("\"action\":\"encart\"") != std::string::npos) {
            // {"action":"encart","largeur":340,"hauteur":180}
            auto Lire = [&](const char* p_Cle) -> LONG {
                const auto s_Pos = p_Json.find(p_Cle);
                return s_Pos == std::string::npos ? 0 : std::atol(p_Json.c_str() + s_Pos + strlen(p_Cle));
            };
            e.m_Encart = { Lire("\"largeur\":"), Lire("\"hauteur\":") };
            Placer(e);
            return;
        }

        if (e.m_Parametres->m_Message) e.m_Parametres->m_Message(p_Json);
    }

    void RelayerSouris(SEtat& e, UINT p_Message, WPARAM wp, LPARAM lp) {
        if (!e.m_Composite) return;

        POINT s_Point { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        UINT32 s_Donnee = 0;
        auto s_Touches = static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(GET_KEYSTATE_WPARAM(wp));

        switch (p_Message) {
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                s_Donnee = static_cast<UINT32>(static_cast<INT32>(GET_WHEEL_DELTA_WPARAM(wp)));
                ScreenToClient(e.m_Hwnd, &s_Point); // la molette arrive en coordonnées d'écran
                break;
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
                SetCapture(e.m_Hwnd);
                break;
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
                ReleaseCapture();
                break;
            case WM_MOUSEMOVE:
                if (!e.m_SourisSuivie) {
                    TRACKMOUSEEVENT s_Suivi { sizeof(s_Suivi), TME_LEAVE, e.m_Hwnd, 0 };
                    TrackMouseEvent(&s_Suivi);
                    e.m_SourisSuivie = true;
                }
                break;
            case WM_MOUSELEAVE:
                e.m_SourisSuivie = false;
                break;
            default:
                break;
        }

        e.m_Composite->SendMouseInput(static_cast<COREWEBVIEW2_MOUSE_EVENT_KIND>(p_Message), s_Touches, s_Donnee, s_Point);
    }

    LRESULT CALLBACK Procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        SEtat* e = g_Etat;

        if (!e) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
            case WM_ROULETTE_POUSSER:
                EnvoyerEtat(*e);
                return 0;

            case WM_ROULETTE_OUVRIR:
                Basculer(*e, wp != 0);
                return 0;

            case WM_ROULETTE_ROMPRE:
                *e->m_Rompu = wp != 0;
                e->m_AvantRouvrir = 0;
                if (wp) {
                    Basculer(*e, false); // repart d'une fiche fermée : focus et pause rejoués
                    Basculer(*e, true);
                }
                Placer(*e);
                EnvoyerEtat(*e);
                return 0;

            case WM_TIMER: {
                // Touche de la fiche, quand le jeu (ou la fiche) est au premier plan.
                DWORD s_Processus = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &s_Processus);
                const bool s_Enfoncee = s_Processus == GetCurrentProcessId() &&
                                        (GetAsyncKeyState(e->m_Parametres->m_Touche) & 0x8000) != 0;

                if (s_Enfoncee && !e->m_ToucheEnfoncee && !*e->m_Rompu) Basculer(*e, !*e->m_Ouverte);
                e->m_ToucheEnfoncee = s_Enfoncee;

                // Contrat rompu et fiche refermée (Alt+Tab, reprise de la partie) :
                // elle revient d'elle-même au bout d'une seconde, jeu au premier plan.
                if (*e->m_Rompu && !*e->m_Ouverte && s_Processus == GetCurrentProcessId()) {
                    if (++e->m_AvantRouvrir >= 40) {
                        e->m_AvantRouvrir = 0;
                        Basculer(*e, true);
                    }
                }

                // Le jeu n'est plus devant avec la fiche ouverte (Alt+Tab) : on la referme.
                if (*e->m_Ouverte && s_Processus != GetCurrentProcessId() && e->m_Parametres->m_FenetreDuJeu) {
                    Basculer(*e, false, true);
                }

                Placer(*e);
                return 0;
            }

            case WM_SIZE:
                if (e->m_Controleur) {
                    RECT r {};
                    GetClientRect(hwnd, &r);
                    e->m_Controleur->put_Bounds(r);
                }
                return 0;

            case WM_DPICHANGED: {
                if (e->m_Controleur) {
                    ComPtr<ICoreWebView2Controller3> s_C3;
                    if (SUCCEEDED(e->m_Controleur.As(&s_C3))) s_C3->put_RasterizationScale(HIWORD(wp) / 96.0);
                }
                return 0;
            }

            case WM_MOUSEACTIVATE:
                return *e->m_Ouverte ? MA_ACTIVATE : MA_NOACTIVATE;

            case WM_NCHITTEST:
                // Encart : les clics passent au travers.
                return *e->m_Ouverte ? HTCLIENT : HTTRANSPARENT;

            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    SetCursor(e->m_Curseur);
                    return TRUE;
                }
                break;

            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_MOUSELEAVE:
                RelayerSouris(*e, msg, wp, lp);
                return 0;

            case WM_CLOSE:
                return 0; // seul le jeu ferme la fenêtre, en se fermant

            default:
                break;
        }

        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // ------------------------------------------------- Runtime WebView2
    //
    // Windows 11 l'a toujours ; Windows 10 le reçoit avec Edge. S'il manque,
    // le mod propose de l'installer : il télécharge le programme d'installation
    // officiel de Microsoft (« Evergreen Bootstrapper ») et le lance en silence.

    bool WebView2Present() {
        LPWSTR s_Version = nullptr;
        const HRESULT s_Resultat = GetAvailableCoreWebView2BrowserVersionString(nullptr, &s_Version);
        const bool s_Present = SUCCEEDED(s_Resultat) && s_Version;
        if (s_Version) CoTaskMemFree(s_Version);
        return s_Present;
    }

    bool InstallerWebView2() {
        wchar_t s_Temp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, s_Temp);
        const std::wstring s_Programme = std::wstring(s_Temp) + L"MicrosoftEdgeWebview2Setup.exe";

        if (FAILED(URLDownloadToFileW(nullptr, L"https://go.microsoft.com/fwlink/p/?LinkId=2124703", s_Programme.c_str(), 0, nullptr))) {
            Diag("Interface : téléchargement de l'installateur WebView2 impossible.");
            return false;
        }

        SHELLEXECUTEINFOW s_Lancement { sizeof(s_Lancement) };
        s_Lancement.fMask = SEE_MASK_NOCLOSEPROCESS;
        s_Lancement.lpFile = s_Programme.c_str();
        s_Lancement.lpParameters = L"/silent /install";
        s_Lancement.nShow = SW_HIDE;

        if (!ShellExecuteExW(&s_Lancement) || !s_Lancement.hProcess) {
            Diag("Interface : installateur WebView2 refusé ({}).", GetLastError());
            DeleteFileW(s_Programme.c_str());
            return false;
        }

        WaitForSingleObject(s_Lancement.hProcess, 10 * 60 * 1000);
        CloseHandle(s_Lancement.hProcess);
        DeleteFileW(s_Programme.c_str());
        return WebView2Present();
    }

    // Vrai si WebView2 est là (déjà, ou installé à l'instant avec l'accord du joueur).
    bool VerifierWebView2() {
        if (WebView2Present()) return true;

        Diag("Interface : runtime WebView2 absent, installation proposée.");
        const auto s_Question = Large(Tx("webview2.question"));
        if (MessageBoxW(nullptr, s_Question.c_str(), L"Roulette 47", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) != IDYES) {
            Diag("Interface : installation de WebView2 refusée ; la fiche restera fermée.");
            return false;
        }

        if (InstallerWebView2()) {
            Diag("Interface : WebView2 installé.");
            return true;
        }

        const auto s_Echec = Large(Tx("webview2.echec"));
        MessageBoxW(nullptr, s_Echec.c_str(), L"Roulette 47", MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
        return false;
    }

    void CreerVue(SEtat& e) {
        const auto& p = *e.m_Parametres;

        const HRESULT s_Resultat = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, p.m_DossierDonnees.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [&e](HRESULT r, ICoreWebView2Environment* p_Env) -> HRESULT {
                    if (FAILED(r) || !p_Env) {
                        Diag("Interface : WebView2 indisponible (0x{:08X}). Le runtime Edge WebView2 est-il installé ?",
                             static_cast<uint32_t>(r));
                        return S_OK;
                    }

                    if (FAILED(p_Env->QueryInterface(IID_PPV_ARGS(&e.m_Environnement)))) {
                        Diag("Interface : WebView2 trop ancien (pas d'hébergement visuel).");
                        return S_OK;
                    }

                    e.m_Environnement->CreateCoreWebView2CompositionController(
                        e.m_Hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                            [&e](HRESULT r2, ICoreWebView2CompositionController* p_Composite) -> HRESULT {
                                if (FAILED(r2) || !p_Composite) {
                                    Diag("Interface : création de la vue impossible (0x{:08X}).", static_cast<uint32_t>(r2));
                                    return S_OK;
                                }

                                e.m_Composite = p_Composite;
                                e.m_Composite.As(&e.m_Controleur);
                                e.m_Composite->put_RootVisualTarget(e.m_Visuel.Get());
                                e.m_Composition->Commit();

                                ComPtr<ICoreWebView2Controller2> s_C2;
                                if (SUCCEEDED(e.m_Controleur.As(&s_C2))) {
                                    s_C2->put_DefaultBackgroundColor({ 0, 0, 0, 0 }); // fond transparent
                                }

                                ComPtr<ICoreWebView2Controller3> s_C3;
                                if (SUCCEEDED(e.m_Controleur.As(&s_C3))) {
                                    s_C3->put_RasterizationScale(Echelle(e.m_Hwnd));
                                }

                                RECT r {};
                                GetClientRect(e.m_Hwnd, &r);
                                e.m_Controleur->put_Bounds(r);
                                e.m_Controleur->put_IsVisible(TRUE);
                                e.m_Controleur->get_CoreWebView2(&e.m_Vue);

                                EventRegistrationToken s_Jeton {};

                                e.m_Composite->add_CursorChanged(
                                    Callback<ICoreWebView2CursorChangedEventHandler>(
                                        [&e](ICoreWebView2CompositionController* c, IUnknown*) -> HRESULT {
                                            HCURSOR s_Curseur = nullptr;
                                            if (SUCCEEDED(c->get_Cursor(&s_Curseur)) && s_Curseur) {
                                                e.m_Curseur = s_Curseur;
                                                SetCursor(s_Curseur);
                                            }
                                            return S_OK;
                                        }).Get(), &s_Jeton);

                                ComPtr<ICoreWebView2Settings> s_Reglages;
                                e.m_Vue->get_Settings(&s_Reglages);
                                s_Reglages->put_AreDefaultContextMenusEnabled(e.m_Parametres->m_OutilsDeveloppement);
                                s_Reglages->put_AreDevToolsEnabled(e.m_Parametres->m_OutilsDeveloppement);
                                s_Reglages->put_IsStatusBarEnabled(FALSE);
                                s_Reglages->put_IsZoomControlEnabled(FALSE);

                                ComPtr<ICoreWebView2Settings3> s_R3;
                                if (SUCCEEDED(s_Reglages.As(&s_R3))) {
                                    s_R3->put_AreBrowserAcceleratorKeysEnabled(e.m_Parametres->m_OutilsDeveloppement);
                                }

                                ComPtr<ICoreWebView2_3> s_V3;
                                if (SUCCEEDED(e.m_Vue.As(&s_V3))) {
                                    s_V3->SetVirtualHostNameToFolderMapping(
                                        L"roulette.mod", e.m_Parametres->m_DossierUi.c_str(),
                                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                                    s_V3->SetVirtualHostNameToFolderMapping(
                                        L"cache.roulette.mod", e.m_Parametres->m_DossierCache.c_str(),
                                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                                }

                                e.m_Vue->AddScriptToExecuteOnDocumentCreated(L"window.ROULETTE_EN_JEU = true;", nullptr);

                                e.m_Vue->add_WebMessageReceived(
                                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [&e](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                                            LPWSTR s_Json = nullptr;
                                            if (SUCCEEDED(a->get_WebMessageAsJson(&s_Json)) && s_Json) {
                                                TraiterMessagePage(e, Etroit(s_Json));
                                                CoTaskMemFree(s_Json);
                                            }
                                            return S_OK;
                                        }).Get(), &s_Jeton);

                                e.m_Vue->add_NavigationCompleted(
                                    Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                        [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
                                            BOOL s_Ok = FALSE;
                                            a->get_IsSuccess(&s_Ok);
                                            Diag("Interface : page {}.", s_Ok ? "chargée" : "EN ÉCHEC");
                                            return S_OK;
                                        }).Get(), &s_Jeton);

                                e.m_Vue->add_ProcessFailed(
                                    Callback<ICoreWebView2ProcessFailedEventHandler>(
                                        [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* a) -> HRESULT {
                                            COREWEBVIEW2_PROCESS_FAILED_KIND s_Type {};
                                            a->get_ProcessFailedKind(&s_Type);
                                            Diag("Interface : un processus WebView2 s'est arrêté (type {}).", static_cast<int>(s_Type));
                                            return S_OK;
                                        }).Get(), &s_Jeton);

                                e.m_Vue->Navigate(L"https://roulette.mod/index.html");
                                return S_OK;
                            }).Get());

                    return S_OK;
                }).Get());

        if (FAILED(s_Resultat)) {
            Diag("Interface : WebView2 n'a pas démarré (0x{:08X}).", static_cast<uint32_t>(s_Resultat));
        }
    }
}

Fenetre::~Fenetre() {
    if (const HWND s_Hwnd = m_Hwnd) {
        PostMessageW(s_Hwnd, WM_QUIT, 0, 0);
    }

    if (m_Thread.joinable()) {
        m_Thread.detach(); // à la fermeture du jeu, on ne l'attend pas
    }
}

void Fenetre::Demarrer(SParametres p_Parametres) {
    m_Parametres = std::move(p_Parametres);
    m_Thread = std::thread(&Fenetre::Boucle, this);
}

void Fenetre::Pousser() {
    if (const HWND s_Hwnd = m_Hwnd) PostMessageW(s_Hwnd, WM_ROULETTE_POUSSER, 0, 0);
}

void Fenetre::Rompre(bool p_Rompu) {
    if (const HWND s_Hwnd = m_Hwnd) PostMessageW(s_Hwnd, WM_ROULETTE_ROMPRE, p_Rompu ? 1 : 0, 0);
}

void Fenetre::Ouvrir(bool p_Ouvrir) {
    if (const HWND s_Hwnd = m_Hwnd) PostMessageW(s_Hwnd, WM_ROULETTE_OUVRIR, p_Ouvrir ? 1 : 0, 0);
}

void Fenetre::Boucle() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        Diag("Interface : COM indisponible.");
        return;
    }

    static SEtat s_Etat;
    s_Etat.m_Parametres = &m_Parametres;
    s_Etat.m_Ouverte = &m_Ouverte;
    s_Etat.m_Rompu = &m_Rompu;
    g_Etat = &s_Etat;

    WNDCLASSEXW s_Classe { sizeof(s_Classe) };
    s_Classe.lpfnWndProc = Procedure;
    s_Classe.hInstance = GetModuleHandleW(nullptr);
    s_Classe.lpszClassName = L"RouletteInterface";
    s_Classe.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&s_Classe);

    s_Etat.m_Hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE, s_Classe.lpszClassName,
        L"Roulette 47", WS_POPUP, 0, 0, 640, 480, nullptr, nullptr, s_Classe.hInstance, nullptr
    );

    if (!s_Etat.m_Hwnd) {
        Diag("Interface : fenêtre impossible à créer ({}).", GetLastError());
        return;
    }

    // DirectComposition : la vue se dessine dans un visuel, sur fond transparent.
    if (FAILED(DCompositionCreateDevice2(nullptr, IID_PPV_ARGS(&s_Etat.m_Composition))) ||
        FAILED(s_Etat.m_Composition->CreateTargetForHwnd(s_Etat.m_Hwnd, TRUE, &s_Etat.m_Cible)) ||
        FAILED(s_Etat.m_Composition->CreateVisual(&s_Etat.m_Visuel))) {
        Diag("Interface : DirectComposition indisponible.");
        return;
    }

    s_Etat.m_Cible->SetRoot(s_Etat.m_Visuel.Get());
    s_Etat.m_Composition->Commit();

    m_Hwnd = s_Etat.m_Hwnd;

    if (VerifierWebView2()) CreerVue(s_Etat);
    SetTimer(s_Etat.m_Hwnd, c_Minuterie, 25, nullptr);

    Diag("Interface : fenêtre prête.");

    MSG s_Message;

    while (GetMessageW(&s_Message, nullptr, 0, 0) > 0) {
        TranslateMessage(&s_Message);
        DispatchMessageW(&s_Message);
    }
}
