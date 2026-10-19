#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The manual as one HTML page, made from README.md: the user's part (from "Contents" through
"Installing on the FM-1", plus the licence and credits), with a contents column, links between
sections, and the pictures inlined, so the page works on its own, offline.

  tools/make_manual.py [OUT.html]          (default build/web/ravel-manual.html)

Needs Python's `markdown` package (pip install markdown). The emulator bundle (web/emu/bundle.py)
embeds the page behind its Manual button; the Pages site serves it as manual.html."""
import base64
import html
import re
import sys
from pathlib import Path

import markdown

sys.path.insert(0, str(Path(__file__).resolve().parent))

SRC = Path(__file__).resolve().parents[1]
DEV = ("What's here", "Building", "The FM-1 build")    # developers' sections: left to the repository


def slug(text, sep="-"):
    """GitHub's heading ids: lower case, punctuation dropped, spaces to hyphens (the README's links use them)"""
    text = re.sub(r"<[^>]+>", "", html.unescape(text)).strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


def sections(md):
    """split at '## ' headings (outside code blocks); returns (title, text) pairs, the first untitled"""
    out, cur, title, fence = [], [], None, False
    for line in md.splitlines():
        if line.startswith("```"):
            fence = not fence
        if not fence and line.startswith("## "):
            out.append((title, "\n".join(cur)))
            cur, title = [], line[3:].strip()
        cur.append(line)
    out.append((title, "\n".join(cur)))
    return out


def inline_images(page):
    def data(m):
        p = SRC / m.group(2)
        if not p.exists():
            return m.group(0)
        mime = "image/svg+xml" if p.suffix == ".svg" else "image/png"
        return f'{m.group(1)}data:{mime};base64,{base64.b64encode(p.read_bytes()).decode()}"'
    return re.sub(r'(src=")((?:docs)/[^"]+)"', data, page)


def build(out):
    md = (SRC / "README.md").read_text(encoding="utf-8")
    parts = sections(md)
    intro = parts[0][1]
    keep = [(t, s) for t, s in parts[1:] if t not in DEV]
    body_md = "\n\n".join(s for _, s in keep)
    # the contents' developer links go to the repository; the H1 "The manual" becomes a plain divider
    body_md = re.sub(r"^\*\*For developers:\*\*.*$", "", body_md, flags=re.M)
    body_md = body_md.replace("\n# The manual\n", "\n")
    lede = re.search(r"^Ravel is .*$", intro, re.M)
    status = re.search(r"^\*\*Status:.*$", intro, re.M)
    conv = markdown.Markdown(extensions=["tables", "toc", "sane_lists"],
                             extension_configs={"toc": {"slugify": slug, "toc_depth": "2-3"}})
    body = conv.convert(body_md)
    toc = conv.toc_tokens
    nav = []
    for h in toc:
        if h["level"] != 2:
            continue
        nav.append(f'<a href="#{h["id"]}">{h["name"]}</a>')
    head_html = markdown.markdown((lede.group(0) if lede else "") + "\n\n" + (status.group(0) if status else ""))
    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ravel manual</title>
<link rel="icon" type="image/png" href="__RAVEL_ICON__">
<link rel="apple-touch-icon" href="__RAVEL_TOUCH__">
<meta name="description" content="Ravel for the M-VAVE FM-1: every page, knob, button and key, how the parts connect, recipes and a quick reference.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Barlow+Semi+Condensed:wght@400;600;700&family=IBM+Plex+Mono:wght@400&display=swap" rel="stylesheet">
<style>
:root {{
  --room: #0b0b0b; --panel: #151515; --line: #2e2e2e; --ink: #e4e4e4; --muted: #9a9a9a;
  --red: #a3161c; --red-hi: #d23038; --code: #1c1c1c;
  --display: "Barlow Semi Condensed", "Arial Narrow", system-ui, sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, Menlo, monospace;
  color-scheme: dark;
}}
@media (prefers-color-scheme: light) {{
  :root:not([data-theme="dark"]) {{ --room: #f4f4f4; --panel: #ffffff; --line: #dcdcdc; --ink: #161616;
    --muted: #5e5e5e; --red: #a3161c; --red-hi: #a3161c; --code: #ececec; color-scheme: light; }}
}}
:root[data-theme="light"] {{ --room: #f4f4f4; --panel: #ffffff; --line: #dcdcdc; --ink: #161616;
  --muted: #5e5e5e; --red-hi: #a3161c; --code: #ececec; color-scheme: light; }}
* {{ box-sizing: border-box; }}
html {{ scroll-behavior: smooth; scroll-padding-top: 16px; }}
body {{ margin: 0; background: var(--room); color: var(--ink); font: 17px/1.55 var(--display); }}
a {{ color: var(--red-hi); }}
.layout {{ display: grid; grid-template-columns: 250px minmax(0, 1fr); max-width: 1180px; margin: 0 auto; }}
nav {{ position: sticky; top: 0; align-self: start; height: 100vh; overflow: auto; padding: 24px 18px 40px 16px;
       border-right: 1px solid var(--line); }}
nav .brand {{ font-weight: 700; letter-spacing: .14em; font-size: 22px; margin-bottom: 4px; }}
nav .brand span {{ color: var(--red-hi); }}
nav .sub {{ color: var(--muted); font-size: 13px; margin-bottom: 18px; }}
nav a {{ display: block; color: var(--ink); text-decoration: none; padding: 4px 8px; border-radius: 5px; font-size: 15px;
         border-left: 2px solid transparent; }}
nav a:hover {{ background: var(--panel); }}
nav a.on {{ border-left-color: var(--red-hi); background: var(--panel); }}
main {{ padding: 28px 32px 80px; min-width: 0; }}
main > .lede {{ font-size: 19px; color: var(--muted); }}
h1, h2, h3 {{ line-height: 1.2; }}
h1 {{ font-size: 34px; letter-spacing: .06em; margin: 0 0 8px; }}
h2 {{ font-size: 26px; margin: 56px 0 14px; padding-top: 14px; border-top: 1px solid var(--line); }}
h3 {{ font-size: 20px; margin: 30px 0 10px; }}
h2 a.anchor, h3 a.anchor {{ color: var(--muted); text-decoration: none; margin-left: 8px; font-size: .8em; opacity: 0; }}
h2:hover a.anchor, h3:hover a.anchor {{ opacity: 1; }}
p, li {{ max-width: 76ch; }}
code {{ font: 14px var(--mono); background: var(--code); padding: 1px 5px; border-radius: 4px; }}
pre {{ background: var(--code); padding: 12px 14px; border-radius: 6px; overflow: auto; }}
pre code {{ padding: 0; }}
table {{ border-collapse: collapse; margin: 14px 0 20px; display: block; overflow-x: auto; max-width: 100%; font-size: 15.5px; }}
th, td {{ border: 1px solid var(--line); padding: 6px 10px; vertical-align: top; text-align: left; }}
th {{ background: var(--panel); }}
img {{ max-width: 100%; height: auto; border-radius: 6px; image-rendering: pixelated; }}
img[src^="data:image/svg"] {{ image-rendering: auto; }}
p[align="center"] {{ display: flex; flex-wrap: wrap; gap: 10px; justify-content: center; }}
blockquote {{ margin: 16px 0; padding: 8px 16px; border-left: 3px solid var(--red-hi); background: var(--panel); }}
.top {{ position: fixed; right: 18px; bottom: 18px; background: var(--panel); border: 1px solid var(--line); color: var(--ink);
        border-radius: 6px; padding: 8px 12px; text-decoration: none; font-weight: 600; font-size: 14px; }}
.menu {{ display: none; }}
@media print {{
  :root, :root:not([data-theme="dark"]), :root[data-theme] {{ --room: #fff; --panel: #f2f2f2; --line: #cfcfcf; --ink: #111; --muted: #555;
    --red-hi: #a3161c; --code: #eeeeee; color-scheme: light; }}
  @page {{ size: A4; margin: 16mm 15mm 18mm; }}
  body {{ font-size: 11pt; line-height: 1.45; }}
  nav, .top {{ display: none !important; }}
  .layout {{ display: block; max-width: none; }}
  main {{ padding: 0; }}
  h1 {{ font-size: 30pt; margin-top: 60mm; }}
  main > .lede {{ font-size: 13pt; page-break-after: always; }}
  h2 {{ page-break-before: always; border-top: 0; margin-top: 0; padding-top: 0; font-size: 20pt; }}
  h2 a.anchor, h3 a.anchor {{ display: none; }}
  h2, h3 {{ page-break-after: avoid; }}
  table, img, blockquote, pre, li {{ page-break-inside: avoid; }}
  table {{ display: table; font-size: 9.5pt; }}
  img {{ max-width: 46%; }}
  img[src^="data:image/svg"] {{ max-width: 100%; }}
  a {{ color: var(--red-hi); text-decoration: none; }}
}}
@media (max-width: 820px) {{
  .layout {{ display: block; }}
  nav {{ position: static; height: auto; border-right: 0; border-bottom: 1px solid var(--line); padding: 16px; }}
  nav .links {{ display: none; }}
  nav.open .links {{ display: block; }}
  .menu {{ display: inline-block; margin-top: 4px; font: 600 14px var(--display); background: var(--panel);
           color: var(--ink); border: 1px solid var(--line); border-radius: 6px; padding: 6px 12px; }}
  main {{ padding: 16px 16px 80px; }}
}}
</style>
</head>
<body>
<div class="layout">
<nav id="nav">
  <div class="brand">RAVEL<span>.</span></div>
  <div class="sub">Manual · M-VAVE FM-1 firmware<span id="pdf" hidden> · <a href="manual.pdf">PDF</a> · <a href="./">Emulator</a></span></div>
  <button class="menu" id="menu" aria-expanded="false" aria-controls="links">Contents</button>
  <div class="links" id="links">
    {"".join(nav)}
  </div>
</nav>
<main>
<h1 id="top">Ravel manual</h1>
<div class="lede">{head_html}</div>
{body}
</main>
</div>
<a class="top" href="#top">Top</a>
<script>
(function () {{
  /* the section on screen is lit in the contents */
  var links = Array.prototype.slice.call(document.querySelectorAll("#links a"));
  var heads = links.map(function (a) {{ return document.getElementById(a.getAttribute("href").slice(1)); }});
  function lit() {{
    var y = window.scrollY + 80, k = 0;
    heads.forEach(function (h, i) {{ if (h && h.offsetTop <= y) k = i; }});
    links.forEach(function (a, i) {{ a.classList.toggle("on", i === k); }});
  }}
  window.addEventListener("scroll", lit, {{ passive: true }});
  lit();
  /* each heading links to itself */
  document.querySelectorAll("main h2[id], main h3[id]").forEach(function (h) {{
    var a = document.createElement("a");
    a.className = "anchor"; a.href = "#" + h.id; a.textContent = "#"; a.setAttribute("aria-label", "Link to this section");
    h.appendChild(a);
  }});
  if (/^https?:$/.test(location.protocol)) document.getElementById("pdf").hidden = false;   /* the site has the PDF beside it */
  var nav = document.getElementById("nav"), menu = document.getElementById("menu");
  menu.addEventListener("click", function () {{
    var open = nav.classList.toggle("open");
    menu.setAttribute("aria-expanded", open ? "true" : "false");
  }});
  links.forEach(function (a) {{ a.addEventListener("click", function () {{ nav.classList.remove("open"); }}); }});
}})();
</script>
</body>
</html>
"""
    page = inline_images(page)
    page = __import__("ravel_icons").apply(page)
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8")
    print(f"manual: {out} ({out.stat().st_size} B)")
    return page


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else SRC / "build" / "web" / "ravel-manual.html")
