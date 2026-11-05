"""Tests of the fit using synthetic inputs, never used as measured calibration."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("calibrate_mass", Path(__file__).parents[1] / "tools/calibrate_mass.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MassFitTest(unittest.TestCase):
    def test_fit_both_polarities_and_unequal_durations(self):
        for counts_per_g in (-1100, 1100):
            samples = []
            windows = []
            for i, mass in enumerate((0, 20, 30, 35)):
                start = i * 20
                duration = i + 4
                windows.append((mass, start, start + duration))
                samples.extend((start + j / 10, 490000 + mass * counts_per_g + (j % 2 * 2 - 1)) for j in range(duration * 10))
            result = module.fit_mass(samples, windows)
            self.assertAlmostEqual(result["counts_per_gram"], counts_per_g)
            self.assertAlmostEqual(result["grams_per_count"], 1 / counts_per_g)
            self.assertAlmostEqual(result["max_abs_residual_g"], 0)

    def test_missing_zero_and_short_plateau_are_rejected(self):
        samples = [(j / 10, 400000) for j in range(100)]
        with self.assertRaises(ValueError):
            module.fit_mass(samples, [(20, 0, 4), (30, 4, 8), (35, 8, 10)])
        with self.assertRaises(ValueError):
            module.fit_mass(samples, [(0, 0, 2), (20, 2, 5), (35, 5, 9)])

    def test_explicitly_excluded_tail_is_not_parsed(self):
        text = ("# ADC timestamp_ms=100 raw_counts=440000\n"
                "# ADC timestamp_ms=200 raw_counts=440001\n"
                "# ADC timestamp_ms=300 raw_counts=440002\n"
                "# ADC timestamp_ms=300 raw_counts=440003\n")
        with patch.object(Path, "read_text", return_value=text):
            path = Path("adc.txt")
            self.assertEqual(len(module.read_adc(path, end_s=0.15)), 2)
            with self.assertRaises(ValueError):
                module.read_adc(path)


if __name__ == "__main__":
    unittest.main()
