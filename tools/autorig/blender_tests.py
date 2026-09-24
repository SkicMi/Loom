"""Run through blender --background --factory-startup --python-exit-code 1 --python this_file."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_autorig import DeformationTests

result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(DeformationTests))
if not result.wasSuccessful():
    raise RuntimeError("Blender deformation tests failed")
