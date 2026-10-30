#!/usr/bin/env python3
"""Portable CLI tests using synthetic, unusable registration values."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class RegistryCLITests(unittest.TestCase):
    def test_windows_export_and_no_overwrite_or_value_logging(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root / 'owned.reg'; output = root / 'halo-vision-registry.txt'
            text = ('Windows Registry Editor Version 5.00\r\n'
                    '[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft Games\\Halo]\r\n'
                    '"PID"="TEST-ONLY"\r\n"DigitalProductID"=hex:' + ','.join(f'{n:02x}' for n in range(1, 33)) + '\r\n')
            source.write_bytes(text.encode('utf-16'))
            command = [sys.executable, str(Path(__file__).with_name('prepare_sideload_registry.py')),
                       '--registry', str(source), '--output', str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('TEST-ONLY', result.stdout + result.stderr)
            seed = output.read_bytes()
            self.assertIn(b'ExitFlag|1|clean', seed)
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 2)
            self.assertEqual(output.read_bytes(), seed)

    def test_bad_registration_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root / 'bad.reg'; output = root / 'out.txt'
            source.write_text('not a Halo export')
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('prepare_sideload_registry.py')),
                                     '--registry', str(source), '--output', str(output)], capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertFalse(output.exists())


if __name__ == '__main__': unittest.main()
