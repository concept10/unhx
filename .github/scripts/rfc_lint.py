#!/usr/bin/env python3
"""rfc_lint.py — Validate UNHOX RFC documents.

Called by the rfc-lint CI job on every pull request that modifies
docs/rfcs/RFC-[0-9]*.md files.

Usage:
    python3 .github/scripts/rfc_lint.py docs/rfcs/RFC-0001-*.md [...]

Exit codes:
    0  all provided RFC files are valid
    1  one or more RFC files have lint errors
"""

import re
import sys
from pathlib import Path

# ── Constants ──────────────────────────────────────────────────────────────────

VALID_STATUSES = {
    "Draft",
    "Proposed",
    "Accepted",
    "Rejected",
    "Withdrawn",
    "Superseded",
}

REQUIRED_SECTIONS = ["Summary", "Motivation"]

# Checklist required once the RFC enters review or acceptance.
CHECKLIST_REQUIRED_FOR = {"Proposed", "Accepted"}

VALID_COMPONENTS = {
    "ipc", "vm", "sched", "platform", "bootstrap",
    "bsd", "vfs", "device", "network", "framework", "docs", "ci",
}

# Regex for a well-formed checklist item:
#   - [ ] `[phase:N]` [`[layer:LN]`] `[component:name]` Description
ITEM_RE = re.compile(
    r"^- \[[ x]\] "
    r"`\[phase:([0-5])\]` "
    r"(?:`\[layer:(L[1-4])\]` )?"
    r"`\[component:([a-z][a-z0-9_-]*)\]` "
    r"(.{3,80})$"
)


# ── Lint function ──────────────────────────────────────────────────────────────

def lint_rfc(path: Path) -> list[str]:
    """Return a list of error strings for *path*. Empty list means OK."""
    errors: list[str] = []

    if not path.exists():
        return [f"File not found: {path}"]

    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()

    # 1. Filename must match RFC-NNNN-slug.md
    m = re.match(r"RFC-(\d{4})-", path.name)
    if not m:
        errors.append(
            f"Filename '{path.name}' does not match pattern RFC-NNNN-slug.md"
        )
        return errors  # further checks require a valid number
    rfc_num = m.group(1)

    # 2. First heading must contain the RFC number
    if lines:
        if not re.match(rf"^#\s+RFC-{rfc_num}:", lines[0]):
            errors.append(
                f"First line must be '# RFC-{rfc_num}: Title ...', got: {lines[0]!r}"
            )

    # 3. Status field
    status: str | None = None
    for line in lines:
        m2 = re.match(r"-\s*\*\*Status\*\*:\s*(.+)", line)
        if m2:
            status = m2.group(1).strip()
            break
    if status is None:
        errors.append("Missing '- **Status**: ...' field")
    elif status not in VALID_STATUSES:
        errors.append(
            f"Invalid status '{status}'; must be one of: "
            + ", ".join(sorted(VALID_STATUSES))
        )

    # 4. Required top-level sections
    headings = {
        m3.group(1).strip()
        for line in lines
        if (m3 := re.match(r"^#{1,3}\s+(.+)", line))
    }
    for sec in REQUIRED_SECTIONS:
        if sec not in headings:
            errors.append(f"Missing required section '## {sec}'")

    # 5. Implementation Checklist (required for Proposed / Accepted)
    if status in CHECKLIST_REQUIRED_FOR:
        if "Implementation Checklist" not in headings:
            errors.append(
                f"Status is '{status}' but '## Implementation Checklist' "
                "section is missing"
            )
        else:
            in_checklist = False
            item_count = 0
            for lineno, line in enumerate(lines, 1):
                if re.match(r"^#{1,3}\s+Implementation Checklist", line):
                    in_checklist = True
                    continue
                if in_checklist and re.match(r"^#{1,3}\s+", line):
                    in_checklist = False
                if in_checklist and re.match(r"^- \[", line):
                    item_count += 1
                    if not ITEM_RE.match(line):
                        errors.append(
                            f"Line {lineno}: checklist item does not match "
                            "required format.\n"
                            "  Expected: "
                            "- [ ] `[phase:N]` [`[layer:LN]`] "
                            "`[component:name]` Description\n"
                            f"  Got:      {line}"
                        )
                    else:
                        # Validate component value
                        cm = ITEM_RE.match(line)
                        if cm and cm.group(3) not in VALID_COMPONENTS:
                            errors.append(
                                f"Line {lineno}: unknown component "
                                f"'{cm.group(3)}'; valid values: "
                                + ", ".join(sorted(VALID_COMPONENTS))
                            )
            if item_count == 0:
                errors.append(
                    "'## Implementation Checklist' section is present but "
                    "contains no checklist items"
                )

    return errors


# ── Entry point ────────────────────────────────────────────────────────────────

def main() -> int:
    # Filter to only files whose names look like RFC-NNNN-*.md
    paths = [
        Path(p)
        for p in sys.argv[1:]
        if re.match(r"RFC-\d{4}-", Path(p).name)
    ]

    if not paths:
        print(
            "rfc_lint.py: no RFC files provided (expected RFC-NNNN-*.md)",
            file=sys.stderr,
        )
        return 1

    total_errors = 0
    for path in paths:
        errors = lint_rfc(path)
        if errors:
            print(f"FAIL  {path.name}")
            for err in errors:
                # Indent multi-line errors
                for i, el in enumerate(err.splitlines()):
                    prefix = "  ✗ " if i == 0 else "    "
                    print(f"{prefix}{el}")
            total_errors += len(errors)
        else:
            print(f"PASS  {path.name}")

    if total_errors:
        print(
            f"\nrfc_lint: {total_errors} error(s) found in "
            f"{len(paths)} file(s).",
            file=sys.stderr,
        )
        return 1

    print(f"\nrfc_lint: all {len(paths)} file(s) OK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
