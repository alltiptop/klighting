# SPDX-License-Identifier: GPL-3.0-or-later
"""Local packaging checks; all destinations are temporary directories."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installer', ROOT / 'tools/install_local.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.bundle = self.base / 'bundle'
        self.bundle.mkdir()
        self.destination = self.base / 'system'
        self.plugin = Path('usr/lib/qt6/plugins/ambient-led/drivers/v4/example.so')
        self.asset = Path('usr/share/klighting/drivers/example/layout.json')
        self.write('bundle.json', json.dumps({'version': 1, 'plugins': 'usr/lib/qt6/plugins', 'data': 'usr/share'}))
        self.write('klighting-plugin-check', 'checker')
        self.write('root/' + str(self.plugin), 'plugin')
        self.write('root/' + str(self.asset), 'layout')
        self.seal()

    def write(self, path, text):
        target = self.bundle / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)

    def seal(self):
        lines = []
        for path in sorted(self.bundle.rglob('*')):
            if path.is_file() and path.name != 'SHA256SUMS':
                lines.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(self.bundle)}\n')
        (self.bundle / 'SHA256SUMS').write_text(''.join(lines))

    def test_arbitrary_module_assets_and_path_qualified_backups(self):
        payload, metadata = installer.verified_bundle(self.bundle)
        target = self.destination / self.plugin
        target.parent.mkdir(parents=True)
        target.write_text('old library')
        # An existing mapped/open file must retain its original bytes.
        with target.open() as old:
            backup = installer.install(self.bundle, payload, [], self.destination)
            self.assertEqual(old.read(), 'old library')
        self.assertEqual(target.read_text(), 'plugin')
        self.assertEqual((backup / self.plugin).read_text(), 'old library')
        self.assertEqual((self.destination / self.asset).read_text(), 'layout')
        self.assertEqual(metadata['plugins'], Path('usr/lib/qt6/plugins'))

    def test_rejects_missing_unlisted_and_corrupt_payload(self):
        for kind in ('missing', 'unlisted', 'corrupt'):
            with self.subTest(kind=kind):
                path = self.bundle / 'root' / self.asset
                path.write_text('layout')
                self.seal()
                if kind == 'missing':
                    path.unlink()
                elif kind == 'unlisted':
                    self.write('unexpected', 'extra')
                else:
                    path.write_text('changed')
                with self.assertRaises(ValueError):
                    installer.verified_bundle(self.bundle)
                (self.bundle / 'unexpected').unlink(missing_ok=True)

    def test_rejects_path_escape_duplicates_and_symlinks(self):
        manifest = self.bundle / 'SHA256SUMS'
        for path in ('../outside', '/absolute', 'root/../escape', './root/file'):
            with self.subTest(path=path):
                manifest.write_text('0' * 64 + '  ' + path + '\n')
                with self.assertRaises(ValueError):
                    installer.verified_bundle(self.bundle)
        self.seal()
        manifest.write_text(manifest.read_text() * 2)
        with self.assertRaises(ValueError):
            installer.verified_bundle(self.bundle)
        self.seal()
        (self.bundle / 'linked').symlink_to(self.bundle / 'bundle.json')
        with self.assertRaises(ValueError):
            installer.verified_bundle(self.bundle)

    def test_module_owned_aliases_preserve_other_modules_and_current_plugin(self):
        self.write('root/usr/share/klighting/drivers/example/legacy-plugins.txt', 'v3/example\nv4/example\n')
        self.seal()
        payload, metadata = installer.verified_bundle(self.bundle)
        plugin_root = self.destination / metadata['plugins'] / 'ambient-led/drivers'
        for name in ('v3/example.so', 'v4/example_dev2.so', 'v4/example_other.so', 'v4/independent.so', 'v4/example.so'):
            target = plugin_root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(name)
        old = installer.obsolete_plugins(self.bundle, payload, metadata, self.destination)
        self.assertEqual([str(path.relative_to(metadata['plugins'])) for path in old],
                         ['ambient-led/drivers/v3/example.so', 'ambient-led/drivers/v4/example_dev2.so'])
        backup = installer.install(self.bundle, payload, old, self.destination)
        self.assertTrue((plugin_root / 'v4/independent.so').exists())
        self.assertTrue((plugin_root / 'v4/example_other.so').exists())
        for path in old:
            self.assertFalse((self.destination / path).exists())
            self.assertTrue((backup / path).exists())

    def test_invalid_alias_fails_before_changes(self):
        self.write('root/usr/share/klighting/drivers/example/legacy-plugins.txt', 'v4/../../other\n')
        self.seal()
        payload, metadata = installer.verified_bundle(self.bundle)
        with self.assertRaises(ValueError):
            installer.obsolete_plugins(self.bundle, payload, metadata, self.destination)
        self.assertFalse(self.destination.exists())

    def test_staging_failure_leaves_installed_file_intact(self):
        payload, _ = installer.verified_bundle(self.bundle)
        target = self.destination / self.plugin
        target.parent.mkdir(parents=True)
        target.write_text('old library')
        copyfile = installer.shutil.copyfile
        def fail_staging(source, destination, *args, **kwargs):
            if Path(destination).name.startswith('.klighting-install.'):
                raise OSError('write failed')
            return copyfile(source, destination, *args, **kwargs)
        with patch.object(installer.shutil, 'copyfile', side_effect=fail_staging):
            with self.assertRaises(OSError):
                installer.install(self.bundle, payload, [], self.destination)
        self.assertEqual(target.read_text(), 'old library')
        self.assertFalse(list(self.destination.rglob('.klighting-install.*')))


if __name__ == '__main__':
    unittest.main()
