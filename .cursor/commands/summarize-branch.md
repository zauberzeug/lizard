---
description: Summarize
---

Read README.md, AGENTS.md, and CONTRIBUTING.md, then write a precise and concise PR description.
Lead with the motivation of the change, then explain what changed and design decisions made.
Also mention special assumptions or risks implied by the changes.

The summary should be in a ```markdown code block, so the user can copy and paste it into the PR description.
Ensure the summary length corresponds to the overall size of the PR (small PRs should be shorter than large PRs).
Adhere to the following format:

```markdown
### Motivation

<!-- What problem does this PR solve? Which new feature or improvement does it implement? -->
<!-- Please provide relevant links to corresponding issues and feature requests. -->

### Implementation

<!-- What is the concept behind the implementation? How does it work? -->
<!-- Include any important technical decisions or trade-offs made -->

### Progress

- [ ] I chose a meaningful title that completes the sentence: "If applied, this PR will..."
- [ ] The implementation is complete.
- [ ] Tested on target hardware (or not applicable).
- [ ] Documentation has been added (or is not necessary).
```

Also provide three suggestions for a PR title which completes the sentence "If applied, this PR will..."
