# README Release Notices Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add prominent bilingual README notices requiring the M5Launcher Beta Release and recommending Type4Me.

**Architecture:** This is a documentation-only change. Add equivalent bold notice blocks immediately below the current release line in the English and Chinese READMEs, then verify wording, link identity, Markdown structure, and tracked-file scope.

**Tech Stack:** GitHub Flavored Markdown, Git

## Global Constraints

- M5Launcher users must install the Beta Release.
- Recommend [Type4Me](https://github.com/joewongjc/type4me) as a companion.
- Add both notices to `README.md` and `README.zh-CN.md`.
- Keep both notices bold and near the top of each README.
- Do not modify firmware, installers, release artifacts, or runtime behavior.

---

### Task 1: Add and verify bilingual release notices

**Files:**
- Modify: `README.md`
- Modify: `README.zh-CN.md`
- Create: `docs/superpowers/plans/2026-07-29-readme-release-notices.md`

**Interfaces:**
- Consumes: Current release line in each README.
- Produces: Two matching bold Markdown notices in each README.

- [ ] **Step 1: Verify the notices are not already present**

Run:

```bash
rg -n 'Beta Release|Type4Me|Beta 版本' README.md README.zh-CN.md
```

Expected: no existing notice block containing both requirements.

- [ ] **Step 2: Add the English notice block**

Immediately below the current release line in `README.md`, add:

```markdown
**M5Launcher users must install the Beta Release.**

**Recommended companion: [Type4Me](https://github.com/joewongjc/type4me).**
```

- [ ] **Step 3: Add the Chinese notice block**

Immediately below the current release line in `README.zh-CN.md`, add:

```markdown
**使用 M5Launcher 时必须安装 Beta Release。**

**建议与 [Type4Me](https://github.com/joewongjc/type4me) 联动使用。**
```

- [ ] **Step 4: Verify content, links, and Markdown**

Run:

```bash
python3 - <<'PY'
from pathlib import Path

expected = {
    "README.md": [
        "**M5Launcher users must install the Beta Release.**",
        "**Recommended companion: [Type4Me](https://github.com/joewongjc/type4me).**",
    ],
    "README.zh-CN.md": [
        "**使用 M5Launcher 时必须安装 Beta Release。**",
        "**建议与 [Type4Me](https://github.com/joewongjc/type4me) 联动使用。**",
    ],
}
for name, notices in expected.items():
    text = Path(name).read_text()
    for notice in notices:
        assert text.count(notice) == 1, (name, notice)
print("README notices verified")
PY
git diff --check
```

Expected: `README notices verified` and no `git diff --check` errors.

- [ ] **Step 5: Confirm tracked-file scope**

Run:

```bash
git status --short
```

Expected: only `README.md`, `README.zh-CN.md`, and this plan are changed.

- [ ] **Step 6: Commit and push**

Run:

```bash
git add README.md README.zh-CN.md docs/superpowers/plans/2026-07-29-readme-release-notices.md
git commit -m "docs: highlight launcher beta and Type4Me"
git push origin main
```

Expected: commit succeeds and `main` is updated on `origin`.
