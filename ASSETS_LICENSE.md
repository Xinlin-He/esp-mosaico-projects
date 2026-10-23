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

The AI service, model, generation date, prompts, input assets and applicable
service terms were not retained in the archive. PNG metadata contains no
additional provenance or license data. The statement above records how the
images were created; it does not by itself establish redistribution rights.
Accordingly, these files are **not covered by the repository's Apache-2.0
license**, and public redistribution remains subject to confirmation of the
applicable AI-service terms.

Before a public release, record the applicable AI-service terms or replace the
images with assets whose generation and redistribution terms are preserved.
The vector fallback rendered by `basketball/main/bball_view.c` does not depend
on these images.

The Basketball gameplay tones are deterministically synthesized by
`basketball/assets_src/generate_audio.py`; those generated tones and their
generator are Apache-2.0 project assets and are not part of the image-license
gap above.
