import unreal, os, json

SRC = "/home/paul/Downloads/quaternius_nature/glTF"
ROOT = "/Game/Quaternius/StaticMeshes"

gltf_files = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".gltf"))
unreal.log("QuaterniusWire: %d gltf files" % len(gltf_files))

meshes = {}
no_material = []
missing = []
for fn in gltf_files:
    name = os.path.splitext(fn)[0]
    # glTF factory layout: <DST>/<Model>/StaticMeshes/<Model>
    path = "%s/%s/StaticMeshes/%s" % (ROOT, name, name)
    mesh = unreal.load_asset(path)
    if not mesh:
        # fallback: directly under DST (flat layout)
        path = "%s/%s" % (ROOT, name)
        mesh = unreal.load_asset(path)
    if not mesh:
        missing.append(name)
        continue
    mats = mesh.get_editor_property("static_materials")
    meshes[name] = path
    if not mats:
        no_material.append(name)

unreal.log("QuaterniusWire: loaded %d/%d meshes" % (len(meshes), len(gltf_files)))
if missing:
    unreal.log_warning("QuaterniusWire: missing meshes: %s" % ", ".join(missing))
if no_material:
    unreal.log_warning("QuaterniusWire: meshes WITHOUT materials: %s" % ", ".join(sorted(no_material)[:12]))

out = {
    "meshes": meshes,
    "no_material": sorted(no_material),
    "missing": missing,
}
with open("/home/paul/UnrealProjects/EmberwildVale/Scripts/quaternius_meshes.json", "w") as f:
    json.dump(out, f, indent=1, sort_keys=True)
unreal.log("QuaterniusWire: wrote quaternius_meshes.json")
