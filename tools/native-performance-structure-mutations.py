"""Emit reviewable negative-test patches; never modify production files or build.

Run with --output <evidence-directory> to write one .patch per mutant plus a
source-hash manifest. Apply/build/run/restore is explicitly owned by the native
test executor. A moved source anchor is an error, not a best-effort replacement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import difflib
import ctypes
import datetime
import os
import re
import subprocess
import time


MUTANTS = (
    ("worker-seconds-as-ms", "app/commandjobrunner.cpp",
     "double(clock.nsecsElapsed())/1e6", "double(clock.nsecsElapsed())/1e9", 2,
     "acceptedWorkerDurationUsesActualMillisecondsAndOwnerSignal"),
    ("worker-cancel-duration-omitted", "app/commandjobrunner.cpp",
     "if(milliseconds)emit operationMeasured",
     "if(milliseconds&&disposition!=JobDisposition::Cancelled)emit operationMeasured", 1,
     "runningCancellationMeasuresOnlyAfterActualWorkerCompletion"),
    ("unobserved-duration-as-zero", "app/nativeperformancemetrics.h",
     'at==stages_.end()?QVariant{}:QVariant(at->second.latest)',
     'at==stages_.end()?QVariant(0.0):QVariant(at->second.latest)', 2,
     "metricsFollowControllerOwnerLifetimeAndUnobservedRemainNull"),
    ("cumulative-total-as-latest", "app/nativeperformancemetrics.h",
     "state.total+=milliseconds", "state.total=milliseconds", 1,
     "realWorkerEventsKeepCumulativeTotalsAndBoundedHistory"),
    ("event-drop-unreported", "app/nativeperformancemetrics.h",
     "events_.removeFirst();++dropped_", "events_.removeFirst();", 1,
     "realWorkerEventsKeepCumulativeTotalsAndBoundedHistory"),
    ("camera-clones-scene", "app/mapscenebridge.cpp",
     "FramePipeline::compose(previous->scene,next,previous,cullingEnabled_)",
     "FramePipeline::compose(previous->scene?std::make_shared<RenderScene>(*previous->scene):previous->scene,next,previous,cullingEnabled_)", 1,
     "cameraChangeRetainsActualSceneAndGeometry"),
)


def windows_arguments(command):
    # Parse the actual Ninja command with Windows quoting, not shell interpolation.
    if re.search(r"\bcmd\.exe\s+/C\b", command, re.IGNORECASE):
        command = command.split("&&", 1)[1].rsplit("&&", 1)[0].strip()
    if "&&" in command:
        raise RuntimeError("Unexpected compound compiler command")
    count = ctypes.c_int()
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    arguments = parse(command, ctypes.byref(count))
    if not arguments:
        raise RuntimeError("Windows command-line parsing failed")
    try:
        return [arguments[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(arguments, ctypes.c_void_p))


def ninja_commands(build, target):
    # Cache comments may retain Windows ACP text from the original Qt setup;
    # the configured executable line itself must remain unambiguous.
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^CMAKE_MAKE_PROGRAM:[^=]+=(.+)$", cache, re.MULTILINE)
    if not match or "\ufffd" in match.group(1):
        raise RuntimeError("Configured Ninja executable is unavailable")
    result = subprocess.run([match.group(1).strip(), "-t", "commands", target], cwd=build,
                            text=True, capture_output=True, check=True)
    return [line for line in result.stdout.splitlines() if "g++.exe" in line]


def compiler_command(build, object_target):
    rows = ninja_commands(build, object_target)
    if not rows or " -c " not in rows[-1]:
        raise RuntimeError(f"No actual configured MinGW compile command for {object_target}")
    return windows_arguments(rows[-1])


def isolated_compile(arguments, source, object_path, shadow):
    result = [arguments[0]] + ["-I" + (shadow / folder).as_posix()
                               for folder in ("app", "renderer", "platform")]
    i = 1
    while i < len(arguments):
        arg = arguments[i]
        if arg == "-MD":
            i += 1
        elif arg in ("-MT", "-MF"):
            i += 2
        elif arg in ("-o", "-c"):
            result += [arg, (object_path if arg == "-o" else source).as_posix()]
            i += 2
        else:
            result.append(arg)
            i += 1
    return result


def build_executor_plans(args, rows):
    build = args.build.resolve()
    output = args.output.resolve()
    repo = args.repo.resolve()
    if output == repo or repo in output.parents or output == build or build in output.parents:
        raise RuntimeError("Isolated output must be outside the repository and shared build")
    link_rows = ninja_commands(build, "native_performance_structure_tests")
    link = windows_arguments(link_rows[-1])
    if "-o" not in link or "-c" in link:
        raise RuntimeError("Actual native structure executable link command is unavailable")
    baseline = (build / link[link.index("-o") + 1]).resolve()
    test_target = "app/CMakeFiles/native_performance_structure_tests.dir/__/tests/native_performance_structure_tests.cpp.obj"
    test_compile = compiler_command(build, test_target)
    test_object = str(Path(test_target)).replace("\\", "/")
    editor_archive = next(arg for arg in link if arg.replace("\\", "/").endswith("app/libpandoeditor_editor.a"))
    qt_library = next(arg for arg in link if arg.replace("\\", "/").endswith("/lib/libQt6Core.a"))
    qt_root = Path(qt_library).parent.parent
    environment = {"QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software",
                   "QT_PLUGIN_PATH": str(qt_root / "plugins"),
                   "PATH": str(qt_root / "bin") + os.pathsep + str(Path(link[0]).parent) + os.pathsep + os.environ.get("PATH", "")}
    plans = []
    for row in rows:
        shadow = output / row["mutant"] / "source"
        shadow.mkdir(parents=True, exist_ok=True)
        for directory in ("app", "renderer", "platform"):
            for header in (repo / directory).rglob("*.h"):
                destination = shadow / header.relative_to(repo)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(header.read_bytes())
        # The current structure test also includes local test-only headers.
        # Compile the actual copied source with its exact adjacent dependencies.
        for header in (repo / "tests").glob("*.h"):
            destination = shadow / header.relative_to(repo)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(header.read_bytes())
        source = repo / row["source"]
        old, new = next((item[2], item[3]) for item in MUTANTS if item[0] == row["mutant"])
        destination = shadow / row["source"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(source.read_text(encoding="utf-8").replace(old, new), encoding="utf-8", newline="")
        commands = []
        overrides = []
        mutant_link = list(link)
        if source.suffix == ".h":
            # Compile both the real test and real controller against copied
            # headers, avoiding an old inline metrics implementation from the
            # existing controller archive. No ABI/layout is changed by mutants.
            test_source = shadow / "tests/native_performance_structure_tests.cpp"
            test_source.parent.mkdir(parents=True, exist_ok=True)
            test_source.write_bytes((repo / "tests/native_performance_structure_tests.cpp").read_bytes())
            test_output = shadow.parent / "test.cpp.obj"
            commands.append(isolated_compile(test_compile, test_source, test_output, shadow))
            index = next(i for i, arg in enumerate(mutant_link) if arg.replace("\\", "/") == test_object)
            mutant_link[index] = test_output.as_posix()
            implementation = "editorcontroller.cpp"
        else:
            implementation = source.name
        implementation_source = shadow / "app" / implementation
        if source.suffix == ".h":
            implementation_source.write_bytes((repo / "app" / implementation).read_bytes())
        implementation_output = shadow.parent / (implementation + ".obj")
        actual_compile = compiler_command(build, "app/CMakeFiles/pandoeditor_editor.dir/" + implementation + ".obj")
        commands.append(isolated_compile(actual_compile, implementation_source, implementation_output, shadow))
        overrides.append(implementation_output.as_posix())
        mutant_link[mutant_link.index(editor_archive):mutant_link.index(editor_archive)] = overrides
        executable = shadow.parent / "native_performance_structure_tests.exe"
        mutant_link[mutant_link.index("-o") + 1] = executable.as_posix()
        for i, argument in enumerate(mutant_link):
            if argument.startswith("-Wl,--out-implib,"):
                mutant_link[i] = "-Wl,--out-implib," + (shadow.parent / "mutant.dll.a").as_posix()
        commands.append(mutant_link)
        plans.append({**row, "cwd": str(build), "commands": commands,
                      "baseline": [str(baseline), row["focusedQtRow"], "-o", "-,txt"],
                      "run": [str(executable), row["focusedQtRow"], "-o", "-,txt"],
                      "mutatedSourceSha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
                      "environment": environment})
    (output / "executor-plans.json").write_text(json.dumps(plans, indent=2) + "\n", encoding="utf-8")
    return plans


def process_evidence(arguments, plan, log):
    environment = os.environ.copy()
    environment.update(plan["environment"])
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    clock = time.monotonic()
    arguments = list(arguments)
    qt_log = None
    if len(arguments) >= 2 and arguments[-2:] == ["-o", "-,txt"]:
        qt_log = log.with_suffix(".qt.txt")
        if qt_log.exists():
            raise RuntimeError(f"Existing Qt execution evidence preserved: {qt_log}")
        arguments[-1] = qt_log.as_posix() + ",txt"
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(arguments, cwd=plan["cwd"], env=environment,
                                   stdout=output, stderr=subprocess.STDOUT, text=True)
        try:
            code = process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError(f"Timed out; not valid negative-test evidence: {log}")
    text = (qt_log if qt_log else log).read_text(encoding="utf-8", errors="replace")
    totals = re.search(r"Totals:\s+(\d+) passed, (\d+) failed, (\d+) skipped", text)
    return {"pid": process.pid, "exit": code, "startedUtc": started,
            "elapsedSeconds": time.monotonic() - clock, "log": str(log), "qtLog": str(qt_log) if qt_log else None,
            "counts": {"passed": int(totals[1]), "failed": int(totals[2]), "skipped": int(totals[3])} if totals else None}


def execute_plans(args, plans):
    results = []
    for plan in plans:
        folder = args.output / plan["mutant"]
        build = Path(plan["cwd"]).resolve()
        inputs = {}
        for argument in plan["commands"][-1]:
            if argument.startswith("-") or not argument.endswith((".obj", ".a")):
                continue
            path = (build / argument).resolve()
            if build in path.parents and path.is_file():
                inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        baseline = process_evidence(plan["baseline"], plan, folder / "baseline.log")
        if (baseline["exit"] != 0 or not baseline["counts"] or baseline["counts"]["failed"]
                or baseline["counts"]["skipped"] or baseline["counts"]["passed"] < 3):
            raise RuntimeError(f"Baseline does not pass; mutant execution withheld: {baseline}")
        stages = []
        for index, command in enumerate(plan["commands"]):
            evidence = process_evidence(command, plan, folder / f"compile-link-{index}.log")
            stages.append(evidence)
            if evidence["exit"]:
                raise RuntimeError(f"Compilation/link failure is not a caught regression: {evidence}")
        negative = process_evidence(plan["run"], plan, folder / "negative.log")
        text = Path(negative["qtLog"] or negative["log"]).read_text(encoding="utf-8", errors="replace")
        caught = (negative["exit"] != 0 and negative["counts"] and negative["counts"]["failed"] > 0
                  and negative["counts"]["skipped"] == 0
                  and re.search(r"FAIL!\s*:\s*NativePerformanceStructureTests::" + re.escape(plan["focusedQtRow"]) + r"\(", text)
                  and "Caught unhandled exception" not in text and "Received signal" not in text)
        source = args.repo / plan["source"]
        if hashlib.sha256(source.read_bytes()).hexdigest() != plan["sha256"]:
            raise RuntimeError("Working source changed during isolated execution; evidence withheld")
        for path, digest in inputs.items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest:
                raise RuntimeError("Shared link input changed during isolated execution; evidence withheld")
        results.append({"mutant": plan["mutant"], "sourceSha256": plan["sha256"],
                        "mutatedSourceSha256": plan["mutatedSourceSha256"], "baseline": baseline,
                        "compileLink": stages, "negative": negative, "caughtRuntimeRegression": bool(caught),
                        "existingLinkInputsSha256": inputs,
                        "baselineExeSha256": hashlib.sha256(Path(plan["baseline"][0]).read_bytes()).hexdigest(),
                        "mutantExeSha256": hashlib.sha256(Path(plan["run"][0]).read_bytes()).hexdigest()})
        (args.output / "execution-results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
        if not caught:
            raise RuntimeError(f"Mutant was not caught by an actual assertion: {negative}")
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, help="Write patches only; production source remains unchanged")
    parser.add_argument("--build", type=Path, help="Read configured Ninja commands and prepare isolated executor plans")
    parser.add_argument("--mutant", action="append", choices=[item[0] for item in MUTANTS])
    parser.add_argument("--execute", action="store_true", help="ROOT executor only: compile copied files and run baseline/negative Qt rows")
    args = parser.parse_args()
    if (args.build or args.execute) and not args.output:
        parser.error("Executor preparation requires an explicit isolated --output directory")
    if args.execute and not args.build:
        parser.error("Explicit --build is required for isolated execution")
    if args.output:
        output = args.output.resolve()
        repo = args.repo.resolve()
        if output == repo or repo in output.parents:
            parser.error("Isolated evidence output must be outside the working repository")
        if args.build:
            build = args.build.resolve()
            if output == build or build in output.parents:
                parser.error("Isolated evidence output must be outside the shared build")
    rows = []
    patches = []
    for name, relative, old, new, occurrences, row in MUTANTS:
        if args.mutant and name not in args.mutant:
            continue
        path = args.repo / relative
        raw = path.read_bytes()
        source = raw.decode("utf-8")
        count = source.count(old)
        if count != occurrences:
            raise SystemExit(f"{name}: expected {occurrences} exact anchors in {relative}, observed {count}")
        mutation = source.replace(old, new)
        patch = "".join(difflib.unified_diff(source.splitlines(keepends=True), mutation.splitlines(keepends=True),
                                            fromfile="a/" + relative, tofile="b/" + relative))
        patches.append((name, patch))
        rows.append({"mutant": name, "source": relative, "sha256": hashlib.sha256(raw).hexdigest(),
                     "focusedQtRow": row, "execution": "not executed by this generator"})
    if args.output:
        if args.output.exists() and any(args.output.iterdir()):
            raise RuntimeError("Use a new empty output directory; existing evidence is never overwritten")
        args.output.mkdir(parents=True, exist_ok=True)
        for name, patch in patches:
            (args.output / (name + ".patch")).write_text(patch, encoding="utf-8", newline="")
        (args.output / "manifest.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    if args.build:
        plans = build_executor_plans(args, rows)
        if args.execute:
            print(json.dumps(execute_plans(args, plans), indent=2))
            return
    print(json.dumps(rows, indent=2))


if __name__ == "__main__":
    main()
