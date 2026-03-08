# UNHOX RFC Process

An RFC (Request for Comments) is the canonical mechanism for proposing and
recording significant decisions about UNHOX: new features, kernel interfaces,
architectural choices, and verification strategies.

RFCs are the single source of truth for *why* a decision was made, not just
*what* was decided. Every non-trivial change to a public interface, a kernel
invariant, or a cross-cutting policy requires an RFC before implementation
begins.

---

## RFC Lifecycle

```
Draft → Proposed → Accepted     (implementation begins)
                 → Rejected     (declined; document preserved)
                 → Withdrawn    (author withdrew before acceptance)
                 → Superseded   (replaced by a later RFC)
```

| Status | Meaning |
|--------|---------|
| **Draft** | Work in progress; not yet ready for review. Can be iterated freely. |
| **Proposed** | Pull request is open against `main`; community review period. |
| **Accepted** | Merged into `main`; CI creates GitHub Issues from the checklist. |
| **Rejected** | Reviewed and declined; document preserved for historical context. |
| **Withdrawn** | Author withdrew the proposal before a decision was reached. |
| **Superseded** | Replaced by a later RFC; the `Superseded-by` field names the successor. |

---

## Numbering and File Naming

RFCs are numbered sequentially starting from `0001`. The template is `0000`.

File names follow the pattern:

```
docs/rfcs/RFC-NNNN-short-title.md
```

- `NNNN` — zero-padded four-digit decimal number (e.g., `0003`)
- `short-title` — lowercase, hyphen-separated slug (e.g., `vm-isolation-policy`)

The title slug must be stable once the RFC is proposed; renaming a file after
the PR is open breaks external references.

---

## How to Submit an RFC

1. **Choose a number.** Look at the highest existing RFC number and increment
   by one.

2. **Copy the template.**
   ```sh
   cp docs/rfcs/RFC-0000-template.md docs/rfcs/RFC-NNNN-your-title.md
   ```

3. **Fill in the RFC.** See the template for required sections. Set
   `Status: Draft` while writing.

4. **Open a Pull Request.** Change `Status` to `Proposed`. The `rfc-lint` CI
   job automatically validates the RFC format.

5. **Iterate.** Address review comments. When consensus is reached, a
   maintainer merges the PR and the RFC status is updated to `Accepted`.

6. **CI creates issues.** The `rfc-issue-sync` job parses the
   **Implementation Checklist** and opens a GitHub Issue for each unchecked
   item. Agents and contributors pick up issues from the `rfc-task` label.

7. **Close issues as work lands.** When an issue is resolved, close it
   **and** mark the corresponding checklist item `[x]` in the RFC file.

---

## Implementation Checklist Format

Every RFC with status `Proposed` or `Accepted` **must** include an
`## Implementation Checklist` section. Items in this section are parsed
automatically by the `rfc-issue-sync` CI job and converted into GitHub Issues.

### Item format

```markdown
- [ ] `[phase:N]` `[component:name]` Short description of the task
- [ ] `[phase:N]` `[layer:LN]` `[component:name]` Short description (verification tasks)
- [x] `[phase:N]` `[component:name]` Already completed task
```

Each item consists of:

| Part | Required | Format | Description |
|------|----------|--------|-------------|
| Checkbox | Yes | `- [ ]` or `- [x]` | Open or completed |
| Phase | Yes | `` `[phase:N]` `` where N ∈ 0–5 | UNHOX milestone phase |
| Layer | For verification RFCs | `` `[layer:LN]` `` where N ∈ 1–4 | Verification layer (see RFC-0002) |
| Component | Yes | `` `[component:name]` `` | Kernel subsystem or area |
| Description | Yes | Plain text, ≤ 80 characters | What needs to be done |

**Valid component values:** `ipc`, `vm`, `sched`, `platform`, `bootstrap`,
`bsd`, `vfs`, `device`, `network`, `framework`, `docs`, `ci`

**Valid layer values (RFC-0002):**

| Layer | Meaning |
|-------|---------|
| `L1` | Unit and integration tests |
| `L2` | Property-based testing (Hypothesis / QuickCheck) |
| `L3` | Model checking (TLA+, SPIN) and fuzz testing |
| `L4` | Mechanised proof (Isabelle/HOL, Lean 4) |

### Example checklist

```markdown
## Implementation Checklist

- [ ] `[phase:1]` `[component:ipc]` Write `tests/unit/ipc/test_ipc_space.c` — alloc/lookup/free
- [ ] `[phase:1]` `[component:ipc]` Write `tests/unit/ipc/test_ipc_port.c` — port lifecycle
- [ ] `[phase:2]` `[layer:L2]` `[component:ipc]` Add Hypothesis property tests for IPC invariants
- [x] `[phase:1]` `[component:ci]` Add QEMU boot test to CI
```

Checked items (`- [x]`) are skipped by `rfc-issue-sync`; they are preserved
in the RFC for historical completeness.

---

## CI Integration

### `rfc-lint` (on pull request)

Triggered on every PR that modifies a file matching `docs/rfcs/RFC-[0-9]*.md`.

Checks performed:

- RFC number in the title matches the filename.
- `Status` field is present and contains a valid value.
- Required sections (`## Summary`, `## Motivation`) are present.
- For `Proposed` and `Accepted` RFCs, `## Implementation Checklist` is present
  and all items follow the tagged format.

### `rfc-issue-sync` (on push to `main`)

Triggered on every push to `main` that modifies a file matching
`docs/rfcs/RFC-[0-9]*.md`.

Behaviour:

1. Reads the RFC file and checks whether `Status` is `Accepted`.
2. If not `Accepted`, skips the file (no issues are created for Draft or
   Proposed RFCs).
3. Parses the `## Implementation Checklist` section.
4. For each **unchecked** item (`- [ ]`), searches for an existing issue
   titled `[RFC-NNNN] <description>`.
5. If no such issue exists, creates one with:
   - Labels: `rfc-task`, `phase:N`, `component:name` (and `layer:LN` if set).
   - Body: link to the RFC, phase/component metadata, and the task description.
6. Already-existing issues are skipped (idempotent; safe to re-run).

---

## Helper Scripts

| Script | Purpose |
|--------|---------|
| `.github/scripts/rfc_lint.py` | RFC format validator (called by `rfc-lint` job) |
| `.github/scripts/rfc_sync_issues.py` | Checklist → GitHub Issues sync (called by `rfc-issue-sync` job) |

Both scripts can be run locally:

```sh
# Lint all RFC files
python3 .github/scripts/rfc_lint.py docs/rfcs/RFC-*.md

# Sync a specific RFC (requires GH_TOKEN and gh CLI)
GH_TOKEN=ghp_... python3 .github/scripts/rfc_sync_issues.py \
    docs/rfcs/RFC-0002-kernel-verification.md
```

---

## File Structure

```
docs/rfcs/
├── README.md                              ← This file (RFC process)
├── RFC-0000-template.md                   ← Copy this to start a new RFC
├── RFC-0001-ipc-message-format.md         ← Accepted
└── RFC-0002-kernel-verification.md        ← Draft
```
