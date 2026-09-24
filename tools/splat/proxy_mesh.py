#!/usr/bin/env python3
"""PROXY MESH iz gaussian splata: geometrija objekta iz snimke, za zaklanjanje CG-a (holdout).

    tools/splat/proxy_mesh.py model_mapa splat.ply izlaz.glb
        [--box cx cy cz hx hy hz [qw qx qy qz]]   samo unutar kutije (sredina, pola velicine, zakret)
        [--box-matrix m00 m01 ... m33]           isto, kao matrica jedinicne kocke (-0.5..0.5) u sustav
                                                 splata, po stupcima (glm) - tako je salje editor
        [--voxels 256] [--every 2] [--downscale 4] [--keep-largest]

Zapise izlaz.glb (Loom, Blender) i izlaz.obj, u ISTOM sustavu kao splat, cameras/images.txt i
kamera.usda - pa proxy u Blenderu sjedne na snimku kroz istu kameru bez ikakvog poravnavanja.

ZASTO OVAKO. Splat nije ploha nego oblak mekih mrlja, ali iz svake rijesene kamere daje DUBINU.
Dubine iz 229 kamera se spoje u volumen s predznakom udaljenosti (TSDF, kao KinectFusion): svaki
voksel pamti koliko je ispred ili iza plohe koju kamere vide, prosjecno po svim pogledima - pa
jedan krivi pogled ili floater ne napravi plohu, a slobodan prostor koji kamere vide kroz njega
ga izbrise. Ploha je gdje TSDF prelazi nulu; izvlaci se Surface Nets metodom (jedan vrh po
celiji koja sijece plohu, cetverokut po bridu mreze), koja ne treba tablice marching cubesa.

Samo PyTorch i gsplat, bez novih paketa. Voksel na kartici, 256^3 je 16 milijuna i stane lako.
"""
import argparse, json, math, struct, sys
from pathlib import Path

import numpy as np
import torch
import gsplat

sys.path.insert(0, str(Path(__file__).resolve().parent))
from train_splats import read_cameras, read_images, read_points
from clean_splats import read_ply


def load_splat(path, device):
    header, names, raw = read_ply(path)
    column = {name: i for i, name in enumerate(names)}
    tensor = lambda cols: torch.from_numpy(np.ascontiguousarray(raw[:, cols])).to(device)
    means = tensor([0, 1, 2])
    quats = tensor([column[f"rot_{i}"] for i in range(4)])
    scales = torch.exp(tensor([column[f"scale_{i}"] for i in range(3)]))
    opacities = torch.sigmoid(tensor(column["opacity"]))
    colours = tensor([column["f_dc_0"], column["f_dc_1"], column["f_dc_2"]])[:, None, :]
    return means, quats, scales, opacities, colours


def quat_matrix(w, x, y, z):
    n = math.sqrt(w * w + x * x + y * y + z * z)
    w, x, y, z = w / n, x / n, y / n, z / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
                     [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
                     [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])


#-------------------------------------------------------------------------------------------
# Surface Nets
#-------------------------------------------------------------------------------------------

#Osam kutova celije i dvanaest bridova medju njima
CORNERS = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0], [0, 0, 1], [1, 0, 1], [0, 1, 1], [1, 1, 1]])
EDGES = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]


def surface_nets(value, known):
    """value: (N,N,N) TSDF, <0 iza plohe; known: gdje je voksel vidjen. Vraca vrhove (u indeksima
    mreze) i trokute. Celija bez ijednog nevidjenog kuta i s promjenom predznaka dobije vrh na
    prosjeku presjeka svojih bridova; svaki brid mreze s promjenom predznaka postane cetverokut od
    cetiri celije oko njega."""
    n = np.array(value.shape)
    corner = [value[c[0]:n[0] - 1 + c[0], c[1]:n[1] - 1 + c[1], c[2]:n[2] - 1 + c[2]] for c in CORNERS]
    seen = [known[c[0]:n[0] - 1 + c[0], c[1]:n[1] - 1 + c[1], c[2]:n[2] - 1 + c[2]] for c in CORNERS]
    inside = [c < 0 for c in corner]
    allSeen = np.logical_and.reduce(seen)
    anyIn = np.logical_or.reduce(inside)
    allIn = np.logical_and.reduce(inside)
    active = allSeen & anyIn & ~allIn

    cells = np.argwhere(active)
    total = np.zeros((len(cells), 3))
    count = np.zeros(len(cells))
    for a, b in EDGES:
        va = corner[a][active]; vb = corner[b][active]
        crosses = (va < 0) != (vb < 0)
        t = np.where(crosses, va / np.where(crosses, va - vb, 1.0), 0.0)
        point = CORNERS[a][None, :] + t[:, None] * (CORNERS[b] - CORNERS[a])[None, :]
        total += np.where(crosses[:, None], point, 0.0)
        count += crosses
    vertices = cells + total / np.maximum(count, 1)[:, None]

    index = -np.ones(n - 1, dtype=np.int64)
    index[tuple(cells.T)] = np.arange(len(cells))

    faces = []
    #Brid mreze uz os 'axis' izmedju tocke p i p + e: cetiri celije koje ga dijele
    for axis in range(3):
        u, v = (axis + 1) % 3, (axis + 2) % 3
        lo = [slice(0, n[0]), slice(0, n[1]), slice(0, n[2])]
        hi = list(lo)
        lo[axis] = slice(0, n[axis] - 1); hi[axis] = slice(1, n[axis])
        a, b = value[tuple(lo)], value[tuple(hi)]
        both = known[tuple(lo)] & known[tuple(hi)]
        crossing = both & ((a < 0) != (b < 0))
        #Celije oko brida trebaju u, v >= 1 i < n-1
        crossing[tuple(slice(0, 1) if d == u else slice(None) for d in range(3))] = False
        crossing[tuple(slice(0, 1) if d == v else slice(None) for d in range(3))] = False
        crossing[tuple(slice(n[d] - 1, n[d]) if d == u else slice(None) for d in range(3))] = False
        crossing[tuple(slice(n[d] - 1, n[d]) if d == v else slice(None) for d in range(3))] = False
        edges = np.argwhere(crossing)
        if len(edges) == 0:
            continue
        flip = a[tuple(edges.T)] < 0
        quads = []
        for du, dv in ((0, 0), (1, 0), (1, 1), (0, 1)):
            c = edges.copy()
            c[:, u] -= 1 - du
            c[:, v] -= 1 - dv
            quads.append(index[tuple(c.T)])
        quads = np.stack(quads, 1)
        ok = (quads >= 0).all(1)
        quads, flip = quads[ok], flip[ok]
        quads[flip] = quads[flip][:, ::-1]
        faces.append(quads[:, [0, 1, 2]])
        faces.append(quads[:, [0, 2, 3]])
    faces = np.concatenate(faces) if faces else np.zeros((0, 3), dtype=np.int64)
    return vertices, faces


def largest_component(faces, count):
    """Trokuti najvece povezane komponente (union-find po zajednickim vrhovima)."""
    parent = np.arange(count)
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for f in faces:
        ra, rb, rc = find(f[0]), find(f[1]), find(f[2])
        parent[rb] = ra; parent[find(rc)] = ra
    roots = np.array([find(f[0]) for f in faces])
    values, counts = np.unique(roots, return_counts=True)
    return faces[roots == values[counts.argmax()]]


#-------------------------------------------------------------------------------------------
# Izvoz
#-------------------------------------------------------------------------------------------

def taubin(vertices, faces, iterations):
    """Zagladjivanje bez skupljanja (Taubin 1995): korak prema prosjeku susjeda (lambda), pa korak
    natrag malo veci (mu) - sum s kamena i dubine splata ode, a volumen i rubovi ostanu gdje su."""
    edges = np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]])
    edges = np.concatenate([edges, edges[:, ::-1]])
    degree = np.bincount(edges[:, 0], minlength=len(vertices)).astype(float)[:, None]
    v = vertices.copy()
    for _ in range(iterations):
        for factor in (0.5, -0.53):
            mean = np.zeros_like(v)
            np.add.at(mean, edges[:, 0], v[edges[:, 1]])
            v = v + factor * (mean / np.maximum(degree, 1) - v)
    return v


def vertex_normals(vertices, faces):
    normals = np.zeros_like(vertices)
    face = np.cross(vertices[faces[:, 1]] - vertices[faces[:, 0]], vertices[faces[:, 2]] - vertices[faces[:, 0]])
    for k in range(3):
        np.add.at(normals, faces[:, k], face)
    return normals / np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)


def write_obj(path, parts):
    with open(path, "w") as f:
        f.write("# Loom proxy (tools/splat/proxy_mesh.py)\n")
        offset = 1
        for name, vertices, normals, faces in parts:
            f.write(f"o {name}\n")
            for v in vertices: f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
            for n in normals: f.write(f"vn {n[0]:.5f} {n[1]:.5f} {n[2]:.5f}\n")
            for t in faces + offset: f.write(f"f {t[0]}//{t[0]} {t[1]}//{t[1]} {t[2]}//{t[2]}\n")
            offset += len(vertices)


def write_glb(path, parts):
    """parts: (ime, vrhovi, normale, trokuti) - svaki svoj cvor i mesh, da se u editoru ili
    Blenderu nepotreban blocker obrise sam za sebe."""
    pad = lambda b: b + b"\0" * ((4 - len(b) % 4) % 4)
    blob = b""
    views, accessors, meshes, nodes = [], [], [], []
    for name, vertices, normals, faces in parts:
        first = len(accessors)
        for data, target, kind in ((vertices.astype(np.float32), 34962, "VEC3"), (normals.astype(np.float32), 34962, "VEC3"),
                                   (faces.astype(np.uint32).reshape(-1), 34963, "SCALAR")):
            raw = data.tobytes()
            views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(raw), "target": target})
            accessor = {"bufferView": len(views) - 1, "componentType": 5125 if kind == "SCALAR" else 5126,
                        "count": int(len(data)), "type": kind}
            if len(accessors) == first:
                accessor["min"] = vertices.min(0).tolist(); accessor["max"] = vertices.max(0).tolist()
            accessors.append(accessor)
            blob += pad(raw)
        meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": first, "NORMAL": first + 1},
                                                     "indices": first + 2, "material": 0}]})
        nodes.append({"name": name, "mesh": len(meshes) - 1})
    doc = {
        "asset": {"version": "2.0", "generator": "Loom proxy_mesh.py"},
        "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes, "meshes": meshes,
        "materials": [{"name": "Proxy", "pbrMetallicRoughness": {"baseColorFactor": [0.55, 0.62, 0.70, 1.0],
                                                                  "metallicFactor": 0.0, "roughnessFactor": 0.8},
                       "doubleSided": True}],
        "buffers": [{"byteLength": len(blob)}], "bufferViews": views, "accessors": accessors,
    }
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * ((4 - len(text) % 4) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)))
        f.write(struct.pack("<II", len(text), 0x4E4F534A)); f.write(text)
        f.write(struct.pack("<II", len(blob), 0x004E4942)); f.write(blob)


#-------------------------------------------------------------------------------------------
# SCENE BLOCKERI: ciste ravnine i kutije umjesto detaljne plohe
#-------------------------------------------------------------------------------------------
#
# Za zaklanjanje ne treba kamen po kamen nego ZID. Ploha iz TSDF-a nosi sum dubine splata (kamen,
# rubovi), a blocker je pravokutnik ili kutija: ravnine se izvlace RANSAC-om iz vrhova plohe, jedna
# po jedna, i poravnaju s gravitacijom (sustav splata je uspravan: gore je +Y, VideoSolve), pa je
# zid tocno okomit, a pod tocno vodoravan. Rubovi su iz raspona tocaka, ne iz suma na rubu

UP = np.array([0.0, 1.0, 0.0])


def quad(corner, a, b, normal):
    vertices = np.array([corner, corner + a, corner + a + b, corner + b])
    faces = np.array([[0, 1, 2], [0, 2, 3]])
    if np.dot(np.cross(a, b), normal) < 0: faces = faces[:, ::-1]
    return vertices, np.repeat(normal[None], 4, 0), faces


def box_mesh(centre, axes, half):
    parts_v, parts_n, parts_f = [], [], []
    for k in range(3):
        for sign in (-1, 1):
            n = axes[k] * sign
            a = axes[(k + 1) % 3] * 2 * half[(k + 1) % 3]
            b = axes[(k + 2) % 3] * 2 * half[(k + 2) % 3]
            corner = centre + n * half[k] - a / 2 - b / 2
            v, nn, f = quad(corner, a, b, n)
            parts_f.append(f + 4 * len(parts_v)); parts_v.append(v); parts_n.append(nn)
    return np.concatenate(parts_v), np.concatenate(parts_n), np.concatenate(parts_f)


def islands(coords, cell, minimum):
    """Tocke ravnine u 2D razdvojene na otoke po mrezi zauzetih celija (zid iza stupa i zid
    pokraj njega su ista ravnina, ali dva blockera)."""
    keys = np.floor(coords / cell).astype(np.int64)
    cells = {}
    for i, key in enumerate(map(tuple, keys)): cells.setdefault(key, []).append(i)
    seen, out = set(), []
    for start in cells:
        if start in seen: continue
        stack, members = [start], []
        seen.add(start)
        while stack:
            c = stack.pop(); members.extend(cells[c])
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    n = (c[0] + dx, c[1] + dy)
                    if n in cells and n not in seen: seen.add(n); stack.append(n)
        if len(members) >= minimum: out.append(np.array(members))
    return out


def plane_blockers(points, normals, voxel, max_planes=16, seed=0):
    rng = np.random.default_rng(seed)
    remaining = np.arange(len(points))
    parts = []
    counts = {"Floor": 0, "Ceiling": 0, "Wall": 0, "Plane": 0}
    minimum = max(300, len(points) // 200)
    threshold = 2.0 * voxel
    for _ in range(max_planes):
        if len(remaining) < minimum: break
        #Hipoteze se boduju na uzorku (brzo), a clanovi ravnine racunaju na svim tockama
        sample = remaining if len(remaining) <= 20000 else rng.choice(remaining, 20000, replace=False)
        P, N = points[sample], normals[sample]
        best, bestCount = None, 0
        for _ in range(400):
            a, b, c = P[rng.choice(len(P), 3, replace=False)]
            n = np.cross(b - a, c - a); length = np.linalg.norm(n)
            if length < 1e-12: continue
            n /= length
            inl = (np.abs((P - a) @ n) < threshold) & (np.abs(N @ n) > 0.85)
            count = int(inl.sum())
            if count > bestCount: best, bestCount = (n, a), count
        if best is None or bestCount * len(remaining) / len(sample) < minimum: break
        n, a = best
        P, N = points[remaining], normals[remaining]
        inl = (np.abs((P - a) @ n) < threshold) & (np.abs(N @ n) > 0.85)
        centre = P[inl].mean(0)
        n = np.linalg.svd(P[inl] - centre)[2][2]
        #Uspravno ili vodoravno kad je blizu toga (10 st): blocker je cist, a ne nagnut za sum
        if abs(n @ UP) > 0.985: n = UP * np.sign(n @ UP)
        elif abs(n @ UP) < 0.17: n = n - (n @ UP) * UP; n /= np.linalg.norm(n)
        inl = (np.abs((P - centre) @ n) < threshold) & (np.abs(N @ n) > 0.85)
        members = remaining[inl]
        remaining = remaining[~inl]
        centre = points[members].mean(0)
        if abs(n @ UP) > 0.985:
            u = np.linalg.svd(points[members] - centre)[2][0]; u = u - (u @ n) * n; u /= np.linalg.norm(u)
        else:
            u = np.cross(UP, n)
            u = u / np.linalg.norm(u) if np.linalg.norm(u) > 1e-6 else np.linalg.svd(points[members] - centre)[2][0]
        v = np.cross(n, u)
        coords = np.stack([(points[members] - centre) @ u, (points[members] - centre) @ v], 1)
        for island in islands(coords, 6.0 * voxel, minimum // 2):
            lo, hi = np.percentile(coords[island], 1, axis=0), np.percentile(coords[island], 99, axis=0)
            if min(hi - lo) < 4 * voxel: continue
            #Normala prema kamerama: vrhovi plohe su je vec tako okrenuli
            facing = n if (normals[members[island]] @ n).mean() >= 0 else -n
            kind = "Floor" if facing @ UP > 0.985 else "Ceiling" if facing @ UP < -0.985 else "Wall" if abs(facing @ UP) < 0.17 else "Plane"
            counts[kind] += 1
            corner = centre + lo[0] * u + lo[1] * v
            parts.append((f"{kind}_{counts[kind]}",) + quad(corner, (hi[0] - lo[0]) * u, (hi[1] - lo[1]) * v, facing))
    return parts


def box_blocker(points, normals, voxel, seed=0):
    """Uspravna kutija oko tocaka - stup, ormar, auto. Smjer iz NAJVECE OKOMITE RAVNINE unutar
    (RANSAC kao za zidove): prosjek svih normala ne valja, jer na kamenu i rubovima sume na sve
    strane (na C0257 dao je 42 st umjesto 10). Kad ravnine nema, iz rasporeda tocaka u tlocrtu.
    Rubovi iz raspona tocaka (2. i 98. percentil)"""
    rng = np.random.default_rng(seed)
    sample = np.arange(len(points)) if len(points) <= 20000 else rng.choice(len(points), 20000, replace=False)
    P, N = points[sample], normals[sample]
    best, bestCount = None, 0
    for _ in range(600):
        a, b, c = P[rng.choice(len(P), 3, replace=False)]
        n = np.cross(b - a, c - a); length = np.linalg.norm(n)
        if length < 1e-12: continue
        n /= length
        if abs(n @ UP) > 0.17: continue
        count = int(((np.abs((P - a) @ n) < 2.0 * voxel) & (np.abs(N @ n) > 0.85)).sum())
        if count > bestCount: best, bestCount = n, count
    if best is not None and bestCount >= 50:
        horizontal = best - (best @ UP) * UP
        theta = np.arctan2(horizontal[2], horizontal[0])
    else:
        flat = points[:, [0, 2]] - points[:, [0, 2]].mean(0)
        major = np.linalg.svd(flat, full_matrices=False)[2][0]
        theta = np.arctan2(major[1], major[0])
    axes = np.array([[np.cos(theta), 0, np.sin(theta)], UP, [-np.sin(theta), 0, np.cos(theta)]])
    coords = points @ axes.T
    lo, hi = np.percentile(coords, 2, axis=0), np.percentile(coords, 98, axis=0)
    centre = ((lo + hi) / 2) @ axes
    return [("Box",) + box_mesh(centre, axes, (hi - lo) / 2)]


#-------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model"); ap.add_argument("splat"); ap.add_argument("output")
    ap.add_argument("--box", type=float, nargs="+", default=None,
                    help="cx cy cz hx hy hz [qw qx qy qz]: samo unutar kutije, u sustavu splata")
    ap.add_argument("--box-matrix", type=float, nargs=16, default=None,
                    help="jedinicna kocka -> sustav splata, 16 brojeva po stupcima (Loomova kutija za rezanje)")
    ap.add_argument("--voxels", type=int, default=256, help="vokseli po najduljoj strani")
    ap.add_argument("--every", type=int, default=2, help="svaka koja kamera ulazi u TSDF")
    ap.add_argument("--downscale", type=int, default=4, help="dubina se crta na 1/N snimke")
    ap.add_argument("--alpha", type=float, default=0.8, help="piksel s manje pokrivenosti ne daje dubinu")
    ap.add_argument("--keep-largest", action="store_true", help="samo najveca povezana komponenta")
    ap.add_argument("--smooth", type=int, default=8, help="Taubin koraka zagladjivanja (0 = bez)")
    ap.add_argument("--blockers", action="store_true",
                    help="umjesto detaljne plohe ciste ravnine (cijela scena) ili jedna kutija (uz --box/--box-matrix)")
    args = ap.parse_args()
    device = "cuda"
    model = Path(args.model)

    means, quats, scales, opacities, colours = load_splat(args.splat, device)
    camera = read_cameras(model / "cameras.txt")
    frames = read_images(model / "images.txt")[::max(1, args.every)]
    width, height = camera["width"] // args.downscale, camera["height"] // args.downscale
    K = torch.tensor([[camera["fx"] / args.downscale, 0, camera["cx"] / args.downscale],
                      [0, camera["fy"] / args.downscale, camera["cy"] / args.downscale], [0, 0, 1]],
                     device=device, dtype=torch.float32)

    #-- volumen ---------------------------------------------------------------------------
    if args.box_matrix:
        M = np.array(args.box_matrix).reshape(4, 4).T
        lengths = np.linalg.norm(M[:3, :3], axis=0)
        centre = M[:3, 3]; half = 0.5 * lengths; rotation = M[:3, :3] / lengths
    elif args.box:
        centre = np.array(args.box[0:3]); half = np.abs(np.array(args.box[3:6]))
        rotation = quat_matrix(*args.box[6:10]) if len(args.box) >= 10 else np.eye(3)
    else:
        points, _ = read_points(model / "points3D.txt")
        low, high = np.percentile(points, 2, axis=0), np.percentile(points, 98, axis=0)
        centre = (low + high) / 2; half = (high - low) / 2 * 1.1; rotation = np.eye(3)
    voxel = 2 * half.max() / args.voxels
    shape = np.maximum(2, np.ceil(2 * half / voxel).astype(int) + 1)
    trunc = 4.0 * voxel
    print(f"Volumen {shape[0]}x{shape[1]}x{shape[2]}, voksel {voxel:.4f}, kamera {len(frames)} ({width}x{height})")

    axes = [torch.linspace(-half[d], half[d], int(shape[d]), device=device) for d in range(3)]
    local = torch.stack(torch.meshgrid(*axes, indexing="ij"), -1).reshape(-1, 3)
    world = local @ torch.tensor(rotation.T, device=device, dtype=torch.float32) + torch.tensor(centre, device=device, dtype=torch.float32)
    tsdf = torch.zeros(len(world), device=device)
    weight = torch.zeros(len(world), device=device)

    #-- dubine iz kamera u volumen -------------------------------------------------------
    with torch.no_grad():
        for number, (name, view) in enumerate(frames):
            viewmat = torch.from_numpy(view).float().to(device)
            rendered, alpha, _ = gsplat.rasterization(means, quats, scales, opacities, colours, viewmat[None], K[None],
                                                      width, height, sh_degree=0, render_mode="ED", packed=True)
            depth = rendered[0, ..., 0]; cover = alpha[0, ..., 0]
            for chunk in torch.split(torch.arange(len(world), device=device), 1 << 22):
                p = world[chunk] @ viewmat[:3, :3].T + viewmat[:3, 3]
                z = p[:, 2]
                u = torch.round(K[0, 0] * p[:, 0] / z.clamp(min=1e-6) + K[0, 2]).long()
                v = torch.round(K[1, 1] * p[:, 1] / z.clamp(min=1e-6) + K[1, 2]).long()
                inside = (z > 1e-4) & (u >= 0) & (u < width) & (v >= 0) & (v < height)
                d = torch.zeros_like(z); a = torch.zeros_like(z)
                d[inside] = depth[v[inside], u[inside]]; a[inside] = cover[v[inside], u[inside]]
                sdf = d - z
                use = inside & (a > args.alpha) & (d > 0) & (sdf > -trunc)
                value = (sdf / trunc).clamp(-1, 1)
                index = chunk[use]
                tsdf[index] = (tsdf[index] * weight[index] + value[use]) / (weight[index] + 1)
                weight[index] += 1
            if number % 20 == 0:
                print(f"  kamera {number + 1}/{len(frames)}", flush=True)

    value = tsdf.reshape(*shape).cpu().numpy()
    known = (weight.reshape(*shape) >= 2).cpu().numpy()
    vertices, faces = surface_nets(value, known)
    if len(faces) == 0:
        raise SystemExit("Nema plohe u volumenu - kutija je prazna ili nijedna kamera ne vidi unutra")
    if args.keep_largest:
        faces = largest_component(faces, len(vertices))
    used = np.unique(faces)
    remap = -np.ones(len(vertices), dtype=np.int64); remap[used] = np.arange(len(used))
    vertices, faces = vertices[used], remap[faces]

    #Iz indeksa mreze u sustav splata
    spacing = 2 * half / (shape - 1)
    vertices = (-half + vertices * spacing) @ rotation.T + centre
    if args.smooth > 0:
        vertices = taubin(vertices, faces, args.smooth)
    normals = vertex_normals(vertices, faces)
    output = Path(args.output)
    if args.blockers:
        parts = box_blocker(vertices, normals, voxel) if (args.box or args.box_matrix) else plane_blockers(vertices, normals, voxel)
        if not parts:
            raise SystemExit("Nijedan blocker - premalo plohe u volumenu")
        print("Blockeri: " + ", ".join(part[0] for part in parts))
    else:
        parts = [(output.stem, vertices, normals, faces)]
    write_glb(output.with_suffix(".glb"), parts)
    write_obj(output.with_suffix(".obj"), parts)
    print(f"Proxy: {sum(len(p[1]) for p in parts)} vrhova, {sum(len(p[3]) for p in parts)} trokuta, "
          f"{len(parts)} dijelova -> {output.with_suffix('.glb')} i .obj")


if __name__ == "__main__":
    main()
