"""Relève dans HITMAN3.exe la table « droit -> DLC Steam » (src/Droits.cpp).

    python tools/droits_steam.py ["...\\HITMAN 3\\Retail\\HITMAN3.exe"]

Le jeu associe chaque droit des menus (H3_ET_SHAMROCK…) à un champ d'un objet
de configuration, puis range dans ces champs les numéros de DLC Steam : une
série par application Steam (cmp eax, <appid> en tête de la fonction). Ce
script retrouve les deux fonctions par les chaînes qu'elles chargent
(LEA rip-relatives), sans adresse codée en dur, et apparie par le décalage.

À rejouer après une mise à jour du jeu qui ajoute un pack, puis recopier les
lignes H3_… dans c_Table de src/Droits.cpp. Lecture seule de l'exécutable.
Il faut pefile et capstone (pip install pefile capstone).
"""
import re
import sys

import capstone
import pefile
from capstone.x86 import X86_OP_MEM

EXE = sys.argv[1] if len(sys.argv) > 1 else r"C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3\Retail\HITMAN3.exe"

pe = pefile.PE(EXE, fast_load=True)
data = open(EXE, "rb").read()
texte = next(s for s in pe.sections if s.Name.startswith(b".text"))
code = texte.get_data()
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
cs.detail = True


def rva_de(off):
    for s in pe.sections:
        if s.PointerToRawData <= off < s.PointerToRawData + s.SizeOfRawData:
            return off - s.PointerToRawData + s.VirtualAddress
    return None


def chaine(rva):
    off = pe.get_offset_from_rva(rva)
    fin = data.index(b"\x00", off)
    try:
        return data[off:fin].decode("ascii")
    except UnicodeDecodeError:
        return None


# Chaînes candidates : droits (H3_…, LOCATION_…) et numéros Steam.
chaines = {}
for m in re.finditer(rb"(?<=\x00)((?:H[123]_|LOCATION_)[A-Z0-9_]+|[1-9][0-9]{6})\x00", data):
    r = rva_de(m.start(1))
    if r:
        chaines[r] = m.group(1).decode()

# Les LEA rip-relatives vers ces chaînes.
refs = []
for m in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", code):
    i = m.start()
    disp = int.from_bytes(code[i + 3:i + 7], "little", signed=True)
    a = texte.VirtualAddress + i
    if a + 7 + disp in chaines:
        refs.append((a, chaines[a + 7 + disp]))
refs.sort()


def groupes(liste, ecart):
    g = []
    for a, v in liste:
        if g and a - g[-1][-1][0] <= ecart:
            g[-1].append((a, v))
        else:
            g.append([(a, v)])
    return g


def instructions(debut, fin):
    o = debut - texte.VirtualAddress
    return list(cs.disasm(code[o:o + (fin - debut)], debut))


def lea_rip(i):
    if i.mnemonic == "lea" and i.operands[1].type == X86_OP_MEM and i.operands[1].mem.base == capstone.x86.X86_REG_RIP:
        return i.address + i.size + i.operands[1].mem.disp
    return None


# ---- noms : le groupe de références qui contient H3_ET_SHAMROCK
noms_g = next(g for g in groupes([r for r in refs if not r[1].isdigit()], 0x200) if any(v == "H3_ET_SHAMROCK" for _, v in g))
noms, nom = {}, None
for i in instructions(noms_g[0][0] - 0x40, noms_g[-1][0] + 0x200):
    c = lea_rip(i)
    if c is not None:
        s = chaine(c)
        if s and re.fullmatch(r"[A-Z0-9_]{6,}", s):
            nom = s
        continue
    if nom and i.mnemonic == "lea" and i.operands[1].type == X86_OP_MEM and i.reg_name(i.operands[1].mem.base) not in ("rip", "rbp", "rsp"):
        noms[nom] = i.operands[1].mem.disp
        nom = None

# ---- numéros : deux séries de références à des nombres, l'une après l'autre
series = [g for g in groupes([r for r in refs if r[1].isdigit()], 0x100) if len(g) > 15][:2]


def champs(g):
    ins = instructions(g[0][0], g[-1][0] + 0x80)
    out, regs, dernier = {}, {}, None
    for k, i in enumerate(ins):
        c = lea_rip(i)
        if c is not None:
            s = chaine(c)
            if s and s.isdigit():
                dernier = s
            continue
        if i.mnemonic == "lea" and i.operands[1].type == X86_OP_MEM and i.reg_name(i.operands[1].mem.base) == "rbx":
            off, dest = i.operands[1].mem.disp, i.reg_name(i.operands[0].reg)
            regs[dest] = off
            if dest != "rcx":
                continue
            voisins = [ins[k - 1], ins[k + 1]] if k + 1 < len(ins) else [ins[k - 1]]
            copie = next((v.op_str.split(", ")[1] for v in voisins if v.mnemonic == "mov" and v.op_str.startswith("rdx, r") and v.op_str != "rdx, rax"), None)
            if copie in regs and regs[copie] in out:
                out[off] = out[regs[copie]]
            elif dernier:
                out[off] = dernier
    return out


A, B = (champs(g) for g in series)
print(f"{'droit':28} {'série 1':>9} {'série 2':>9}   (série 1 : appli 1659040/1847520, série 2 : 1172010/1850750)")
for n, off in noms.items():
    if n.startswith("H3_"):
        print(f"{n:28} {A.get(off, '-'):>9} {B.get(off, '-'):>9}")
