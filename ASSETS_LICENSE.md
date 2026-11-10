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

## ToF Instruments

Application code under `tof_instruments/` is Apache-2.0. The drum hits and
backing tracks are synthesized in `tof_instruments/main/instrument_audio.c`.
Guitar notes are embedded 16 kHz PCM files named `n040.pcm` through `n067.pcm`;
this repository does not include a separate license for those recordings.

`tof_instruments/website/cover.png` is a poster made for this project. The
other files in `website/` and `page-shots/` are screenshots of the application.

Fonts:

- `ui/fonts/DejaVuSans.ttf` — see `ui/fonts/LICENSE-DejaVuSans`
- `ui/fonts/MaShanZheng-Regular.ttf` — SIL OFL 1.1, see
  `ui/fonts/OFL-MaShanZheng.txt`
- `ui/fonts/InstrumentSans-Bold.ttf` — bundled without a license file in this
  tree

## MiniHub Panel

Application code under `minihub_panel/` is Apache-2.0. UI previews under
`minihub_panel/docs/ui-preview/` are screenshots of this application.

Fonts:

- `minihub_panel/components/app_panel/ui/fonts/DejaVuSans.ttf` and
  `DejaVuSans-Bold.ttf` — Bitstream Vera / DejaVu, see
  `minihub_panel/components/app_panel/ui/fonts/LICENSE`
- `minihub_panel/components/app_panel/ui/fonts/NotoSansSC-Regular.otf` —
  Noto Sans CJK SC Regular, SIL OFL 1.1, see
  `minihub_panel/components/app_panel/ui/fonts/OFL-NotoSansSC.txt`
