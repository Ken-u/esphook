#!/usr/bin/env python3
"""Check the documentation people and AI agents start from.

* every relative markdown link and `#anchor` in README, AGENTS, TODO, docs/ and the skills points at something real;
* every skill under .agents/skills is listed in AGENTS.md, and AGENTS.md lists nothing that does not exist;
* every page in docs/ can be reached from the README (directly or through other pages).

Usage: python3 scripts/check-docs.py [--root DIR]
"""
import argparse
from pathlib import Path
import re
import sys
from urllib.parse import unquote

LINK = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
FENCE = re.compile(r"^\s*(```|~~~)")


def prose(path):
    """The lines of a markdown file outside fenced code blocks."""
    fenced = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            yield line


def slug(heading):
    text = re.sub(r"`", "", heading)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[^\w\- ]", "", text.lower())
    return text.replace(" ", "-")


def anchors(path):
    seen = {}
    found = set()
    for line in prose(path):
        match = HEADING.match(line)
        if not match:
            continue
        base = slug(match.group(2))
        count = seen.get(base, 0)
        seen[base] = count + 1
        found.add(base if count == 0 else f"{base}-{count}")
    return found


def pages(root):
    names = ["README.md", "AGENTS.md", "TODO.md"]
    found = [root / name for name in names if (root / name).is_file()]
    found += sorted((root / "docs").rglob("*.md"))
    found += sorted((root / ".agents/skills").glob("*/SKILL.md"))
    return found


def links(path):
    for line in prose(path):
        line = re.sub(r"`[^`]*`", "", line)  # an inline code span is not a link
        for target in LINK.findall(line):
            if not re.match(r"^[a-z][a-z0-9+.-]*:", target):  # skip http:, https:, mailto:, ...
                yield unquote(target)


def check(root):
    errors = []
    graph = {}
    cache = {}

    def anchors_of(path):
        if path not in cache:
            cache[path] = anchors(path)
        return cache[path]

    for page in pages(root):
        rel = page.relative_to(root)
        graph[page.resolve()] = set()
        for target in links(page):
            name, _, fragment = target.partition("#")
            destination = page if not name else (page.parent / name)
            if not destination.exists():
                errors.append(f"{rel}: broken link {target}")
                continue
            if name and destination.suffix == ".md":
                graph[page.resolve()].add(destination.resolve())
            if fragment and destination.suffix == ".md" and fragment not in anchors_of(destination):
                errors.append(f"{rel}: no heading for #{fragment} in {destination.relative_to(root)}")

    skills = sorted(path.parent.name for path in (root / ".agents/skills").glob("*/SKILL.md"))
    agents = (root / "AGENTS.md").read_text(encoding="utf-8")
    for skill in skills:
        if f"`{skill}`" not in agents:
            errors.append(f"AGENTS.md: skill {skill} is not listed")
    for name in sorted(set(re.findall(r"`(esphook-[a-z0-9-]+)`", agents))):
        if name not in skills:
            errors.append(f"AGENTS.md: lists a skill that does not exist: {name}")

    reached = set()
    todo = [(root / "README.md").resolve()]
    while todo:
        current = todo.pop()
        if current in reached:
            continue
        reached.add(current)
        todo.extend(graph.get(current, ()))
    for page in sorted((root / "docs").glob("*.md")):
        if page.resolve() not in reached:
            errors.append(f"docs/{page.name}: not reachable from the README (add it to the documentation index)")
    return errors, len(graph)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    errors, count = check(args.root.resolve())
    for error in errors:
        print(f"ERROR {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"docs ok: {count} pages, links, anchors, skills and the documentation index")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
