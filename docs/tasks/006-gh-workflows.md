# Add GitHub Workflows, 2026-02-21

We need to add three GitHub workflows. 

- build-and-test.yml
- release.yml
- release-check.yml

## Build-and-Test

This workflow is triggered on push, pull-request, manually AND on a schedule.

- Push and pull request are normal and obvious
- On workflow-dispatch I find useful for debugging
- And on a weekly schedule to provide relatively early warning of bit-rot of the workflow.

The workflow verifies that the application can build and that the functional tests pass.
Builds will use the -Werror flag, which is passed as an extra CFLAGS override in CI only.

## Release

- This workflow is triggered on a tag-push which matches `v*.*.*`.
- If the git-tag matches `v*.*.*-TEXT` then it is marked as a prerelease.
- The workflow can be triggered manually too, with the git-tag defaulting to `v0.0.0`.
- The text for the relevant release is snipped out of the CHANGELOG
  - Skipped when the tag is `v0.0.0` (dry-run / debugging mode).
  - Otherwise the first level2 (##) heading must exist and have a matching version.
  - The version can appear anywhere inside the title string.
  - It is inserted as the release text.
- herescript is built with the -Werror flag on a platform matrix of ubuntu-latest and macos-latest.
- The functional tests are run on both platforms.
- The herescript executable is added as a release asset, with one asset per platform tagged accordingly.

## Release check

This workflow checks whether or not the branch is in a good state for a release.
It serves as a reminder to myself as to what I have not done.

- It is manually triggered.
- It checks that the top-section in the CHANGELOG.md matches a release or says 
  "Unreleased" (and if so fails)
- It verifies the build works and the tests pass
  - For a platform matrix of ubuntu-latest and macos-latest
  - A compiler matrix of GCC & Clang

## Finally

Review the [definition of done](../definition-of-done.md) before closing the task.