# Arcade Hoops Agent Guide

This directory contains the `basketball` Raylib Lite application. It is not a
standalone ESP-IDF or ESP-Mosaico Vibe workspace.

## Required workspace

- Mirror this directory to `<esp-mosaico-vibe>/projects/basketball` for
  development and validation, then synchronize validated source changes back.
- Treat the enclosing Vibe checkout as the owning workspace and follow its root
  and `projects/AGENTS.md` guidance.
- Use the Vibe version and dependency commits recorded by the project archive.
  Do not vendor Vibe, BSP, ESP-Iris, Recovery or Raylib Lite dependencies here.

## Project boundaries

- Keep portable game state and rules in `main/bball_game.*`, rendering in
  `main/bball_view.*`, and Host/native integration in `main/game_module.c`.
- Host and device must continue to use the same game module. Do not restore the
  retired project `app_main`, GSP canvas, private framebuffer, screen mirror,
  audio task or direct NVS implementation.
- Keep source images, atlas manifests and deterministic audio generation in
  `assets_src/`; keep store-facing assets in `store/` and deterministic inputs
  in `replays/`.
- Do not commit generated atlases, build directories, managed components,
  firmware bundles, simulator evidence, device logs or design backup files.
- Preserve the Vibe 0.2 immutable partition prefix, `main_app` layout, Recovery
  ABI and Iris system-update integration.
- Keep `ASSETS_LICENSE.md` synchronized when visual assets are added or
  replaced.

## Validation

Run from the enclosing Vibe workspace root:

```sh
python mosaico.py game sim --project projects/basketball --headless \
  --frames 40 --replay projects/basketball/replays/one_shot.json
python mosaico.py game sim --project projects/basketball --headless \
  --frames 1810 --replay projects/basketball/replays/full_match.json
python mosaico.py game build --target iris --project projects/basketball \
  --idf-path <ESP-IDF-path>
```

Rule, input or rendering changes require replay regression. Changes to audio,
haptics, persistence, assets, partitions or platform integration also require
target-device validation through `python mosaico.py`.
