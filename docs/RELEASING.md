# Publishing 117 source

The publication target is `zschwendi/117-vision`. The upstream foundation remains `mitchaiet/master-chef`; do not push this project's releases to upstream without separate authorization.

1. Freeze the source revision, version and build number. Preserve original licenses and attribution.
2. Run relevant native, Swift, Metal and setup checks. Validate owned-input build routes. Report device acceptance separately.
3. Export only the reviewed source manifest to a clean directory. Exclude development history, `.setup`, generated game code, game assets, registration, saves, diagnostic reports and signing material.
4. Run `python3 tools/check_repository_hygiene.py --strict --root <export>` and review the exact outgoing diff and paths. Preserve the scanner; fix findings rather than broadly exempting files.
5. Commit only that export into the standalone public repository. Verify the remote commit/tree and download hash after publication.

This release publishes source. Do not upload the developer's signed app or private game installation. Any future binary or asset release requires its own exact manifest, licensing/privacy review, and separate verification.
