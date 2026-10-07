"""Build/run isolated native regression mutations when invoked by the gate owner.

Only copied headers are mutated. The actual 20-case C++ test source is copied
byte-for-byte; no assertion, expectation, or production source is rewritten.
Every invocation creates a new external evidence directory and deletes nothing.
Process receipts prove local subprocess execution, not trusted remote attestation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
import uuid


def utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_record(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(data), "sha256": digest(data)}


def save_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


MUTATIONS = (
    {
        "name": "displayed-only-fallback",
        "regression": "Ignore submitted resident prefetch that was never displayed.",
        "case": "submittedPrefetchSuppliesFallbackAfterPan",
        "message": "submitted cached prefetch is fallback only over overlapping missing targets; unrelated tiles are excluded",
        "before": """        for(const auto& entry:uploaded_) {
            const auto& resource=entry.second;
            if(isBaseLevel(resource.level)||isCurrentTarget(resource))continue;""",
        "after": """        for(const auto& entry:uploaded_) {
            const auto& resource=entry.second;
            if(std::none_of(displayed_.begin(),displayed_.end(),[&](const auto& draw){return draw.resource==resource;}))continue;
            if(isBaseLevel(resource.level)||isCurrentTarget(resource))continue;""",
    },
    {
        "name": "protected-same-key-content-rejection",
        "regression": "Refuse a replacement backing while another content of its logical key remains displayed.",
        "case": "replacementContentsShareKeyWithoutBlockingHandoff",
        "message": "new immutable backing is CPU-ready while old same-key backing is displayed",
        "before": """    bool cpuReady(const TerrainDisplayScope& scope,const TerrainDisplayResource& resource) {
        startEvent();if(!current(scope)||!demandResourceMatches(resource))return reject();""",
        "after": """    bool cpuReady(const TerrainDisplayScope& scope,const TerrainDisplayResource& resource) {
        startEvent();if(!current(scope)||!demandResourceMatches(resource))return reject();
        for(const auto& old:uploaded_)if(old.second.key==resource.key&&old.second.contentKey!=resource.contentKey&&isProtected(identity(old.second)))return reject();""",
    },
    {
        "name": "cancelled-scope-reauthentication",
        "regression": "Remove the bounded monotone demand fence, allowing A->cancel->B->A reauthentication.",
        "case": "demandSequenceFencePreventsCancelledScopeReactivation",
        "message": "A then B cannot reactivate A after cancellation",
        "before": """        if(sameOwner&&(demand.scope.requestSequence<=demand_->scope.requestSequence||
                       demand.scope.candidateSequence<demand_->scope.candidateSequence))
            throw std::invalid_argument("stale terrain demand sequence");""",
        "after": """        // Deliberate isolated regression: no monotone demand fence.""",
    },
    {
        "name": "cancelled-retirement-rejection",
        "regression": "Reject actual unprotected backing retirement acknowledgements while scheduling is cancelled.",
        "case": "cancelledUnshownResourcesCanRetireWithoutFalseReuse",
        "message": "cancelled owner retirement acknowledgements update resident inventory",
        "before": "        if(!demand_||!ownerMatches(scope,demand_->scope)||isProtected(id))return reject();",
        "after": "        if(!demand_||cancelled_||!ownerMatches(scope,demand_->scope)||isProtected(id))return reject();",
    },
)

CMAKE = """cmake_minimum_required(VERSION 3.21)
project(TerrainDisplayMutation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 {qt_version} EXACT REQUIRED COMPONENTS Core)
add_executable(terrain_display_state_probe tests/terrain_display_state_tests.cpp)
target_link_libraries(terrain_display_state_probe PRIVATE Qt6::Core)
"""


def encoded_anchor(text: str, original: bytes) -> bytes:
    newline = "\r\n" if b"\r\n" in original else "\n"
    if newline == "\r\n" and b"\n" in original.replace(b"\r\n", b""):
        raise ValueError("Mixed header newlines: byte-exact mutations require a consistent original.")
    return text.replace("\n", newline).encode("utf-8")


def validate_mutations(header: bytes, tests: bytes) -> list[dict]:
    source = tests.decode("utf-8")
    table = re.search(r"void\(\*const cases\[\]\)\(\)=\{(.*?)\};", source, re.DOTALL)
    if table is None:
        raise ValueError("Actual native case table was not found.")
    names = [name.strip() for name in table.group(1).split(",") if name.strip()]
    if len(names) != 20 or len(set(names)) != 20:
        raise ValueError(f"Expected the actual 20 distinct native cases; found {len(names)}.")
    prepared = []
    for mutation in MUTATIONS:
        before, after = encoded_anchor(mutation["before"], header), encoded_anchor(mutation["after"], header)
        if header.count(before) != 1:
            raise ValueError(f'{mutation["name"]}: exact mutation anchor must occur once.')
        case, message = mutation["case"], mutation["message"]
        start = source.find(f"void {case}() {{")
        end = source.find("\nvoid ", start + 1)
        if start < 0 or case not in names or source.count('"' + message + '"') != 1:
            raise ValueError(f'{mutation["name"]}: native failure assertion/case must be unique.')
        if message not in source[start:end if end >= 0 else len(source)]:
            raise ValueError(f'{mutation["name"]}: assertion must be inside its named native case.')
        prepared.append({**mutation, "beforeBytes": before, "afterBytes": after})
    return prepared


def check_originals(originals: dict[Path, str]) -> None:
    for path, expected in originals.items():
        if digest(path.read_bytes()) != expected:
            raise RuntimeError(f"Original source changed during gate: {path}")


def run_process(argv: list[str], cwd: Path, env: dict[str, str], directory: Path,
                label: str, timeout: int) -> dict:
    stdout_path, stderr_path = directory / f"{label}.stdout.bin", directory / f"{label}.stderr.bin"
    receipt = {"argv": argv, "cwd": str(cwd), "startedUtc": utc(), "pid": None,
               "exitCode": None, "timedOut": False, "interrupted": False, "spawnError": None}
    started = time.monotonic()
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        try:
            process = subprocess.Popen(argv, cwd=str(cwd), env=env, stdin=subprocess.DEVNULL,
                                       stdout=stdout, stderr=stderr, shell=False,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            receipt["pid"] = process.pid
            try:
                receipt["exitCode"] = process.wait(timeout=timeout)
            except (subprocess.TimeoutExpired, KeyboardInterrupt) as stopped:
                receipt["timedOut"] = isinstance(stopped, subprocess.TimeoutExpired)
                receipt["interrupted"] = isinstance(stopped, KeyboardInterrupt)
                # Terminate only this runner's recorded subprocess tree. Logs
                # and source/build directories remain intact for diagnosis.
                cleanup = directory / f"{label}.termination-cleanup.bin"
                if os.name == "nt":
                    with cleanup.open("wb") as log:
                        killed = subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                                stdout=log, stderr=subprocess.STDOUT, shell=False,
                                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=30)
                    receipt["treeTerminationExitCode"] = killed.returncode
                    receipt["treeTerminationLog"] = file_record(cleanup)
                else:
                    process.kill()
                receipt["exitCode"] = process.wait(timeout=30)
        except OSError as error:
            receipt["spawnError"] = str(error)
        finally:
            receipt["finishedUtc"] = utc()
            receipt["elapsedSeconds"] = time.monotonic() - started
    receipt["stdout"], receipt["stderr"] = file_record(stdout_path), file_record(stderr_path)
    save_json(directory / f"{label}.receipt.json", receipt)
    return receipt


def native_result(execution: dict, mutation: dict | None) -> dict:
    stdout = Path(execution["stdout"]["path"]).read_bytes().decode("utf-8", errors="replace")
    stderr = Path(execution["stderr"]["path"]).read_bytes().decode("utf-8", errors="replace")
    summary = re.findall(r"terrain display state: processed=(\d+) passed=(\d+) failed=(\d+) skip=(\d+)", stdout)
    prefix = "terrain display state FAIL: "
    failures = [line[len(prefix):] for line in stderr.splitlines() if line.startswith(prefix)]
    reported = dict(zip(("processed", "passed", "failed", "skip"), map(int, summary[0]))) if len(summary) == 1 else None
    result = {"nativeExitCode": execution["exitCode"], "reportedCaseCounts": reported,
              "rawFailureMessages": failures, "namedFailures": [],
              "caseCountEvidence": "native summary" if reported else "native abort does not report executed/passed counts"}
    if mutation is None:
        result["accepted"] = (execution["exitCode"] == 0 and not execution["timedOut"] and not execution["interrupted"]
                              and reported == {"processed": 20, "passed": 20, "failed": 0, "skip": 0}
                              and not failures)
    else:
        if failures == [mutation["message"]]:
            result["namedFailures"] = [{"case": mutation["case"], "assertion": mutation["message"],
                                        "evidence": "exact unique assertion in byte-identical native test source"}]
        result["accepted"] = (execution["exitCode"] == 1 and not execution["timedOut"] and not execution["interrupted"]
                              and len(result["namedFailures"]) == 1 and not summary)
    result["namedFailureCount"] = len(result["namedFailures"])
    return result


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    result.add_argument("--output-root", type=Path, required=True, help="External evidence root; a unique run directory is created.")
    result.add_argument("--cmake", type=Path, required=True)
    result.add_argument("--ninja", type=Path, required=True)
    result.add_argument("--cxx", type=Path, required=True, help="Installed MinGW g++.exe.")
    result.add_argument("--qt-prefix", type=Path, required=True, help="Installed Qt 6.8.3 mingw_64 prefix.")
    result.add_argument("--qt-version", default="6.8.3")
    result.add_argument("--configure-timeout", type=int, default=180)
    result.add_argument("--build-timeout", type=int, default=360)
    result.add_argument("--execute-timeout", type=int, default=30)
    return result


def main() -> int:
    args = parser().parse_args()
    if os.name != "nt":
        raise ValueError("This gate requires the configured Windows MinGW/Qt native runtime.")
    repo, output = args.repo.resolve(), args.output_root.resolve()
    if output == repo or repo in output.parents:
        raise ValueError("Evidence/build output must stay outside the production repository.")
    if not re.fullmatch(r"\d+\.\d+\.\d+", args.qt_version):
        raise ValueError("Qt version must be a literal version, for example 6.8.3.")
    if min(args.configure_timeout, args.build_timeout, args.execute_timeout) <= 0:
        raise ValueError("Process timeouts must be positive.")
    paths = {name: getattr(args, name).resolve() for name in ("cmake", "ninja", "cxx")}
    for name, path in paths.items():
        if not path.is_file():
            raise ValueError(f"Installed {name} executable missing: {path}")
    qt = args.qt_prefix.resolve()
    qt_core = qt / "bin" / "Qt6Core.dll"
    if not qt_core.is_file() or not (qt / "lib" / "cmake" / "Qt6").is_dir():
        raise ValueError(f"Installed Qt Core runtime/config missing: {qt}")
    header_path, tests_path = repo / "app" / "terraindisplaystate.h", repo / "tests" / "terrain_display_state_tests.cpp"
    header, tests = header_path.read_bytes(), tests_path.read_bytes()
    mutations = validate_mutations(header, tests)
    originals = {header_path: digest(header), tests_path: digest(tests)}
    output.mkdir(parents=True, exist_ok=True)
    run = output / ("terrain-display-mutations-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8])
    run.mkdir(exist_ok=False)
    env = os.environ.copy()
    env["PATH"] = os.pathsep.join([str(paths["cxx"].parent), str(qt / "bin"),
                                  str(paths["cmake"].parent), str(paths["ninja"].parent), env.get("PATH", "")])
    manifest = {"schema": "pandoeditor-terrain-display-native-mutations", "version": 1,
                "startedUtc": utc(), "runDirectory": str(run), "runnerPid": os.getpid(),
                "runner": file_record(Path(__file__)), "originalHeader": file_record(header_path),
                "originalTests": file_record(tests_path), "tools": {key: file_record(value) for key, value in paths.items()},
                "qtCoreRuntime": file_record(qt_core), "qtVersionRequired": args.qt_version,
                "execution": "local-native-subprocess", "buildIsolation": "fresh standalone CMake/MinGW/Ninja per variant",
                "instrumentation": "none; byte-identical actual 20-case test source",
                "processEvidence": "direct child PIDs; verbose compiler argv, but compiler descendant PIDs are not separately attested",
                "variants": [], "accepted": False}
    receipt_path = run / "results.json"
    save_json(receipt_path, manifest)
    try:
        git = run_process(["git", "rev-parse", "HEAD"], repo, env, run, "git-head", 30)
        manifest["gitHeadReceipt"] = git
        manifest["gitHead"] = Path(git["stdout"]["path"]).read_text(encoding="utf-8").strip() if git["exitCode"] == 0 else None
        for mutation in (None, *mutations):
            check_originals(originals)
            name = "baseline" if mutation is None else mutation["name"]
            directory = run / name
            source, build = directory / "source", directory / "build"
            (source / "app").mkdir(parents=True)
            (source / "tests").mkdir()
            copied_header = header if mutation is None else header.replace(mutation["beforeBytes"], mutation["afterBytes"], 1)
            (source / "app" / "terraindisplaystate.h").write_bytes(copied_header)
            (source / "tests" / "terrain_display_state_tests.cpp").write_bytes(tests)
            (source / "CMakeLists.txt").write_text(CMAKE.format(qt_version=args.qt_version), encoding="utf-8")
            variant = {"name": name, "accepted": False, "sourceHeader": file_record(source / "app" / "terraindisplaystate.h"),
                       "sourceTests": file_record(source / "tests" / "terrain_display_state_tests.cpp"),
                       "cmakeSource": file_record(source / "CMakeLists.txt"), "commands": []}
            if mutation:
                patch = {key: value for key, value in mutation.items() if not key.endswith("Bytes")}
                patch.update({"beforeSha256": digest(mutation["beforeBytes"]), "afterSha256": digest(mutation["afterBytes"]),
                              "originalHeaderSha256": digest(header), "mutatedHeaderSha256": digest(copied_header),
                              "anchorMatches": header.count(mutation["beforeBytes"])})
                save_json(directory / "mutation.json", patch)
                variant["mutation"] = patch
            manifest["variants"].append(variant)
            save_json(receipt_path, manifest)
            configure = run_process([str(paths["cmake"]), "-S", str(source), "-B", str(build), "-G", "Ninja",
                                     "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_CXX_COMPILER=" + paths["cxx"].as_posix(),
                                     "-DCMAKE_MAKE_PROGRAM=" + paths["ninja"].as_posix(), "-DCMAKE_PREFIX_PATH=" + qt.as_posix()],
                                    directory, env, directory, "configure", args.configure_timeout)
            variant["commands"].append(configure)
            if configure["interrupted"]:
                raise RuntimeError("Native configure interrupted; spawned process tree was terminated.")
            if configure["exitCode"] == 0 and not configure["timedOut"]:
                variant["cmakeCache"] = file_record(build / "CMakeCache.txt")
                variant["compileCommands"] = file_record(build / "compile_commands.json")
                compile_receipt = run_process([str(paths["cmake"]), "--build", str(build),
                                               "--target", "terrain_display_state_probe", "--parallel", "1", "--verbose"],
                                              directory, env, directory, "build", args.build_timeout)
                variant["commands"].append(compile_receipt)
                if compile_receipt["interrupted"]:
                    raise RuntimeError("Native build interrupted; spawned process tree was terminated.")
                binary = build / "terrain_display_state_probe.exe"
                if compile_receipt["exitCode"] == 0 and not compile_receipt["timedOut"] and binary.is_file():
                    check_originals(originals)
                    if digest((source / "app" / "terraindisplaystate.h").read_bytes()) != digest(copied_header) or \
                            (source / "tests" / "terrain_display_state_tests.cpp").read_bytes() != tests:
                        raise RuntimeError("Isolated source changed during native build.")
                    variant["nativeBinary"] = file_record(binary)
                    execution = run_process([str(binary)], directory, env, directory, "execute", args.execute_timeout)
                    variant["commands"].append(execution)
                    if execution["interrupted"]:
                        raise RuntimeError("Native execution interrupted; spawned process tree was terminated.")
                    variant["nativeResult"] = native_result(execution, mutation)
                    variant["accepted"] = variant["nativeResult"]["accepted"]
            check_originals(originals)
            save_json(directory / "variant.json", variant)
            save_json(receipt_path, manifest)
            print(f'{name}: accepted={variant["accepted"]} evidence={directory}', flush=True)
            if mutation is None and not variant["accepted"]:
                manifest["stopReason"] = "Unmodified native baseline failed; mutant failures cannot establish a negative gate."
                break
        check_originals(originals)
        manifest["originalSourcesUnchanged"] = True
        manifest["accepted"] = len(manifest["variants"]) == 5 and all(item["accepted"] for item in manifest["variants"])
    except Exception as error:
        manifest["runnerError"] = f"{type(error).__name__}: {error}"
        manifest["accepted"] = False
        try:
            check_originals(originals)
            manifest["originalSourcesUnchanged"] = True
        except Exception as changed:
            manifest["originalSourcesUnchanged"] = False
            manifest["originalSourceError"] = str(changed)
    finally:
        manifest["finishedUtc"] = utc()
        save_json(receipt_path, manifest)
    print(f'negative_gate={"PASS" if manifest["accepted"] else "FAIL"} results={receipt_path}', flush=True)
    return 0 if manifest["accepted"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"terrain mutation setup FAIL: {error}", file=sys.stderr)
        sys.exit(1)
