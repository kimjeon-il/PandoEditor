"""Read-only P5 provenance audit against original bytes of the approved Web commit.

This records the 64 non-coordinate overrides. It neither builds production place
data nor applies the separate 35 Romanian coordinate review overrides.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import subprocess

WEB_SHA = "ebcfae4d27b29cbbea6416a7045a4806930204be"
PATHS = ["reports/places/candidate-review-overrides.json",
         "reports/places/candidate-reviews.json",
         "reports/places/candidate-review-summary.json",
         "tools/build-place-candidates.py", "assets/data/places/manifest.json"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web-repo", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()
    blobs = {p: subprocess.check_output(["git", "-C", args.web_repo,
                                        "show", WEB_SHA + ":" + p]) for p in PATHS}
    overrides, ledger, summary = (json.loads(blobs[p]) for p in PATHS[:3])
    selected = [x for x in overrides["overrides"] if "coordinateCorrectionRef" not in x]
    assert len(selected) == 64 and len({x["geonameId"] for x in selected}) == 64
    assert collections.Counter(x["action"] for x in selected) == {"retain": 17, "exclude": 47}
    assert overrides["sourceSha256"] == ledger["source"]["sha256"]
    # Execute the pinned offline generator's actual overlay implementation. Its
    # CLI main is disabled; no downloads, runtime-data generation or writes occur.
    namespace = {"__name__": "p5_readonly_audit", "__file__": str(Path(args.web_repo) / PATHS[3])}
    exec(compile(blobs[PATHS[3]], WEB_SHA + ":" + PATHS[3], "exec"), namespace)
    overlay = dict(overrides, overrides=selected)
    merged = namespace["apply_review_overrides"](ledger, overlay)
    decisions = {x["geonameId"]: x for x in merged["decisions"]}
    base = {x["geonameId"]: x for x in ledger["decisions"]}
    reviewed = {x["record"]["geonameId"]: x for x in summary["reviewedRecords"]}
    rows = []
    for override in selected:
        identifier = override["geonameId"]
        applied = decisions[identifier]
        assert applied["action"] == override["action"]
        assert override["reasonAppend"] in applied["reason"]
        assert applied["expected"] == base[identifier]["expected"]
        recorded = reviewed.get(identifier)
        rows.append({"override": override, "expectedSourceRecord": applied["expected"],
                     "baseAction": base[identifier]["action"], "appliedReason": applied["reason"],
                     "summaryAction": recorded["action"] if recorded else None,
                     "summaryMatches": bool(recorded and recorded["action"] == applied["action"]
                                            and override["reasonAppend"] in recorded["reason"])})
    # No other IDs, especially the separate coordinate overrides, may change.
    assert all(decisions[i] == value for i, value in base.items()
               if i not in {x["geonameId"] for x in selected})
    manifest = json.loads(blobs[PATHS[4]])
    app_manifest = Path(__file__).resolve().parents[1] / "assets/place/manifest.json"
    assert app_manifest.read_bytes() == blobs[PATHS[4]]
    result = {"webSHA": WEB_SHA, "sourceSnapshotSHA256": overrides["sourceSha256"],
              "sourceBlobs": [{"path": p, "bytes": len(b), "sha256": hashlib.sha256(b).hexdigest()}
                              for p, b in blobs.items()],
              "overlayExecution": {"processed": 64, "failed": 0, "skipped": 0,
                                   "unchangedCoordinates": 64, "excludedSeparateCoordinateOverrides": 35},
              "summaryLedgerHashMatches": summary["reviewLedgerSha256"] == hashlib.sha256(blobs[PATHS[1]]).hexdigest(),
              "summaryOverrideMatches": sum(x["summaryMatches"] for x in rows),
              "summaryOverrideMismatches": sum(not x["summaryMatches"] for x in rows),
              "productionManifest": manifest, "productionDataGate": "BLOCKED",
              "reason": "Pinned empty-v1 contains no deployed place records. Pinned review summary is stale; original source/expected not rewritten.",
              "rows": rows}
    target = Path(args.report)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        raise FileExistsError("Preserve previous evidence; choose a new report path")
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("webSHA", "overlayExecution", "summaryLedgerHashMatches", "summaryOverrideMatches", "summaryOverrideMismatches", "productionDataGate")}))


if __name__ == "__main__":
    main()
