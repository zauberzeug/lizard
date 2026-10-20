---
name: release-notes
description: Write Lizard release notes for a milestone to tmp.md. Use when asked to draft release notes or a release announcement for a given milestone/version (e.g. "write release notes for 0.12.0").
---

Write release notes for the milestone passed as an argument (e.g. `0.12.0`) to `tmp.md`.

The milestone title is the version (e.g. `0.12.0`). If given only a milestone URL or number, resolve its title first: `gh api repos/zauberzeug/lizard/milestones/<n> --jq .title`.

## How to gather data

**Start with the helper script** [`release-notes.sh`](release-notes.sh) (in this skill's directory), which collects the deterministic data so nothing is forgotten:

- `release-notes.sh dossier "<milestone>"` → one JSON dossier: every milestone issue/PR with labels, author, committers, reviewers, commenters, and extracted cross-reference `#`s; each cross-reference resolved to issue/PR/**discussion** with its author (discussions are the easiest contributor to miss).
- `release-notes.sh verify "<milestone>" tmp.md` → milestone tickets missing from the draft (and `#`s in the draft that aren't milestone tickets — expected for cross-refs).
- `release-notes.sh check-docs <slug> [slug...]` → HTTP status per docs page, to verify links before using them.

The dossier gives you the raw facts; you still apply judgment for grouping tickets into stories, writing descriptions, and deciding which commenters are substantive. The manual `gh` recipes below remain valid as a fallback or for spot-checks.

1. Use `gh` to list all issues and PRs in the milestone with their labels, linked PRs, authors, and participants.
2. For each issue/PR, inspect the timeline and comments to identify contributors (see contributor rules below).
3. For each merged PR, also fetch its reviewers and committers explicitly — `gh pr view <n> --json reviews,commits` — and fold them into the contributor list. Comment scraping alone misses reviewers who only left a formal review. (Do not credit the merger; see Contributors.)
4. Resolve cross-referenced numbers that are **discussions**, not issues/PRs. `gh issue view <n>` / `gh pr view <n>` fail on a discussion number; query it via GraphQL instead, e.g. `gh api graphql -f query='{repository(owner:"zauberzeug",name:"lizard"){discussion(number:N){title author{login}}}}'`, and credit the discussion author.
5. Check if any PRs reference a GHSA (GitHub Security Advisory) — these go into a separate Security section.

## Structure

Follow the exact structure used in prior releases at https://github.com/zauberzeug/lizard/releases (curated sections with `###` headings and one item per story — not GitHub's auto-generated "What's Changed" list).

### Section mapping (by label)

| Label / nature     | Section heading               |
| ------------------ | ----------------------------- |
| (GHSA-based PRs)   | Security                      |
| `enhancement`      | New features and enhancements |
| `bug`              | Bugfix                        |
| `documentation`    | Documentation                 |
| (CI/tooling/build) | Infrastructure                |

Lizard has no `feature`, `infrastructure`, `testing`, or `dependencies` labels, so use judgment: CI, build, pre-commit, and packaging work goes under **Infrastructure** even when it carries the `enhancement` label. Items double-labelled `enhancement`+`documentation` usually belong under whichever is the dominant change. If something doesn't fit, create an appropriate section following the established pattern.

Security section (if any) always comes first. Then the remaining sections in the order listed above.
Security items start with a "⚠️" prefix (e.g. `⚠️ Prevent memory exhaustion via media streaming routes`).

### Documentation links

When an item mentions a DSL element or concept that has a documentation page, you may link it, e.g. [`bus.send`](https://lizard.dev/module_reference/) — prior Lizard releases mostly omit doc links, so keep this light and only add a link when it clearly helps.

**Important:** Do not guess URLs. Verify each link exists by fetching it (the `check-docs` helper does this in bulk). Lizard docs pages live at `https://lizard.dev/<slug>/` (trailing slash; `/<slug>` 301-redirects to it).
If a page doesn't exist yet (e.g. for something added in this release), ask the user whether to include the link anyway (it will go live after deployment).

### Item format

Each line follows this pattern:

```
- Short description of the change (#issue, #pr by @author1, @author2, @contributor3)
```

- One item per story (a story is usually an issue with a PR that fixes it, but may include follow-up fixes)

- Check PR descriptions for references to feature requests, discussions, or other issues that led to the change — include those ticket numbers too

- Group related tickets into a single item — all ticket numbers for the milestone should appear somewhere

- Ticket numbers go in parentheses: `(#123, #456 by @user1, @user2)`

- If a PR has a breaking change, add a `**Breaking change:**` block below the item, like this:

  ```
  - Short description of the change (#issue, #pr by @author1, @author2)

      **Breaking change:** Explanation of what changed and how to migrate.
      For example, if an API was removed, show the old usage and the new replacement.
  ```

### Contributors

For each item, mention all involved contributors in the `by @...` list:

- Issue author
- **The author of any feature request / discussion that led to the change** (often referenced in the PR description), plus anyone who contributed substantively to that discussion — not only the issue/PR participants
- Users with relevant contributions to discussions (substantive comments, reproductions, debugging - not just "me too" comments)
- **Reviewers** who provided relevant feedback — include both review submissions (`gh pr view <n> --json reviews`) **and** inline review comments, not just top-level issue/PR comments. A formal review with no top-level comment is easy to miss with comment scraping alone.
- Committers (anyone who pushed commits to the PR), ignoring AI/bot co-author logins such as `claude`

Do **not** credit the merger as such — merging a PR is not, on its own, a contribution worth listing.

**Make sure no contributor is forgotten** — when a borderline commenter is genuinely engaged (offered to help, attempted a fix, added environment detail), lean toward including them rather than filtering them out. Only ignore:

- Simple "me too" comments with no added information
- Review approvals without valuable comments or own commits

## Verification

After writing `tmp.md`, verify completeness — `release-notes.sh verify "<milestone>" tmp.md` does steps 1-3:

1. Use `gh` to list all issue and PR numbers in the milestone.
2. Extract all `#nnn` ticket numbers from the generated `tmp.md`.
3. Compare the two sets. Report any milestone tickets missing from the release notes.
4. If there are missing tickets, investigate them and either add them to the appropriate section or explain why they were excluded (e.g. duplicates, reverted, not actually resolved).

## Output & posting

- Write the draft to `tmp.md` in the project root (a leading `# Release notes for Lizard x.y.z` H1 is fine as a working title — strip it before posting to GitHub, where the tag/version is already the heading).
- Do not create, edit, or publish the GitHub release yourself. Point at `tmp.md` and let the user post it (e.g. by editing the release for the tag with `gh release edit v<x.y.z> --notes-file tmp.md` after confirmation).
