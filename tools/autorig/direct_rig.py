#!/usr/bin/env python3
"""Direct Manny rig: joints from the mesh itself, no UniRig.

WHY. UniRig predicts the skeleton autoregressively; on the HumanoidMascott (clean A-pose robot, fingers
spread) it returned 40 joints (three fingers per hand) for seeds 42, 1 and 7, and Manny conversion needs
all 52. A standing A/T-posed character has its joints where the geometry says: in the middle of each
limb's cross-section, at the narrowings (neck, wrist) and where fingers leave the palm. So the 52 joints
that manny_rig.py expects are measured here directly, the skin is bound per rigid part, and the rest
(Manny's 88 bones, hand rig, validation) is the existing pipeline.

Conventions (Blender after glTF import): Z up, the character faces -Y, its left is +X.
Pure numpy (runs inside Blender's Python); the Blender part is at the bottom.

    blender --background --python direct_rig.py -- input.glb unirig52.glb
"""
from __future__ import annotations

import sys

import numpy as np

UNIRIG_NAMES = [
    "pelvis", "spine_01", "spine_02", "spine_05", "neck_01", "head",
    "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
    "thumb_01_l", "thumb_02_l", "thumb_03_l",
    "index_01_l", "index_02_l", "index_03_l",
    "middle_01_l", "middle_02_l", "middle_03_l",
    "ring_01_l", "ring_02_l", "ring_03_l",
    "pinky_01_l", "pinky_02_l", "pinky_03_l",
    "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
    "thumb_01_r", "thumb_02_r", "thumb_03_r",
    "index_01_r", "index_02_r", "index_03_r",
    "middle_01_r", "middle_02_r", "middle_03_r",
    "ring_01_r", "ring_02_r", "ring_03_r",
    "pinky_01_r", "pinky_02_r", "pinky_03_r",
    "thigh_l", "calf_l", "foot_l", "ball_l",
    "thigh_r", "calf_r", "foot_r", "ball_r",
]
FINGERS = ("thumb", "index", "middle", "ring", "pinky")
#UniRig-52 parent tree (Loom verifies bone_N rigs by it: motionUniRigExpectedParents)
UNIRIG_PARENTS = [-1, 0, 1, 2, 3, 4, 3, 6, 7, 8, 9, 10, 11, 9, 13, 14, 9, 16, 17, 9, 19, 20, 9, 22, 23,
                  3, 25, 26, 27, 28, 29, 30, 28, 32, 33, 28, 35, 36, 28, 38, 39, 28, 41, 42, 0, 44, 45, 46, 0, 48, 49, 50]


def gaps_1d(values: np.ndarray, gap: float):
    """Sorted 1-D values split where neighbours are further apart than gap: list of (lo, hi)."""
    if len(values) == 0:
        return []
    v = np.sort(values)
    cut = np.nonzero(np.diff(v) > gap)[0]
    starts = np.concatenate(([0], cut + 1))
    ends = np.concatenate((cut, [len(v) - 1]))
    return [(v[a], v[b]) for a, b in zip(starts, ends)]


def voxel_components(points: np.ndarray, size: float):
    """Connected components of points on a voxel grid (26-neighbourhood). Label per point."""
    keys = np.floor(points / size).astype(np.int64)
    keys -= keys.min(axis=0)
    dims = keys.max(axis=0) + 1
    flat = (keys[:, 0] * dims[1] + keys[:, 1]) * dims[2] + keys[:, 2]
    cells, inverse = np.unique(flat, return_inverse=True)
    cell_xyz = np.stack(np.unravel_index(cells, dims), axis=1)
    lookup = {int(c): i for i, c in enumerate(cells)}
    parent = np.arange(len(cells))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    offsets = [(dx, dy, dz) for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1) if (dx, dy, dz) > (0, 0, 0)]
    for i, (x, y, z) in enumerate(cell_xyz):
        for dx, dy, dz in offsets:
            nx, ny, nz = x + dx, y + dy, z + dz
            if nx < 0 or ny < 0 or nz < 0 or nx >= dims[0] or ny >= dims[1] or nz >= dims[2]:
                continue
            j = lookup.get(int((nx * dims[1] + ny) * dims[2] + nz))
            if j is not None:
                a, b = find(i), find(j)
                if a != b:
                    parent[a] = b
    roots = np.array([find(i) for i in range(len(cells))])
    _, label = np.unique(roots, return_inverse=True)
    return label[inverse]


class Body:
    """Measurements of a standing character (points in metres or any unit, Z up, facing -Y)."""

    def __init__(self, points: np.ndarray):
        self.p = np.asarray(points, dtype=float)
        self.zmin, self.zmax = self.p[:, 2].min(), self.p[:, 2].max()
        self.h = self.zmax - self.zmin
        self.joints: dict[str, np.ndarray] = {}

    def slab(self, z: float, half: float, mask=None):
        sel = np.abs(self.p[:, 2] - z) < half
        if mask is not None:
            sel &= mask
        return self.p[sel]

    #--- torso and legs --------------------------------------------------------------------------
    def crotch(self) -> float:
        """Lowest height where the middle (x ~ 0) is solid: legs join the pelvis there."""
        centre = np.abs(self.p[:, 0]) < 0.012 * self.h
        z = np.sort(self.p[centre, 2])
        z = z[z > self.zmin + 0.25 * self.h]
        return float(z[min(len(z) - 1, 20)])

    def side_clusters(self, z: float, side: int, half: float):
        """x-clusters of one side of a horizontal slab (innermost first) as point arrays."""
        pts = self.slab(z, half, side * self.p[:, 0] > 0.004 * self.h)
        if len(pts) == 0:
            return []
        spans = gaps_1d(side * pts[:, 0], 0.012 * self.h)
        return [pts[(side * pts[:, 0] >= lo) & (side * pts[:, 0] <= hi)] for lo, hi in spans]

    def centre_of(self, z: float, side: int, half: float, keep=None):
        """Centroid of one side's innermost slab cluster (a leg below the crotch)."""
        clusters = self.side_clusters(z, side, half)
        if keep is not None:
            clusters = [c for c in clusters if keep(c)]
        return clusters[0].mean(axis=0) if clusters else None

    def legs(self, crotch: float):
        #Manny heights (cm above the floor): thigh 93.5, calf 50.3, foot 8.2, ball 0.8. The hip joint
        #sits ~5.5 % of the height above the crotch; the rest follows the measured leg length
        thigh_z = crotch + 0.055 * self.h
        leg = thigh_z - self.zmin
        for side, tag in ((1, "l"), (-1, "r")):
            below = self.centre_of(crotch - 0.03 * self.h, side, 0.006 * self.h)
            self.joints["thigh_" + tag] = np.array([below[0], below[1], thigh_z])
            knee_z = self.zmin + leg * 50.3 / 93.5
            self.joints["calf_" + tag] = self.centre_of(knee_z, side, 0.006 * self.h)
            ankle_z = self.zmin + leg * 8.2 / 93.5
            above = self.centre_of(ankle_z + 0.03 * self.h, side, 0.006 * self.h)
            self.joints["foot_" + tag] = np.array([above[0], above[1], ankle_z])
            foot = self.p[(side * self.p[:, 0] > 0.004 * self.h) & (self.p[:, 2] < ankle_z) &
                          (np.abs(self.p[:, 0] - above[0]) < 0.08 * self.h)]
            toe_y = foot[:, 1].min()          #the character faces -Y
            ball_y = above[1] + 0.71 * (toe_y - above[1])
            near = foot[np.abs(foot[:, 1] - ball_y) < 0.01 * self.h]
            self.joints["ball_" + tag] = np.array([near[:, 0].mean(), ball_y, self.zmin + leg * 0.8 / 93.5])
            self.joints["_toe_" + tag] = np.array([near[:, 0].mean(), toe_y, self.zmin + leg * 0.8 / 93.5])
        return thigh_z

    #--- arms ----------------------------------------------------------------------------------------
    def arm(self, side: int, crotch: float):
        """Shoulder, elbow, wrist from horizontal slabs (A-pose: the arm hangs away from the body).
        Returns the tracked arm levels too (for the hand)."""
        h = self.h
        step, half = 0.0025 * h, 0.003 * h
        #Armpit: going up from the hip, the highest slab where the arm is still its own cluster
        z, armpit, previous = crotch + 0.1 * h, None, None
        while z < self.zmax:
            clusters = self.side_clusters(z, side, half)
            outer = clusters[-1] if len(clusters) >= 2 else None
            if outer is None or len(outer) < 10 or (previous is not None and
                                                     abs(outer[:, 0].mean() - previous) > 0.03 * h):
                if armpit is not None:
                    break
            else:
                armpit, previous = z, outer[:, 0].mean()
            z += step
        #Arm axis just below the armpit (x, y as a line in z)
        levels = []
        for zz in np.arange(armpit - 0.06 * h, armpit - 0.005 * h, step):
            outer = self.side_clusters(zz, side, half)[-1]
            levels.append((zz, outer.mean(axis=0), np.ptp(side * outer[:, 0])))
        zs = np.array([l[0] for l in levels])
        cx = np.polyfit(zs, np.array([l[1][0] for l in levels]), 1)
        width = float(np.median([l[2] for l in levels]))
        cos_tilt = 1.0 / np.sqrt(1.0 + cx[0] ** 2)
        radius = 0.5 * width * cos_tilt
        #Shoulder: one arm radius (x1.2) under the top of the shoulder cap, halfway between the axis
        #line and the outer edge of the shoulder minus a radius
        axis_x_at_pit = np.polyval(cx, armpit)
        cap = self.p[(side * self.p[:, 0] > side * axis_x_at_pit - 0.5 * width) & (self.p[:, 2] > armpit)]
        top = cap[:, 2].max()
        shoulder_z = top - 1.2 * radius
        edge = self.slab(shoulder_z, half, side * self.p[:, 0] > 0.0)
        x_edge = side * (side * edge[:, 0]).max() - side * radius
        x_axis = np.polyval(cx, shoulder_z)
        shoulder = np.array([0.5 * (x_edge + x_axis), np.mean([l[1][1] for l in levels]), shoulder_z])
        #Down the arm: clusters near the predicted arm position (fingers become several clusters)
        track = []
        z, x_prev = armpit, axis_x_at_pit
        while z > self.zmin:
            clusters = [c for c in self.side_clusters(z, side, half)
                        if side * c[:, 0].max() > side * x_prev - 0.06 * h and len(c) >= 3]
            clusters = [c for c in clusters if side * c[:, 0].min() > side * x_prev - 0.08 * h]
            if not clusters:
                break
            pts = np.concatenate(clusters)
            track.append((z, pts.mean(axis=0), np.ptp(pts[:, 0]), len(clusters)))
            x_prev = pts[:, 0].mean()
            z -= step
        #Wrist: the narrowest single-cluster level below mid-arm, before the hand widens
        arm_len = armpit - track[-1][0]
        fingers_from = next((t[0] for t in track if t[3] > 1), track[-1][0])
        candidates = [t for t in track if armpit - 0.4 * arm_len > t[0] > fingers_from and t[3] == 1]
        wrist_level = min(candidates, key=lambda t: t[2])
        wrist = wrist_level[1]
        #Elbow: Manny upper arm 27.8 cm, forearm 27.2 cm -> 50.5 % from shoulder to wrist
        mid_z = shoulder[2] + 0.505 * (wrist[2] - shoulder[2])
        elbow_level = min(track, key=lambda t: abs(t[0] - mid_z))
        elbow = elbow_level[1]
        tag = "l" if side > 0 else "r"
        self.joints["upperarm_" + tag] = shoulder
        self.joints["lowerarm_" + tag] = elbow
        self.joints["hand_" + tag] = wrist
        #Manny clavicle: 7 % of the shoulder's x from the middle, 15 % of it higher
        self.joints["clavicle_" + tag] = np.array([0.07 * shoulder[0], shoulder[1], shoulder[2] + 0.15 * abs(shoulder[0])])
        return track, wrist, elbow

    def hand(self, side: int, wrist: np.ndarray, elbow: np.ndarray):
        """Fingers from the level-set tree of the hand; joints by phalanx proportions."""
        h = self.h
        down = wrist - elbow
        down /= np.linalg.norm(down)
        near = np.linalg.norm(self.p - wrist, axis=1) < 0.18 * h
        hand = self.p[near & ((self.p - wrist) @ down > -0.005 * h)]
        forward = hand.mean(axis=0) - wrist
        forward /= np.linalg.norm(forward)
        fingers = finger_branches(hand, wrist, forward, 0.002 * h, 0.0025 * h)
        if len(fingers) != 5:
            raise ValueError(f"expected 5 fingers on the {'left' if side > 0 else 'right'} hand, found {len(fingers)}")
        #Thumb: first to split off (nearest the wrist); the others ordered from the thumb side
        fingers.sort(key=lambda f: f[1])
        thumb, rest = fingers[0], fingers[1:]
        thumb_centre = thumb[0].mean(axis=0)
        rest.sort(key=lambda f: np.linalg.norm(f[0].mean(axis=0) - thumb_centre))
        tag = "l" if side > 0 else "r"
        for name, (pts, _) in zip(FINGERS, [thumb] + rest):
            centre = pts.mean(axis=0)
            axis = np.linalg.eigh(np.cov((pts - centre).T))[1][:, 2]
            if (centre - wrist) @ axis < 0:
                axis = -axis
            t = (pts - centre) @ axis
            base, length = centre + axis * t.min(), float(np.ptp(t))
            tip = centre + axis * t.max()
            if name == "thumb":
                #free part = 0.3 metacarpal + proximal + distal (1.44 : 1 : 0.78)
                k = [-0.455, 0.195, 0.645]
            else:
                #free part = 0.75 proximal + middle + distal (1 : 0.63 : 0.45)
                k = [-0.137, 0.41, 0.754]
            for i, f in enumerate(k):
                self.joints[f"{name}_0{i + 1}_{tag}"] = base + axis * f * length
            self.joints[f"_{name}_tip_{tag}"] = tip
        return hand

    def torso(self, thigh_z: float):
        """Pelvis, spine, neck, head: Manny heights between the hip and the shoulder."""
        shoulder_z = 0.5 * (self.joints["upperarm_l"][2] + self.joints["upperarm_r"][2])
        pelvis_z = thigh_z + 0.013 * self.h
        span = shoulder_z - pelvis_z
        #Manny: pelvis 95.9, spine_01 99.6, spine_02 106.2, spine_05 141.1, neck_01 152.8, head 162.6,
        #upperarm 143.6 -> ratios of the pelvis-to-shoulder span
        for name, ratio in (("pelvis", 0.0), ("spine_01", 0.078), ("spine_02", 0.216), ("spine_05", 0.948),
                            ("neck_01", 1.19), ("head", 1.395)):
            z = pelvis_z + ratio * span
            middle = self.slab(z, 0.006 * self.h, np.abs(self.p[:, 0]) < 0.05 * self.h)
            y = float(np.median(middle[:, 1])) if len(middle) else 0.0
            self.joints[name] = np.array([0.0, y, z])
        self.joints["_head_top"] = np.array([0.0, self.joints["head"][1], self.zmax])

    def measure(self):
        crotch = self.crotch()
        thigh_z = self.legs(crotch)
        for side in (1, -1):
            _, wrist, elbow = self.arm(side, crotch)
            self.hand(side, wrist, elbow)
        self.torso(thigh_z)
        missing = [n for n in UNIRIG_NAMES if n not in self.joints]
        if missing:
            raise ValueError(f"joints not found: {missing}")
        return self.joints


def finger_branches(hand: np.ndarray, wrist: np.ndarray, forward: np.ndarray, voxel: float,
                    step: float, min_points: int = 40):
    """Fingers as branches of the hand's level-set tree along `forward`.

    Cutting the hand at growing distance tau from the wrist, the palm is one piece and every finger
    splits off as its own piece. A lineage that splits is palm; one that never splits again is a
    finger, and its points at the level where it appeared are the free part of that finger (web to
    tip). Returns (points, level) per finger."""
    u = (hand - wrist) @ forward
    lineages = []          #dict(born, first, current)
    fingers = []
    for tau in np.arange(0.0, u.max(), step):
        ids = np.nonzero(u > tau)[0]
        if len(ids) < min_points:
            break
        label = voxel_components(hand[ids], voxel)
        counts = np.bincount(label)
        pieces = [ids[label == l] for l in np.nonzero(counts >= min_points)[0]]
        if not lineages:
            lineages = [dict(born=tau, first=p, current=p, root=True) for p in pieces]
            continue
        nextLineages = []
        for lineage in lineages:
            members = set(lineage["current"].tolist())
            children = [p for p in pieces if int(p[0]) in members]
            if len(children) == 1:
                lineage["current"] = children[0]
                nextLineages.append(lineage)
            elif len(children) >= 2:
                nextLineages += [dict(born=tau, first=c, current=c, root=False) for c in children]
            elif not lineage["root"]:
                fingers.append((hand[lineage["first"]], lineage["born"]))
        lineages = nextLineages
    fingers += [(hand[l["first"]], l["born"]) for l in lineages if not l["root"]]
    return fingers
#--- bones and skin -------------------------------------------------------------------------------
def bone_tails(joints: dict) -> dict:
    """Tail of each of the 52 bones: the next joint along the chain (tips for the last bones)."""
    tails = {"pelvis": "spine_01", "spine_01": "spine_02", "spine_02": "spine_05", "spine_05": "neck_01",
             "neck_01": "head", "head": "_head_top"}
    for t in "lr":
        tails.update({f"clavicle_{t}": f"upperarm_{t}", f"upperarm_{t}": f"lowerarm_{t}",
                      f"lowerarm_{t}": f"hand_{t}", f"hand_{t}": f"middle_01_{t}",
                      f"thigh_{t}": f"calf_{t}", f"calf_{t}": f"foot_{t}", f"foot_{t}": f"ball_{t}",
                      f"ball_{t}": f"_toe_{t}"})
        for f in FINGERS:
            tails.update({f"{f}_01_{t}": f"{f}_02_{t}", f"{f}_02_{t}": f"{f}_03_{t}", f"{f}_03_{t}": f"_{f}_tip_{t}"})
    return {name: joints[tails[name]] for name in UNIRIG_NAMES}


def segment_distances(points: np.ndarray, heads: np.ndarray, tails: np.ndarray, chunk: int = 50000):
    """Distance of every point to every bone segment, (points, bones)."""
    out = np.empty((len(points), len(heads)), dtype=np.float32)
    d = tails - heads
    dd = np.maximum((d * d).sum(axis=1), 1e-12)
    for a in range(0, len(points), chunk):
        p = points[a:a + chunk, None, :] - heads[None]
        t = np.clip((p * d[None]).sum(axis=2) / dd[None], 0.0, 1.0)
        out[a:a + chunk] = np.linalg.norm(p - t[..., None] * d[None], axis=2)
    return out


def welded_parts(points: np.ndarray, edges: np.ndarray, tolerance: float):
    """Connected parts with coincident vertices merged (UV seams split a part into several islands)."""
    keys = np.round(points / tolerance).astype(np.int64)
    _, same = np.unique(keys, axis=0, return_inverse=True)
    same = same.reshape(-1)
    parent = np.arange(len(points))
    #first vertex of each position is the representative of the others
    first = np.full(same.max() + 1, -1)
    for i, k in enumerate(same):
        if first[k] < 0:
            first[k] = i
    pairs = np.concatenate([edges, np.stack([np.arange(len(points)), first[same]], axis=1)])

    def find(a):
        root = a
        while parent[root] != root:
            root = parent[root]
        while parent[a] != root:
            parent[a], a = root, parent[a]
        return root

    for a, b in pairs:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    roots = np.array([find(i) for i in range(len(points))])
    return np.unique(roots, return_inverse=True)[1].reshape(-1)


def skin_weights(points: np.ndarray, edges: np.ndarray, joints: dict, height: float):
    """Two-bone weights per vertex. A hard-surface part that is small (a finger segment, a plate)
    moves with one bone as a whole; long parts crossing a joint keep a narrow blend.
    Returns (bone index per vertex x2, weight x2)."""
    tails = bone_tails(joints)
    heads = np.array([joints[n] for n in UNIRIG_NAMES])
    tail_array = np.array([tails[n] for n in UNIRIG_NAMES])
    dist = segment_distances(points, heads, tail_array)
    #a vertex on one side never follows the other side's limbs
    side = np.array([1 if n.endswith("_l") else -1 if n.endswith("_r") else 0 for n in UNIRIG_NAMES])
    x = points[:, 0]
    dist[np.ix_(x > 0.01 * height, side < 0)] = np.inf
    dist[np.ix_(x < -0.01 * height, side > 0)] = np.inf
    order = np.argsort(dist, axis=1)[:, :2]
    near = np.take_along_axis(dist, order, axis=1).astype(float)
    #narrow blend: the second bone only counts within ~1 cm of the first one's distance
    falloff = 0.005 * height
    w = np.exp(-(near - near[:, :1]) / falloff)
    w /= w.sum(axis=1, keepdims=True)
    parts = welded_parts(points, edges, 1e-5 * height)
    labels = parts
    sorted_idx = np.argsort(labels, kind="stable")
    bounds = np.flatnonzero(np.diff(labels[sorted_idx])) + 1
    rigid = 0
    for idx in np.split(sorted_idx, bounds):
        if np.ptp(points[idx], axis=0).max() > 0.08 * height:
            continue          #long part (cable, shell over a joint): keep the blend
        votes = np.bincount(order[idx, 0], weights=w[idx, 0], minlength=len(UNIRIG_NAMES))
        winner = int(np.argmax(votes))
        order[idx, 0], order[idx, 1] = winner, winner
        w[idx, 0], w[idx, 1] = 1.0, 0.0
        rigid += 1
    return order, w, rigid


def build(source: str, output: str) -> None:
    """Blender: import the unrigged model, measure joints, bind the skin, export a UniRig-52-like GLB
    (bone_0..bone_51 in UNIRIG_NAMES order) for manny_rig.py."""
    import bpy
    from mathutils import Vector

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=source)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise ValueError("The model has no mesh")
    all_points, all_edges, offsets = [], [], [0]
    for mesh in meshes:
        n = len(mesh.data.vertices)
        co = np.empty(n * 3)
        mesh.data.vertices.foreach_get("co", co)
        m = np.array(mesh.matrix_world)
        all_points.append(co.reshape(-1, 3) @ m[:3, :3].T + m[:3, 3])
        e = np.empty(len(mesh.data.edges) * 2, dtype=np.int64)
        mesh.data.edges.foreach_get("vertices", e)
        all_edges.append(e.reshape(-1, 2) + offsets[-1])
        offsets.append(offsets[-1] + n)
    points = np.concatenate(all_points)
    edges = np.concatenate(all_edges)
    body = Body(points)
    joints = body.measure()
    print("Direct rig: joints " + ", ".join(f"{n} {np.round(joints[n], 3).tolist()}" for n in
                                            ("pelvis", "upperarm_l", "hand_l", "index_01_l", "thigh_l", "foot_l")), flush=True)
    order, weights, rigid = skin_weights(points, edges, joints, body.h)
    print(f"Direct rig: {len(points)} vertices, {rigid} rigid parts", flush=True)
    tails = bone_tails(joints)

    armature = bpy.data.armatures.new("Armature")
    rig = bpy.data.objects.new("Armature", armature)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    for i, name in enumerate(UNIRIG_NAMES):
        bone = armature.edit_bones.new(f"bone_{i}")
        bone.head = Vector(joints[name])
        tail = Vector(tails[name])
        bone.tail = tail if (tail - bone.head).length > 1e-4 else bone.head + Vector((0, 0, 0.01))
    for i, parent in enumerate(UNIRIG_PARENTS):
        if parent >= 0:
            armature.edit_bones[f"bone_{i}"].parent = armature.edit_bones[f"bone_{parent}"]
    bpy.ops.object.mode_set(mode="OBJECT")

    for k, mesh in enumerate(meshes):
        a, b = offsets[k], offsets[k + 1]
        groups = [mesh.vertex_groups.new(name=f"bone_{i}") for i in range(len(UNIRIG_NAMES))]
        for slot in range(2):
            bones = order[a:b, slot]
            ws = weights[a:b, slot]
            for i in np.unique(bones):
                sel = np.nonzero((bones == i) & (ws > 1e-4))[0]
                if not len(sel):
                    continue
                #group.add takes one weight per call: bucket the weights to 1/256
                q = np.round(ws[sel] * 256).astype(int)
                for level in np.unique(q):
                    members = sel[q == level].tolist()
                    groups[i].add(members, level / 256.0, "ADD")
        world = mesh.matrix_world.copy()
        mesh.parent = rig
        mesh.matrix_world = world
        modifier = mesh.modifiers.new("Armature", "ARMATURE")
        modifier.object = rig

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=output, export_format="GLB", use_selection=True,
                              export_animations=False, export_skins=True)
    print(f"Direct rig: 52 joints -> {output}", flush=True)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    if len(argv) != 2:
        raise SystemExit("Usage: blender --background --python direct_rig.py -- input.glb direct52.glb")
    build(argv[0], argv[1])
