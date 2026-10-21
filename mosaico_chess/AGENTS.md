# Mosaico Chess Agent Guide

This directory contains the `mosaico_chess` application. It is not a standalone
ESP-IDF workspace.

## Required workspace

- Mirror this directory to `<esp-mosaico-vibe>/projects/mosaico_chess` for
  development and validation, then synchronize validated source changes back.
- Treat the enclosing `esp-mosaico-vibe` checkout as the owning workspace.
- Read and follow the enclosing repository's `AGENTS.md` and relevant skills
  before changing, building, simulating or installing the application.
- Use the official Vibe repository version and its recorded submodule commits.
  Do not vendor `mosaico.py`, BSP, ESP-Iris, recovery firmware, GSP or other
  Vibe dependencies into this repository.

## Project boundaries

- Application source belongs in `main/`, desktop simulation integration in
  `pc/`, UI source and assets in `ui/`, and host tests in `tests/`.
- Keep generated build directories, managed components, device/Gateway state,
  simulator evidence and release archives out of version control.
- Preserve the retained Vibe Mode integration, immutable partition prefix,
  recovery component, Iris OTA startup and project CMake integration.
- Do not replace inspected component APIs with invented interfaces.

## Validation

Run commands from the enclosing Vibe workspace root:

```sh
cmake -S projects/mosaico_chess/tests -B projects/mosaico_chess/build-tests
cmake --build projects/mosaico_chess/build-tests
ctest --test-dir projects/mosaico_chess/build-tests --output-on-failure
python mosaico.py project sim \
  --project projects/mosaico_chess --interactive --duration 120
```

For firmware builds, use the Vibe workspace's pinned low-noise ESP-IDF build
workflow. For device installation or updates, use only `python mosaico.py`
commands and follow the enclosing repository's device-operation guidance.
