# Roulette 47 — mod autonome pour HITMAN World of Assassination.
#
# Deux DLL :
#   version.dll           chargeur, posé dans Retail/ (le jeu le charge seul)
#   Roulette47/Roulette47.dll le mod, plus son interface Roulette47/ui/
#
# Aucune bibliothèque de mod : ce que le mod sait de la mémoire du moteur est
# décrit dans src/Glacier.h, écrit pour lui. Seul le chargeur de WebView2
# (sdk/webview2, récupéré par tools/recuperer_webview2.ps1) vient de l'extérieur.
cmake_minimum_required(VERSION 3.15)
project(Roulette47 C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded") # rien à installer à côté

set(GAME_INSTALL_PATH "C:/Program Files (x86)/Steam/steamapps/common/HITMAN 3" CACHE PATH "Dossier du jeu")

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
add_compile_options(/utf-8 /permissive-)

include(FetchContent)

# nlohmann/json, pour lire les événements du jeu et parler à la page.
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

# Le désassembleur de MinHook (hde64, BSD) : il mesure les premières
# instructions de la fonction détournée (src/Moteur.cpp, PoserDetour).
# MinHook lui-même n'est pas utilisé : il ne trouvait pas de mémoire libre
# près du jeu (MH_ERROR_MEMORY_ALLOC).
FetchContent_Declare(minhook URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.3.tar.gz)
FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
    FetchContent_Populate(minhook)
endif()
set(SOURCES_MINHOOK "${minhook_SOURCE_DIR}/src/hde/hde64.c")

# miniz (MIT) : décompresse les réponses gzip du serveur d'IOI (src/Http.cpp).
FetchContent_Declare(miniz URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip)
FetchContent_GetProperties(miniz)
if(NOT miniz_POPULATED)
    FetchContent_Populate(miniz)
endif()

set(WEBVIEW2 "${CMAKE_SOURCE_DIR}/sdk/webview2/build/native")

# ---------------------------------------------------------------- version.dll
add_library(version SHARED src/chargeur/version.cpp)
set_target_properties(version PROPERTIES OUTPUT_NAME version)
target_compile_options(version PRIVATE /EHsc)

# ---------------------------------------------------------------- Roulette.dll
set(SOURCES_MOD
    src/Entree.cpp
    src/Roulette.cpp
    src/Moteur.cpp
    src/Fenetre.cpp
    src/Images.cpp
    src/Depot.cpp
    src/Tirage.cpp
    src/Langue.cpp
    src/Glacier.cpp
    src/CodeContrat.cpp
    src/Discord.cpp
    src/Diagnostic.cpp
    src/Http.cpp
    src/Contrat.cpp
    src/Menus.cpp
)
add_library(Roulette SHARED ${SOURCES_MOD} ${SOURCES_MINHOOK} "${miniz_SOURCE_DIR}/miniz.c")
set_target_properties(Roulette PROPERTIES OUTPUT_NAME Roulette47)
# /EHa : une erreur d'accès en lisant la mémoire du jeu (rattrapée par __except)
# déroule quand même les destructeurs. Avec /EHsc, un verrou resterait pris et
# le thread du jeu se bloquerait à l'image suivante.
target_compile_options(Roulette PRIVATE /EHa)
target_include_directories(Roulette PRIVATE
    src
    "${WEBVIEW2}/include"
    "${minhook_SOURCE_DIR}/src/hde"
    "${miniz_SOURCE_DIR}"
)
target_link_libraries(Roulette PRIVATE
    nlohmann_json::nlohmann_json winhttp dcomp urlmon
    "${WEBVIEW2}/x64/WebView2LoaderStatic.lib"
)

# ---------------------------------------------------------------- Essai de l'interface
# La fenêtre et la page sur le bureau, sans le jeu (données réelles de Paris).
add_executable(EssaiInterface WIN32 tools/essai_interface.cpp src/Fenetre.cpp src/Langue.cpp src/Diagnostic.cpp)
target_compile_options(EssaiInterface PRIVATE /EHsc)
target_include_directories(EssaiInterface PRIVATE src "${WEBVIEW2}/include")
target_link_libraries(EssaiInterface PRIVATE dcomp urlmon "${WEBVIEW2}/x64/WebView2LoaderStatic.lib")

# ---------------------------------------------------------------- Essai du contrat
# La roulette appliquée au vrai contrat relevé, hors du jeu (tools/essai_contrat.cpp).
add_executable(EssaiContrat tools/essai_contrat.cpp src/Contrat.cpp src/Menus.cpp src/Tirage.cpp src/Langue.cpp src/Glacier.cpp src/Depot.cpp src/Moteur.cpp
    src/Diagnostic.cpp src/Http.cpp ${SOURCES_MINHOOK} "${miniz_SOURCE_DIR}/miniz.c")
target_compile_options(EssaiContrat PRIVATE /EHa)
target_include_directories(EssaiContrat PRIVATE src
    "${minhook_SOURCE_DIR}/src/hde" "${miniz_SOURCE_DIR}")
target_link_libraries(EssaiContrat PRIVATE nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------- Essai des menus
# Destinations, missions, préparation : lues et réécrites sur les vraies pages
# relevées, hors du jeu (tools/essai_menus.cpp).
add_executable(EssaiMenus tools/essai_menus.cpp src/Menus.cpp src/Contrat.cpp src/Tirage.cpp src/Langue.cpp src/Glacier.cpp src/Depot.cpp src/Moteur.cpp
    src/Diagnostic.cpp src/Http.cpp ${SOURCES_MINHOOK} "${miniz_SOURCE_DIR}/miniz.c")
target_compile_options(EssaiMenus PRIVATE /EHa)
target_include_directories(EssaiMenus PRIVATE src
    "${minhook_SOURCE_DIR}/src/hde" "${miniz_SOURCE_DIR}")
target_link_libraries(EssaiMenus PRIVATE nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------- Essai de toutes les missions
# Chaque contrat de mission : cibles lues, roulette, objectifs du jeu masqués
# (tools/essai_missions.cpp).
add_executable(EssaiMissions tools/essai_missions.cpp src/Menus.cpp src/Contrat.cpp src/Tirage.cpp src/Langue.cpp src/Glacier.cpp src/Depot.cpp src/Moteur.cpp
    src/Diagnostic.cpp src/Http.cpp ${SOURCES_MINHOOK} "${miniz_SOURCE_DIR}/miniz.c")
target_compile_options(EssaiMissions PRIVATE /EHa)
target_include_directories(EssaiMissions PRIVATE src
    "${minhook_SOURCE_DIR}/src/hde" "${miniz_SOURCE_DIR}")
target_link_libraries(EssaiMissions PRIVATE nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------- Installation
# cmake --install build : jeu FERMÉ.
install(FILES $<TARGET_FILE:version> DESTINATION "${GAME_INSTALL_PATH}/Retail")
install(FILES $<TARGET_FILE:Roulette> DESTINATION "${GAME_INSTALL_PATH}/Retail/Roulette47")
install(DIRECTORY ui/ DESTINATION "${GAME_INSTALL_PATH}/Retail/Roulette47/ui")
# Les PNJ et tenues relevés sur chaque mission, pour un premier tirage sans visite.
install(DIRECTORY data/lieux_initiaux/ DESTINATION "${GAME_INSTALL_PATH}/Retail/Roulette47/lieux_initiaux")
