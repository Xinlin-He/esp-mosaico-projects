"""Normalize gspc Windows paths before CMake imports its Ninja depfile."""

from pathlib import Path
import sys

EXTENDED_PATH_PREFIX = "\\\\\\\\?\\\\"


def main() -> int:
    try:
        index = sys.argv.index("--depfile")
        path = Path(sys.argv[index + 1])
    except (ValueError, IndexError):
        return 0
    if not path.is_file():
        return 0
    text = path.read_text(encoding="utf-8")
    cleaned = text.replace(EXTENDED_PATH_PREFIX, "")
    if cleaned != text:
        path.write_text(cleaned, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
