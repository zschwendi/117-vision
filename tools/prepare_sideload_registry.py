#!/usr/bin/env python3
"""Convert your own Halo registry export for the on-device folder importer."""
import argparse
import os
from pathlib import Path

from halo_setup_registry import RegistryError, read_registry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--registry', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        seed = read_registry(args.registry)
        # Never overwrite an existing registration or print product values.
        fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as stream:
            stream.write(seed)
    except (OSError, RegistryError) as error:
        parser.exit(2, 'Registry preparation failed: ' + str(error) + '\n')
    print('Prepared private Halo registration. Keep this file with your own game folder; do not upload it.')


if __name__ == '__main__':
    main()
