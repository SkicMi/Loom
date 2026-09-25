"""Loom -> Blender: rijesena kamera, snimka i scene blockeri spremni za slaganje CG-a u snimku.

    blender --python tools/blender/loom_holdout.py -- MAPA_REZULTATA [BLOCKERI.glb] [--light SVJETLO.json] [--save scena.blend]

MAPA_REZULTATA je izlaz VideoSolvea (kamera.usda, izvor.txt). BLOCKERI.glb je izlaz editorovog
"Make Scene Blockers" / "Make Blocker Box" ili tools/splat/proxy_mesh.py --blockers - u istom
sustavu kao kamera.usda, pa nista ne treba poravnavati.

Sto napravi:
  - kamera iz kamera.usda, animirana po kadru, sa zarisnom i senzorom u milimetrima
  - snimka (iz izvor.txt) kao pozadina kamere u pogledu i kao podloga u kompozitoru
  - blockeri: zidovi, kutije i ostalo kao HOLDOUT (lik iza njih se ne crta, pa kroz rupu
    vidi snimka), a pod (Floor_*) kao SHADOW CATCHER (lik na njega baca sjenu)
  - Cycles, 3840x2160, prozirna pozadina; kompozitor slozi render preko snimke, a za Nuke
    ostaje i sam render s alfom

--light: svjetlo koje je tools/splat/relight.py procijenio iz snimke - sunce (smjer i boja) i
jednolicna okolina, u jedinicama u kojima 1.0 znaci bijelo na snimci (Standard prikaz, ne AgX/Filmic).
Lik je tada osvijetljen onako kako je snimka.

Onda: uvezi lik, postavi ga iza auta (blockera), i renderaj. Mjerilo solvea je slobodno - scena
nije u metrima - pa lik treba skalirati prema necemu poznatom u snimci.
"""
import json
import re
import sys
from pathlib import Path

import bpy


def arguments():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    save = None
    light = None
    if "--save" in argv:
        i = argv.index("--save"); save = argv[i + 1]; del argv[i:i + 2]
    if "--light" in argv:
        i = argv.index("--light"); light = Path(argv[i + 1]).resolve(); del argv[i:i + 2]
    if not argv:
        raise SystemExit("Upotreba: blender --python loom_holdout.py -- MAPA_REZULTATA [BLOCKERI.glb] [--save x.blend]")
    result = Path(argv[0]).resolve()
    blockers = Path(argv[1]).resolve() if len(argv) > 1 else None
    return result, blockers, light, Path(save).resolve() if save else result / "loom_holdout.blend"


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def read_usd_camera(usd):
    """kamera.usda iz VideoSolvea je tekst: zarisna, otvor senzora i matrica po timeCodeu. Cita se
    izravno jer Blender iz Ubuntuovog paketa nema USD (bpy.ops.wm.usd_import ne postoji), a i
    kad ga ima, ovako ne ovisi o tome kako uvoznik slaze osi."""
    text = usd.read_text()
    number = lambda key: float(re.search(rf"float {key} = ([-0-9.e+]+)", text).group(1))
    lens, aperture_x, aperture_y = number("focalLength"), number("horizontalAperture"), number("verticalAperture")
    fps = float(re.search(r"framesPerSecond = ([0-9.]+)", text).group(1))
    block = text[text.index("xformOp:transform.timeSamples"):]
    block = block[:block.index("}")]
    samples = {}
    for line in block.splitlines():
        match = re.match(r"\s*(\d+): \((.*)\),?\s*$", line)
        if not match:
            continue
        values = [float(v) for v in re.findall(r"[-0-9.e+]+", match.group(2))]
        samples[int(match.group(1))] = values
    return lens, aperture_x, aperture_y, fps, samples


def import_camera(result):
    from mathutils import Matrix
    usd = result / "kamera.usda"
    if not usd.exists():
        raise SystemExit(f"Nema {usd} - VideoSolve ga pise uz rezultat")
    lens, aperture_x, aperture_y, fps, samples = read_usd_camera(usd)
    if not samples:
        raise SystemExit("U kamera.usda nema animirane kamere")
    data = bpy.data.cameras.new("Loom_kamera")
    data.lens = lens
    data.sensor_fit = "HORIZONTAL"
    data.sensor_width = aperture_x
    data.sensor_height = aperture_y
    data.clip_start = 0.001
    data.clip_end = 100000.0
    camera = bpy.data.objects.new("Loom_kamera", data)
    bpy.context.scene.collection.objects.link(camera)
    bpy.context.scene.camera = camera

    #USD pise retke (tocka * M), Blender stupce; svijet solvea je Y gore, Blenderov Z gore - isti
    #zakret (x, y, z) -> (x, -z, y) kao glTF uvoznik za blockere. Kamera i u USD-u i u Blenderu
    #gleda po -Z s Y gore, pa se sama kamera ne okrece
    upright = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    frames = sorted(samples)
    locations, rotations = [], []
    previous = None
    for frame in frames:
        v = samples[frame]
        world = upright @ Matrix([v[0:4], v[4:8], v[8:12], v[12:16]]).transposed()
        location, rotation, _ = world.decompose()
        if previous is not None and previous.dot(rotation) < 0:
            rotation.negate()              #ista orijentacija, bez skoka u interpolaciji
        previous = rotation
        locations.append(location); rotations.append(rotation)

    #Tisuce kljuceva: izravno u krivulje, keyframe_insert po kadru traje minutama
    camera.rotation_mode = "QUATERNION"
    camera.animation_data_create()
    action = bpy.data.actions.new("Loom_kamera")
    camera.animation_data.action = action
    for path, count, values in (("location", 3, locations), ("rotation_quaternion", 4, rotations)):
        for index in range(count):
            curve = action.fcurves.new(path, index=index)
            curve.keyframe_points.add(len(frames))
            flat = []
            for frame, value in zip(frames, values):
                flat += [float(frame), value[index]]
            curve.keyframe_points.foreach_set("co", flat)
            for point in curve.keyframe_points:
                point.interpolation = "LINEAR"
            curve.update()

    scene = bpy.context.scene
    scene.frame_start, scene.frame_end = frames[0], frames[-1]
    scene.render.fps = int(round(fps))
    return camera


def plate_path(result):
    source = result / "izvor.txt"
    if source.exists():
        plate = Path(source.read_text().strip())
        if plate.exists():
            return plate
    return None


def setup_plate(camera, plate):
    scene = bpy.context.scene
    clip = bpy.data.movieclips.load(str(plate))
    #Kadar snimke k je USD timeCode k + 1 (VideoSolve), a timeline pocinje od 1 - pa isti pomak
    clip.frame_start = 1
    camera.data.show_background_images = True
    background = camera.data.background_images.new()
    background.source = "MOVIE_CLIP"
    background.clip = clip
    background.alpha = 1.0
    background.display_depth = "BACK"
    return clip


def setup_render(clip):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    #Ubuntuov Blender je izgradjen bez OpenImageDenoisea, a Cycles ga zadano trazi - render tada
    #padne s "Build without OpenImageDenoiser". Bez njega se renderira bez odsumljivanja
    import _cycles
    if not getattr(_cycles, "with_openimagedenoise", True):
        scene.cycles.use_denoising = False
        scene.cycles.use_preview_denoising = False
    scene.render.film_transparent = True
    scene.render.resolution_x = clip.size[0] if clip else 3840
    scene.render.resolution_y = clip.size[1] if clip else 2160
    scene.render.resolution_percentage = 100

    #Kompozitor: render (s holdout rupama i sjenom na podu) preko snimke
    scene.use_nodes = True
    tree = scene.node_tree
    tree.nodes.clear()
    layers = tree.nodes.new("CompositorNodeRLayers"); layers.location = (0, 200)
    out = tree.nodes.new("CompositorNodeComposite"); out.location = (600, 200)
    if clip:
        plate = tree.nodes.new("CompositorNodeMovieClip"); plate.clip = clip; plate.location = (0, -100)
        over = tree.nodes.new("CompositorNodeAlphaOver"); over.location = (300, 200)
        tree.links.new(plate.outputs["Image"], over.inputs[1])
        tree.links.new(layers.outputs["Image"], over.inputs[2])
        tree.links.new(over.outputs["Image"], out.inputs["Image"])
    else:
        tree.links.new(layers.outputs["Image"], out.inputs["Image"])
    viewer = tree.nodes.new("CompositorNodeViewer"); viewer.location = (600, -50)
    tree.links.new(layers.outputs["Image"], viewer.inputs["Image"])


def import_blockers(blockers):
    if not blockers:
        return []
    if not blockers.exists():
        raise SystemExit(f"Nema {blockers}")
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(blockers))
    added = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    collection = bpy.data.collections.new("Loom_blockeri")
    bpy.context.scene.collection.children.link(collection)
    for obj in added:
        for other in list(obj.users_collection):
            other.objects.unlink(obj)
        collection.objects.link(obj)
        if obj.name.startswith("Floor"):
            #Pod hvata sjenu lika, a sam se ne vidi
            obj.is_shadow_catcher = True
        else:
            #Sve ostalo zaklanja: lik iza toga se ne crta, pa kroz rupu vidi snimka
            obj.is_holdout = True
        obj.display_type = "WIRE"
    return added


def setup_light(path):
    """Sunce i okolina iz relight.py. Svijet solvea je Y gore; USD i glTF uvoznik ga oboje prevedu
    u Blenderov Z gore istim zakretom (x, y, z) -> (x, -z, y), pa i smjer sunca ide kroz njega."""
    from mathutils import Vector
    light = json.loads(path.read_text())
    x, y, z = light["sunce_smjer_prema_svjetlu"]
    towards = Vector((x, -z, y)).normalized()
    sun = light["sunce_boja"]
    strength = max(sun)
    data = bpy.data.lights.new("Loom_sunce", type="SUN")
    data.energy = strength                    #W/m^2 = irradijancija okomito na sunce, kao E u relight.py
    data.color = [c / strength for c in sun] if strength > 0 else (1, 1, 1)
    data.angle = 0.05
    obj = bpy.data.objects.new("Loom_sunce", data)
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = towards.to_track_quat("Z", "Y")   #svjetlo ide po -Z objekta, od sunca
    bpy.context.scene.collection.objects.link(obj)

    #Nebo s gradijentom gore-dolje (relight.py: radijancija linearno od "dolje" do "gore" po smjeru).
    #Smjer iz Generated koordinata svijeta; Z je Blenderov gore
    world = bpy.data.worlds.new("Loom_okolina")
    world.use_nodes = True
    nodes, links = world.node_tree.nodes, world.node_tree.links
    background = nodes["Background"]
    top = light.get("nebo_gore", light["okolina_boja"])
    bottom = light.get("nebo_dolje", light["okolina_boja"])
    coordinates = nodes.new("ShaderNodeTexCoord")
    separate = nodes.new("ShaderNodeSeparateXYZ")
    remap = nodes.new("ShaderNodeMapRange")
    remap.inputs["From Min"].default_value = -1.0
    remap.inputs["From Max"].default_value = 1.0
    mix = nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.inputs[6].default_value = (*bottom, 1.0)      #A
    mix.inputs[7].default_value = (*top, 1.0)         #B
    links.new(coordinates.outputs["Generated"], separate.inputs["Vector"])
    links.new(separate.outputs["Z"], remap.inputs["Value"])
    links.new(remap.outputs["Result"], mix.inputs["Factor"])
    links.new(mix.outputs[2], background.inputs["Color"])
    background.inputs["Strength"].default_value = 1.0
    bpy.context.scene.world = world
    #Jedinice relight.py: 1.0 je bijelo na snimci, bez tonske krivulje
    bpy.context.scene.view_settings.view_transform = "Standard"
    return obj


def main():
    result, blockers, light, save = arguments()
    clear_scene()
    camera = import_camera(result)
    plate = plate_path(result)
    clip = setup_plate(camera, plate) if plate else None
    setup_render(clip)
    added = import_blockers(blockers)
    sun = setup_light(light) if light else None
    bpy.ops.wm.save_as_mainfile(filepath=str(save))
    scene = bpy.context.scene
    print(f"Loom: kamera {scene.frame_start}-{scene.frame_end} @ {scene.render.fps} fps, "
          f"snimka {'da' if clip else 'NE'}, blockera {len(added)}, svjetlo {'da' if sun else 'ne'} -> {save}")


if __name__ == "__main__":
    main()
