# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts' / 'prepare_ci_context.py'
spec = importlib.util.spec_from_file_location('prepare_ci_context', SCRIPT)
context = importlib.util.module_from_spec(spec)
spec.loader.exec_module(context)


class CiContextTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.base = Path(directory.name).resolve()
        self.source = self.base / 'source'
        self.image = self.source / '.github' / 'ci'
        self.image.mkdir(parents=True)
        (self.image / 'Dockerfile').write_text('FROM example@sha256:fixture\n')
        (self.image / 'install.sh').write_text('exit 0\n')
        (self.image / 'packages.arch').write_text('gcc\ncmake\n')
        self.driver = self.source / 'src' / 'drivers' / 'example'
        self.driver.mkdir(parents=True)
        (self.driver / 'dependencies.arch').write_text('libusb\ncmake\n')

    def test_context_contains_only_image_inputs_and_driver_packages(self):
        (self.source / '.env').write_text('private local settings\n')
        (self.source / '.git').mkdir()
        (self.source / '.git' / 'config').write_text('private metadata\n')
        (self.driver / 'driver.cpp').write_text('driver source\n')
        target = self.base / 'context'
        digest = context.prepare_context(self.source, target)
        self.assertEqual(set(p.name for p in target.iterdir()), {'Dockerfile', 'install.sh', 'packages.arch'})
        self.assertEqual((target / 'packages.arch').read_text(), 'cmake\ngcc\nlibusb\n')
        self.assertRegex(digest, r'^[a-f0-9]{64}$')

    def test_only_dependency_changes_invalidate_the_image(self):
        original = context.prepare_context(self.source, self.base / 'first')
        (self.driver / 'driver.cpp').write_text('changed implementation\n')
        self.assertEqual(context.prepare_context(self.source, self.base / 'second'), original)
        added = self.driver.parent / 'another-device'
        added.mkdir()
        (added / 'dependencies.arch').write_text('hidapi\n')
        changed = context.prepare_context(self.source, self.base / 'third')
        self.assertNotEqual(changed, original)
        self.assertIn('hidapi\n', (self.base / 'third' / 'packages.arch').read_text())

    def test_invalid_package_names_fail_before_writing(self):
        for text in ('', 'gcc\n\n', '--noconfirm\n', 'gcc; echo example\n', 'package name\n'):
            with self.subTest(text=text):
                (self.driver / 'dependencies.arch').write_text(text)
                target = self.base / 'context'
                with self.assertRaises(ValueError):
                    context.prepare_context(self.source, target)
                self.assertFalse(target.exists())

    def test_symlinked_manifest_is_rejected(self):
        manifest = self.driver / 'dependencies.arch'
        manifest.unlink()
        manifest.symlink_to(self.image / 'packages.arch')
        with self.assertRaises(ValueError):
            context.prepare_context(self.source, self.base / 'context')

    def test_existing_context_is_not_overwritten_or_extended(self):
        target = self.base / 'context'
        target.mkdir()
        (target / 'unrelated.txt').write_text('keep\n')
        with self.assertRaises(FileExistsError):
            context.prepare_context(self.source, target)
        self.assertEqual(list(target.iterdir()), [target / 'unrelated.txt'])


if __name__ == '__main__':
    unittest.main()
