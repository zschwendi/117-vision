"""Run original translated memmove against libc using private verified PE tables."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from .upstream import PE, PE_SHA256, load_pe
from .decode import EngineDisassembler

ROOT = Path(__file__).resolve().parents[2]


class OriginalMemmoveTests(unittest.TestCase):
    def test_original_overlapping_copies(self):
        generated = Path(os.environ.get(
            'HALO_GENERATED_ENGINE', ROOT / 'native/build/engine-reuse/whole-exe'))
        if not PE.is_file() or not (generated / 'sub_006236F0.c').is_file():
            self.skipTest('Needs the owned verified executable and generated engine')
        self.assertEqual(hashlib.sha256(PE.read_bytes()).hexdigest(), PE_SHA256)
        info, data, _ = load_pe(str(PE))
        decoder = EngineDisassembler(data, info.image_base, info.sections)
        with tempfile.TemporaryDirectory(prefix='halo-memmove-test-') as temp:
            temp = Path(temp)
            tables = temp / 'original-memmove.bin'
            tables.write_bytes(decoder.read_bytes(0x6236f0, 0x340))
            binary = temp / 'test-memmove'
            flags = (['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
                     if os.environ.get('HALO_TEST_SANITIZE') == '1' else ['-O2'])
            subprocess.run([
                os.environ.get('CC', 'clang'), *flags, '-DENGINE_FLAT_MEMORY=1',
                '-I', str(ROOT / 'native/EngineReuse'), '-I', str(generated),
                str(ROOT / 'native/EngineHost/tests/test_memmove_overlap.c'),
                '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary), str(tables)], check=True, timeout=30)


if __name__ == '__main__':
    unittest.main()
