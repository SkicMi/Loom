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
    def __init__(self, ident, category, placement="wall", clearance=0.6):
        self.id, self.category, self.placement, self.clearance = ident, category, placement, clearance
        self.parameters, self.nodes, self.links, self.parts = [], [], [], []

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
               "parameters": self.parameters, "bounds": bounds, "placement": self.placement,
               "clearance_front": self.clearance,
               "recipe": {"format": "loom.weaverprocedura.recipe", "schema_version": 8, "name": self.id, "seed": 1,
                          "nodes": self.nodes, "links": self.links}}
        folder = os.path.join(HERE, "furniture")
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, self.id + ".loomasset.json"), "w") as f: json.dump(doc, f, indent=1)


WOOD, DARK, FABRIC, METAL, CERAMIC, GLASS = "wood_planks", "wood_beam", "fabric", "metal", "ceramic", "glass"


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
    a.box([P("width"), 0.1, 0.5], [0, 0.05, -0.05], DARK)
    a.box([P("width"), 0.76, 0.56], [0, 0.48, -0.02], WOOD)
    a.box([P("width"), 0.04, 0.6], [0, 0.88, 0], "stone")
    a.box([0.5, 0.004, 0.4], [P("width", -0.25), 0.902, 0], METAL)
    a.box([0.6, 0.004, 0.5], [P("width", 0.25), 0.902, 0], METAL)
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


if __name__ == "__main__":
    for make in (bed, nightstand, wardrobe, desk, chair, sofa, coffee_table, tv_stand, shelf, table,
                 kitchen_counter, fridge, toilet, sink, bathtub, shower, shoe_cabinet):
        make()
    sofa("armchair_basic", "armchair", (0.85, 0.7, 1.0))
    print("gotovo:", len(os.listdir(os.path.join(HERE, "furniture"))), "asseta")
