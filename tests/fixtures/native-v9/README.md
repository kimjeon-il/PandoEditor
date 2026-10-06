# Genuine native v9 compatibility fixtures

These files were captured on 2026-10-05 from the production native v9 codec,
before the native v10 provenance implementation, using the existing
`m975_model_exchange_probe` in codec mode against `timeline-exchange/static.json`
and `timeline-exchange/content.json`. They are not v10 files with provenance
removed. The v9 writer's complete archive, including its materialized inline
rows, is intentionally semantic when loaded by a v10 reader.

Captured encoder source SHA-256:
`5935500adfd9c7db3e3d73a30fe99a7cade2b932203b59ab2df892486054cd72`

Captured probe binary SHA-256:
`e65a26a5c750921b78d31ea46b1807e6e1ca012ac4a44c84ce5153b77a4e5e2a`

The static fixture has four archive rows and no embedded assets. The content
fixture has six archive rows and one embedded SVG flag. Tests must preserve
these rows when migrating; they must not infer ownership from synthetic IDs.

Fixture SHA-256:
- `static.pando.json`: `c811e52351d8cc397f4729a2658f3676a7d814909a3eff06d956bf5c4deff579`
- `content.pando.json`: `3163155c288cca9c8529e6caad7fa16cb561e496e47cc5c10833d671658f9061`
