import unreal, os

SRC = "/home/paul/Downloads/quaternius_nature/glTF"
DST = "/Game/Quaternius/StaticMeshes"

gltf_files = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".gltf"))
unreal.log("QuaterniusImport: %d gltf files" % len(gltf_files))

tasks = []
for fn in gltf_files:
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SRC, fn)
    task.destination_path = DST
    task.destination_name = os.path.splitext(fn)[0]
    task.automated = True
    task.save = True
    task.replace_existing = True
    tasks.append(task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

count = 0
missing_material = []
for fn in gltf_files:
    name = os.path.splitext(fn)[0]
    path = "%s/%s" % (DST, name)
    mesh = unreal.load_asset(path)
    if mesh:
        count += 1
        mats = unreal.EditorStaticMeshLibrary.get_static_mesh_materials(mesh)
        if not mats:
            missing_material.append(name)
    else:
        unreal.log_warning("QuaterniusImport: MISSING mesh %s" % path)

unreal.log("QuaterniusImport: done — %d/%d static meshes imported" % (count, len(gltf_files)))
if missing_material:
    unreal.log_warning("QuaterniusImport: meshes without materials: %s" % ", ".join(missing_material[:10]))
