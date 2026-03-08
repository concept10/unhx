#!/usr/bin/env python3
"""rfc_sync_issues.py — Sync Implementation Checklist items from an Accepted
RFC to GitHub Issues so that agents and contributors can pick up the work.

Called by the rfc-issue-sync CI job on every push to main that modifies an
RFC file.  Requires:
  - GH_TOKEN environment variable set to a token with issues:write scope.
  - gh CLI available in PATH (pre-installed on GitHub-hosted runners).

Usage:
    python3 .github/scripts/rfc_sync_issues.py docs/rfcs/RFC-0002-*.md [...]

Behaviour:
  1. Reads each RFC file.
  2. Skips files whose Status is not 'Accepted'.
  3. Parses the '## Implementation Checklist' section.
  4. For each unchecked item (- [ ] ...) that has valid tags, checks whether
     a GitHub Issue with the same title already exists.
  5. Creates a new issue for each item that does not already have one.
     The issue is labelled: rfc-task, phase:N, component:name, [layer:LN].
  6. Checked items (- [x] ...) are skipped — they represent completed work.

The sync is idempotent: running it multiple times does not create duplicates.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

# ── Constants ──────────────────────────────────────────────────────────────────

# Must match the regex in rfc_lint.py
ITEM_RE = re.compile(
    r"^- \[ \] "
    r"`\[phase:([0-5])\]` "
    r"(?:`\[layer:(L[1-4])\]` )?"
    r"`\[component:([a-z][a-z0-9_-]*)\]` "
    r"(.{3,80})$"
)

# Label definitions: (name, hex-color-without-#, description)
BASE_LABELS: list[tuple[str, str, str]] = [
    ("rfc-task", "0075ca", "Task generated from an RFC Implementation Checklist"),
]
PHASE_COLORS = ["e4e669", "f9d0c4", "fef2c0", "bfd4f2", "d4c5f9", "c2e0c6"]
LAYER_COLOR = "bfd4f2"
COMPONENT_COLOR = "d4c5f9"


# ── gh CLI helpers ─────────────────────────────────────────────────────────────

def gh(*args: str, check: bool = False) -> subprocess.CompletedProcess:
    """Run a gh CLI command and return the CompletedProcess."""
    result = subprocess.run(
        ["gh", *args],
        capture_output=True,
        text=True,
    )
    if check and result.returncode != 0:
        print(f"gh {' '.join(args)} failed:\n{result.stderr}", file=sys.stderr)
    return result


def ensure_label(name: str, color: str, description: str) -> None:
    """Create *name* label if it does not already exist."""
    # gh label create exits non-zero if label exists; use --force to upsert.
    # We prefer not to overwrite user-customised labels, so check first.
    result = gh("label", "list", "--json", "name", "--limit", "500")
    if result.returncode != 0:
        # Cannot list labels — try creating and ignore errors.
        gh("label", "create", name, "--color", color, "--description", description)
        return

    existing = {item["name"] for item in json.loads(result.stdout or "[]")}
    if name not in existing:
        r = gh(
            "label", "create", name,
            "--color", color,
            "--description", description,
        )
        if r.returncode == 0:
            print(f"    label created: {name}")
        else:
            # Non-fatal — issue creation will fail with an informative error.
            print(f"    warning: could not create label '{name}': {r.stderr.strip()}")


def issue_exists(title: str) -> bool:
    """Return True if an issue with *title* already exists (open or closed)."""
    result = gh(
        "issue", "list",
        "--label", "rfc-task",
        "--state", "all",
        "--json", "title",
        "--limit", "500",
    )
    if result.returncode != 0:
        return False
    issues = json.loads(result.stdout or "[]")
    return any(i["title"] == title for i in issues)


# ── RFC parsing ────────────────────────────────────────────────────────────────

def get_rfc_status(lines: list[str]) -> str | None:
    for line in lines:
        m = re.match(r"-\s*\*\*Status\*\*:\s*(.+)", line)
        if m:
            return m.group(1).strip()
    return None


def parse_checklist_items(rfc_num: str, rfc_path: Path, lines: list[str]) -> list[dict]:
    """Return a list of item dicts for every unchecked checklist item."""
    items: list[dict] = []
    in_checklist = False

    for line in lines:
        if re.match(r"^#{1,3}\s+Implementation Checklist", line):
            in_checklist = True
            continue
        if in_checklist and re.match(r"^#{1,3}\s+", line):
            # Next section starts — exit checklist
            in_checklist = False
        if not in_checklist:
            continue

        m = ITEM_RE.match(line)
        if m:
            phase, layer, component, description = m.groups()
            description = description.strip()
            items.append({
                "rfc_num": rfc_num,
                "rfc_path": str(rfc_path),
                "phase": phase,
                "layer": layer,        # may be None
                "component": component,
                "description": description,
                "title": f"[RFC-{rfc_num}] {description}",
            })

    return items


def build_issue_body(item: dict) -> str:
    repo = os.environ.get("GITHUB_REPOSITORY", "concept10/unhx")
    rfc_url = f"https://github.com/{repo}/blob/main/{item['rfc_path']}"
    layer_line = (
        f"**Verification Layer:** {item['layer']}\n" if item["layer"] else ""
    )
    return (
        f"**RFC:** [RFC-{item['rfc_num']}]({rfc_url})\n\n"
        f"**Phase:** {item['phase']}\n"
        + layer_line
        + f"**Component:** `{item['component']}`\n\n"
        "### Task\n\n"
        f"{item['description']}\n\n"
        "---\n"
        f"_Auto-generated from RFC-{item['rfc_num']} Implementation Checklist._\n"
        "_When this work is done, close this issue and mark the corresponding_\n"
        "_checklist item `[x]` in the RFC file._"
    )


# ── Main sync logic ────────────────────────────────────────────────────────────

def sync_rfc(path: Path) -> int:
    """Sync checklist items for a single RFC file. Returns 0 on success."""
    if not path.exists():
        print(f"  SKIP {path}: file not found", file=sys.stderr)
        return 0

    lines = path.read_text(encoding="utf-8").splitlines()
    status = get_rfc_status(lines)

    if status != "Accepted":
        print(f"  SKIP {path.name}: status is '{status}' (only sync Accepted RFCs)")
        return 0

    m = re.match(r"RFC-(\d{4})-", path.name)
    if not m:
        print(
            f"  ERROR {path.name}: cannot parse RFC number from filename",
            file=sys.stderr,
        )
        return 1
    rfc_num = m.group(1)

    items = parse_checklist_items(rfc_num, path, lines)
    if not items:
        print(f"  RFC-{rfc_num}: no open checklist items found — nothing to sync")
        return 0

    print(f"  RFC-{rfc_num}: found {len(items)} open checklist item(s)")

    # Ensure required labels exist before creating issues
    for name, color, desc in BASE_LABELS:
        ensure_label(name, color, desc)
    for item in items:
        phase = item["phase"]
        ensure_label(f"phase:{phase}", PHASE_COLORS[int(phase)], f"UNHOX Phase {phase}")
        ensure_label(
            f"component:{item['component']}",
            COMPONENT_COLOR,
            f"Kernel component: {item['component']}",
        )
        if item["layer"]:
            ensure_label(
                f"layer:{item['layer']}",
                LAYER_COLOR,
                f"Verification layer {item['layer']} (see RFC-0002)",
            )

    created = 0
    skipped = 0
    errors = 0

    for item in items:
        if issue_exists(item["title"]):
            print(f"  SKIP (exists): {item['title']}")
            skipped += 1
            continue

        labels = [
            "rfc-task",
            f"phase:{item['phase']}",
            f"component:{item['component']}",
        ]
        if item["layer"]:
            labels.append(f"layer:{item['layer']}")

        body = build_issue_body(item)

        result = gh(
            "issue", "create",
            "--title", item["title"],
            "--body", body,
            "--label", ",".join(labels),
        )
        if result.returncode == 0:
            issue_url = result.stdout.strip()
            print(f"  CREATE: {item['title']} → {issue_url}")
            created += 1
        else:
            print(
                f"  ERROR creating '{item['title']}': {result.stderr.strip()}",
                file=sys.stderr,
            )
            errors += 1

    print(
        f"  RFC-{rfc_num}: {created} created, {skipped} already existed"
        + (f", {errors} error(s)" if errors else "")
    )
    return 1 if errors else 0


def main() -> int:
    if len(sys.argv) < 2:
        print(
            "Usage: rfc_sync_issues.py <rfc-file> [<rfc-file> ...]",
            file=sys.stderr,
        )
        return 1

    rc = 0
    for arg in sys.argv[1:]:
        print(f"Processing {arg}")
        rc |= sync_rfc(Path(arg))

    return rc


if __name__ == "__main__":
    sys.exit(main())
