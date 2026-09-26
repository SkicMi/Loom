#!/usr/bin/env python3
"""Pise testne recepte (kuca, lanac na stupovima) u ovu mapu. Pokreni: python3 napravi.py"""
import json, os

class Recipe:
    def __init__(self, name, seed=1):
        self.name, self.seed, self.nodes, self.links = name, seed, [], []
    def node(self, params, x=0.0, y=0.0):
        nid = len(self.nodes) + 1
        self.nodes.append({"id": nid, "position": [x, y], "parameters": params})
        return nid
    def link(self, a, b, port=0):
        self.links.append({"from": a, "from_port": 0, "to": b, "to_port": port})
    def chain(self, first, *params):
        """first -> params[0] -> params[1] ...; vraca zadnji cvor."""
        cur = first
        for p in params:
            n = self.node(p); self.link(cur, n); cur = n
        return cur
    def save(self, path):
        doc = {"format": "loom.weaverprocedura.recipe", "schema_version": 6, "name": self.name,
               "seed": self.seed, "nodes": self.nodes, "links": self.links}
        with open(path, "w") as f: json.dump(doc, f, indent=1)

ALL = {"semantic": "", "use_direction": False, "direction": [0, 1, 0], "max_angle_degrees": 30}
def prim(kind, size, tube=0.25): return {"type": "add_primitive", "primitive": kind, "size": size, "tube_ratio": tube}
def move(x, y, z): return {"type": "move", "offset": [x, y, z]}
def rotate(x, y, z): return {"type": "rotate", "degrees": [x, y, z], "pivot": [0, 0, 0]}
def scale(x, y, z): return {"type": "scale", "factor": [x, y, z], "pivot": [0, 0, 0]}
def sem(name): return {"type": "set_semantic", "semantic": name, "filter": ALL}
def mat(name): return {"type": "set_material", "material": name, "filter": ALL}
def uv(tile=1.0): return {"type": "uv_project", "tile_size": tile}

def part(r, kind, size, offset, semantic, material, rot=None, sc=None):
    n = r.node(prim(kind, size))
    steps = []
    if rot: steps.append(rotate(*rot))
    if sc: steps.append(scale(*sc))
    steps += [move(*offset), sem(semantic), mat(material), uv()]
    return r.chain(n, *steps)

def merge(r, parts):
    m = r.node({"type": "merge"})
    for i, p in enumerate(parts): r.link(p, m, i)
    return m

def house():
    r = Recipe("Test House")
    parts = [
        part(r, "plane", [14, 1, 12], [0, 0, 0], "terrain", "grass"),
        part(r, "cube", [6.2, 0.3, 5.2], [0, 0.15, 0], "foundation", "concrete"),
        part(r, "cube", [6, 3, 5], [0, 1.8, 0], "wall_exterior", "brick"),
        # dvostresni krov: kocka zakrenuta 45 oko X, pa spljostena; gornja polovica viri iznad zidova
        part(r, "cube", [6.6, 1, 1], [0, 3.3, 0], "roof", "roof_tiles", rot=[45, 0, 0], sc=[1, 1.5, 4.1]),
        part(r, "cube", [1.0, 2.0, 0.1], [0, 1.3, 2.52], "door", "wood_planks"),
        part(r, "cube", [1.1, 1.0, 0.08], [-1.9, 2.0, 2.52], "window", "glass"),
        part(r, "cube", [1.1, 1.0, 0.08], [1.9, 2.0, 2.52], "window", "glass"),
    ]
    merge(r, parts)
    return r

def chain_posts():
    r = Recipe("Chain Between Posts")
    posts = [part(r, "cylinder", [0.25, 3.2, 0.25], [x, 1.6, 0], "prop", "wood_beam") for x in (-3.1, 3.1)]
    path = r.node({"type": "catenary_curve", "start": [-3, 3, 0], "end": [3, 3, 0], "sag": 1.0, "samples": 96})
    link = r.node(prim("torus", [0.16, 0.16, 0.10], 0.22))
    along = r.node({"type": "copy_along_curve", "spacing": 0.085, "start_offset": 0, "roll_degrees": 0,
                    "alternate_roll_degrees": 90, "reference_up": [0, 1, 0], "max_copies": 10000})
    r.link(link, along, 0); r.link(path, along, 1)
    chain = r.chain(along, sem("chain_link"), mat("steel_chain"))
    rope_path = r.node({"type": "catenary_curve", "start": [-3, 2.2, 0], "end": [3, 2.2, 0], "sag": 0.6, "samples": 48})
    profile = r.node({"type": "circle_profile", "radius": 0.03, "sides": 10})
    sweep = r.node({"type": "sweep", "sample_spacing": 0.1, "reference_up": [0, 1, 0], "cap_ends": True, "max_vertices": 1000000})
    r.link(rope_path, sweep, 0); r.link(profile, sweep, 1)
    rope = r.chain(sweep, sem("rope"), mat("rope_fiber"))
    ground = part(r, "plane", [10, 1, 4], [0, 0, 0], "terrain", "ground_dirt")
    merge(r, posts + [chain, rope, ground])
    return r

def building(name, shape, width, depth, wing, floors, roof_type, pitch=35, wall_material=None):
    """Footprint -> Floor Stack -> Walls/Slab/Roof -> Merge (schema 7, cvorovi srednje razine)."""
    r = Recipe(name)
    fp = r.node({"type": "footprint", "shape": shape, "width": width, "depth": depth, "wing_width": wing,
                 "center": [0, 0], "rotation_degrees": 0})
    st = r.node({"type": "floor_stack", "floors": floors, "floor_height": 3.0, "elevation": 0.4})
    r.link(fp, st)
    walls = r.node({"type": "walls", "thickness": 0.25, "windows": True, "window_width": 1.2, "window_height": 1.4,
                    "sill_height": 0.9, "window_spacing": 3.0, "door": True, "door_edge": 0, "door_width": 1.0,
                    "door_height": 2.2})
    slab = r.node({"type": "slab", "thickness": 0.2, "inset": 0.1, "top_ceiling": True, "foundation": True})
    roof = r.node({"type": "roof", "roof_type": roof_type, "pitch_degrees": pitch, "overhang": 0.4,
                   "thickness": 0.25, "parapet_height": 0.9 if roof_type == "flat" else 0.0})
    for n in (walls, slab, roof): r.link(st, n)
    ground = part(r, "plane", [width + 10, 1, depth + 10], [0, 0, 0], "terrain", "grass")
    m = merge(r, [walls, slab, roof, ground])
    if wall_material:
        r.chain(m, {"type": "set_material", "material": wall_material,
                    "filter": {**ALL, "semantic": "wall_exterior"}})
    return r

def street():
    r = Recipe("Street")
    c = r.node({"type": "curve", "closed": False,
                "points": [[-20, 0, -6], [-6, 0, 0], [6, 0, 0], [20, 0, 8]]})
    r.chain(c, {"type": "curve_smooth", "subdivisions": 8},
            {"type": "road_from_curve", "road_width": 6, "sidewalks": True, "sidewalk_width": 1.8,
             "curb_height": 0.15, "sample_spacing": 1.0})
    return r

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    house().save(os.path.join(here, "kuca.loomrecipe.json"))
    chain_posts().save(os.path.join(here, "lanac.loomrecipe.json"))
    building("L House", "l_shape", 12, 10, 5, 2, "gable", wall_material="brick").save(os.path.join(here, "kuca_l.loomrecipe.json"))
    building("U Villa", "u_shape", 16, 12, 5, 1, "hip").save(os.path.join(here, "vila_u.loomrecipe.json"))
    building("Flat Block", "rectangle", 14, 10, 4, 5, "flat").save(os.path.join(here, "blok.loomrecipe.json"))
    street().save(os.path.join(here, "ulica.loomrecipe.json"))
    print("ok")
