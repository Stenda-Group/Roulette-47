"""Aperçu de l'interface hors du jeu : sert ui/ et, sous /cache/, le cache
d'images que le mod remplit dans le jeu (rien n'est recopié dans le projet).

    python tools/apercu_ui.py [port]      (8772 par défaut)
"""
import functools
import http.server
import pathlib
import sys
import urllib.parse

RACINE = pathlib.Path(__file__).resolve().parent.parent / "ui"
CACHE = pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3\Retail\Roulette\cache")


class Gestion(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        chemin = urllib.parse.unquote(urllib.parse.urlsplit(path).path)
        if chemin.startswith("/cache/"):
            cible = (CACHE / chemin[len("/cache/"):]).resolve()
            # Aucune remontée hors du cache.
            return str(cible) if CACHE.resolve() in cible.parents else str(CACHE / "__refuse__")
        return super().translate_path(path)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8772
    serveur = http.server.ThreadingHTTPServer(
        ("127.0.0.1", port), functools.partial(Gestion, directory=str(RACINE))
    )
    print(f"http://127.0.0.1:{port}/")
    serveur.serve_forever()
