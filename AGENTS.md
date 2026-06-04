# Agent Workflow

This repository uses a branch-first workflow for all future feature work.

## Branch Rules

- Do not commit new features directly to the default branch.
- Create a feature branch before starting any new feature.
- Branch names should be short and descriptive, for example:

```text
feature/compact-mode-settings
feature/tray-autostart
fix/click-through-recovery
```

## Merge Rules

- Merge completed feature branches back into the default branch after the work is implemented and verified.
- The current default branch is `main`.
- If the repository is later renamed to use `master`, treat `master` as the default branch and apply the same rules.

## Recommended Flow

```sh
git switch main
git pull
git switch -c feature/my-feature

# make changes, build, and test

git add .
git commit -m "Add my feature"
git switch main
git pull
git merge --no-ff feature/my-feature
git push
```

## Verification

Before merging, run the relevant build or test command. For the current Windows executable prototype:

```sh
./scripts/build-mingw.sh
```
