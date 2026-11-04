# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual module dispatcher with independent synthetic contributions."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DriverModulesTests(unittest.TestCase):
    def test_discovery_tests_installation_and_exclusion_without_central_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / 'source'
            modules = source / 'src/drivers'
            (modules / 'generic').mkdir(parents=True)
            (modules / 'generic/CMakeLists.txt').write_text('')
            shutil.copyfile(ROOT / 'src/drivers/CMakeLists.txt', modules / 'CMakeLists.txt')
            (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.25)
project(driver_module_contract LANGUAGES CXX)
include(CTest)
set(KDE_INSTALL_QTPLUGINDIR lib/qt6/plugins)
add_subdirectory(src/drivers)
''')
            for name in ('first', 'second'):
                module = modules / name
                module.mkdir()
                (module / 'driver.cpp').write_text('extern "C" int fixture() { return 0; }\n')
                (module / 'layout.json').write_text('{}\n')
                (module / 'CMakeLists.txt').write_text(f'''add_library({name} MODULE driver.cpp)
klighting_register_driver({name})
install(FILES layout.json DESTINATION share/klighting/drivers/{name})
if(BUILD_TESTING)
    add_test(NAME {name}_contract COMMAND "${{CMAKE_COMMAND}}" -E true)
endif()
''')
            for disabled in ('', 'second'):
                build = base / ('build-' + (disabled or 'all'))
                stage = base / ('stage-' + (disabled or 'all'))
                commands = [
                    ['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                     '-DKLIGHTING_DISABLED_DRIVERS=' + disabled],
                    ['cmake', '--build', str(build), '--target', 'klighting_drivers'],
                    ['ctest', '--test-dir', str(build), '--output-on-failure'],
                    ['cmake', '--install', str(build), '--prefix', str(stage)],
                ]
                for command in commands:
                    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout)
                tests = subprocess.check_output(['ctest', '--test-dir', str(build), '--show-only'], text=True)
                for name in ('first', 'second'):
                    expected = name != disabled
                    self.assertEqual((f'{name}_contract' in tests), expected)
                    self.assertEqual((stage / f'lib/qt6/plugins/ambient-led/drivers/v4/{name}.so').is_file(), expected)
                    self.assertEqual((stage / f'share/klighting/drivers/{name}/layout.json').is_file(), expected)


if __name__ == '__main__':
    unittest.main()
