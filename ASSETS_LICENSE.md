# Asset provenance and licensing

The repository software source is licensed under Apache-2.0 as stated in
`LICENSE` and the source-file SPDX headers. Third-party components fetched by
the build are not vendored here and remain under their respective licenses.

## Mosaico Snake

`snake/cover.png` was generated specifically for this project from the game's
own rendering primitives during the documented cover-production workflow. It is
distributed under Apache-2.0 with the rest of the Snake project.

## Arcade Hoops

The following image files were imported from an external development asset
directory and then processed for the game:

- `basketball/assets_src/basketball.png`
- `basketball/assets_src/hoop.png`
- `basketball/assets_src/court.png`
- `basketball/store/cover.png` (a derivative promotional image)

The surviving Git history and project records do not identify their original
authors, source URLs, generation prompts, stock-asset licenses, or written
redistribution grants. PNG metadata contains no attribution or license data.
Accordingly, these files are **not covered by the repository's Apache-2.0
license and are not cleared for redistribution**.

Before a public release, replace them with newly created assets whose copyright
holder and license are documented, or add verifiable source and license records.
The vector fallback rendered by `basketball/main/bball_view.c` does not depend
on these images.
