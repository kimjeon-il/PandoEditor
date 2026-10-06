"""Deterministic byte/HTTP regression tests; no official data is fabricated."""
import copy
import hashlib
import http.server
import io
import importlib.util
import json
import tempfile
import threading
import unittest
from pathlib import Path
spec = importlib.util.spec_from_file_location("acquire_terrain_dem", Path(__file__).with_name("acquire-terrain-dem.py"))
acquisition = importlib.util.module_from_spec(spec)
spec.loader.exec_module(acquisition)


def entry(data, path="terrain/v0.13.0/0/0-0.webp"):
    return {"path": path, "size": len(data),
            "sha": hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()}


WEBP = b"RIFF\x10\x00\x00\x00WEBPVP8Lfixture-payload"


def manifest():
    return {"version": "0.13.3", "representation": "dem-relief-v1", "crs": "EPSG:4326",
            "extent": [-180, -90, 180, 90], "gutter": 1, "tileFormat": "lossless WebP RGBA",
            "registration": "cell-center", "sourceGridOrigin": [-180, 90],
            "sourceResolutionDegrees": [1/120, 1/120],
            "elevation": {"decode": "R*256+G-12000", "biasMeters": 12000,
                          "spacingMeters": 1, "validEncodedRange": [0, 65535]},
            "channels": {"r": "encoded elevation high byte", "g": "encoded elevation low byte",
                         "b": "precomputed hillshade", "a": "255"},
            "levels": [{"id": i, "width": 1350 * 2**i, "height": 675 * 2**i,
                        "tileSize": 1024, "columns": (1350 * 2**i + 1023)//1024,
                        "rows": (675 * 2**i + 1023)//1024} for i in range(6)],
            "urlTemplate": "https://kimjeon-il.github.io/world-map-terrain-v0.13.0/terrain/v0.13.0/{level}/{column}-{row}.webp",
            "tint": {"url": "https://kimjeon-il.github.io/world-map-terrain-v0.13.0/terrain/v0.13.3/tint.webp",
                     "width": 4096, "height": 2048,
                     "sha256": "1ae4c70e05494917c11d3c6b32869db61bf4f9e7e4b0239f9b4bd2b9bec582f3"},
            "assetsSha256": "622a6d272bdda6b507a1f4ba49794fe9ef41aca06f29d8b38e24114caa50d023"}


class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(acquisition, "production acquisition tool is not implemented")

    def test_complete_bytes_compute_independent_blob_and_sha256(self):
        result = acquisition.verify_stream(io.BytesIO(WEBP), entry(WEBP))
        self.assertEqual(result["bytes"], len(WEBP))
        self.assertEqual(result["sha256"], hashlib.sha256(WEBP).hexdigest())

    def test_empty_truncated_and_oversized_responses_fail(self):
        for data in (b"", WEBP[:-1], WEBP + b"extra"):
            with self.subTest(length=len(data)), self.assertRaises(acquisition.VerificationError):
                acquisition.verify_stream(io.BytesIO(data), entry(WEBP))

    def test_wrong_git_blob_fails(self):
        wrong = dict(entry(WEBP), sha="0"*40)
        with self.assertRaises(acquisition.VerificationError):
            acquisition.verify_stream(io.BytesIO(WEBP), wrong)

    def test_wrong_sha256_fails(self):
        with self.assertRaises(acquisition.VerificationError):
            acquisition.verify_stream(io.BytesIO(WEBP), entry(WEBP), expected_sha256="0"*64)

    def test_html_with_matching_size_and_blob_is_rejected_as_webp(self):
        html = b"<!doctype html><html>rate limited</html>"
        with self.assertRaises(acquisition.VerificationError):
            acquisition.verify_stream(io.BytesIO(html), entry(html))

    def test_manifest_rejects_wrong_channel_version_grid_and_tile_path(self):
        acquisition.validate_manifest(manifest())
        for change in (lambda m: m.update(version="0.13.4"),
                       lambda m: m["channels"].update(a="alpha"),
                       lambda m: m["levels"][5].update(columns=42),
                       lambda m: m.update(urlTemplate=m["urlTemplate"].replace("v0.13.0", "v0.13.3"))):
            altered = copy.deepcopy(manifest()); change(altered)
            with self.subTest(altered=altered), self.assertRaises(acquisition.VerificationError):
                acquisition.validate_manifest(altered)

    def test_aggregate_uses_level_row_column_binary_digest_order(self):
        levels = [{"id": 1, "rows": 2, "columns": 2}, {"id": 0, "rows": 1, "columns": 1}]
        paths = ["1/0-0.webp", "1/1-0.webp", "1/0-1.webp", "1/1-1.webp", "0/0-0.webp"]
        rows = {"terrain/v0.13.0/"+p: {"sha256": hashlib.sha256(p.encode()).hexdigest()} for p in reversed(paths)}
        rows["terrain/v0.13.3/tint.webp"] = {"sha256": hashlib.sha256(b"tint fixture").hexdigest()}
        expected = hashlib.sha256()
        for p in paths:
            expected.update(p.encode()); expected.update(hashlib.sha256(p.encode()).digest())
        expected.update(hashlib.sha256(b"tint fixture").digest())
        self.assertEqual(acquisition.aggregate_hash(levels, rows), expected.hexdigest())
        del rows["terrain/v0.13.0/1/0-1.webp"]
        with self.assertRaises(acquisition.VerificationError):
            acquisition.aggregate_hash(levels, rows)

    def test_truncated_tree_and_wrong_commit_fail(self):
        for tree in ({"sha": "0"*40, "truncated": False, "tree": []},
                     {"sha": acquisition.SOURCE_COMMIT, "truncated": True, "tree": []}):
            with self.assertRaises(acquisition.VerificationError):
                acquisition.runtime_entries(tree)

    def test_real_http_download_is_atomic_and_corrupt_existing_file_is_preserved(self):
        payloads = {"/ok": WEBP, "/short": WEBP[:-1]}
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200); self.end_headers(); self.wfile.write(payloads[self.path])
            def log_message(self, *_):
                pass
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
        try:
            with tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)/"tile.webp"
                url = f"http://127.0.0.1:{server.server_port}"
                with self.assertRaises(acquisition.VerificationError):
                    acquisition.acquire_asset(entry(WEBP), path, url+"/short", retries=1)
                self.assertFalse(path.exists())
                result = acquisition.acquire_asset(entry(WEBP), path, url+"/ok", retries=0)
                self.assertEqual(path.read_bytes(), WEBP)
                self.assertFalse(result["reused"])
                result = acquisition.acquire_asset(entry(WEBP), path, url+"/ok", retries=0)
                self.assertTrue(result["reused"])
                path.write_bytes(b"original corrupt user bytes")
                with self.assertRaises(acquisition.VerificationError):
                    acquisition.acquire_asset(entry(WEBP), path, url+"/ok", retries=0)
                self.assertEqual(path.read_bytes(), b"original corrupt user bytes")
        finally:
            server.shutdown(); server.server_close(); worker.join()


if __name__ == "__main__":
    unittest.main()
