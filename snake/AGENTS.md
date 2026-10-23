# Mosaico Snake Agent Guide

This directory contains the `snake` Raylib Lite application. It is not a
standalone ESP-IDF or ESP-Mosaico Vibe workspace.

## Required workspace

- Mirror this directory to `<esp-mosaico-vibe>/projects/snake` for development
  and validation, then synchronize validated source changes back.
- Treat the enclosing Vibe checkout as the owning workspace and follow its root
  and `projects/AGENTS.md` guidance.
- Use the Vibe version and dependency commits recorded by the project archive.
  Do not vendor Vibe, BSP, ESP-Iris, Recovery or Raylib Lite dependencies here.

## Project boundaries

- Keep portable rules and state in `main/snake_game.*`, rendering in
  `main/snake_view.*`, and Host/native integration in `main/game_module.c`.
- Host and device must continue to use the same game module. Do not restore the
  retired project `app_main`, GSP canvas, private screen mirror or audio task.
- Keep deterministic audio sources and generation in `assets_src/`, the
  approved cover at `cover.png`, and deterministic inputs in `replays/`.
- Do not commit generated assets, build directories, managed components,
  firmware bundles, simulator evidence, device logs or temporary cover tools.
- Preserve the Vibe 0.2 immutable partition prefix, `main_app` layout, Recovery
  ABI and Iris system-update integration.
- Keep `BEST` volatile unless persistence is an explicit product change; any
  persistence implementation belongs in the native module, not the rule layer.

## Validation

Run from the enclosing Vibe workspace root:

```sh
python mosaico.py game sim --project projects/snake --headless \
  --frames 500 --replay projects/snake/replays/eat_first_food.json
python mosaico.py game sim --project projects/snake --headless \
  --frames 9000 --replay projects/snake/replays/noop.json
python mosaico.py game build --target iris --project projects/snake \
  --idf-path <ESP-IDF-path>
```

Rule, input or rendering changes require replay regression. Changes to audio,
assets, partitions or platform integration also require target-device
validation through `python mosaico.py`.
