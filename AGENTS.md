# ESP-Mosaico Projects Agent Guide

This repository is a collection of user applications, not an ESP-Mosaico Vibe
workspace. Applications belong under `projects/<name>/`.

## Required Vibe context

- Develop and validate each application at
  `<esp-mosaico-vibe>/projects/<name>`, preserving the same relative path used
  in this repository.
- Use the official `esp-mosaico-vibe` checkout as the owning workspace.
- Before changing, building, simulating or installing an application, read and
  follow the Vibe repository's root `AGENTS.md`, its `projects/AGENTS.md`, and
  the relevant skills.
- Verify the Vibe checkout version and recorded submodule commits. Do not vendor
  `mosaico.py`, BSP, ESP-Iris, recovery firmware, GSP or other Vibe dependencies
  into this repository.

## Repository boundaries

- Keep every application self-contained in its own `projects/<name>/` directory.
- Keep application-specific rules in that application's `AGENTS.md`.
- Do not commit generated builds, managed components, device/Gateway state,
  simulator evidence, local agent runs or release archives.
- Preserve each application's retained Vibe Mode and recovery integration.
- Use the Vibe workspace's documented build, simulator and device workflows;
  device writes must go through `python mosaico.py`.

## Validation

Run an application's documented commands from the enclosing Vibe workspace
root. At minimum, run its host tests and relevant simulator flow before
publishing changes. Hardware validation remains required for device-only
behavior.
