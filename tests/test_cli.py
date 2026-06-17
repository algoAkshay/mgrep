"""Small black-box regression suite for the mgrep command-line program."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = str(pathlib.Path(sys.argv[1]).resolve())
sys.argv = sys.argv[:1]


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mgrep-test-")
        self.root = pathlib.Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def run_search(self, *args):
        return subprocess.run(
            [EXECUTABLE, "--no-color", *map(str, args)],
            capture_output=True, text=True, errors="replace", timeout=10,
        )

    def test_empty_literal_rejected_and_zero_length_regex_finishes(self):
        path = self.root / "one.txt"
        path.write_text("alpha\n", encoding="ascii")
        result = self.run_search("", path)
        self.assertEqual(result.returncode, 1)
        self.assertIn("empty literal pattern", result.stderr)
        self.assertEqual(self.run_search("-E", "^", path).returncode, 0)

    def test_multiple_matches_keep_count_and_one_output_line(self):
        path = self.root / "dense.txt"
        path.write_text("alpha alpha\n", encoding="ascii")
        result = self.run_search("alpha", path)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.count("1:alpha alpha\n"), 1)
        self.assertIn("2 match(es) in 1/1 file(s)", result.stdout)
        count = self.run_search("-c", "alpha", path)
        self.assertRegex(count.stdout, r"dense\.txt: 2\n")

    def test_binary_scan_covers_first_512_file_bytes(self):
        path = self.root / "binary.txt"
        path.write_bytes(b"header\nalpha\0alpha\n")
        result = self.run_search("-c", "alpha", path)
        self.assertEqual(result.returncode, 1)
        self.assertIn("0/1 file(s)", result.stdout)

        path.write_bytes(b"x" * 511 + b"\0alpha\n")
        self.assertEqual(self.run_search("-c", "alpha", path).returncode, 1)
        path.write_bytes(b"x" * 512 + b"\0alpha\n")
        self.assertEqual(self.run_search("-c", "alpha", path).returncode, 0)

    def test_extension_filter_direct_and_directory(self):
        txt = self.root / "sample.txt"
        cpp = self.root / "CAP.CPP"
        txt.write_text("alpha\n", encoding="ascii")
        cpp.write_text("alpha\n", encoding="ascii")
        direct = self.run_search("--ext=.cpp", "alpha", txt)
        self.assertEqual(direct.returncode, 1)
        self.assertIn("0/0 file(s)", direct.stdout)
        directory = self.run_search("--ext=.cpp", "alpha", self.root)
        self.assertEqual(directory.returncode, 0)
        self.assertIn("1/1 file(s)", directory.stdout)
        self.assertIn("CAP.CPP", directory.stdout)
        self.assertNotIn("sample.txt", directory.stdout)

    def test_cli_argument_validation(self):
        path = self.root / "one.txt"
        path.write_text("alpha\n", encoding="ascii")
        for args in [
            ("--depth=1junk", "alpha", path),
            ("--depth=-2", "alpha", path),
            ("alpha", path, "stray"),
            ("--wat", "alpha", path),
            ("-E", "(", path),
        ]:
            with self.subTest(args=args):
                self.assertEqual(self.run_search(*args).returncode, 1)
        self.assertEqual(self.run_search("--depth=-1", "alpha", path).returncode, 0)

    def test_filesystem_error_is_distinct_from_no_match(self):
        missing = self.root / "missing.txt"
        result = self.run_search("alpha", missing)
        self.assertEqual(result.returncode, 2)
        self.assertIn("path does not exist", result.stderr)
        path = self.root / "no.txt"
        path.write_text("beta\n", encoding="ascii")
        self.assertEqual(self.run_search("alpha", path).returncode, 1)

    def test_case_insensitive_high_bytes_and_regex(self):
        path = self.root / "bytes.txt"
        path.write_bytes(b"Alpha\n\x80alpha\n")
        self.assertIn("2 match(es)", self.run_search("-i", "alpha", path).stdout)
        result = self.run_search("-E", "alpha|Alpha", path)
        self.assertEqual(result.returncode, 0)
        self.assertIn("2 match(es)", result.stdout)

    def test_depth_and_sorted_multifile_output(self):
        (self.root / "sub").mkdir()
        for i in range(30):
            (self.root / f"{30-i:03}.txt").write_text("hit\n", encoding="ascii")
        (self.root / "sub" / "nested.txt").write_text("hit\n", encoding="ascii")
        shallow = self.run_search("--depth=0", "-c", "hit", self.root)
        self.assertEqual(shallow.returncode, 0)
        self.assertIn("30/30 file(s)", shallow.stdout)
        rows = [line for line in shallow.stdout.splitlines() if line.endswith(": 1")]
        self.assertEqual(len(rows), 30)
        self.assertEqual(rows, sorted(rows))


if __name__ == "__main__":
    unittest.main()
