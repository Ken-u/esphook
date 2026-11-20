import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("check_docs", ROOT / "scripts/check-docs.py")
check_docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_docs)


def tree(files):
    root = Path(tempfile.mkdtemp())
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return root


GOOD = {
    "README.md": "# T\n## 7.4 测试\n[a](docs/a.md#第一节) [self](#74-测试)\n",
    "AGENTS.md": "skills: `esphook-x`\n",
    ".agents/skills/esphook-x/SKILL.md": "---\nname: esphook-x\n---\n",
    "docs/a.md": "# A\n## 第一节\n[back](../README.md#74-测试) [up](b.md)\n",
    "docs/b.md": "# B\n",
}


class CheckDocsTests(unittest.TestCase):
    def errors(self, **changes):
        files = dict(GOOD)
        files.update(changes)
        return check_docs.check(tree(files))[0]

    def test_a_consistent_tree_passes(self):
        self.assertEqual(self.errors(), [])

    def test_slugs_follow_github_for_chinese_numbers_and_code(self):
        self.assertEqual(check_docs.slug("7.4 测试"), "74-测试")
        self.assertEqual(check_docs.slug("从旧分区布局升级（只需一次）"), "从旧分区布局升级只需一次")
        self.assertEqual(check_docs.slug("`run.py` device"), "runpy-device")

    def test_broken_file_and_anchor_links_are_reported(self):
        errors = self.errors(**{"docs/b.md": "# B\n[x](missing.md) [y](a.md#nope) [z](#nowhere)\n"})
        self.assertEqual(len(errors), 3, errors)

    def test_links_in_code_are_ignored(self):
        self.assertEqual(self.errors(**{"docs/b.md": "# B\n```\n[x](missing.md)\n```\n`[y](gone.md)`\n"}), [])

    def test_external_links_are_not_checked(self):
        self.assertEqual(self.errors(**{"docs/b.md": "# B\n[x](https://example.com/none) [m](mailto:a@b.c)\n"}), [])

    def test_an_unlinked_doc_and_an_unlisted_skill_are_reported(self):
        errors = self.errors(**{"docs/c.md": "# C\n", ".agents/skills/esphook-y/SKILL.md": "---\n---\n"})
        self.assertTrue(any("docs/c.md" in e and "not reachable" in e for e in errors), errors)
        self.assertTrue(any("esphook-y" in e and "not listed" in e for e in errors), errors)

    def test_a_listed_skill_that_does_not_exist_is_reported(self):
        errors = self.errors(**{"AGENTS.md": "`esphook-x` `esphook-ghost`\n"})
        self.assertEqual(len(errors), 1)
        self.assertIn("esphook-ghost", errors[0])

    def test_the_real_repository_is_clean(self):
        self.assertEqual(check_docs.check(ROOT)[0], [])


if __name__ == "__main__":
    unittest.main()
