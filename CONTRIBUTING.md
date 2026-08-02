# Contributing to Crux64

Thanks for your interest in contributing! Crux64 is a small (but mighty!) ;-P ^_^ N64 game built with libdragon and tiny3d. This document explains how to report issues, propose changes, and submit pull requests so contributions are easy to review and land.

Table of contents
- Quick start (clone & build)
- Reporting bugs
- Proposing features
- Development workflow (branches, commits, PRs)
- Build & test instructions
- Coding style & guidelines
- Assets, audio, and licensing
- Pull request checklist
- Code of conduct & communication

---

## Quick start (clone & build)
To work on the project locally:

1. Clone the repository and initialize submodules:
   ```bash
   git clone https://github.com/Cyd0n1a/Crux64.git
   cd Crux64
   git submodule update --init --recursive
   ```

2. Make sure Docker is running (build uses a Docker image).
3. Build the ROM:
   ```bash
   ./build.sh
   ```
   The built ROM is `game.z64` in the repository root.

Notes:
- The first build will pull the Docker image `ghcr.io/dragonminded/libdragon:latest` and may take a few minutes.
- If you run into stale build artifacts, run:
  ```bash
  make clean
  ./build.sh
  ```

---

## Reporting bugs
When opening an issue, please include:
- Clear title and short description
- Steps to reproduce
- What you expected vs what happened
- Build commit hash / commit OID used to build the ROM
- Emulator used and version (e.g., Ares, Mupen64Plus), and platform (Windows/macOS/Linux)
- Screenshots, video, or a small GIF when helpful
- Any relevant logs or console output

A minimal bug report example:
- Title: "Player cursor stuck after placing wall"
- Steps: 1) Build at commit abc123; 2) Start ROM in Ares 0.8.1; 3) Press A to place wall; 4) Cursor stops responding.
- Expected: Cursor continues to respond after wall placement.
- Actual: Cursor freezes.

If the issue is security-sensitive, please do not open a public issue — contact the maintainers instead.

---

## Proposing features
For larger changes or new features, open an issue first describing:
- Motivation and user-visible behavior
- Proposed design or implementation sketch
- Any trade-offs or alternatives considered

This helps maintainers and other contributors give feedback before you implement the change.

---

## Development workflow
Preferred flow:
1. Fork the repository and create a feature branch:
   - Branch naming: `feature/<short-desc>`, `fix/<short-desc>`, or `docs/<short-desc>`
   ```bash
   git checkout -b feature/wireframe-glow
   ```
2. Make focused commits with clear messages. Keep PRs small and focused.
3. Run the build and smoke tests locally (see Build & test).
4. Push your branch and open a Pull Request (PR) against `master` (or the repository's default branch).

Commit message guidance:
- Short subject line (max ~72 characters)
- Optional body that explains why the change was made
- If applicable, reference an issue: `Fixes #123`

Sign-offs and licensing:
- Contributions are accepted under the repository license (GPLv2). See LICENSE.md.
- If you want, include a Signed-off-by line in the commit (DCO style): `Signed-off-by: Your Name <you@example.com>`

---

## Build & test instructions
- Build with Docker using `./build.sh`. The script pulls the libdragon image, builds tiny3d, compiles the sources, and produces `game.z64`.
- Always run `make clean` before building if you've made significant changes or suspect stale objects.
- After building, run `game.z64` in an emulator such as Ares, Gopher64, or Mupen64Plus to verify:
  - Title screen appears (for example, 3D sphere + music)
  - Gameplay starts when pressing Start/A
  - Cursor and wall placement behaviour.
  - Audio (XM module and/or MP3/Wav64) plays if the emulator supports it.

Testing tips:
- If a bug only appears on real hardware, provide as much emulator-based reproduction info as possible.
- Include the full `build` output when reporting build failures.

---

## Coding style & guidelines
- Language: C (MIPS/N64 target). Follow the existing project style for indentation and bracing. If you change style, do it consistently across the affected files.
- Keep changes small and well-documented.
- Use clear variable and function names; add comments for non-obvious behavior (e.g., flood-fill logic in field.c).
- Avoid large binary blobs in commits. Add audio/sound or other large assets via the assets directory only with appropriate licensing and justification.

If you want an automatic formatter, propose it in an issue first so the project can decide on exact rules (e.g., clang-format).

---

## Assets, audio, and licensing
- This project is distributed under the GPL v2 (see LICENSE.md). Any contributed code or assets must be compatible with GPL v2.
- If you add third-party libraries or assets, include information about their licenses and confirm GPL compatibility.

---

## Pull request checklist
When you open a PR, please ensure:
- [ ] The branch is up to date with the default branch.
- [ ] The build completes locally with `./build.sh` (or CI if available).
- [ ] A clear description of what the change does and why.
- [ ] Relevant issues linked (e.g., `Fixes #123`).
- [ ] Small, focused changes (split larger work into multiple PRs).
- [ ] Tests or manual verification steps included (how to reproduce/verify).
- [ ] Any new or modified assets include licensing information.

---

## CI and automated checks (suggestion)
Consider adding a GitHub Actions workflow to:
- Run a build inside Docker on push/PR to smoke-check that the project builds.
- Optionally run static analysis (e.g., clang-tidy) if cross-compilation tooling is available.

---

## Code of conduct & communication
Please follow a respectful and collaborative tone when interacting in issues, PRs, and comments. If you don’t already have one, consider adding a CODE_OF_CONDUCT.md (for example, the Contributor Covenant). Link to it from this file.

---

## Maintainers
If a PR does not get a response within a reasonable time, ping a maintainer or open an issue to ask for a review. Maintainers reserve the right to request changes or close PRs that are out-of-scope or conflict with project goals.

---

Thank you for contributing to Crux64 — your help makes the game better for everyone!
