"""Compile isolated retained-terrain regressions and require real RHI assertion failures.

Production files, shared objects, libraries and binaries remain byte-identical.
Synthetic image inputs exercise the real owner/material/mask/window pipeline.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
from pathlib import Path

MUTANTS = (
    ("old-gray-underlay-still-visible", "renderer/terrainlayeritem.cpp",
     "setDrawSuppressed(ready)", "setDrawSuppressed(false)"),
    ("adopted-reserve-evicted-before-replacement", "renderer/terrainrenderowner.cpp",
     "const auto protectedSet=protectedIds();",
     "const std::set<TerrainDisplayResourceId> protectedSet(input_->baseResources.begin(),input_->baseResources.end());"),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--execute", action="store_true")
    args = parser.parse_args()
    repo, build, output = args.repo.resolve(), args.build.resolve(), args.output.resolve()
    if output.exists() or output == repo or repo in output.parents or output == build or build in output.parents:
        raise RuntimeError("Use a fresh isolated evidence directory outside source and build")
    output.mkdir(parents=True)
    spec = importlib.util.spec_from_file_location("native_mutation_helpers", repo / "tools/native-performance-structure-mutations.py")
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    link = helpers.windows_arguments(helpers.ninja_commands(build, "terrain_render_owner_tests")[-1])
    archive = next(item for item in link if item.replace("\\", "/").endswith("app/libpandoeditor_editor.a"))
    baseline = build / "terrain_render_owner_tests.exe"
    inputs = {str(repo / relative): sha(repo / relative) for _, relative, _, _ in MUTANTS}
    for relative in ("tests/terrain_render_owner_tests.cpp", "tools/native-performance-structure-mutations.py", "tools/terrain-retained-device-mutations.py"):
        inputs[str(repo / relative)] = sha(repo / relative)
    inputs[str(baseline)] = sha(baseline)
    for argument in link:
        if not argument.startswith("-") and argument.endswith((".obj", ".a")):
            path = (build / argument).resolve()
            if path.is_file():
                inputs[str(path)] = sha(path)
    plans = []
    for name, relative, before, after in MUTANTS:
        source = repo / relative
        original = source.read_text(encoding="utf-8")
        if original.count(before) != 1:
            raise RuntimeError(f"Moved/ambiguous mutation anchor: {name}")
        shadow = output / name / "source"
        destination = shadow / relative
        destination.parent.mkdir(parents=True)
        destination.write_text(original.replace(before, after), encoding="utf-8", newline="")
        obj = shadow.parent / "mutant.cpp.obj"
        target = "app/CMakeFiles/pandoeditor_editor.dir/__/" + relative + ".obj"
        compile_command = helpers.isolated_compile(helpers.compiler_command(build, target), destination, obj, shadow)
        mutant_link = list(link)
        mutant_link.insert(mutant_link.index(archive), obj.as_posix())
        executable = shadow.parent / "terrain_render_owner_tests.exe"
        mutant_link[mutant_link.index("-o") + 1] = executable.as_posix()
        for index, argument in enumerate(mutant_link):
            if argument.startswith("-Wl,--out-implib,"):
                mutant_link[index] = "-Wl,--out-implib," + (shadow.parent / "mutant.dll.a").as_posix()
        plans.append({"mutant": name, "source": relative, "mutatedSourceSha256": sha(destination),
                      "cwd": str(build), "commands": [compile_command, mutant_link],
                      "baseline": [str(baseline), "actualPressureSourceMaskAndWindowHandoff", "-o", "-,txt"],
                      "run": [str(executable), "actualPressureSourceMaskAndWindowHandoff", "-o", "-,txt"],
                      "environment": {"PATH": "C:/Users/taeeu/Qt/Tools/mingw1310_64/bin;C:/Users/taeeu/Qt/6.8.3/mingw_64/bin;" + os.environ["PATH"],
                                      "QT_QPA_PLATFORM": "windows", "QT_QUICK_BACKEND": "", "QSG_RHI_BACKEND": "d3d11", "QSG_INFO": "1"}})
    manifest = {"appSourceCommit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
                "webSourceCommit": "ebcfae4d27b29cbbea6416a7045a4806930204be", "sourceDirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=repo, text=True).strip()),
                "inputs": inputs, "plans": plans, "results": [], "complete": False}
    report = output / "results.json"
    report.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if not args.execute:
        print(f"Two isolated mutation plans: {report}")
        return
    for plan in plans:
        folder = output / plan["mutant"]
        positive = helpers.process_evidence(plan["baseline"], plan, folder / "baseline.txt")
        if positive["exit"] != 0 or positive["counts"] != {"passed": 3, "failed": 0, "skipped": 0}:
            raise RuntimeError(f"Baseline did not actually pass: {positive}")
        environment = os.environ.copy()
        environment.update(plan["environment"])
        for index, command in enumerate(plan["commands"]):
            with (folder / f"build-{index}.txt").open("w", encoding="utf-8") as log:
                process = subprocess.run(command, cwd=build, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=180)
            if process.returncode:
                raise RuntimeError(f"Compile/link failure is not a caught mutant: {plan['mutant']}")
        negative = helpers.process_evidence(plan["run"], plan, folder / "negative.txt")
        raw = Path(negative["qtLog"]).read_text(encoding="utf-8")
        caught = negative["exit"] == 1 and negative["counts"] == {"passed": 2, "failed": 1, "skipped": 0} and "FAIL!  : TerrainRenderOwnerTests::actualPressureSourceMaskAndWindowHandoff()" in raw
        manifest["results"].append({"mutant": plan["mutant"], "baseline": positive, "negative": negative, "caught": caught})
        report.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        if not caught:
            raise RuntimeError(f"Required actual RHI assertion did not catch mutant: {negative}")
        for path, expected in inputs.items():
            if sha(Path(path)) != expected:
                raise RuntimeError(f"Shared source/build input changed: {path}")
    manifest["complete"] = True
    report.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Actual retained-terrain device mutants: expected=2 processed=2 caught=2 fail=0 skip=0")


if __name__ == "__main__":
    main()
