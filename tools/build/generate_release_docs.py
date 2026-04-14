#!/usr/bin/env python3
"""Generate packaged HTML documentation for OpenQ4 releases."""

from __future__ import annotations

import argparse
import html
import os
import re
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT_DOCS = (
    Path("README.md"),
    Path("BUILDING.md"),
    Path("TECHNICAL.md"),
    Path("TODO.md"),
)


SITE_CSS = r"""
:root {
  --bg: #091019;
  --panel: rgba(15, 22, 33, 0.9);
  --panel-strong: rgba(18, 27, 39, 0.97);
  --border: rgba(255, 139, 61, 0.2);
  --border-strong: rgba(255, 139, 61, 0.46);
  --text: #edf1f6;
  --muted: #a8b6c7;
  --accent: #ff8b3d;
  --accent-soft: #ffc08d;
  --link: #ffc08d;
  --code-bg: #0d131b;
  --code-border: rgba(255, 255, 255, 0.08);
  --shadow: 0 28px 70px rgba(0, 0, 0, 0.34);
  --radius: 18px;
  --radius-sm: 13px;
}

* {
  box-sizing: border-box;
}

html {
  scroll-behavior: smooth;
}

body {
  margin: 0;
  color: var(--text);
  font-family: "Bahnschrift", "Trebuchet MS", "Segoe UI", sans-serif;
  line-height: 1.7;
  background:
    radial-gradient(circle at top, rgba(255, 139, 61, 0.14), transparent 30%),
    radial-gradient(circle at 84% 14%, rgba(61, 133, 255, 0.12), transparent 18%),
    linear-gradient(180deg, #091019 0%, #090d14 44%, #0b1017 100%);
  min-height: 100vh;
}

body::before {
  content: "";
  position: fixed;
  inset: 0;
  pointer-events: none;
  background: linear-gradient(rgba(255, 255, 255, 0.012), rgba(255, 255, 255, 0.012)) 0 0 / 100% 3px;
  opacity: 0.18;
}

a {
  color: var(--link);
  text-decoration: none;
}

a:hover {
  text-decoration: underline;
}

img {
  max-width: 100%;
}

code,
pre,
kbd,
samp {
  font-family: "Cascadia Code", Consolas, monospace;
}

.shell {
  width: min(1400px, calc(100% - 28px));
  margin: 24px auto 48px;
}

.topbar,
.sidebar,
.article,
.toc,
.hero,
.catalog,
.footer {
  border: 1px solid var(--border);
  border-radius: var(--radius);
  background: var(--panel);
  box-shadow: var(--shadow);
  backdrop-filter: blur(10px);
}

.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 18px;
  padding: 18px 22px;
  margin-bottom: 20px;
  background:
    linear-gradient(145deg, rgba(255, 139, 61, 0.1), rgba(15, 22, 33, 0.96) 42%, rgba(15, 22, 33, 0.95)),
    var(--panel);
}

.brand {
  display: grid;
  gap: 4px;
}

.brand a {
  color: var(--text);
}

.eyebrow {
  display: inline-block;
  letter-spacing: 0.16em;
  text-transform: uppercase;
  font-size: 0.72rem;
  color: var(--accent-soft);
}

.brand-title {
  font-size: clamp(1.45rem, 3vw, 2rem);
  font-weight: 700;
  letter-spacing: 0.03em;
}

.brand-subtitle {
  color: var(--muted);
  font-size: 0.95rem;
}

.action-row,
.hero-actions,
.hero-meta,
.article-meta {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.button,
.chip {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  border-radius: 999px;
  padding: 10px 15px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  background: rgba(255, 255, 255, 0.03);
  color: var(--text);
}

.button:hover {
  text-decoration: none;
  border-color: var(--border-strong);
  background: rgba(255, 139, 61, 0.12);
}

.button.primary {
  background: linear-gradient(180deg, #ff9a54, #d46522);
  color: #111822;
  border-color: rgba(255, 139, 61, 0.5);
  font-weight: 700;
}

.chip {
  color: var(--muted);
  padding: 8px 12px;
}

.layout {
  display: grid;
  grid-template-columns: 270px minmax(0, 1fr) 240px;
  gap: 18px;
}

.sidebar,
.toc {
  position: sticky;
  top: 20px;
  align-self: start;
  padding: 18px;
}

.sidebar h2,
.toc h2 {
  margin: 0 0 14px;
  font-size: 0.96rem;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--accent-soft);
}

.nav-group + .nav-group {
  margin-top: 18px;
}

.nav-group h3 {
  margin: 0 0 10px;
  color: var(--muted);
  font-size: 0.8rem;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.nav-links,
.toc-links {
  list-style: none;
  margin: 0;
  padding: 0;
  display: grid;
  gap: 6px;
}

.nav-links a,
.toc-links a {
  display: block;
  padding: 9px 11px;
  border-radius: 12px;
  color: var(--muted);
  border: 1px solid transparent;
}

.nav-links a:hover,
.toc-links a:hover {
  text-decoration: none;
  color: var(--text);
  border-color: rgba(255, 255, 255, 0.07);
  background: rgba(255, 255, 255, 0.03);
}

.nav-links a.active,
.toc-links a.active {
  color: var(--text);
  background: rgba(255, 139, 61, 0.12);
  border-color: rgba(255, 139, 61, 0.35);
}

.toc-links ul {
  list-style: none;
  margin: 6px 0 0 12px;
  padding: 0 0 0 10px;
  border-left: 1px solid rgba(255, 255, 255, 0.08);
}

.article,
.hero,
.catalog,
.footer {
  padding: 26px;
}

.hero {
  background:
    linear-gradient(140deg, rgba(255, 139, 61, 0.12), rgba(17, 24, 33, 0.94) 44%, rgba(17, 24, 33, 0.98)),
    var(--panel);
}

.hero-banner {
  width: 100%;
  display: block;
  border-radius: 14px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  margin-bottom: 18px;
}

.hero h1,
.article-header h1 {
  margin: 0 0 12px;
  font-size: clamp(2rem, 4.2vw, 3.2rem);
  line-height: 1.08;
  letter-spacing: 0.02em;
}

.hero p,
.article-header p {
  margin: 0 0 14px;
}

.hero .lead,
.article-header .lead {
  max-width: 820px;
  color: var(--muted);
  font-size: 1.04rem;
}

.catalog {
  margin-top: 20px;
}

.catalog h2 {
  margin: 0 0 14px;
  color: var(--accent-soft);
  font-size: clamp(1.4rem, 2.5vw, 1.9rem);
}

.card-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
}

.card {
  border-radius: var(--radius-sm);
  border: 1px solid rgba(255, 255, 255, 0.08);
  background: var(--panel-strong);
  padding: 18px;
}

.card h3 {
  margin: 0 0 10px;
  font-size: 1.08rem;
}

.card p {
  margin: 0 0 10px;
  color: var(--muted);
}

.card ul {
  margin: 0;
  padding-left: 18px;
  color: var(--muted);
}

.card li + li {
  margin-top: 7px;
}

.article-header {
  padding-bottom: 18px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  margin-bottom: 20px;
}

.article-content {
  min-width: 0;
}

.article-content h1,
.article-content h2,
.article-content h3,
.article-content h4,
.article-content h5,
.article-content h6 {
  margin: 28px 0 12px;
  line-height: 1.2;
}

.article-content h2 {
  font-size: 1.7rem;
  color: var(--accent-soft);
}

.article-content h3 {
  font-size: 1.25rem;
}

.article-content h4 {
  font-size: 1.05rem;
}

.article-content p {
  margin: 0 0 16px;
}

.article-content ul,
.article-content ol {
  margin: 0 0 18px;
  padding-left: 24px;
}

.article-content li + li {
  margin-top: 8px;
}

.article-content hr {
  border: 0;
  height: 1px;
  margin: 26px 0;
  background: linear-gradient(90deg, transparent, rgba(255, 255, 255, 0.16), transparent);
}

.article-content blockquote,
.article-content .admonition {
  margin: 18px 0;
  padding: 16px 18px;
  border-radius: 14px;
  border-left: 4px solid var(--accent);
  background: rgba(255, 255, 255, 0.04);
}

.article-content .admonition-title {
  margin: 0 0 8px;
  font-weight: 700;
  color: var(--text);
}

.article-content .admonition.tip {
  border-left-color: #5ac98f;
}

.article-content .admonition.important {
  border-left-color: #6fb5ff;
}

.article-content .admonition.warning,
.article-content .admonition.caution {
  border-left-color: #ff6f61;
}

.article-content pre {
  margin: 16px 0;
  padding: 16px;
  overflow-x: auto;
  border-radius: 14px;
  border: 1px solid var(--code-border);
  background: var(--code-bg);
  color: #f6c59d;
  font-size: 0.9rem;
  line-height: 1.58;
}

.article-content code {
  padding: 0.14em 0.42em;
  border-radius: 8px;
  background: var(--code-bg);
  color: #f6c59d;
  font-size: 0.92em;
}

.article-content pre code {
  padding: 0;
  background: transparent;
}

.article-content table {
  width: 100%;
  border-collapse: collapse;
  margin: 18px 0;
  overflow: hidden;
  border-radius: 14px;
  border: 1px solid rgba(255, 255, 255, 0.08);
}

.article-content thead {
  background: rgba(255, 139, 61, 0.1);
}

.article-content th,
.article-content td {
  padding: 12px 14px;
  text-align: left;
  vertical-align: top;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
}

.article-content tbody tr:nth-child(even) {
  background: rgba(255, 255, 255, 0.02);
}

.article-content details {
  margin: 18px 0;
  border-radius: 14px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  background: rgba(255, 255, 255, 0.03);
  padding: 12px 16px;
}

.article-content summary {
  cursor: pointer;
  color: var(--text);
  font-weight: 700;
}

.article-content .task-box {
  display: inline-grid;
  place-items: center;
  width: 1.1rem;
  height: 1.1rem;
  margin-right: 0.45rem;
  border-radius: 4px;
  border: 1px solid rgba(255, 255, 255, 0.25);
  background: rgba(255, 255, 255, 0.04);
  color: transparent;
  font-size: 0.8rem;
  font-weight: 700;
}

.article-content .task-box.done {
  background: rgba(90, 201, 143, 0.14);
  border-color: rgba(90, 201, 143, 0.4);
  color: #7ee5a7;
}

.article-content img {
  display: block;
  border-radius: 14px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  margin: 18px auto;
}

.article-content p[align="center"],
.article-content div[align="center"] {
  text-align: center;
}

.article-content sub {
  color: var(--muted);
}

.footer {
  margin-top: 20px;
  color: var(--muted);
  font-size: 0.92rem;
}

.footer p:last-child {
  margin-bottom: 0;
}

@media (max-width: 1180px) {
  .layout {
    grid-template-columns: 250px minmax(0, 1fr);
  }

  .toc {
    display: none;
  }
}

@media (max-width: 860px) {
  .shell {
    width: min(100% - 18px, 1400px);
    margin-top: 16px;
  }

  .topbar,
  .hero,
  .catalog,
  .sidebar,
  .article,
  .footer {
    padding: 18px;
  }

  .topbar {
    align-items: start;
    flex-direction: column;
  }

  .layout,
  .card-grid {
    grid-template-columns: 1fr;
  }

  .sidebar,
  .toc {
    position: static;
  }
}
"""


@dataclass(frozen=True)
class DocSpec:
    source_relative: Path
    output_relative: Path
    group: str
    title: str
    nav_title: str
    summary: str


@dataclass(frozen=True)
class GeneratedDocSite:
    output_root: Path
    index_path: Path
    page_count: int


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate packaged HTML documentation for an OpenQ4 release."
    )
    parser.add_argument("--source-root", default=".", help="OpenQ4 repository root.")
    parser.add_argument("--output-dir", required=True, help="Directory to populate with HTML docs.")
    parser.add_argument("--version", required=True, help="Release version string.")
    parser.add_argument("--platform", required=True, help="Target package platform.")
    parser.add_argument("--arch", required=True, help="Target package architecture.")
    return parser.parse_args(argv[1:])


def require_markdown_module() -> Any:
    try:
        import markdown as markdown_lib
    except ImportError as exc:
        raise RuntimeError(
            "Release HTML docs require the Python-Markdown package. "
            "Install it with: python -m pip install markdown"
        ) from exc
    return markdown_lib


def collect_doc_sources(source_root: Path) -> list[Path]:
    docs: list[Path] = []
    seen: set[str] = set()

    for relative in ROOT_DOCS:
        if (source_root / relative).is_file():
            docs.append(relative)
            seen.add(relative.as_posix().lower())

    for base in (Path("docs-user"), Path("docs-dev")):
        base_path = source_root / base
        if not base_path.is_dir():
            continue
        for path in sorted(base_path.rglob("*.md")):
            relative = path.relative_to(source_root)
            key = relative.as_posix().lower()
            if key in seen:
                continue
            docs.append(relative)
            seen.add(key)

    return docs


def classify_group(relative: Path) -> str:
    if relative.parent == Path("."):
        return "Project"
    if relative.parts[0] == "docs-user":
        return "User Guides"
    if len(relative.parts) >= 2 and relative.parts[0] == "docs-dev" and relative.parts[1] == "proposals":
        return "Research and Proposals"
    if len(relative.parts) >= 2 and relative.parts[0] == "docs-dev" and relative.parts[1] == "legacy":
        return "Legacy Notes"
    return "Developer Guides"


def default_nav_title(relative: Path) -> str:
    if relative == Path("README.md"):
        return "Overview"
    if relative == Path("BUILDING.md"):
        return "Build Guide"
    if relative == Path("TECHNICAL.md"):
        return "Technical Reference"
    if relative == Path("TODO.md"):
        return "TODO"

    stem = relative.stem.replace("-", " ").replace("_", " ").strip()
    if not stem:
        stem = relative.name
    return " ".join(part.capitalize() if part.islower() else part for part in stem.split())


def rewrite_markdown_links(text: str) -> str:
    pattern = re.compile(r"(!?\[[^\]]+\]\()([^)]+)(\))")

    def replace(match: re.Match[str]) -> str:
        prefix, target, suffix = match.groups()
        stripped_target = target.strip()
        if stripped_target.startswith(("http://", "https://", "mailto:", "#")):
            return match.group(0)

        hash_suffix = ""
        if "#" in stripped_target:
            base, fragment = stripped_target.split("#", 1)
            stripped_target = base
            hash_suffix = "#" + fragment

        if stripped_target.lower().endswith(".md"):
            stripped_target = stripped_target[:-3] + ".html"

        return prefix + stripped_target + hash_suffix + suffix

    return pattern.sub(replace, text)


def convert_github_callouts(text: str) -> str:
    lines = text.splitlines()
    result: list[str] = []
    index = 0

    while index < len(lines):
        stripped = lines[index].strip()
        match = re.match(r"^>\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*$", stripped)
        if match is None:
            result.append(lines[index])
            index += 1
            continue

        level = match.group(1).lower()
        result.append(f"!!! {level}")
        index += 1
        while index < len(lines) and lines[index].lstrip().startswith(">"):
            content = lines[index].lstrip()[1:]
            if content.startswith(" "):
                content = content[1:]
            if content:
                result.append("    " + content)
            else:
                result.append("")
            index += 1

    return "\n".join(result)


def convert_task_lists(text: str) -> str:
    result: list[str] = []
    pattern = re.compile(r"^(\s*[-*+]\s+)\[( |x|X)\]\s+(.*)$")

    for line in text.splitlines():
        match = pattern.match(line)
        if match is None:
            result.append(line)
            continue

        prefix, state, body = match.groups()
        classes = "task-box done" if state.lower() == "x" else "task-box"
        symbol = "&#10003;" if state.lower() == "x" else "&nbsp;"
        result.append(f'{prefix}<span class="{classes}">{symbol}</span> {body}')

    return "\n".join(result)


def prepare_markdown(text: str) -> str:
    normalized = text.replace("\r\n", "\n")
    normalized = re.sub(
        r"(?m)^<details(?![^>]*markdown=)",
        '<details markdown="1"',
        normalized,
    )
    normalized = re.sub(
        r"(?m)^<div(?![^>]*markdown=)",
        '<div markdown="1"',
        normalized,
    )
    normalized = rewrite_markdown_links(normalized)
    normalized = convert_task_lists(normalized)
    normalized = convert_github_callouts(normalized)
    return normalized


def strip_markdown_preview(text: str) -> str:
    preview = re.sub(r"!\[([^\]]*)\]\([^)]+\)", r"\1", text)
    preview = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", preview)
    preview = re.sub(r"`([^`]+)`", r"\1", preview)
    preview = re.sub(r"<[^>]+>", "", preview)
    preview = preview.replace("**", "").replace("__", "").replace("*", "").replace("_", "")
    preview = re.sub(r"\s+", " ", preview)
    return preview.strip()


def extract_title_and_summary(relative: Path, text: str) -> tuple[str, str]:
    lines = text.replace("\r\n", "\n").splitlines()

    title = default_nav_title(relative)
    for line in lines:
        match = re.match(r"^#\s+(.*)$", line.strip())
        if match is not None:
            title = strip_markdown_preview(match.group(1))
            break

    summary_lines: list[str] = []
    in_code_block = False
    title_found = False
    for raw_line in lines:
        stripped = raw_line.strip()
        if stripped.startswith("```"):
            in_code_block = not in_code_block
            continue
        if in_code_block:
            continue

        if not title_found:
            if re.match(r"^#\s+", stripped):
                title_found = True
            continue

        if not stripped:
            if summary_lines:
                break
            continue

        if stripped.startswith(("#", ">", "-", "*", "|", "<")):
            if summary_lines:
                break
            continue

        if re.match(r"^\d+\.\s+", stripped):
            if summary_lines:
                break
            continue

        summary_lines.append(stripped)

    summary = strip_markdown_preview(" ".join(summary_lines))
    return title, summary


def build_doc_specs(source_root: Path) -> list[DocSpec]:
    specs: list[DocSpec] = []
    for relative in collect_doc_sources(source_root):
        title, summary = extract_title_and_summary(
            relative,
            (source_root / relative).read_text(encoding="utf-8"),
        )
        if not summary and relative == Path("README.md"):
            summary = (
                "Project overview, runtime scope, compatibility goals, and the main "
                "documentation map for OpenQ4."
            )
        specs.append(
            DocSpec(
                source_relative=relative,
                output_relative=relative.with_suffix(".html"),
                group=classify_group(relative),
                title=title,
                nav_title=default_nav_title(relative) if relative.parent == Path(".") else title,
                summary=summary,
            )
        )
    return specs


def relative_href(current_parent: Path, target: Path) -> str:
    return os.path.relpath(target, start=current_parent if current_parent != Path(".") else Path(".")).replace("\\", "/")


def render_sidebar(specs: list[DocSpec], current_output: Path) -> str:
    grouped: dict[str, list[DocSpec]] = {}
    for spec in specs:
        grouped.setdefault(spec.group, []).append(spec)

    group_order = (
        "Project",
        "User Guides",
        "Developer Guides",
        "Research and Proposals",
        "Legacy Notes",
    )

    sections: list[str] = []
    current_parent = current_output.parent
    for group in group_order:
        group_specs = grouped.get(group)
        if not group_specs:
            continue

        links = []
        for spec in group_specs:
            href = relative_href(current_parent, spec.output_relative)
            if spec.output_relative == current_output:
                links.append(
                    f'<li><a class="active" href="{html.escape(href)}">{html.escape(spec.nav_title)}</a></li>'
                )
            else:
                links.append(
                    f'<li><a href="{html.escape(href)}">{html.escape(spec.nav_title)}</a></li>'
                )

        sections.append(
            "\n".join(
                [
                    '<section class="nav-group">',
                    f"<h3>{html.escape(group)}</h3>",
                    '<ul class="nav-links">',
                    *links,
                    "</ul>",
                    "</section>",
                ]
            )
        )

    return "\n".join(sections)


def normalize_toc_tokens(tokens: list[dict[str, Any]], title: str) -> list[dict[str, Any]]:
    if len(tokens) == 1 and tokens[0].get("level") == 1:
        token_name = strip_markdown_preview(str(tokens[0].get("name", "")))
        if token_name == title:
            return list(tokens[0].get("children", []))
    return tokens


def render_toc_items(items: list[dict[str, Any]]) -> str:
    if not items:
        return ""

    rendered: list[str] = ['<ul class="toc-links">']
    for item in items:
        heading_id = html.escape(str(item.get("id", "")))
        heading_name = strip_markdown_preview(str(item.get("name", "")))
        rendered.append(f'<li><a href="#{heading_id}">{html.escape(heading_name)}</a>')
        children = item.get("children") or []
        if children:
            rendered.append(render_toc_items(list(children)))
        rendered.append("</li>")
    rendered.append("</ul>")
    return "\n".join(rendered)


def render_doc_page(
    spec: DocSpec,
    specs: list[DocSpec],
    version: str,
    platform: str,
    arch: str,
    body_html: str,
    toc_tokens: list[dict[str, Any]],
) -> str:
    current_parent = spec.output_relative.parent
    css_href = relative_href(current_parent, Path("_static") / "site.css")
    docs_home_href = relative_href(current_parent, Path("index.html"))
    package_home_href = relative_href(current_parent, Path("..") / "README.html")
    overview_href = relative_href(current_parent, Path("README.html"))

    sidebar_html = render_sidebar(specs, spec.output_relative)
    toc_html = render_toc_items(normalize_toc_tokens(toc_tokens, spec.title))
    if not toc_html:
        toc_html = '<p class="brand-subtitle">This page has no generated section index.</p>'

    summary_html = (
        f'<p class="lead">{html.escape(spec.summary)}</p>'
        if spec.summary
        else '<p class="lead">Included in the packaged OpenQ4 offline documentation set.</p>'
    )

    return "\n".join(
        [
            "<!DOCTYPE html>",
            '<html lang="en">',
            "<head>",
            '  <meta charset="UTF-8">',
            '  <meta name="viewport" content="width=device-width, initial-scale=1.0">',
            '  <meta name="color-scheme" content="dark">',
            f"  <title>{html.escape(spec.title)} | OpenQ4 Documentation</title>",
            f'  <link rel="stylesheet" href="{html.escape(css_href)}">',
            "</head>",
            "<body>",
            '  <div class="shell">',
            '    <header class="topbar">',
            '      <div class="brand">',
            '        <span class="eyebrow">Packaged HTML documentation</span>',
            f'        <a class="brand-title" href="{html.escape(docs_home_href)}">OpenQ4 Documentation</a>',
            f'        <div class="brand-subtitle">Release {html.escape(version)} · {html.escape(platform)} · {html.escape(arch)}</div>',
            "      </div>",
            '      <div class="action-row">',
            f'        <a class="button primary" href="{html.escape(docs_home_href)}">Docs Home</a>',
            f'        <a class="button" href="{html.escape(overview_href)}">Project Overview</a>',
            f'        <a class="button" href="{html.escape(package_home_href)}">Package Home</a>',
            '        <a class="button" href="https://github.com/themuffinator/OpenQ4">GitHub</a>',
            "      </div>",
            "    </header>",
            '    <div class="layout">',
            '      <aside class="sidebar">',
            '        <h2>Documentation</h2>',
            sidebar_html,
            "      </aside>",
            '      <main class="article">',
            '        <header class="article-header">',
            f'          <span class="eyebrow">{html.escape(spec.group)}</span>',
            f"          <h1>{html.escape(spec.title)}</h1>",
            summary_html,
            '          <div class="article-meta">',
            f'            <span class="chip">{html.escape(spec.source_relative.as_posix())}</span>',
            f'            <span class="chip">Release {html.escape(version)}</span>',
            "          </div>",
            "        </header>",
            f'        <article class="article-content">{body_html}</article>',
            "      </main>",
            '      <aside class="toc">',
            '        <h2>On This Page</h2>',
            toc_html,
            "      </aside>",
            "    </div>",
            '    <footer class="footer">',
            f'      <p>Offline release documentation generated from the OpenQ4 repository sources for version <strong>{html.escape(version)}</strong>.</p>',
            f'      <p><a href="{html.escape(docs_home_href)}">Documentation home</a> | <a href="{html.escape(package_home_href)}">Package home</a> | <a href="https://www.darkmatter-quake.com">Website</a></p>',
            "    </footer>",
            "  </div>",
            "</body>",
            "</html>",
        ]
    )


def render_index_page(
    specs: list[DocSpec],
    version: str,
    platform: str,
    arch: str,
) -> str:
    grouped: dict[str, list[DocSpec]] = {}
    for spec in specs:
        grouped.setdefault(spec.group, []).append(spec)

    def render_group(group: str, description: str) -> str:
        links = grouped.get(group, [])
        if not links:
            return ""

        items = [
            f'<li><a href="{html.escape(spec.output_relative.as_posix())}">{html.escape(spec.nav_title)}</a></li>'
            for spec in links
        ]
        return "\n".join(
            [
                '<article class="card">',
                f'<span class="eyebrow">{html.escape(group)}</span>',
                f"<h3>{html.escape(description)}</h3>",
                '<ul>',
                *items,
                "</ul>",
                "</article>",
            ]
        )

    cards = [
        render_group("Project", "Core project guides and high-level references."),
        render_group("User Guides", "Runtime settings, gameplay-facing features, and troubleshooting."),
        render_group("Developer Guides", "Implementation notes, audits, and current engineering references."),
        render_group("Research and Proposals", "Long-form technical investigations and design proposals."),
        render_group("Legacy Notes", "Historical context and archived notes kept for reference."),
    ]
    cards = [card for card in cards if card]

    return "\n".join(
        [
            "<!DOCTYPE html>",
            '<html lang="en">',
            "<head>",
            '  <meta charset="UTF-8">',
            '  <meta name="viewport" content="width=device-width, initial-scale=1.0">',
            '  <meta name="color-scheme" content="dark">',
            "  <title>OpenQ4 Documentation Portal</title>",
            '  <link rel="stylesheet" href="_static/site.css">',
            "</head>",
            "<body>",
            '  <div class="shell">',
            '    <header class="topbar">',
            '      <div class="brand">',
            '        <span class="eyebrow">Offline HTML documentation</span>',
            '        <a class="brand-title" href="index.html">OpenQ4 Documentation Portal</a>',
            f'        <div class="brand-subtitle">Release {html.escape(version)} · {html.escape(platform)} · {html.escape(arch)} · {len(specs)} documents</div>',
            "      </div>",
            '      <div class="action-row">',
            '        <a class="button primary" href="README.html">Project Overview</a>',
            '        <a class="button" href="BUILDING.html">Build Guide</a>',
            '        <a class="button" href="TECHNICAL.html">Technical Reference</a>',
            '        <a class="button" href="../README.html">Package Home</a>',
            "      </div>",
            "    </header>",
            '    <section class="hero">',
            '      <img class="hero-banner" src="assets/docs/img/banner.png" alt="OpenQ4 banner">',
            '      <span class="eyebrow">OpenQ4 Release Documentation</span>',
            '      <h1>Full offline HTML docs, shipped with the release package.</h1>',
            '      <p class="lead">This package includes the project overview, build and technical references, user guides, developer notes, and proposal research converted into a browsable HTML site with internal links preserved.</p>',
            '      <div class="hero-actions">',
            '        <a class="button primary" href="README.html">Open Project Overview</a>',
            '        <a class="button" href="docs-user/light-grids.html">User Guide Example</a>',
            '        <a class="button" href="docs-dev/platform-support.html">Platform Roadmap</a>',
            '      </div>',
            '      <div class="hero-meta">',
            f'        <span class="chip">Release {html.escape(version)}</span>',
            f'        <span class="chip">{html.escape(platform)}</span>',
            f'        <span class="chip">{html.escape(arch)}</span>',
            '        <span class="chip">Generated from repository Markdown sources</span>',
            "      </div>",
            "    </section>",
            '    <section class="catalog">',
            "      <h2>Included Documentation</h2>",
            '      <div class="card-grid">',
            *cards,
            "      </div>",
            "    </section>",
            '    <footer class="footer">',
            '      <p>These pages are bundled directly into the release archive so the package remains self-documenting even when viewed offline.</p>',
            '      <p><a href="../README.html">Package home</a> | <a href="https://github.com/themuffinator/OpenQ4">Repository</a> | <a href="https://www.darkmatter-quake.com">Website</a></p>',
            "    </footer>",
            "  </div>",
            "</body>",
            "</html>",
        ]
    )


def generate_release_docs_site(
    *,
    source_root: Path,
    output_root: Path,
    version: str,
    platform: str,
    arch: str,
) -> GeneratedDocSite:
    markdown_lib = require_markdown_module()
    specs = build_doc_specs(source_root)

    if output_root.exists():
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True, exist_ok=True)

    (output_root / "_static").mkdir(parents=True, exist_ok=True)
    (output_root / "_static" / "site.css").write_text(SITE_CSS, encoding="utf-8")

    docs_assets_source = source_root / "assets" / "docs" / "img"
    if docs_assets_source.is_dir():
        docs_assets_dest = output_root / "assets" / "docs" / "img"
        docs_assets_dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(docs_assets_source, docs_assets_dest, dirs_exist_ok=True)

    for spec in specs:
        raw_text = (source_root / spec.source_relative).read_text(encoding="utf-8")
        prepared_text = prepare_markdown(raw_text)
        markdown_engine = markdown_lib.Markdown(
            extensions=[
                "extra",
                "admonition",
                "sane_lists",
                "toc",
                "md_in_html",
            ],
            extension_configs={
                "toc": {
                    "permalink": False,
                }
            },
        )
        body_html = markdown_engine.convert(prepared_text)
        body_html = re.sub(r"<h1[^>]*>.*?</h1>\s*", "", body_html, count=1, flags=re.S)
        body_html = re.sub(r"<div align=\"center\">\s*</div>", "", body_html, flags=re.S)
        page_html = render_doc_page(
            spec,
            specs,
            version,
            platform,
            arch,
            body_html,
            list(markdown_engine.toc_tokens),
        )

        destination = output_root / spec.output_relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(page_html, encoding="utf-8")

    index_path = output_root / "index.html"
    index_path.write_text(
        render_index_page(specs, version, platform, arch),
        encoding="utf-8",
    )

    return GeneratedDocSite(
        output_root=output_root,
        index_path=index_path,
        page_count=len(specs),
    )


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    result = generate_release_docs_site(
        source_root=Path(args.source_root).resolve(),
        output_root=Path(args.output_dir).resolve(),
        version=args.version,
        platform=args.platform,
        arch=args.arch,
    )
    print(f"Generated OpenQ4 HTML docs: {result.index_path} ({result.page_count} pages)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
