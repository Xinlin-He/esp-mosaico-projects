# Asset provenance and licensing

The repository software source is licensed under Apache-2.0 as stated in
`LICENSE` and the source-file SPDX headers. Third-party components fetched by
the build are not vendored here and remain under their respective licenses.

## Mosaico Snake

`snake/cover.png` was generated specifically for this project from the game's
own rendering primitives during the documented cover-production workflow. It is
distributed under Apache-2.0 with the rest of the Snake project.
The Snake gameplay tones are deterministically synthesized by
`snake/assets_src/generate_audio.py` and are also Apache-2.0 project assets.

## Arcade Hoops

The project owner confirms that the following image files were generated with
AI for this project, imported from the development asset directory, and then
processed for the game:

- `basketball/assets_src/basketball.png`
- `basketball/assets_src/hoop.png`
- `basketball/assets_src/court.png`
- `basketball/store/cover.png` (a derivative promotional image)

These files are project-specific AI-generated visual assets.
The vector fallback rendered by `basketball/main/bball_view.c` does not depend
on these images.

The Basketball gameplay tones are deterministically synthesized by
`basketball/assets_src/generate_audio.py`; those generated tones and their
generator are Apache-2.0 project assets.
