#!/usr/bin/env python3
"""Extract the release notes for a given version tag from CHANGELOG.md.

Usage: extract-changelog.py <tag>

Prints the body of the first ## section whose heading contains the version
number extracted from the tag (i.e. the tag with a leading 'v' stripped).
Exits with a non-zero status if no matching section is found, or if the first
## heading does not match (to catch version/tag mismatches early).
"""
import re
import sys


def main() -> None:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <tag>", file=sys.stderr)
        sys.exit(1)

    tag = sys.argv[1]
    version = tag.lstrip("v")

    with open("CHANGELOG.md") as f:
        content = f.read()

    # Split on level-2 headings.
    parts = re.split(r"\n(?=## )", content)
    for part in parts:
        if not part.startswith("## "):
            continue
        heading_end = part.index("\n") if "\n" in part else len(part)
        heading = part[:heading_end]
        body = part[heading_end:].strip()
        # The spec requires the version to appear anywhere in the title string.
        if version in heading or tag in heading:
            print(body)
            sys.exit(0)
        else:
            print(
                f"ERROR: First ## heading '{heading}' does not contain "
                f"version '{version}'.",
                file=sys.stderr,
            )
            sys.exit(1)

    print("ERROR: No ## heading found in CHANGELOG.md.", file=sys.stderr)
    sys.exit(1)


if __name__ == "__main__":
    main()
