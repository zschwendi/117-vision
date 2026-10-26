# Working on 117

Read `docs/AGENT_SETUP.md` and `docs/SETUP.md` for setup. Begin with `./setup.sh --stage check --json`.

- This project's repository is `https://github.com/zschwendi/117-vision`. Its upstream foundation is `https://github.com/mitchaiet/master-chef`; preserve the MIT license and attribution.
- Preserve existing game data, saves, bundle identifiers, signing choices and unrelated work. Local setup/build outputs belong in ignored directories.
- The user supplies their own supported retail Halo PC installation and valid registration. Do not request private keys/registration in chat or invent installation values.
- Keep generated translated game code, private registration, saved profiles and signing material out of source releases. Follow `docs/RELEASING.md` for an authorized publication.
- Run checks appropriate to the change. Setup changes use `python3 tools/test_setup_halo.py`; source checks are in `tools/run_source_checks.py`.
- Separate source/build/signing/install proof from physical gameplay acceptance. Never launch a device app when the user requested install only or explicitly said not to launch.
