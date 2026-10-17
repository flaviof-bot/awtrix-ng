"""The docs text checks: words that have one name only."""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_site  # noqa: E402


class Terms(unittest.TestCase):
    def test_each_word_is_named_with_its_replacement(self):
        problems = build_site.term_problems(
            "esp32", "guides/x/",
            "It joins the loop. The matrix and the screen and the panel, e.g. here.")
        self.assertEqual(5, len(problems))
        self.assertTrue(problems[0].startswith("esp32/guides/x/: 'the loop', write \"the rotation\""))

    def test_words_that_stay(self):
        text = "Matrix fonts, the Matrix font, matrix-light6, in loop mode, a loop() hook."
        self.assertEqual([], build_site.term_problems("esp32", "guides/x/", text))

    def test_capitalised_labels_and_names_stay(self):
        text = "Open System → Panel. The Matrix entity and the Panel width field."
        self.assertEqual([], build_site.term_problems("esp32", "guides/x/", text))

    def test_diy_pages_may_say_panel_and_matrix(self):
        text = "Chain two panels into one LED matrix."
        self.assertEqual([], build_site.term_problems("esp32", "advanced/diy-build/", text))
        self.assertEqual(2, len(build_site.term_problems("esp32", "guides/x/", text)))


class OtherClocks(unittest.TestCase):
    NAV = '<div class="md-sidebar--primary">Home</div><div class="md-content">'

    def lint(self, variant, body):
        page = ('<html>%s<article class="md-content__inner md-typeset"><p>%s</p></article></html>'
                % (self.NAV, body))
        with tempfile.TemporaryDirectory() as site:
            with open(os.path.join(site, "index.html"), "w", encoding="utf-8") as f:
                f.write(page)
            return build_site.lint(variant, site, {})

    def test_galactic_unicorn_pages_name_no_other_clock(self):
        for word, found in (("ESP32", 1), ("ESP32-S3", 2), ("TC001", 1), ("TC002", 1)):
            with self.subTest(word=word):
                self.assertEqual(found, len(self.lint("rp2040", "Hold B on the %s." % word)))
        self.assertEqual([], self.lint("rp2040", "Hold B on the Galactic Unicorn."))

    def test_other_clocks_do_not_name_the_galactic_unicorn(self):
        for variant in ("esp32", "esp32-s3", "tc002"):
            with self.subTest(variant=variant):
                problems = self.lint(variant, "Flash the Pico W in the Galactic Unicorn.")
                self.assertEqual(2, len(problems))


class PageText(unittest.TestCase):
    def test_words_split_by_markup_are_found(self):
        page = ('<html><article class="md-content__inner md-typeset"><p>It joins the '
                '<strong>loop</strong>.</p></article></html>')
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "index.html")
            with open(path, "w", encoding="utf-8") as f:
                f.write(page)
            problems = build_site.lint_terms("esp32", "guides/x/", path, [])
        self.assertEqual(1, len(problems))

    def test_code_can_be_left_out(self):
        page = ('<html><article class="md-content__inner md-typeset"><p>Use <code>the loop</code>'
                '</p><pre><code>panel</code></pre><p>end</p></article></html>')
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "index.html")
            with open(path, "w", encoding="utf-8") as f:
                f.write(page)
            self.assertIn("the loop", build_site.page_text(path))
            plain = build_site.page_text(path, code=False)
        self.assertNotIn("the loop", plain)
        self.assertNotIn("panel", plain)
        self.assertIn("end", plain)


if __name__ == "__main__":
    unittest.main()
