#!/usr/bin/env python3
"""Render doc/release/v*.md into the site's release-notes pages (Python 3 standard library only).

  python3 scripts/release/make_release_pages.py                 write site/public/releases/ and the sitemap entries
  python3 scripts/release/make_release_pages.py --summary 0.3.4 print the one-line summary used for the Discord post

Output:
  site/public/releases/index.html            the list (newest first): version, date, first sentence
  site/public/releases/<version>/index.html  the full notes of one release
  site/public/sitemap.xml                    gets /releases/ and every /releases/<version>/

The page frame (head, header, footer) is copied from site/public/guide/index.html at run time, so a change to the
site's header or footer reaches the release pages on the next run. The pages carry no script of their own (the
site's CSP is self-only). Everything in the notes is HTML-escaped; only the Markdown the notes use is converted:
headings, paragraphs, bold, italics, inline code, links, bullet and numbered lists (nested), tables, fenced code.

The date of a release is its git tag mdmm-v<version> (creator date); --date VERSION=YYYY-MM-DD overrides it, and a
release without a tag shows no date.
"""
import argparse
import html
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NOTES_DIR = ROOT / "doc" / "release"
SITE = ROOT / "site" / "public"
FRAME = SITE / "guide" / "index.html"
SITE_URL = "https://mdmm.dev"
FILE_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.]+))?\.md$")
DISCORD_RE = re.compile(r"<!--\s*discord:\s*(.*?)\s*-->", re.S)
NOTE_LINK_RE = re.compile(r"https://github\.com/[^/\s)]+/[^/\s)]+/blob/[^/\s)]+/doc/release/v([0-9][0-9A-Za-z.\-]*?)\.md")
URL_RE = re.compile(r"https?://[^\s<>()\"']+")


# ---------- inline ----------

def rewrite_url(url):
    """Links to a release-notes file on GitHub become the site's own page."""
    m = NOTE_LINK_RE.fullmatch(url)
    return f"/releases/{m.group(1)}/" if m else url


def safe_url(url):
    return url if re.match(r"^(https?://|mailto:|/|#)", url) else "#"


def inline(text):
    """Escaped HTML for one run of Markdown inline text."""
    stash = []

    def keep(markup):
        stash.append(markup)
        return f"\x00{len(stash) - 1}\x00"

    text = text.replace("\x00", "")
    text = re.sub(r"`([^`]+)`", lambda m: keep(f"<code>{html.escape(m.group(1), quote=False)}</code>"), text)

    def link(m):
        label, url = m.group(1), rewrite_url(m.group(2).strip())
        url = safe_url(url)
        external = url.startswith("http") and not url.startswith(SITE_URL)
        rel = ' rel="noopener"' if external else ""
        return keep(f'<a href="{html.escape(url)}"{rel}>{inline(label)}</a>')

    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)

    def bare(m):
        url = m.group(0)
        tail = ""
        while url and url[-1] in ".,;:!?*":
            tail = url[-1] + tail
            url = url[:-1]
        return keep(f'<a href="{html.escape(url)}">{html.escape(url, quote=False)}</a>') + tail

    text = URL_RE.sub(bare, text)
    text = html.escape(text, quote=False)
    text = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"(?<![\w*])\*(?![\s*])(.+?)(?<![\s*])\*(?![\w*])", r"<i>\1</i>", text)
    return re.sub(r"\x00(\d+)\x00", lambda m: stash[int(m.group(1))], text)


def plain(text):
    """Inline Markdown as plain text (for the list, the meta description and the Discord line)."""
    text = re.sub(r"`([^`]+)`", r"\1", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"\*\*(.+?)\*\*", r"\1", text)
    text = re.sub(r"(?<![\w*])\*(?![\s*])(.+?)(?<![\s*])\*(?![\w*])", r"\1", text)
    return re.sub(r"\s+", " ", text).strip()


# ---------- blocks ----------

LIST_RE = re.compile(r"^(\s*)([-*]|\d+\.)\s+(.*)$")
TABLE_SEP_RE = re.compile(r"^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$")


def split_row(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [c.strip() for c in re.split(r"(?<!\\)\|", line)]


def render_list(lines):
    """lines: the raw lines of one list block (items, nested items, continuation lines)."""
    items = []  # [indent, ordered, [text lines]]
    for line in lines:
        m = LIST_RE.match(line.expandtabs(4))
        if m:
            items.append([len(m.group(1)), m.group(2)[0].isdigit(), [m.group(3)]])
        elif items and line.strip():
            items[-1][2].append(line.strip())
    out = []
    stack = []  # (indent, ordered) of the open lists

    def close_to(indent):
        while stack and stack[-1][0] > indent:
            out.append("</li>")
            out.append("</ol>" if stack.pop()[1] else "</ul>")

    for indent, ordered, text in items:
        close_to(indent)
        if stack and stack[-1][0] == indent:
            out.append("</li>")
        else:
            stack.append((indent, ordered))
            out.append("<ol>" if ordered else "<ul>")
        out.append("<li>" + inline(" ".join(text)))
    close_to(-1)
    return "\n".join(out)


def render_table(header, rows):
    head = "".join(f"<th>{inline(c)}</th>" for c in header)
    body = "\n".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in rows)
    return f'<div class="tablewrap"><table><thead><tr>{head}</tr></thead>\n<tbody>\n{body}\n</tbody></table></div>'


def render_blocks(lines):
    out = []
    i, n = 0, len(lines)
    while i < n:
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        if line.lstrip().startswith("```"):
            j = i + 1
            while j < n and not lines[j].lstrip().startswith("```"):
                j += 1
            code = "\n".join(lines[i + 1:j])
            out.append(f'<div class="tablewrap"><pre><code>{html.escape(code, quote=False)}</code></pre></div>')
            i = j + 1
            continue
        m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
        if m:
            level = min(max(len(m.group(1)), 2), 4)
            out.append(f"<h{level}>{inline(m.group(2))}</h{level}>")
            i += 1
            continue
        if re.match(r"^\s*(-{3,}|\*{3,})\s*$", line):
            out.append("<hr>")
            i += 1
            continue
        if "|" in line and i + 1 < n and TABLE_SEP_RE.match(lines[i + 1]):
            header = split_row(line)
            j = i + 2
            rows = []
            while j < n and "|" in lines[j] and lines[j].strip():
                rows.append(split_row(lines[j]))
                j += 1
            out.append(render_table(header, rows))
            i = j
            continue
        if LIST_RE.match(line.expandtabs(4)):
            j = i
            block = []
            while j < n:
                cur = lines[j]
                if not cur.strip():
                    # a blank line stays inside the list only when an indented line or another item follows
                    k = j + 1
                    while k < n and not lines[k].strip():
                        k += 1
                    if k < n and (LIST_RE.match(lines[k].expandtabs(4)) or lines[k].startswith((" ", "\t"))):
                        j = k
                        continue
                    break
                if cur.lstrip().startswith("```") or (not LIST_RE.match(cur.expandtabs(4)) and not cur.startswith((" ", "\t"))):
                    break
                block.append(cur)
                j += 1
            out.append(render_list(block))
            i = j
            continue
        j = i
        para = []
        while j < n and lines[j].strip() and not re.match(r"^(#{1,6}\s|```)", lines[j]) \
                and not LIST_RE.match(lines[j].expandtabs(4)):
            para.append(lines[j].strip())
            j += 1
        out.append("<p>" + inline(" ".join(para)) + "</p>")
        i = j
    return "\n".join(out)


# ---------- the notes ----------

class Notes:
    def __init__(self, path):
        self.path = path
        m = FILE_RE.match(path.name)
        self.version = path.name[1:-3]
        self.key = (int(m.group(1)), int(m.group(2)), int(m.group(3)), 0 if m.group(4) else 1, m.group(4) or "")
        text = path.read_text(encoding="utf-8")
        self.discord = None
        d = DISCORD_RE.search(text)
        if d:
            self.discord = re.sub(r"\s+", " ", d.group(1)).strip()
        text = DISCORD_RE.sub("", text)
        lines = text.splitlines()
        while lines and not lines[0].strip():
            lines.pop(0)
        if not lines or not re.match(r"^# \S", lines[0]):
            raise ValueError(f"{path.name}: no '# title' on the first line")
        self.title = plain(lines[0][2:])
        self.body_lines = lines[1:]
        self.sentence = first_sentence(self.body_lines)
        if not self.sentence:
            raise ValueError(f"{path.name}: no paragraph after the title")

    @property
    def date(self):
        return DATES.get(self.version, "")

    def summary(self, limit=140):
        """The Discord one-liner: the explicit comment if present, else the first sentence, trimmed."""
        return trim(self.discord or self.sentence, limit)


def first_sentence(body_lines):
    para = []
    for line in body_lines:
        if not line.strip():
            if para:
                break
            continue
        if re.match(r"^(#|```|\||\s*([-*]|\d+\.)\s)", line):
            if para:
                break
            return ""
        para.append(line.strip())
    text = plain(" ".join(para))
    m = re.search(r"(?<=[.!?])\s+(?=[A-Z0-9*(\[])", text)
    return text[:m.start()].strip() if m else text


def trim(text, limit):
    text = text.strip()
    if len(text) <= limit:
        return text
    cut = text[:limit].rsplit(" ", 1)[0].rstrip(" ,;:-")
    return cut + "…"


DATES = {}


def load_dates(versions, overrides):
    for v in versions:
        try:
            out = subprocess.run(
                ["git", "for-each-ref", "--format=%(creatordate:short)", f"refs/tags/mdmm-v{v}"],
                cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            out = ""
        if out:
            DATES[v] = out
    DATES.update(overrides)


# ---------- the pages ----------

def load_frame():
    page = FRAME.read_text(encoding="utf-8")
    head = page[:page.index("<main")]
    foot = page[page.index("</main>") + len("</main>"):]
    return head, foot


def with_meta(head, title, description, canonical):
    head = re.sub(r"<title>.*?</title>", f"<title>{html.escape(title, quote=False)}</title>", head, count=1, flags=re.S)
    head = re.sub(r'(<meta name="description" content=")[^"]*"', lambda m: m.group(1) + html.escape(description) + '"', head, count=1)
    head = re.sub(r'(<link rel="canonical" href=")[^"]*"', lambda m: m.group(1) + canonical + '"', head, count=1)
    head = re.sub(r'(<meta property="og:url" content=")[^"]*"', lambda m: m.group(1) + canonical + '"', head, count=1)
    head = re.sub(r'(<meta property="og:title" content=")[^"]*"', lambda m: m.group(1) + html.escape(title) + '"', head, count=1)
    head = re.sub(r'(<meta property="og:description" content=")[^"]*"', lambda m: m.group(1) + html.escape(description) + '"', head, count=1)
    head = re.sub(r'(<meta name="twitter:title" content=")[^"]*"', lambda m: m.group(1) + html.escape(title) + '"', head, count=1)
    head = re.sub(r'(<meta name="twitter:description" content=")[^"]*"', lambda m: m.group(1) + html.escape(description) + '"', head, count=1)
    return head


def page(head, foot, title, description, canonical, main_inner):
    return (with_meta(head, title, description, canonical)
            + '<main id="main" class="checkout legalpage guide">\n  <div class="wrap">\n'
            + main_inner + "\n  </div>\n</main>" + foot)


def release_page(head, foot, n):
    title = f"MD + MM Editor {n.version} release notes"
    date = f'<p><small>Released {html.escape(n.date)}</small></p>\n' if n.date else ""
    inner = (f'    <span class="eyebrow">Release notes</span>\n    <h1>{inline(n.title)}</h1>\n{date}'
             + render_blocks(n.body_lines)
             + '\n    <p><a href="/releases/">All release notes</a> · <a href="/get/">Download</a></p>')
    return page(head, foot, title, n.sentence, f"{SITE_URL}/releases/{n.version}/", inner)


def index_page(head, foot, notes):
    items = []
    for n in notes:
        when = f" · {html.escape(n.date)}" if n.date else ""
        items.append(f'      <li><a href="/releases/{n.version}/"><b>{html.escape(n.version)}</b></a>{when}<br>'
                     f'{html.escape(trim(n.sentence, 220), quote=False)}</li>')
    inner = ('    <span class="eyebrow">Release notes</span>\n    <h1>Release notes</h1>\n'
             '    <p>What changed in each version of the Machinedrum Editor and the Monomachine Editor, newest first. '
             'The latest download is on the <a href="/get/">Download page</a>.</p>\n    <ul>\n'
             + "\n".join(items) + "\n    </ul>")
    return page(head, foot, "MD + MM Editor release notes",
                "What changed in each version of the Machinedrum Editor and the Monomachine Editor.",
                f"{SITE_URL}/releases/", inner)


def update_sitemap(notes):
    path = SITE / "sitemap.xml"
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if "/releases/" not in l]
    entries = [f"  <url><loc>{SITE_URL}/releases/</loc></url>"]
    entries += [f"  <url><loc>{SITE_URL}/releases/{n.version}/</loc></url>" for n in notes]
    at = next((i for i, l in enumerate(lines) if f"{SITE_URL}/guide/" in l), len(lines) - 2) + 1
    path.write_text("\n".join(lines[:at] + entries + lines[at:]) + "\n", encoding="utf-8")


def load_notes():
    notes = []
    for path in sorted(NOTES_DIR.glob("v*.md")):
        if not FILE_RE.match(path.name):
            continue
        try:
            notes.append(Notes(path))
        except ValueError as e:
            print(f"skipped: {e}", file=sys.stderr)
    notes.sort(key=lambda n: n.key, reverse=True)
    return notes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--summary", metavar="VERSION", help="print the Discord one-line summary of a version and stop")
    ap.add_argument("--date", action="append", default=[], metavar="VERSION=YYYY-MM-DD", help="release date when there is no tag")
    args = ap.parse_args()
    notes = load_notes()
    overrides = dict(d.split("=", 1) for d in args.date)
    if args.summary:
        for n in notes:
            if n.version == args.summary:
                print(n.summary())
                return 0
        print(f"no notes for {args.summary}", file=sys.stderr)
        return 1
    load_dates([n.version for n in notes], overrides)
    head, foot = load_frame()
    out = SITE / "releases"
    out.mkdir(parents=True, exist_ok=True)
    (out / "index.html").write_text(index_page(head, foot, notes), encoding="utf-8")
    for n in notes:
        (out / n.version).mkdir(exist_ok=True)
        (out / n.version / "index.html").write_text(release_page(head, foot, n), encoding="utf-8")
    update_sitemap(notes)
    print(f"{len(notes)} release pages in {out.relative_to(ROOT)}: " + ", ".join(n.version for n in notes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
