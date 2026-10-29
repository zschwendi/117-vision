#!/usr/bin/env python3
"""Audit release source paths and text; never print suspected secret values.

Use --strict for a distribution tree, including normally ignored artifacts.
This is a layered guard, not a guarantee that every possible secret is detected.
"""
import argparse
import hashlib
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SKIP = {'.git', '.build', '.setup', '__pycache__', '.venv', 'game', 'build', 'logs', 'dist', 'DerivedData'}
# Explicitly reviewed documentation imagery, never a general binary allowlist.
# Includes generated historical art and recorded gameplay screenshots. Any image
# change requires visual review and an updated digest at its exact path.
DOCUMENTATION_ART = {
    'docs/assets/117-sense-grenade-throw.png': '412cfb28c1a0a09130dc87fce0203315f06b1465313e0e23daadd9bda1804ce8',
    'docs/assets/117-sense-rifle-combat.png': '0667a42b8c750001c83a0d0d738f14097de6495440ef83be5fc3c6e87fef06d9',
    'docs/assets/117-sense-sniper-scope.png': '3f226f1c2d551b6176bfecc5bc6ad182c2ca85f5a4688449d687a6c74e4dd114',
    'docs/assets/master-chef-header.png': 'e15bc1102ca36f40e357c39d48149445f94115971a9469406f7e67c9f442a386',
}
# Original generated app artwork is also admitted only at its reviewed path and
# exact digest. Other image files remain subject to the binary-file guard.
APP_ICON_ART = {'native/EnginePad/Resources/Assets.xcassets/AppIcon.appiconset/117-plasma.png': '1f62a7f9210cdfc751d4700f63f9124e9f554796e92aefd1e20fee2261ea99d8', 'native/EngineVision/Resources/Assets.xcassets/AppIcon.solidimagestack/Front.solidimagestacklayer/Content.imageset/117-plasma-foreground.png': 'f12ddfea20dcca2fa350d75d5ed2ae76a4fffbcdfd2b8f2357558b90072dab9f', 'native/EngineVision/Resources/Assets.xcassets/AppIcon.solidimagestack/Back.solidimagestacklayer/Content.imageset/117-plasma-background.png': '1f62a7f9210cdfc751d4700f63f9124e9f554796e92aefd1e20fee2261ea99d8'}
BAD_SUFFIX = {'.exe', '.dll', '.map', '.iso', '.ipa', '.p12', '.p8', '.pfx', '.pem', '.key',
              '.mobileprovision', '.provisionprofile', '.o', '.a', '.dylib', '.so', '.pyc',
              '.zip', '.tpf', '.hvt', '.hvs', '.mc-save', '.bgra', '.mov', '.mp4', '.jsonl', '.log', '.reg'}
RULES = {
    'home-directory': re.compile(r'/(?:Users|home)/[^/\s"\']+'),
    'private-key': re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'),
    'github-token': re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b'),
    'cloud-key': re.compile(r'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
    'service-token': re.compile(r'\b(?:sk-(?:proj-)?[A-Za-z0-9_-]{30,}|xox[baprs]-[A-Za-z0-9-]{20,})\b'),
    'private-network': re.compile(r'\b(?:192\.168\.\d{1,3}\.\d{1,3}|10\.\d{1,3}\.\d{1,3}\.\d{1,3})\b'),
    'apple-device-id': re.compile(r'\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{16}\b'),
}

def audit(root, strict=False):
    failures=[]; count=0
    for p in sorted(root.rglob('*')):
        rel=p.relative_to(root)
        if '.git' in rel.parts:continue
        if not strict and any(part in SKIP for part in rel.parts):continue
        if p.is_symlink():failures.append((str(rel),0,'symbolic-link'));continue
        if not p.is_file():continue
        count+=1
        if (p.suffix.lower() in BAD_SUFFIX or p.name in {'.DS_Store','.env','halo-vision-registry.txt'}
            or any(part.endswith(('.app','.xcarchive','.dSYM')) for part in rel.parts)
            or any(part in {'assessment','local-agent-inputs','.context','.claude','.setup'} for part in rel.parts)):
            failures.append((str(rel),0,'non-source-artifact'))
        art = DOCUMENTATION_ART if rel.as_posix() in DOCUMENTATION_ART else APP_ICON_ART
        if rel.as_posix() in art:
            data=p.read_bytes()
            if not data.startswith(b'\x89PNG\r\n\x1a\n') or hashlib.sha256(data).hexdigest()!=art[rel.as_posix()]:
                kind='unreviewed-documentation-art' if art is DOCUMENTATION_ART else 'unreviewed-app-icon-art'
                failures.append((str(rel),0,kind))
            continue
        try:text=p.read_text(encoding='utf-8')
        except UnicodeError:
            failures.append((str(rel),0,'binary-file'));continue
        for n,line in enumerate(text.splitlines(),1):
            for kind,pattern in RULES.items():
                if pattern.search(line):failures.append((str(rel),n,kind))
    return count,failures

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--strict',action='store_true')
    parser.add_argument('--root',type=Path,default=ROOT);args=parser.parse_args()
    count,failures=audit(args.root.resolve(),args.strict)
    for path,line,kind in failures:print(f'{path}:{line}: {kind}')
    print(f'{"FAIL" if failures else "PASS"}: {count} source files checked; {len(failures)} findings')
    return bool(failures)

if __name__=='__main__':sys.exit(main())
