#!/usr/bin/env python3
"""Pise osnovni namjestaj kao Procedura assete (*.loomasset.json) u furniture/. Pokreni: python3 napravi.py

Svaki asset je obican recept od kvadara; broj moze biti P("width", scale, offset) = width * scale + offset.
Prostor asseta: baza na y = 0, sredina u ishodistu, sirina po X, prednja strana prema +Z (leda uz zid).
Test (test_procedura_assets) provjerava da kutija mreze odgovara `bounds` na min/default/max parametrima."""
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))
ALL = {"semantic": "", "use_direction": False, "direction": [0, 1, 0], "max_angle_degrees": 30}


def P(name, scale=1.0, offset=0.0):
    value = {"param": name}
    if scale != 1.0: value["scale"] = scale
    if offset != 0.0: value["offset"] = offset
    return value


class Asset:
    def __init__(self, ident, category, placement="wall", clearance=0.6, style="basic"):
        self.id, self.category, self.placement, self.clearance, self.style = ident, category, placement, clearance, style
        self.parameters, self.nodes, self.links, self.parts, self.grips = [], [], [], [], []

    def param(self, name, default, low, high):
        self.parameters.append({"name": name, "default": default, "min": low, "max": high})

    def node(self, params):
        nid = len(self.nodes) + 1
        self.nodes.append({"id": nid, "position": [0, 0], "parameters": params})
        return nid

    def link(self, a, b, port=0):
        self.links.append({"from": a, "from_port": 0, "to": b, "to_port": port})

    def box(self, size, at, material, semantic="furniture"):
        """Kvadar velicine size (x, y, z) sa sredistem u at."""
        cur = self.node({"type": "add_primitive", "primitive": "cube", "size": size, "tube_ratio": 0.25})
        for params in ({"type": "move", "offset": at},
                       {"type": "set_semantic", "semantic": semantic, "filter": ALL},
                       {"type": "set_material", "material": material, "filter": ALL},
                       {"type": "uv_project", "tile_size": 0.5}):
            n = self.node(params); self.link(cur, n); cur = n
        self.parts.append(cur)

    def grip(self, point, thickness, axis=(0, 1, 0), palm=(1, 0, 0), preset="grip", name="Main"):
        """Hvat kao Warp Grip: tocka na osi drske, os od malog prsta prema palcu, strana dlana."""
        self.grips.append({"name": name, "point": point, "axis": list(axis), "palm": list(palm),
                           "thickness": thickness, "preset": preset, "hand": 0})

    def cylinder(self, size, at, material, semantic="furniture", primitive="cylinder"):
        """Valjak (os Y) velicine size sa sredistem u at (ili drugi primitiv iz jedinicne kutije)."""
        cur = self.node({"type": "add_primitive", "primitive": primitive, "size": size, "tube_ratio": 0.25})
        for params in ({"type": "move", "offset": at},
                       {"type": "set_semantic", "semantic": semantic, "filter": ALL},
                       {"type": "set_material", "material": material, "filter": ALL},
                       {"type": "uv_project", "tile_size": 0.5}):
            n = self.node(params); self.link(cur, n); cur = n
        self.parts.append(cur)

    def save(self, bounds):
        parts = self.parts
        while len(parts) > 1:   # Merge prima najvise 8 ulaza
            merged = []
            for i in range(0, len(parts), 8):
                group = parts[i:i + 8]
                if len(group) == 1: merged.append(group[0]); continue
                m = self.node({"type": "merge"})
                for port, p in enumerate(group): self.link(p, m, port)
                merged.append(m)
            parts = merged
        doc = {"format": "loom.weaverprocedura.asset", "id": self.id, "category": self.category,
               "style": self.style, "parameters": self.parameters, "bounds": bounds, "placement": self.placement,
               "clearance_front": self.clearance, **({"grips": self.grips} if self.grips else {}),
               "recipe": {"format": "loom.weaverprocedura.recipe", "schema_version": 8, "name": self.id, "seed": 1,
                          "nodes": self.nodes, "links": self.links}}
        folder = os.path.join(HERE, FOLDER.get(self.category, "furniture"))
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, self.id + ".loomasset.json"), "w") as f: json.dump(doc, f, indent=1)


# Mapa po vrsti, da se alati, oruzje i rekviziti mogu uciti odvojeno od namjestaja.
FOLDER = {**{c: "tools" for c in ("hammer", "axe", "saw", "shovel", "pickaxe", "wrench", "screwdriver", "knife")},
          **{c: "weapons" for c in ("sword", "spear", "mace", "club")},
          **{c: "props" for c in ("crate", "barrel", "bucket", "lantern", "bottle", "book", "plant_pot", "chest")}}

WOOD, DARK, FABRIC, METAL, CERAMIC, GLASS = "wood_planks", "wood_beam", "fabric", "metal", "ceramic", "glass"
LACQUER, LEATHER, LINEN, STONE = "lacquer", "leather", "linen", "stone"


def legs(a, w, d, height, inset=0.04, thick=0.05):
    """Cetiri noge ispod ploce; w i d su parametri (ime) ili brojevi."""
    for sx in (-1, 1):
        for sz in (-1, 1):
            x = P(w, 0.5 * sx, -sx * inset) if isinstance(w, str) else sx * (w / 2 - inset)
            z = P(d, 0.5 * sz, -sz * inset) if isinstance(d, str) else sz * (d / 2 - inset)
            a.box([thick, height, thick], [x, height / 2, z], DARK)


def bed():
    a = Asset("bed_basic", "bed", clearance=0.6)
    a.param("width", 1.6, 0.8, 2.0); a.param("length", 2.0, 1.9, 2.2)
    # dubina = length + 0.08 (uzglavlje); okvir i madrac pocinju iza uzglavlja
    a.box([P("width"), 0.4, P("length")], [0, 0.2, 0.04], DARK)
    a.box([P("width", 1, -0.04), 0.2, P("length", 1, -0.04)], [0, 0.5, 0.04], FABRIC)
    a.box([P("width"), 1.0, 0.08], [0, 0.5, P("length", -0.5)], DARK)
    a.box([P("width", 1, -0.3), 0.12, 0.4], [0, 0.66, P("length", -0.5, 0.33)], FABRIC)
    a.save([P("width"), 1.0, P("length", 1, 0.08)])


def nightstand():
    a = Asset("nightstand_basic", "nightstand", clearance=0.3)
    a.param("width", 0.45, 0.4, 0.6)
    a.box([P("width"), 0.55, 0.38], [0, 0.275, -0.01], DARK)
    a.box([P("width", 1, -0.06), 0.18, 0.02], [0, 0.4, 0.19], WOOD)
    a.save([P("width"), 0.55, 0.4])


def wardrobe():
    a = Asset("wardrobe_basic", "wardrobe", clearance=0.7)
    a.param("width", 1.6, 0.8, 2.4)
    a.box([P("width"), 2.1, 0.56], [0, 1.05, -0.02], DARK)
    for side in (-1, 1):
        a.box([P("width", 0.5, -0.02), 2.0, 0.02], [P("width", 0.25 * side), 1.05, 0.27], WOOD)
        a.box([0.03, 0.3, 0.02], [0.06 * side, 1.1, 0.29], METAL)
    a.save([P("width"), 2.1, 0.6])


def desk():
    a = Asset("desk_basic", "desk", clearance=0.8)
    a.param("width", 1.4, 1.0, 1.8); a.param("depth", 0.7, 0.6, 0.8)
    a.box([P("width"), 0.04, P("depth")], [0, 0.73, 0], WOOD)
    legs(a, "width", "depth", 0.71, 0.03)
    a.save([P("width"), 0.75, P("depth")])


def chair():
    a = Asset("chair_basic", "chair", placement="center", clearance=0.3)
    a.box([0.45, 0.05, 0.45], [0, 0.45, 0], WOOD)
    a.box([0.45, 0.425, 0.04], [0, 0.6875, -0.205], WOOD)
    legs(a, 0.45, 0.45, 0.425, 0.03, 0.04)
    a.save([0.45, 0.9, 0.45])


def sofa(ident="sofa_basic", category="sofa", width=(2.0, 1.4, 2.6)):
    a = Asset(ident, category, clearance=0.5)
    a.param("width", *width)
    a.box([P("width"), 0.42, 0.9], [0, 0.21, 0], FABRIC)
    a.box([P("width"), 0.43, 0.2], [0, 0.635, -0.35], FABRIC)
    for side in (-1, 1):
        a.box([0.18, 0.2, 0.7], [P("width", 0.5 * side, -0.09 * side), 0.52, 0.1], FABRIC)
    a.box([P("width", 1, -0.38), 0.1, 0.66], [0, 0.47, 0.11], FABRIC)
    a.save([P("width"), 0.85, 0.9])


def coffee_table():
    a = Asset("coffee_table_basic", "coffee_table", placement="center", clearance=0.4)
    a.param("width", 1.1, 0.8, 1.3)
    a.box([P("width"), 0.05, 0.6], [0, 0.425, 0], WOOD)
    legs(a, "width", 0.6, 0.4)
    a.save([P("width"), 0.45, 0.6])


def tv_stand():
    a = Asset("tv_stand_basic", "tv_stand", clearance=0.3)
    a.param("width", 1.6, 1.0, 2.0)
    a.box([P("width"), 0.45, 0.45], [0, 0.225, 0], DARK)
    a.box([0.1, 0.06, 0.1], [0, 0.48, -0.1], METAL)
    a.box([P("width", 0.7), 0.6, 0.05], [0, 0.8, -0.1], GLASS)
    a.save([P("width"), 1.1, 0.45])


def shelf():
    a = Asset("shelf_basic", "shelf", clearance=0.6)
    a.param("width", 1.2, 0.6, 1.6)
    for side in (-1, 1):
        a.box([0.03, 2.0, 0.35], [P("width", 0.5 * side, -0.015 * side), 1.0, 0], DARK)
    for y in (0.015, 0.5, 1.0, 1.5, 1.985):
        a.box([P("width", 1, -0.06), 0.03, 0.33], [0, y, 0.01], DARK)
    a.box([P("width", 1, -0.06), 2.0, 0.01], [0, 1.0, -0.17], WOOD)
    a.box([P("width", 0.3), 0.25, 0.22], [P("width", -0.2), 0.64, 0], FABRIC)
    a.box([P("width", 0.25), 0.3, 0.24], [P("width", 0.15), 1.165, 0], FABRIC)
    a.save([P("width"), 2.0, 0.35])


def table():
    a = Asset("table_basic", "table", placement="center", clearance=0.6)
    a.param("width", 1.2, 0.8, 4.0); a.param("depth", 0.8, 0.7, 1.2)
    a.box([P("width"), 0.04, P("depth")], [0, 0.73, 0], WOOD)
    legs(a, "width", "depth", 0.71, 0.05, 0.06)
    a.save([P("width"), 0.75, P("depth")])


def kitchen_counter():
    a = Asset("kitchen_counter_basic", "kitchen_counter", clearance=1.0)
    a.param("width", 2.4, 1.2, 4.5)
    a.param("fixtures", 1.0, 0.0, 1.0)   # 0: sudoper i ploca spusteni u korpus (krak kutnog niza)
    a.box([P("width"), 0.1, 0.5], [0, 0.05, -0.05], DARK)
    a.box([P("width"), 0.76, 0.56], [0, 0.48, -0.02], WOOD)
    a.box([P("width"), 0.04, 0.6], [0, 0.88, 0], "stone")
    a.box([0.5, 0.004, 0.4], [P("width", -0.25), P("fixtures", 0.1, 0.802), 0], METAL)
    a.box([0.6, 0.004, 0.5], [P("width", 0.25), P("fixtures", 0.1, 0.802), 0], METAL)
    a.save([P("width"), 0.904, 0.6])


def fridge():
    a = Asset("fridge_basic", "fridge", clearance=0.8)
    a.param("width", 0.6, 0.55, 0.9)
    a.box([P("width"), 1.85, 0.63], [0, 0.925, -0.01], "plaster")
    a.box([0.03, 0.5, 0.02], [P("width", 0.5, -0.06), 1.1, 0.315], METAL)
    a.save([P("width"), 1.85, 0.65])


def toilet():
    a = Asset("toilet_basic", "toilet", clearance=0.5)
    a.box([0.4, 0.4, 0.18], [0, 0.6, -0.26], CERAMIC)
    a.box([0.36, 0.4, 0.5], [0, 0.2, 0.1], CERAMIC)
    a.box([0.38, 0.03, 0.46], [0, 0.415, 0.1], CERAMIC)
    a.save([0.4, 0.8, 0.7])


def sink():
    a = Asset("sink_basic", "sink", clearance=0.6)
    a.param("width", 0.6, 0.45, 1.0)
    a.box([P("width"), 0.7, 0.43], [0, 0.35, -0.01], WOOD)
    a.box([P("width"), 0.12, 0.45], [0, 0.76, 0], CERAMIC)
    a.box([0.04, 0.13, 0.04], [0, 0.885, -0.17], METAL)
    a.save([P("width"), 0.95, 0.45])


def bathtub():
    a = Asset("bathtub_basic", "bathtub", clearance=0.6)
    a.param("width", 1.7, 1.4, 1.9)
    a.box([P("width"), 0.1, 0.75], [0, 0.05, 0], CERAMIC)
    for side in (-1, 1):
        a.box([P("width"), 0.45, 0.06], [0, 0.325, 0.345 * side], CERAMIC)
        a.box([0.06, 0.45, 0.63], [P("width", 0.5 * side, -0.03 * side), 0.325, 0], CERAMIC)
    a.save([P("width"), 0.55, 0.75])


def shower():
    a = Asset("shower_basic", "shower", clearance=0.6)
    a.param("width", 0.9, 0.8, 1.0)
    a.box([P("width"), 0.08, P("width")], [0, 0.04, 0], CERAMIC)
    a.box([P("width"), 1.9, 0.01], [0, 1.03, P("width", 0.5, -0.005)], GLASS)
    a.box([0.03, 1.0, 0.03], [0, 1.5, P("width", -0.5, 0.03)], METAL)
    a.box([0.15, 0.02, 0.15], [0, 1.99, P("width", -0.5, 0.12)], METAL)
    a.save([P("width"), 2.0, P("width")])


def shoe_cabinet():
    a = Asset("shoe_cabinet_basic", "shoe_cabinet", clearance=0.5)
    a.param("width", 0.8, 0.6, 1.2)
    a.box([P("width"), 0.9, 0.33], [0, 0.45, -0.01], DARK)
    a.box([P("width", 1, -0.04), 0.4, 0.02], [0, 0.65, 0.165], WOOD)
    a.save([P("width"), 0.9, 0.35])


def rug():
    a = Asset("rug_basic", "rug", placement="center", clearance=0.0)
    a.param("width", 2.0, 0.8, 3.0); a.param("depth", 1.4, 0.6, 2.5)
    a.box([P("width"), 0.012, P("depth")], [0, 0.006, 0], FABRIC)
    a.box([P("width", 1, -0.16), 0.002, P("depth", 1, -0.16)], [0, 0.013, 0], "plaster")
    a.save([P("width"), 0.014, P("depth")])


def floor_lamp():
    a = Asset("floor_lamp_basic", "floor_lamp", clearance=0.0)
    a.box([0.3, 0.03, 0.3], [0, 0.015, 0], METAL)
    a.box([0.03, 1.3, 0.03], [0, 0.68, 0], METAL)
    a.cylinder([0.35, 0.3, 0.35], [0, 1.45, 0], FABRIC)
    a.save([0.35, 1.6, 0.35])


def table_lamp():
    a = Asset("table_lamp_basic", "table_lamp", placement="center", clearance=0.0)
    a.box([0.12, 0.03, 0.12], [0, 0.015, 0], METAL)
    a.box([0.02, 0.22, 0.02], [0, 0.14, 0], METAL)
    a.cylinder([0.25, 0.2, 0.25], [0, 0.35, 0], FABRIC)
    a.save([0.25, 0.45, 0.25])


def wall_cabinet():
    a = Asset("wall_cabinet_basic", "wall_cabinet", clearance=0.0)
    a.param("width", 1.2, 0.4, 4.5)
    a.box([P("width"), 0.7, 0.33], [0, 0.35, -0.01], WOOD)
    a.box([P("width", 1, -0.02), 0.66, 0.02], [0, 0.35, 0.165], DARK)
    a.save([P("width"), 0.7, 0.35])



# ---- Stilovi -------------------------------------------------------------------------------
# modern: bijeli lak, metal, staklo, niski oblici, tanke noge; rustic: tamno drvo, debele ploce i
# noge, koza, lan, kamena radna ploca. Kategorije bez svog stila uzimaju "basic".

def modern_set():
    M = dict(style="modern")
    a = Asset("bed_modern", "bed", clearance=0.6, **M)
    a.param("width", 1.6, 0.8, 2.0); a.param("length", 2.0, 1.9, 2.2)
    a.box([P("width"), 0.25, P("length")], [0, 0.125, 0.03], LACQUER)
    a.box([P("width", 1, -0.04), 0.2, P("length", 1, -0.04)], [0, 0.35, 0.03], LINEN)
    a.box([P("width"), 0.9, 0.04], [0, 0.45, P("length", -0.5, -0.01)], WOOD)
    a.box([P("width", 1, -0.3), 0.1, 0.35], [0, 0.5, P("length", -0.5, 0.25)], LINEN)
    a.save([P("width"), 0.9, P("length", 1, 0.06)])

    a = Asset("nightstand_modern", "nightstand", clearance=0.3, **M)
    a.param("width", 0.45, 0.4, 0.6)
    a.box([P("width"), 0.3, 0.4], [0, 0.4, 0], LACQUER)
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.box([0.02, 0.25, 0.02], [P("width", 0.5 * sx, -0.03 * sx), 0.125, sz * 0.17], METAL)
    a.save([P("width"), 0.55, 0.4])

    a = Asset("wardrobe_modern", "wardrobe", clearance=0.7, **M)
    a.param("width", 1.6, 0.8, 2.4)
    a.box([P("width"), 2.1, 0.58], [0, 1.05, -0.01], LACQUER)
    a.box([P("width", 1, -0.04), 2.02, 0.02], [0, 1.05, 0.29], GLASS)
    a.save([P("width"), 2.1, 0.6])

    for ident, cat, width in (("sofa_modern", "sofa", (2.0, 1.4, 2.6)), ("armchair_modern", "armchair", (0.85, 0.7, 1.0))):
        a = Asset(ident, cat, clearance=0.5, **M)
        a.param("width", *width)
        for sx in (-1, 1):
            for sz in (-1, 1):
                a.box([0.04, 0.08, 0.04], [P("width", 0.5 * sx, -0.05 * sx), 0.04, sz * 0.4], METAL)
        a.box([P("width"), 0.3, 0.9], [0, 0.23, 0], LINEN)
        a.box([P("width"), 0.35, 0.15], [0, 0.555, -0.375], LINEN)
        for side in (-1, 1):
            a.box([0.08, 0.15, 0.75], [P("width", 0.5 * side, -0.04 * side), 0.455, 0.075], LINEN)
        a.box([P("width", 1, -0.16), 0.08, 0.72], [0, 0.42, 0.08], LINEN)
        a.save([P("width"), 0.73, 0.9])

    a = Asset("coffee_table_modern", "coffee_table", placement="center", clearance=0.4, **M)
    a.param("width", 1.1, 0.8, 1.3)
    a.box([P("width"), 0.02, 0.6], [0, 0.39, 0], GLASS)
    for side in (-1, 1):
        a.box([0.03, 0.38, 0.6], [P("width", 0.5 * side, -0.015 * side), 0.19, 0], METAL)
    a.save([P("width"), 0.4, 0.6])

    a = Asset("table_modern", "table", placement="center", clearance=0.6, **M)
    a.param("width", 1.2, 0.8, 4.0); a.param("depth", 0.8, 0.7, 1.2)
    a.box([P("width"), 0.03, P("depth")], [0, 0.735, 0], LACQUER)
    legs(a, "width", "depth", 0.72, 0.06, 0.035)
    a.save([P("width"), 0.75, P("depth")])

    a = Asset("chair_modern", "chair", placement="center", clearance=0.3, **M)
    a.box([0.45, 0.04, 0.45], [0, 0.45, 0], LACQUER)
    a.box([0.45, 0.35, 0.03], [0, 0.645, -0.21], LACQUER)
    a.box([0.45, 0.08, 0.03], [0, 0.86, -0.21], LACQUER)
    legs(a, 0.45, 0.45, 0.43, 0.02, 0.02)
    a.save([0.45, 0.9, 0.45])

    a = Asset("tv_stand_modern", "tv_stand", clearance=0.3, **M)
    a.param("width", 1.6, 1.0, 2.0)
    a.box([P("width"), 0.3, 0.4], [0, 0.25, 0], LACQUER)
    a.box([P("width", 1, -0.1), 0.1, 0.3], [0, 0.05, -0.05], METAL)
    a.box([P("width", 0.75), 0.62, 0.03], [0, 0.69, -0.1], GLASS)
    a.box([0.12, 0.02, 0.12], [0, 0.41, -0.1], METAL)
    a.save([P("width"), 1.0, 0.4])

    a = Asset("desk_modern", "desk", clearance=0.8, **M)
    a.param("width", 1.4, 1.0, 1.8); a.param("depth", 0.7, 0.6, 0.8)
    a.box([P("width"), 0.03, P("depth")], [0, 0.735, 0], LACQUER)
    for side in (-1, 1):
        a.box([0.03, 0.72, P("depth", 1, -0.04)], [P("width", 0.5 * side, -0.04 * side), 0.36, 0], METAL)
    a.save([P("width"), 0.75, P("depth")])

    a = Asset("shelf_modern", "shelf", clearance=0.6, **M)
    a.param("width", 1.2, 0.6, 1.6)
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.box([0.02, 2.0, 0.02], [P("width", 0.5 * sx, -0.01 * sx), 1.0, sz * 0.165], METAL)
    for y in (0.3, 0.75, 1.2, 1.65, 1.99):
        a.box([P("width", 1, -0.04), 0.02, 0.33], [0, y, 0], LACQUER)
    a.box([P("width", 0.3), 0.3, 0.22], [P("width", 0.2), 0.46, 0], LINEN)
    a.save([P("width"), 2.0, 0.35])

    a = Asset("kitchen_counter_modern", "kitchen_counter", clearance=1.0, **M)
    a.param("width", 2.4, 1.2, 4.5); a.param("fixtures", 1.0, 0.0, 1.0)
    a.box([P("width"), 0.1, 0.5], [0, 0.05, -0.05], METAL)
    a.box([P("width"), 0.76, 0.58], [0, 0.48, -0.01], LACQUER)
    a.box([P("width"), 0.03, 0.6], [0, 0.875, 0], "concrete")
    a.box([0.5, 0.004, 0.4], [P("width", -0.25), P("fixtures", 0.1, 0.792), 0], METAL)
    a.box([0.6, 0.004, 0.5], [P("width", 0.25), P("fixtures", 0.1, 0.792), 0], GLASS)
    a.save([P("width"), 0.894, 0.6])

    a = Asset("wall_cabinet_modern", "wall_cabinet", clearance=0.0, **M)
    a.param("width", 1.2, 0.4, 4.5)
    a.box([P("width"), 0.7, 0.35], [0, 0.35, 0], LACQUER)
    a.save([P("width"), 0.7, 0.35])

    a = Asset("rug_modern", "rug", placement="center", clearance=0.0, **M)
    a.param("width", 2.0, 0.8, 3.0); a.param("depth", 1.4, 0.6, 2.5)
    a.box([P("width"), 0.012, P("depth")], [0, 0.006, 0], LINEN)
    a.save([P("width"), 0.012, P("depth")])

    a = Asset("floor_lamp_modern", "floor_lamp", clearance=0.0, **M)
    a.cylinder([0.3, 0.02, 0.3], [0, 0.01, 0], METAL)
    a.box([0.02, 1.4, 0.02], [0, 0.72, 0], METAL)
    a.cylinder([0.35, 0.18, 0.35], [0, 1.51, 0], LACQUER)
    a.save([0.35, 1.6, 0.35])


def rustic_set():
    R = dict(style="rustic")
    a = Asset("bed_rustic", "bed", clearance=0.6, **R)
    a.param("width", 1.6, 0.8, 2.0); a.param("length", 2.0, 1.9, 2.2)
    a.box([P("width"), 0.45, P("length")], [0, 0.225, 0.05], DARK)
    a.box([P("width", 1, -0.08), 0.22, P("length", 1, -0.08)], [0, 0.56, 0.05], LINEN)
    a.box([P("width"), 1.1, 0.1], [0, 0.55, P("length", -0.5)], DARK)
    for side in (-1, 1):
        a.box([0.1, 1.2, 0.1], [P("width", 0.5 * side, -0.05 * side), 0.6, P("length", -0.5)], DARK)
        a.box([0.1, 0.7, 0.1], [P("width", 0.5 * side, -0.05 * side), 0.35, P("length", 0.5)], DARK)
    a.box([P("width", 1, -0.3), 0.12, 0.4], [0, 0.73, P("length", -0.5, 0.35)], LINEN)
    a.save([P("width"), 1.2, P("length", 1, 0.1)])

    a = Asset("nightstand_rustic", "nightstand", clearance=0.3, **R)
    a.param("width", 0.5, 0.4, 0.6)
    a.box([P("width", 1, -0.04), 0.5, 0.38], [0, 0.25, -0.01], DARK)
    a.box([P("width"), 0.06, 0.4], [0, 0.53, 0], WOOD)
    a.save([P("width"), 0.56, 0.4])

    a = Asset("wardrobe_rustic", "wardrobe", clearance=0.7, **R)
    a.param("width", 1.6, 0.8, 2.4)
    a.box([P("width", 1, -0.06), 2.0, 0.56], [0, 1.0, -0.02], DARK)
    a.box([P("width"), 0.1, 0.6], [0, 2.05, 0], DARK)
    for side in (-1, 1):
        a.box([P("width", 0.5, -0.08), 1.8, 0.02], [P("width", 0.25 * side, -0.005 * side), 1.0, 0.27], WOOD)
        a.box([0.03, 0.06, 0.02], [0.08 * side, 1.1, 0.29], METAL)
    a.save([P("width"), 2.1, 0.6])

    for ident, cat, width in (("sofa_rustic", "sofa", (2.0, 1.4, 2.6)), ("armchair_rustic", "armchair", (0.9, 0.75, 1.0))):
        a = Asset(ident, cat, clearance=0.5, **R)
        a.param("width", *width)
        a.box([P("width"), 0.45, 0.95], [0, 0.225, 0], LEATHER)
        a.box([P("width"), 0.5, 0.25], [0, 0.7, -0.35], LEATHER)
        for side in (-1, 1):
            a.box([0.22, 0.25, 0.7], [P("width", 0.5 * side, -0.11 * side), 0.575, 0.125], LEATHER)
        a.box([P("width", 1, -0.44), 0.12, 0.68], [0, 0.51, 0.12], LEATHER)
        a.save([P("width"), 0.95, 0.95])

    a = Asset("coffee_table_rustic", "coffee_table", placement="center", clearance=0.4, **R)
    a.param("width", 1.1, 0.8, 1.3)
    a.box([P("width"), 0.08, 0.65], [0, 0.44, 0], DARK)
    legs(a, "width", 0.65, 0.4, 0.07, 0.1)
    a.save([P("width"), 0.48, 0.65])

    a = Asset("table_rustic", "table", placement="center", clearance=0.6, **R)
    a.param("width", 1.2, 0.8, 4.0); a.param("depth", 0.9, 0.7, 1.2)
    a.box([P("width"), 0.07, P("depth")], [0, 0.735, 0], DARK)
    legs(a, "width", "depth", 0.7, 0.08, 0.1)
    a.box([P("width", 1, -0.25), 0.08, 0.08], [0, 0.15, 0], DARK)
    a.save([P("width"), 0.77, P("depth")])

    a = Asset("chair_rustic", "chair", placement="center", clearance=0.3, **R)
    a.box([0.45, 0.05, 0.45], [0, 0.45, 0], WOOD)
    for sx in (-1, 1):
        a.box([0.05, 0.95, 0.05], [sx * 0.2, 0.475, -0.2], DARK)
        a.box([0.05, 0.425, 0.05], [sx * 0.2, 0.2125, 0.2], DARK)
    for y in (0.62, 0.78, 0.92):
        a.box([0.35, 0.05, 0.03], [0, y, -0.2], DARK)
    a.save([0.45, 0.95, 0.45])

    a = Asset("tv_stand_rustic", "tv_stand", clearance=0.3, **R)
    a.param("width", 1.6, 1.0, 2.0)
    a.box([P("width"), 0.55, 0.45], [0, 0.275, -0.01], DARK)
    a.box([P("width", 1, -0.1), 0.35, 0.02], [0, 0.3, 0.225], WOOD)
    a.box([P("width", 0.65), 0.55, 0.05], [0, 0.875, -0.1], GLASS)
    a.save([P("width"), 1.15, 0.47])

    a = Asset("desk_rustic", "desk", clearance=0.8, **R)
    a.param("width", 1.4, 1.0, 1.8); a.param("depth", 0.7, 0.6, 0.8)
    a.box([P("width"), 0.06, P("depth")], [0, 0.75, 0], DARK)
    legs(a, "width", "depth", 0.72, 0.06, 0.08)
    a.save([P("width"), 0.78, P("depth")])

    a = Asset("shelf_rustic", "shelf", clearance=0.6, **R)
    a.param("width", 1.2, 0.6, 1.6)
    for side in (-1, 1):
        a.box([0.06, 2.0, 0.38], [P("width", 0.5 * side, -0.03 * side), 1.0, 0], DARK)
    for y in (0.03, 0.6, 1.15, 1.7, 1.97):
        a.box([P("width", 1, -0.12), 0.06, 0.36], [0, y, 0], DARK)
    a.box([P("width", 0.35), 0.3, 0.25], [P("width", -0.15), 0.21, 0], LEATHER)
    a.save([P("width"), 2.0, 0.38])

    a = Asset("kitchen_counter_rustic", "kitchen_counter", clearance=1.0, **R)
    a.param("width", 2.4, 1.2, 4.5); a.param("fixtures", 1.0, 0.0, 1.0)
    a.box([P("width"), 0.1, 0.5], [0, 0.05, -0.05], DARK)
    a.box([P("width"), 0.74, 0.56], [0, 0.47, -0.02], WOOD)
    a.box([P("width"), 0.06, 0.6], [0, 0.87, 0], STONE)
    a.box([0.5, 0.004, 0.4], [P("width", -0.25), P("fixtures", 0.1, 0.802), 0], CERAMIC)
    a.box([0.6, 0.004, 0.5], [P("width", 0.25), P("fixtures", 0.1, 0.802), 0], METAL)
    a.save([P("width"), 0.904, 0.6])

    a = Asset("wall_cabinet_rustic", "wall_cabinet", clearance=0.0, **R)
    a.param("width", 1.2, 0.4, 4.5)
    a.box([P("width"), 0.64, 0.33], [0, 0.32, -0.01], WOOD)
    a.box([P("width"), 0.06, 0.35], [0, 0.67, 0], DARK)
    a.save([P("width"), 0.7, 0.35])

    a = Asset("rug_rustic", "rug", placement="center", clearance=0.0, **R)
    a.param("width", 2.0, 0.8, 3.0); a.param("depth", 1.4, 0.6, 2.5)
    a.box([P("width"), 0.015, P("depth")], [0, 0.0075, 0], LEATHER)
    a.box([P("width", 1, -0.3), 0.002, P("depth", 1, -0.3)], [0, 0.016, 0], LINEN)
    a.save([P("width"), 0.017, P("depth")])


# ---- Alati, oruzje, rekviziti --------------------------------------------------------------
# Prostor alata: kraj drske na y = 0, glava ili ostrica prema +Y, udarna strana / ostrica prema +Z.
# Kutija je i ovdje centrirana u X i Z, pa drska smije biti pomaknuta od sredine (hvat to kaze).
P_ = "prop"


def tool_set():
    a = Asset("hammer_basic", "hammer", placement="center", clearance=0.0)
    a.param("length", 0.33, 0.25, 0.45); a.param("head", 0.12, 0.09, 0.16)
    a.cylinder([0.03, P("length", 1, -0.02), 0.03], [0, P("length", 0.5, -0.01), 0], DARK, P_)
    a.box([0.035, 0.035, P("head")], [0, P("length", 1, -0.0175), 0], METAL, P_)
    a.grip([0, P("length", 0.3), 0], 0.015)
    a.save([0.035, P("length"), P("head")])

    a = Asset("axe_basic", "axe", placement="center", clearance=0.0)
    a.param("length", 0.7, 0.4, 0.9)
    a.cylinder([0.035, P("length", 1, -0.02), 0.035], [0, P("length", 0.5, -0.01), -0.07], DARK, P_)
    a.box([0.025, 0.12, 0.18], [0, P("length", 1, -0.06), 0], METAL, P_)
    a.grip([0, P("length", 0.2), -0.07], 0.0175)
    a.save([0.035, P("length"), 0.18])

    a = Asset("saw_basic", "saw", placement="center", clearance=0.0)
    a.param("length", 0.5, 0.35, 0.6)
    a.box([0.03, 0.14, 0.12], [0, 0.07, 0], DARK, P_)
    a.box([0.002, P("length"), 0.12], [0, P("length", 0.5, 0.12), 0], METAL, P_)
    a.grip([0, 0.07, -0.02], 0.015, axis=(0, 0, 1), preset="pistol")
    a.save([0.03, P("length", 1, 0.12), 0.12])

    a = Asset("shovel_basic", "shovel", placement="center", clearance=0.0)
    a.param("length", 1.0, 0.8, 1.3)
    a.cylinder([0.035, P("length"), 0.035], [0, P("length", 0.5), 0], DARK, P_)
    a.box([0.22, 0.28, 0.02], [0, P("length", 1, 0.14), 0], METAL, P_)
    a.grip([0, 0.1, 0], 0.0175)
    a.grip([0, P("length", 0.55), 0], 0.0175, name="Support")
    a.save([0.22, P("length", 1, 0.28), 0.035])

    a = Asset("pickaxe_basic", "pickaxe", placement="center", clearance=0.0)
    a.param("length", 0.8, 0.6, 0.9)
    a.cylinder([0.035, P("length", 1, -0.02), 0.035], [0, P("length", 0.5, -0.01), 0], DARK, P_)
    a.box([0.03, 0.04, 0.6], [0, P("length", 1, -0.02), 0], METAL, P_)
    a.grip([0, P("length", 0.2), 0], 0.0175)
    a.save([0.035, P("length"), 0.6])

    a = Asset("wrench_basic", "wrench", placement="center", clearance=0.0)
    a.param("length", 0.25, 0.15, 0.35)
    a.box([0.025, P("length", 1, -0.04), 0.008], [0, P("length", 0.5, -0.02), 0], METAL, P_)
    a.box([0.05, 0.04, 0.012], [0, P("length", 1, -0.02), 0], METAL, P_)
    a.grip([0, P("length", 0.3), 0], 0.012)
    a.save([0.05, P("length"), 0.012])

    a = Asset("screwdriver_basic", "screwdriver", placement="center", clearance=0.0)
    a.param("length", 0.12, 0.06, 0.2)
    a.cylinder([0.03, 0.1, 0.03], [0, 0.05, 0], "fabric", P_)
    a.cylinder([0.006, P("length"), 0.006], [0, P("length", 0.5, 0.1), 0], METAL, P_)
    a.grip([0, 0.05, 0], 0.015)
    a.save([0.03, P("length", 1, 0.1), 0.03])

    a = Asset("knife_basic", "knife", placement="center", clearance=0.0)
    a.param("length", 0.18, 0.1, 0.25)
    a.box([0.02, 0.11, 0.025], [0, 0.055, 0], DARK, P_)
    a.box([0.002, P("length"), 0.03], [0, P("length", 0.5, 0.11), 0], METAL, P_)
    a.grip([0, 0.055, 0], 0.0125)
    a.save([0.02, P("length", 1, 0.11), 0.03])


def weapon_set():
    a = Asset("sword_basic", "sword", placement="center", clearance=0.0)
    a.param("length", 0.8, 0.5, 1.0)
    a.box([0.045, 0.03, 0.045], [0, 0.015, 0], METAL, P_)
    a.cylinder([0.03, 0.13, 0.03], [0, 0.095, 0], LEATHER, P_)
    a.box([0.03, 0.025, 0.2], [0, 0.1725, 0], METAL, P_)
    a.box([0.006, P("length"), 0.05], [0, P("length", 0.5, 0.185), 0], METAL, P_)
    a.grip([0, 0.095, 0], 0.015)
    a.save([0.045, P("length", 1, 0.185), 0.2])

    a = Asset("spear_basic", "spear", placement="center", clearance=0.0)
    a.param("length", 2.0, 1.6, 2.4)
    a.cylinder([0.035, P("length"), 0.035], [0, P("length", 0.5), 0], DARK, P_)
    a.cylinder([0.05, 0.25, 0.05], [0, P("length", 1, 0.125), 0], METAL, P_, primitive="pyramid")
    a.grip([0, P("length", 0.35), 0], 0.0175)
    a.grip([0, P("length", 0.6), 0], 0.0175, name="Support")
    a.save([0.05, P("length", 1, 0.25), 0.05])

    a = Asset("mace_basic", "mace", placement="center", clearance=0.0)
    a.param("length", 0.6, 0.5, 0.8)
    a.cylinder([0.035, P("length"), 0.035], [0, P("length", 0.5), 0], DARK, P_)
    a.cylinder([0.14, 0.14, 0.14], [0, P("length"), 0], METAL, P_, primitive="sphere")
    a.grip([0, P("length", 0.2), 0], 0.0175)
    a.save([0.14, P("length", 1, 0.07), 0.14])

    a = Asset("club_basic", "club", placement="center", clearance=0.0)
    a.param("length", 0.7, 0.5, 0.9)
    a.cylinder([0.04, P("length", 0.5), 0.04], [0, P("length", 0.25), 0], DARK, P_)
    a.cylinder([0.09, P("length", 0.5), 0.09], [0, P("length", 0.75), 0], DARK, P_)
    a.grip([0, P("length", 0.2), 0], 0.02)
    a.save([0.09, P("length"), 0.09])


def prop_set():
    a = Asset("crate_basic", "crate", placement="center", clearance=0.0)
    a.param("size", 0.6, 0.3, 1.0)
    a.box([P("size", 1, -0.02), P("size", 1, -0.02), P("size", 1, -0.02)], [0, P("size", 0.5), 0], WOOD, P_)
    for sx in (-1, 1):
        for sz in (-1, 1):
            a.box([0.04, P("size"), 0.04], [P("size", 0.5 * sx, -0.02 * sx), P("size", 0.5), P("size", 0.5 * sz, -0.02 * sz)], DARK, P_)
    a.save([P("size"), P("size"), P("size")])

    a = Asset("barrel_basic", "barrel", placement="center", clearance=0.0)
    a.param("height", 0.9, 0.5, 1.2)
    a.cylinder([0.6, P("height"), 0.6], [0, P("height", 0.5), 0], WOOD, P_)
    for f in (0.12, 0.88):
        a.cylinder([0.62, 0.04, 0.62], [0, P("height", f), 0], METAL, P_)
    a.save([0.62, P("height"), 0.62])

    a = Asset("bucket_basic", "bucket", placement="center", clearance=0.0)
    a.cylinder([0.3, 0.3, 0.3], [0, 0.15, 0], METAL, P_)
    for sx in (-1, 1):
        a.box([0.01, 0.1, 0.01], [sx * 0.145, 0.35, 0], METAL, P_)
    a.box([0.3, 0.01, 0.01], [0, 0.395, 0], METAL, P_)
    a.grip([0, 0.395, 0], 0.006, axis=(1, 0, 0), palm=(0, 1, 0))
    a.save([0.3, 0.4, 0.3])

    a = Asset("lantern_basic", "lantern", placement="center", clearance=0.0)
    a.box([0.16, 0.02, 0.16], [0, 0.01, 0], METAL, P_)
    a.box([0.14, 0.2, 0.14], [0, 0.12, 0], GLASS, P_)
    a.cylinder([0.16, 0.06, 0.16], [0, 0.25, 0], METAL, P_, primitive="pyramid")
    a.box([0.006, 0.04, 0.006], [0, 0.295, 0], METAL, P_)
    a.box([0.08, 0.01, 0.01], [0, 0.315, 0], METAL, P_)
    a.grip([0, 0.315, 0], 0.005, axis=(1, 0, 0), palm=(0, 1, 0))
    a.save([0.16, 0.32, 0.16])

    a = Asset("bottle_basic", "bottle", placement="center", clearance=0.0)
    a.cylinder([0.08, 0.22, 0.08], [0, 0.11, 0], GLASS, P_)
    a.cylinder([0.03, 0.08, 0.03], [0, 0.26, 0], GLASS, P_)
    a.grip([0, 0.1, 0], 0.04, preset="cup")
    a.save([0.08, 0.3, 0.08])

    a = Asset("book_basic", "book", placement="center", clearance=0.0)
    a.box([0.17, 0.004, 0.24], [0, 0.002, 0], LEATHER, P_)
    a.box([0.17, 0.004, 0.24], [0, 0.028, 0], LEATHER, P_)
    a.box([0.004, 0.03, 0.24], [-0.083, 0.015, 0], LEATHER, P_)
    a.box([0.16, 0.022, 0.23], [0.004, 0.015, 0], LINEN, P_)
    a.save([0.17, 0.03, 0.24])

    a = Asset("plant_pot_basic", "plant_pot", placement="center", clearance=0.0)
    a.param("height", 0.8, 0.4, 1.4)
    a.cylinder([0.35, 0.3, 0.35], [0, 0.15, 0], CERAMIC, P_)
    a.cylinder([0.04, P("height", 0.5, -0.1), 0.04], [0, P("height", 0.25, 0.25), 0], WOOD, P_)
    a.cylinder([0.5, P("height", 0.5), 0.5], [0, P("height", 0.75), 0], "grass", P_, primitive="sphere")
    a.save([0.5, P("height"), 0.5])

    a = Asset("chest_basic", "chest", placement="wall", clearance=0.5)
    a.param("width", 0.9, 0.6, 1.2)
    a.box([P("width"), 0.4, 0.5], [0, 0.2, 0], DARK, P_)
    a.box([P("width"), 0.12, 0.5], [0, 0.46, 0], WOOD, P_)
    for sx in (-1, 1):
        a.box([0.04, 0.52, 0.52], [P("width", 0.3 * sx), 0.26, 0], METAL, P_)
    a.save([P("width"), 0.52, 0.52])

if __name__ == "__main__":
    for make in (bed, nightstand, wardrobe, desk, chair, sofa, coffee_table, tv_stand, shelf, table,
                 kitchen_counter, fridge, toilet, sink, bathtub, shower, shoe_cabinet, rug, floor_lamp,
                 table_lamp, wall_cabinet):
        make()
    sofa("armchair_basic", "armchair", (0.85, 0.7, 1.0))
    modern_set()
    rustic_set()
    tool_set()
    weapon_set()
    prop_set()
    print("gotovo:", sum(len(os.listdir(os.path.join(HERE, f))) for f in ("furniture", "tools", "weapons", "props")), "asseta")
