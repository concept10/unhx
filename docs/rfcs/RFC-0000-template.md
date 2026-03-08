# RFC-0000: Title

<!--
  Replace "0000" with the next available RFC number in the title above
  and in the filename (RFC-NNNN-your-slug.md).
  Replace "Title" with a short descriptive title.
  Remove all HTML comments before opening the PR.
  See docs/rfcs/README.md for the full RFC process.
-->

- **Status**: Draft
- **Author**: <!-- Your name or GitHub handle -->
- **Date**: <!-- YYYY-MM-DD -->
- **Phase**: <!-- Target UNHOX phase: 0–5 -->
- **Supersedes**: none
- **Superseded-by**: none

## Summary

<!--
  One paragraph. What does this RFC propose? Write as if the reader has
  never heard of this feature. Be concrete.
-->

## Motivation

<!--
  Why is this change needed? What problem does it solve?
  What happens if we do nothing?
-->

## Design

<!--
  The main body of the RFC. Describe the proposed design in enough detail
  that a contributor can implement it from this document alone.

  Include:
  - Data structures, API signatures, or wire formats (with code sketches)
  - Invariants the implementation must maintain
  - Interactions with other kernel subsystems
  - Diagrams or ASCII art where helpful
-->

## Alternatives Considered

<!--
  List at least one alternative approach. Explain why it was rejected.
  "No alternatives" is almost never the right answer.
-->

## Open Questions

<!--
  Issues that must be resolved before or during implementation.
  Number them so they can be referenced in review comments.

  1. Question one.
  2. Question two.
-->

## Implementation Checklist

<!--
  REQUIRED for Proposed and Accepted RFCs.
  Each unchecked item is automatically converted to a GitHub Issue by the
  rfc-issue-sync CI job when this RFC is merged as Accepted.

  FORMAT (one item per line):
    - [ ] `[phase:N]` `[component:name]` Short description (≤ 80 chars)
    - [ ] `[phase:N]` `[layer:LN]` `[component:name]` Short description

  phase   : 0–5  (UNHOX milestone phase)
  layer   : L1 (unit tests), L2 (property tests), L3 (model check/fuzz),
            L4 (mechanised proof)  — only for verification tasks
  component: ipc | vm | sched | platform | bootstrap | bsd | vfs |
             device | network | framework | docs | ci

  Mark completed items with [x]. Checked items are preserved for history
  but are not synced to new issues.
-->

- [ ] `[phase:N]` `[component:name]` First implementation task
- [ ] `[phase:N]` `[component:name]` Second implementation task

## References

<!--
  Links, papers, prior art, related RFCs.
  Use the format: - Author, "Title" (Year/URL)
-->
