#include "Langue.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

#include <Windows.h>

namespace {
    std::atomic<int> g_Langue = static_cast<int>(ELangue::Fr);

    using Traductions = std::array<const char*, 5>; // fr, en, it, es, ru

    const std::unordered_map<std::string, Traductions>& Table() {
        static const std::unordered_map<std::string, Traductions> s_Table = {
            // ---------------------------------------------------- Méthodes
            { "methode.accident", { "Accident (au choix)", "Accident (any)", "Incidente (qualsiasi)", "Accidente (cualquiera)", "Несчастный случай (любой)" } },
            { "methode.noyade", { "Noyade", "Drowning", "Annegamento", "Ahogamiento", "Утопление" } },
            { "methode.chute", { "Chute (poussée)", "Fall (push)", "Caduta (spinta)", "Caída (empujón)", "Падение (толчок)" } },
            { "methode.electrocution", { "Électrocution", "Electrocution", "Elettrocuzione", "Electrocución", "Удар током" } },
            { "methode.objet_suspendu", { "Objet suspendu", "Falling object", "Oggetto sospeso", "Objeto suspendido", "Падающий предмет" } },
            { "methode.brulure", { "Brûlure", "Burn", "Ustione", "Quemadura", "Сожжение" } },
            { "methode.explosion_accident", { "Explosion accidentelle", "Accidental explosion", "Esplosione accidentale", "Explosión accidental", "Случайный взрыв" } },
            { "methode.poison", { "Poison (au choix)", "Poison (any)", "Veleno (qualsiasi)", "Veneno (cualquiera)", "Яд (любой)" } },
            { "methode.poison_ingere", { "Poison ingéré", "Consumed poison", "Veleno ingerito", "Veneno ingerido", "Яд внутрь" } },
            { "methode.poison_injecte", { "Poison injecté", "Injected poison", "Veleno iniettato", "Veneno inyectado", "Инъекция яда" } },
            { "methode.pistolet", { "Pistolet", "Pistol", "Pistola", "Pistola", "Пистолет" } },
            { "methode.pm", { "Pistolet-mitrailleur", "SMG", "Mitraglietta", "Subfusil", "Пистолет-пулемёт" } },
            { "methode.pompe", { "Fusil à pompe", "Shotgun", "Fucile a pompa", "Escopeta", "Дробовик" } },
            { "methode.assaut", { "Fusil d'assaut", "Assault rifle", "Fucile d'assalto", "Fusil de asalto", "Штурмовая винтовка" } },
            { "methode.precision", { "Fusil de précision", "Sniper rifle", "Fucile di precisione", "Fusil de francotirador", "Снайперская винтовка" } },
            { "methode.explosif", { "Explosif", "Explosive", "Esplosivo", "Explosivo", "Взрывчатка" } },
            { "methode.corde", { "Corde de piano", "Fiber wire", "Corda di pianoforte", "Cable de fibra", "Струна" } },
            { "methode.mains_nues", { "À mains nues", "Unarmed", "A mani nude", "Sin armas", "Голыми руками" } },
            { "methode.melee", { "Arme de mêlée", "Melee weapon", "Arma da mischia", "Arma cuerpo a cuerpo", "Холодное оружие" } },

            // ---------------------------------------------------- Familles (clé : le nom français)
            { "famille.Accidents", { "Accidents", "Accidents", "Incidenti", "Accidentes", "Несчастные случаи" } },
            { "famille.Poisons", { "Poisons", "Poisons", "Veleni", "Venenos", "Яды" } },
            { "famille.Armes à feu", { "Armes à feu", "Firearms", "Armi da fuoco", "Armas de fuego", "Огнестрельное" } },
            { "famille.Explosifs", { "Explosifs", "Explosives", "Esplosivi", "Explosivos", "Взрывчатка" } },
            { "famille.Corps à corps", { "Corps à corps", "Close combat", "Corpo a corpo", "Cuerpo a cuerpo", "Ближний бой" } },
            { "famille.Au choix", { "Au choix", "Any", "A scelta", "A elección", "На выбор" } },

            // ---------------------------------------------------- Complications
            { "compl.sans_etre_repere.titre", { "Ne pas être repéré", "Don't get spotted", "Non farti scoprire", "Que no te descubran", "Не попасться" } },
            { "compl.sans_etre_repere.detail", { "Personne ne doit vous voir commettre un acte suspect.", "No one may see you do anything suspicious.",
                                                 "Nessuno deve vederti compiere azioni sospette.", "Nadie debe verte hacer nada sospechoso.",
                                                 "Никто не должен видеть ваших подозрительных действий." } },
            { "compl.aucun_corps.titre", { "Aucun corps découvert", "No bodies found", "Nessun corpo trovato", "Ningún cuerpo encontrado", "Тела не найдены" } },
            { "compl.aucun_corps.detail", { "Les corps d'accidents ne comptent pas.", "Accident victims don't count.", "I corpi degli incidenti non contano.",
                                            "Los cuerpos de accidentes no cuentan.", "Жертвы несчастных случаев не в счёт." } },
            { "compl.aucune_victime.titre", { "Aucune victime collatérale", "No collateral kills", "Nessuna vittima collaterale", "Ninguna víctima colateral", "Без лишних жертв" } },
            { "compl.aucune_victime.detail", { "Seules les cibles meurent.", "Only the targets die.", "Muoiono solo i bersagli.", "Solo mueren los objetivos.",
                                               "Умирают только цели." } },
            { "compl.aucune_neutralisation.titre", { "Aucune neutralisation", "No pacifications", "Nessuna neutralizzazione", "Ninguna neutralización", "Без нейтрализаций" } },
            { "compl.aucune_neutralisation.detail", { "Ne neutralisez personne d'autre que les cibles.", "Don't subdue anyone but the targets.",
                                                      "Non neutralizzare nessuno oltre ai bersagli.", "No neutralices a nadie salvo a los objetivos.",
                                                      "Не обезвреживайте никого, кроме целей." } },
            { "compl.aucun_changement.titre", { "Aucun changement de tenue", "No disguise change", "Nessun cambio di travestimento", "Sin cambiar de disfraz",
                                                "Без смены маскировки" } },
            { "compl.aucun_changement.detail", { "Vous gardez votre tenue de départ.", "Keep your starting outfit.", "Tieni il travestimento iniziale.",
                                                 "Mantén tu ropa inicial.", "Оставайтесь в начальной одежде." } },
            { "compl.aucun_enregistrement.titre", { "Aucun enregistrement", "No recordings", "Nessuna registrazione", "Sin grabaciones", "Без записей" } },
            { "compl.aucun_enregistrement.detail", { "Aucune caméra ne doit vous filmer.", "No camera may film you.", "Nessuna telecamera deve riprenderti.",
                                                     "Ninguna cámara debe grabarte.", "Ни одна камера не должна вас снять." } },
            { "compl.ordre_impose.titre", { "Ordre d'élimination imposé", "Kill order enforced", "Ordine di eliminazione imposto", "Orden de eliminación impuesto", "Строгий порядок устранения" } },
            { "compl.ordre_impose.detail", { "Éliminez les cibles dans l'ordre de la fiche.", "Eliminate the targets in the order shown on the sheet.",
                                             "Elimina i bersagli nell'ordine della scheda.", "Elimina a los objetivos en el orden de la ficha.",
                                             "Устраните цели в порядке, указанном в карточке." } },
            { "compl.tirs_tete.titre", { "Tirs à la tête uniquement", "Headshots only", "Solo colpi alla testa", "Solo disparos a la cabeza", "Только в голову" } },
            { "compl.tirs_tete.detail", { "Toute élimination par balle se fait à la tête.", "Every shooting kill must be a headshot.",
                                          "Ogni eliminazione con arma da fuoco deve essere alla testa.", "Toda eliminación a tiros debe ser a la cabeza.",
                                          "Все убийства из огнестрела — только в голову." } },

            // ---------------------------------------------------- Objectifs écrits dans le contrat
            { "obj.prefixe", { "Roulette 47 : ", "Roulette 47: ", "Roulette 47: ", "Roulette 47: ", "Roulette 47: " } },
            { "obj.eliminer", { "Roulette 47 : éliminer %s", "Roulette 47: eliminate %s", "Roulette 47: elimina %s", "Roulette 47: elimina a %s", "Roulette 47: устранить %s" } },
            { "obj.tenue", { "en %s", "wearing %s", "indossando %s", "vistiendo %s", "одежда: %s" } },

            // ---------------------------------------------------- Divers
            { "costume47", { "Costume de 47", "Agent 47's suit", "Completo di 47", "Traje de 47", "Костюм 47" } },
            { "diff.pnj", { "Cible hors contrat", "Off-contract target", "Bersaglio fuori contratto", "Objetivo fuera de contrato", "Цель вне контракта" } },
            { "diff.arme", { "Arme à trouver sur place", "Weapon to find on site", "Arma da trovare sul posto", "Arma que encontrar allí", "Оружие найти на месте" } },
            { "cible", { "Cible", "Target", "Bersaglio", "Objetivo", "Цель" } },
            { "detail.methode", { "Méthode : %s", "Method: %s", "Metodo: %s", "Método: %s", "Способ: %s" } },
            { "detail.tenue", { "mauvaise tenue", "wrong outfit", "travestimento sbagliato", "disfraz equivocado", "не та маскировка" } },
            { "detail.non_eliminee", { "Cible non éliminée", "Target not eliminated", "Bersaglio non eliminato", "Objetivo no eliminado", "Цель не устранена" } },
            { "raison.preparation", { "Préparation non respectée : %s n'a pas été emporté", "Planning not followed: %s was not carried",
                                      "Preparazione non rispettata: %s non è stato portato", "Preparación no respetada: no se llevó %s",
                                      "Подготовка нарушена: %s не взято" } },

            // ---------------------------------------------------- Pilote (secours au clavier)
            { "pilote.premier_plan", { "interrompu (le jeu n'est pas au premier plan).", "stopped (the game isn't in the foreground).",
                                       "interrotto (il gioco non è in primo piano).", "interrumpido (el juego no está en primer plano).",
                                       "прервано (игра не на переднем плане)." } },
            { "pilote.en_route", { "en route vers la préparation…", "heading to the planning screen…", "verso la preparazione…",
                                   "rumbo a la preparación…", "переход к подготовке…" } },
            // ---------------------------------------------------- WebView2 absent
            { "webview2.question", {
                "Roulette 47 a besoin du composant Microsoft Edge WebView2 pour afficher sa fiche, et il n'est pas installé sur ce PC.\n\nLe télécharger et l'installer maintenant (programme officiel de Microsoft, environ 2 Mo) ?",
                "Roulette 47 needs the Microsoft Edge WebView2 Runtime to show its sheet, and it is not installed on this PC.\n\nDownload and install it now (official Microsoft installer, about 2 MB)?",
                "Roulette 47 ha bisogno di Microsoft Edge WebView2 per mostrare la sua scheda, e non è installato su questo PC.\n\nScaricarlo e installarlo ora (programma ufficiale Microsoft, circa 2 MB)?",
                "Roulette 47 necesita Microsoft Edge WebView2 para mostrar su ficha, y no está instalado en este PC.\n\n¿Descargarlo e instalarlo ahora (instalador oficial de Microsoft, unos 2 MB)?",
                "Для показа карточки Roulette 47 нужен компонент Microsoft Edge WebView2, а он не установлен на этом ПК.\n\nСкачать и установить его сейчас (официальный установщик Microsoft, около 2 МБ)?" } },
            { "webview2.echec", {
                "L'installation de WebView2 n'a pas abouti. Installez-le depuis le site de Microsoft (« WebView2 Runtime »), puis relancez le jeu.",
                "WebView2 could not be installed. Install it from Microsoft's website (\"WebView2 Runtime\"), then restart the game.",
                "Impossibile installare WebView2. Installalo dal sito Microsoft (\"WebView2 Runtime\"), poi riavvia il gioco.",
                "No se pudo instalar WebView2. Instálalo desde el sitio de Microsoft («WebView2 Runtime») y reinicia el juego.",
                "Не удалось установить WebView2. Установите его с сайта Microsoft («WebView2 Runtime») и перезапустите игру." } },
            // ---------------------------------------------------- Code de contrat
            { "code.illisible", { "Ce code de contrat est illisible.", "This contract code cannot be read.", "Questo codice contratto è illeggibile.",
                                  "Este código de contrato no se puede leer.", "Этот код контракта не читается." } },
            { "code.mission", { "La mission de ce code n'est pas dans tes destinations (non possédée, ou menus pas encore ouverts).",
                                "This code's mission is not among your destinations (not owned, or menus not opened yet).",
                                "La missione di questo codice non è tra le tue destinazioni (non posseduta o menu non ancora aperti).",
                                "La misión de este código no está en tus destinos (no la tienes, o los menús aún no se han abierto).",
                                "Миссии из этого кода нет среди твоих направлений (не куплена или меню ещё не открыто)." } },
            { "code.manquants", { "Une entrée ou un objet de ce contrat n'est pas débloqué chez toi : il est laissé libre.",
                                  "A starting location or item of this contract isn't unlocked for you: it is left free.",
                                  "Un punto di partenza o un oggetto di questo contratto non è sbloccato: resta libero.",
                                  "Un punto de partida u objeto de este contrato no está desbloqueado: queda libre.",
                                  "Точка старта или предмет этого контракта у тебя не открыты: они оставлены свободными." } },
            { "code.dlc", { "La mission de ce code demande un DLC que tu ne possèdes pas.",
                            "This code's mission needs a DLC you don't own.",
                            "La missione di questo codice richiede un DLC che non possiedi.",
                            "La misión de este código requiere un DLC que no tienes.",
                            "Миссия из этого кода требует DLC, которого у тебя нет." } },
            { "code.en_mission", { "Charge un code depuis les menus, pas en mission.", "Load a code from the menus, not during a mission.",
                                   "Carica un codice dai menu, non in missione.", "Carga un código desde los menús, no en misión.",
                                   "Загружай код из меню, а не во время миссии." } },
            { "code.hors_ligne", { "Le jeu est hors ligne : connecte-toi aux serveurs d’IOI pour charger un code.",
                                   "The game is offline: connect to IOI’s servers to load a code.",
                                   "Il gioco è offline: connettiti ai server di IOI per caricare un codice.",
                                   "El juego está sin conexión: conéctate a los servidores de IOI para cargar un código.",
                                   "Игра не в сети: подключись к серверам IOI, чтобы загрузить код." } },
            // ---------------------------------------------------- Mise à jour
            { "maj.reseau", { "téléchargement impossible, réessaie plus tard ou passe par la page de la version",
                              "download failed, try again later or use the release page",
                              "download non riuscito, riprova più tardi o usa la pagina della versione",
                              "descarga fallida, inténtalo más tarde o usa la página de la versión",
                              "загрузка не удалась, попробуй позже или открой страницу версии" } },
            { "maj.somme", { "l'archive ne correspond pas à sa somme de contrôle : installe-la depuis la page de la version",
                             "the archive doesn't match its checksum: install it from the release page",
                             "l'archivio non corrisponde al suo checksum: installalo dalla pagina della versione",
                             "el archivo no coincide con su suma de control: instálalo desde la página de la versión",
                             "архив не совпадает с контрольной суммой: установи его со страницы версии" } },
            { "maj.archive", { "archive illisible ou incomplète", "unreadable or incomplete archive", "archivio illeggibile o incompleto",
                               "archivo ilegible o incompleto", "архив повреждён или неполон" } },
            { "maj.ecriture", { "écriture impossible dans le dossier du jeu", "couldn't write to the game folder",
                                "impossibile scrivere nella cartella del gioco", "no se pudo escribir en la carpeta del juego",
                                "не удалось записать в папку игры" } },
            // ---------------------------------------------------- Discord
            { "discord.menus", { "Dans les menus", "In the menus", "Nei menu", "En los menús", "В меню" } },
            { "discord.propose", { "Contrat proposé · %s", "Contract drawn · %s", "Contratto estratto · %s", "Contrato sorteado · %s", "Контракт выпал · %s" } },
            { "discord.accepte", { "Préparation · %s", "Planning · %s", "Pianificazione · %s", "Planificación · %s", "Подготовка · %s" } },
            { "discord.mission", { "En mission · %s", "On a mission · %s", "In missione · %s", "En misión · %s", "На задании · %s" } },
            { "discord.sans_roulette", { "En mission, sans roulette", "On a mission, no roulette", "In missione, senza roulette",
                                         "En misión, sin ruleta", "На задании, без рулетки" } },
            { "discord.cibles", { "%s cible(s) restante(s)", "%s target(s) left", "%s bersaglio/i rimasti", "%s objetivo(s) restante(s)", "Осталось целей: %s" } },
            { "discord.reussi", { "Contrat réussi", "Contract complete", "Contratto riuscito", "Contrato cumplido", "Контракт выполнен" } },
            { "discord.echoue", { "Contrat échoué", "Contract failed", "Contratto fallito", "Contrato fallido", "Контракт провален" } },
            { "discord.mode.assassin", { "Assassin", "Assassin", "Assassino", "Asesino", "Убийца" } },
            { "discord.mode.puriste", { "Puriste", "Purist", "Purista", "Purista", "Пурист" } },
            { "discord.mode.freestyle", { "Freestyle", "Freestyle", "Freestyle", "Freestyle", "Фристайл" } },
            { "pilote.interrompu", { "interrompu.", "stopped.", "interrotto.", "interrumpido.", "прервано." } },
            { "pilote.lieu", { "la page du lieu n'est pas arrivée : ouvre Destinations puis n'importe quel lieu.",
                               "the location page didn't load: open Destinations, then any location.",
                               "la pagina della località non è arrivata: apri Destinazioni e poi una località qualsiasi.",
                               "la página del destino no llegó: abre Destinos y luego cualquier destino.",
                               "страница локации не загрузилась: откройте «Локации» и любую локацию." } },
            { "pilote.preparation", { "la préparation n'est pas arrivée : ouvre la mission.", "the planning screen didn't load: open the mission.",
                                      "la preparazione non è arrivata: apri la missione.", "la preparación no llegó: abre la misión.",
                                      "экран подготовки не загрузился: откройте миссию." } },
            { "pilote.atteinte", { "préparation atteinte : il ne reste qu'à jouer.", "planning screen reached: just hit Play.",
                                   "preparazione raggiunta: basta giocare.", "preparación alcanzada: solo queda jugar.",
                                   "подготовка открыта: осталось только играть." } },
        };
        return s_Table;
    }

    std::string Minuscules(std::string p_Texte) {
        for (auto& c : p_Texte) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return p_Texte;
    }
}

ELangue Langue() {
    return static_cast<ELangue>(g_Langue.load());
}

void DefinirLangue(ELangue p_Langue) {
    g_Langue = static_cast<int>(p_Langue);
}

const char* CodeLangue(ELangue p_Langue) {
    switch (p_Langue) {
        case ELangue::En: return "en";
        case ELangue::It: return "it";
        case ELangue::Es: return "es";
        case ELangue::Ru: return "ru";
        default: return "fr";
    }
}

bool LangueDepuisCode(const std::string& p_Code, ELangue& p_Langue) {
    const auto c = Minuscules(p_Code);
    if (c == "fr" || c == "french" || c == "francais" || c == "français") p_Langue = ELangue::Fr;
    else if (c == "en" || c == "english") p_Langue = ELangue::En;
    else if (c == "it" || c == "italian") p_Langue = ELangue::It;
    else if (c == "es" || c == "spanish" || c == "latam") p_Langue = ELangue::Es;
    else if (c == "ru" || c == "russian") p_Langue = ELangue::Ru;
    else return false;
    return true;
}

ELangue LangueDeWindows() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_FRENCH: return ELangue::Fr;
        case LANG_ITALIAN: return ELangue::It;
        case LANG_SPANISH: return ELangue::Es;
        case LANG_RUSSIAN: return ELangue::Ru;
        default: return ELangue::En;
    }
}

// Le mod est dans steamapps/common/HITMAN 3/Retail/Roulette ; le manifeste du
// jeu est dans steamapps. Une autre langue que les cinq donne l'anglais.
bool LangueDeSteam(const std::string& p_DossierDuMod, ELangue& p_Langue) {
    std::error_code s_Erreur;
    auto s_Dossier = std::filesystem::path(p_DossierDuMod);
    for (int i = 0; i < 4 && s_Dossier.has_parent_path(); ++i) s_Dossier = s_Dossier.parent_path();

    std::ifstream s_Flux(s_Dossier / "appmanifest_1659040.acf", std::ios::binary);
    if (!s_Flux) return false;

    std::stringstream s_Texte;
    s_Texte << s_Flux.rdbuf();
    std::smatch s_Trouve;
    const std::string s_Contenu = s_Texte.str();
    if (!std::regex_search(s_Contenu, s_Trouve, std::regex("\"language\"\\s+\"([A-Za-z_]+)\""))) return false;

    if (!LangueDepuisCode(s_Trouve[1].str(), p_Langue)) p_Langue = ELangue::En;
    return true;
}

bool LangueDepuisTexteDuJeu(const std::string& p_EtatsUnis, ELangue& p_Langue) {
    if (p_EtatsUnis.empty()) return false;
    const auto t = Minuscules(p_EtatsUnis);

    if (t.find("tats") != std::string::npos) p_Langue = ELangue::Fr;          // États-Unis
    else if (t.find("stati") != std::string::npos) p_Langue = ELangue::It;    // Stati Uniti
    else if (t.find("estados") != std::string::npos) p_Langue = ELangue::Es;  // Estados Unidos
    else if (static_cast<unsigned char>(p_EtatsUnis[0]) == 0xD0 || static_cast<unsigned char>(p_EtatsUnis[0]) == 0xD1) {
        p_Langue = ELangue::Ru; // cyrillique (США)
    }
    else p_Langue = ELangue::En; // anglais, ou une langue que le mod n'a pas
    return true;
}

std::string Tx(const std::string& p_Cle) {
    const auto& s_Table = Table();
    const auto s_It = s_Table.find(p_Cle);
    if (s_It == s_Table.end()) return p_Cle;
    const char* s_Texte = s_It->second[static_cast<size_t>(Langue())];
    return s_Texte ? s_Texte : s_It->second[0];
}

std::string TxF(const std::string& p_Cle, const std::string& p_Valeur) {
    auto s_Texte = Tx(p_Cle);
    if (const auto p = s_Texte.find("%s"); p != std::string::npos) s_Texte.replace(p, 2, p_Valeur);
    return s_Texte;
}
