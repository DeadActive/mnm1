"""The site accepts MNM-1's longer identity without dropping loader validation."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'web'))
import make_site


class SiteIdentityTest(unittest.TestCase):
    def check_package(self, identity, loader=True):
        raw = bytearray(960)
        for i in range(20):
            raw[i * 48 + 47] = ord(identity[i]) + i + 1 if i < len(identity) else 0x7D
        if loader:
            raw += b'FELUCCA-LOADER-1'
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'test.fwsc'
            p.write_bytes(raw)
            return make_site.checked(p)[1]

    def test_mnm_and_legacy_identities(self):
        for identity in ('FM-1_61000100', 'FM-1_900'):
            self.assertEqual(self.check_package(identity), identity)

    def test_invalid_identity_or_missing_loader(self):
        for identity, loader in (('FM-2_61000100', True), ('FM-1_wrong', True),
                                 ('FM-1_', True), ('FM-1_61000100', False)):
            with self.subTest(identity=identity, loader=loader), self.assertRaises(SystemExit):
                self.check_package(identity, loader)


if __name__ == '__main__':
    unittest.main()
