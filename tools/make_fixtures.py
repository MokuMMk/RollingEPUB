#!/usr/bin/env python3
"""Generate deterministic EPUB fixtures for the RollingEPUB test suite.

Run from the repository root:  python tools/make_fixtures.py
Requires Pillow (PIL) for the image resources.

The generated books are intentionally tiny and cover:
  * EPUB 3 nav + EPUB 2 NCX (metadata/toc parsing)
  * Latin reflow text, headings, lists, blockquote, pre, hr, links
  * CJK text, punctuation and ruby annotations
  * same-file and cross-file footnotes (text markers and icon markers)
  * PNG / JPEG / GIF block images with known dimensions
  * CSS classes/ids, inline styles, @font-face, display:none, page breaks
  * cover image via properties="cover-image"
  * malformed containers for error-path tests
"""
from __future__ import annotations

import io
import os
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "tests" / "fixtures" / "epub"

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

CSS = """
@font-face { font-family: "BookSerif"; src: url("../fonts/book.ttf"); font-weight: normal; }
html, body { margin: 0; padding: 0; }
body { font-family: serif; font-size: 100%; line-height: 160%; }
p { margin-top: 0; margin-bottom: 0.6em; text-indent: 2em; text-align: justify; }
p.noindent { text-indent: 0; }
h1 { font-size: 180%; font-weight: bold; text-align: center; margin-top: 1em; margin-bottom: 1em; text-indent: 0; }
h2 { font-size: 140%; margin-top: 0.8em; margin-bottom: 0.6em; text-indent: 0; }
blockquote { margin-left: 2em; margin-right: 2em; font-style: italic; }
pre { white-space: pre; font-family: monospace; }
.center { text-align: center; text-indent: 0; }
.right { text-align: right; text-indent: 0; }
.small { font-size: 80%; }
.big { font-size: 150%; }
.bold { font-weight: bold; }
.italic { font-style: italic; }
.underline { text-decoration: underline; }
.strike { text-decoration: line-through; }
.hidden { display: none; }
.pb { page-break-before: always; }
.shaded { background-color: #eeeeee; border: 1px solid black; padding: 0.4em; }
#special-id { color: red; font-weight: bold; }
div.poem p { text-indent: 0; text-align: left; }
ul { margin-left: 1.5em; }
li { margin-bottom: 0.2em; }
sup { vertical-align: super; font-size: 70%; }
sub { vertical-align: sub; font-size: 70%; }
.dropcap::first-letter { initial-letter: 2; font-weight: bold; }
"""

LATIN_BODY = [
    ("h1", "The Rolling EPUB"),
    ("p", "This is a small deterministic book used by the RollingEPUB test suite. "
          "It contains enough text to exercise line wrapping, justification, first line "
          "indentation and hyphenation. The extraordinary representation of hyphenation "
          "opportunities should be discoverable in words like representation, "
          "extraordinary, typography and internationalization."),
    ("p", "A second paragraph follows, with an <em>italic phrase</em>, a <strong>bold "
          "phrase</strong>, an <a href=\"chapter2.xhtml\">internal link</a>, an "
          "<a href=\"https://example.com\">external link</a>, and a footnote marker"
          "<a id=\"r1\" href=\"#n1\"><sup>1</sup></a>."),
    ("blockquote", "A quotation sits in a block, indented on both sides and set in italic."),
    ("ul", "<li>First list item with a few words.</li>"
           "<li>Second list item with a longer sentence so wrapping is exercised in the list.</li>"
           "<li>Third item.</li>"),
    ("pre", "def hello():\n    return \"world\"\n\nprint(hello())"),
    ("hr", ""),
    ("p", "A paragraph after the horizontal rule tests spacing around a rule."),
    ("p", "Superscript x<sup>2</sup> and subscript H<sub>2</sub>O appear here. "
          "A drop cap paragraph follows."),
    ("p", "Rolling epub readers render reflowable books on small screens and the layout "
          "engine must paginate deterministically across devices."),
]
DROP_CAP = ("p", "Once upon a time a typesetter measured every glyph carefully, and the "
                 "reader noticed the quiet rhythm of well set text on paper.")

CJK_BODY = [
    ("h1", "河边记事"),
    ("p", "清晨的河面泛着薄雾，岸边的芦苇在风里轻轻摇动。"
          "他把书摊开在膝头，读了一页又一页，直到太阳升到树梢。"),
    ("p", "这本书讲的是普通人的故事：有人远行，有人归来，有人守着一间旧屋。"
          "文字平实，却让人想起许多被忘记的细节。"),
    ("p", "标点符号应当遵守禁则：句号、逗号、问号、叹号不能出现在行首；"
          "左括号和左引号不能出现在行尾。"),
    ("p", "这是一段很长的中文段落，用来测试分页是否稳定。"
          "排版引擎需要同时处理汉字逐字换行、标点挤压和首行缩进。"
          "如果实现正确，同一段文字在任何一次布局中都会得到相同的页数与位置。"
          "秋风起时，河边的树叶落了一地，水面上漂着几只纸船。"),
    ("p", "竖排文字从右向左阅读，标点位于字框的右上角。"),
    ("p", "@@@RUBY@@@"),
    ("blockquote", "引用的句子也要遵守标点禁则。"),
]

NOTE1 = ("The first footnote explains the historical background of the phrase used above. "
         "It is deliberately long enough to need several lines in a popup.")
NOTE2 = ("A cross-file note lives in another document and is pulled in by the body link. "
         "Back-links and noteref anchors must be skipped when extracting the text.")
NOTE_ICON = "Icon footnote text."


def img_png() -> bytes:
    im = Image.new("RGBA", (64, 40), (255, 255, 255, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 63, 39), fill=(30, 60, 90, 255))
    d.ellipse((16, 8, 47, 31), fill=(220, 180, 40, 255))
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()


def img_jpeg() -> bytes:
    im = Image.new("RGB", (80, 60), (240, 240, 240))
    d = ImageDraw.Draw(im)
    for x in range(80):
        d.line((x, 0, x, 59), fill=(x * 3 % 255, 50, 200 - x * 2 % 180))
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def img_gif() -> bytes:
    im = Image.new("P", (32, 32))
    palette = []
    for i in range(256):
        palette += [i, (i * 7) % 256, (i * 13) % 256]
    im.putpalette(palette)
    px = im.load()
    for y in range(32):
        for x in range(32):
            px[x, y] = (x + y) % 256
    buf = io.BytesIO()
    im.save(buf, format="GIF")
    return buf.getvalue()


def xhtml(title: str, body: str, extra_ns: str = "") -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"{extra_ns}>
<head><title>{title}</title><link rel="stylesheet" type="text/css" href="../styles/book.css"/></head>
<body>
{body}
</body>
</html>
"""


def chapter1() -> str:
    parts = []
    for kind, text in LATIN_BODY:
        if kind == "ul":
            parts.append(f"<ul>{text}</ul>")
        elif kind == "hr":
            parts.append("<hr/>")
        else:
            parts.append(f"<{kind}>{text}</{kind}>")
    parts.append(f"<p class=\"dropcap\">{DROP_CAP[1]}</p>")
    parts.append("<p class=\"hidden\">This hidden paragraph must never be laid out.</p>")
    parts.append("<p id=\"special-id\">A paragraph with a stylesheet id selector.</p>")
    parts.append("<div class=\"poem\"><p>line one of a poem</p><p>line two of a poem</p></div>")
    parts.append("<p class=\"small\">Small text.</p><p class=\"big\">Big text.</p>")
    parts.append("<p class=\"bold\">Bold class.</p><p class=\"italic\">Italic class.</p>")
    parts.append("<p class=\"underline\">Underlined text.</p>")
    parts.append("<p class=\"strike\">Struck text.</p>")
    parts.append("<p class=\"shaded\">Shaded bordered box.</p>")
    parts.append("<p class=\"pb\">This paragraph forces a page break before it.</p>")
    parts.append("<p><img src=\"../images/pic.png\" alt=\"illustration\"/></p>")
    parts.append("<p>Text after an image.</p>")
    parts.append(f"<aside epub:type=\"footnote\" id=\"n1\">{NOTE1}</aside>")
    parts.append("<p>Cross-file notes: a text marker<a id=\"r2\" href=\"notes.xhtml#note-x\">"
                 "<sup>2</sup></a> and an icon marker<a id=\"r3\" href=\"notes.xhtml#note-icon\">"
                 "<img src=\"../images/pic.png\"/></a> point at another document.</p>")
    return xhtml("Chapter One", "\n".join(parts))


def chapter2() -> str:
    body = """
<h1>Chapter Two</h1>
<p>A short chapter used for internal link targets and progress checks.</p>
<p><a id="target-anchor"/>Anchored paragraph target.</p>
<p>Another paragraph with enough words to occupy a couple of lines on a small page. The end.</p>
<table>
  <tr><th>Name</th><th>Value</th></tr>
  <tr><td>alpha</td><td>one</td></tr>
  <tr><td>beta</td><td>two</td></tr>
</table>
"""
    return xhtml("Chapter Two", body)


def notes_chapter() -> str:
    body = f"""
<h1>Notes</h1>
<aside epub:type="footnote" id="note-x">{NOTE2}</aside>
<aside epub:type="footnote" id="note-icon">{NOTE_ICON}</aside>
"""
    return xhtml("Notes", body)


def cjk_chapter() -> str:
    parts = []
    for kind, text in CJK_BODY:
        if "@@@" in text:
            parts.append('<p><ruby>汉<rt>hàn</rt></ruby>字<ruby>注<rt>zhù</rt></ruby>音。</p>')
        else:
            parts.append(f"<{kind}>{text}</{kind}>")
    parts.append("<p><img src=\"../images/pic.png\"/></p>")
    return xhtml("河边记事", "\n".join(parts))


def opf(title: str, language: str, chapters: list[tuple[str, str]],
        extra_manifest: str = "", extra_metadata: str = "") -> str:
    items = "\n".join(
        f'    <item id="{cid}" href="{href}" media-type="application/xhtml+xml"/>'
        for cid, href in chapters)
    spine = "\n".join(f'    <itemref idref="{cid}"/>' for cid, _ in chapters)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">urn:uuid:rolling-epub-fixture</dc:identifier>
    <dc:title>{title}</dc:title>
    <dc:creator>RollingEPUB Test Suite</dc:creator>
    <dc:language>{language}</dc:language>
    <dc:publisher>RollingEPUB</dc:publisher>
    <dc:date>2026-01-01</dc:date>
    <dc:description>Deterministic test fixture.</dc:description>
    <meta property="belongs-to-collection">Test Series</meta>
    <meta property="group-position">2</meta>
    <meta name="cover" content="cover-image"/>
    {extra_metadata}
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="../styles/book.css" media-type="text/css"/>
    <item id="cover-image" href="../images/cover.png" media-type="image/png" properties="cover-image"/>
    {items}
    {extra_manifest}
  </manifest>
  <spine toc="ncx">
{spine}
  </spine>
</package>
"""


def nav_doc(chapters: list[tuple[str, str, str]]) -> str:
    lis = "\n".join(f'      <li><a href="{href}">{title}</a></li>' for href, title, _ in chapters)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Contents</title></head>
<body>
  <nav epub:type="toc" id="toc">
    <h1>Contents</h1>
    <ol>
{lis}
    </ol>
  </nav>
</body>
</html>
"""


def ncx_doc(title: str, chapters: list[tuple[str, str, str]]) -> str:
    points = "\n".join(
        f'    <navPoint id="np{i}" playOrder="{i}"><navLabel><text>{t}</text></navLabel>'
        f'<content src="{h}"/></navPoint>'
        for i, (h, t, _) in enumerate(chapters, 1))
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:uuid:rolling-epub-fixture"/></head>
  <docTitle><text>{title}</text></docTitle>
  <navMap>
{points}
  </navMap>
</ncx>
"""


def build(path: Path, files: dict[str, bytes | str], manifest: dict[str, bytes | str]) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        written = set()
        for name, data in files.items():
            if name in written:
                continue
            written.add(name)
            z.writestr(name, data if isinstance(data, bytes) else data.encode("utf-8"))
        for name, data in manifest.items():
            if name in written:
                continue
            z.writestr(name, data if isinstance(data, bytes) else data.encode("utf-8"))


def common_files(cover: bytes, pic: bytes, jpeg: bytes, gif: bytes,
                 chapters: dict[str, str], nav: str, ncx: str, opf_text: str) -> dict[str, bytes | str]:
    files: dict[str, bytes | str] = {
        "mimetype": b"application/epub+zip",
        "META-INF/container.xml": CONTAINER,
        "OEBPS/content.opf": opf_text,
        "OEBPS/nav.xhtml": nav,
        "OEBPS/toc.ncx": ncx,
        "styles/book.css": CSS,
        "images/cover.png": cover,
        "images/pic.png": pic,
        "images/photo.jpg": jpeg,
        "images/anim.gif": gif,
    }
    files.update(chapters)
    return files


def make_latin() -> None:
    chapters = [("ch1", "chapter1.xhtml"), ("ch2", "chapter2.xhtml"),
                ("notes", "notes.xhtml")]
    opf_text = opf("The Rolling EPUB", "en", chapters,
                   extra_manifest='    <item id="photo" href="../images/photo.jpg" media-type="image/jpeg"/>\n'
                                  '    <item id="anim" href="../images/anim.gif" media-type="image/gif"/>')
    nav = nav_doc([("chapter1.xhtml", "Chapter One", "ch1"),
                   ("chapter2.xhtml", "Chapter Two", "ch2"),
                   ("notes.xhtml", "Notes", "notes")])
    ncx = ncx_doc("The Rolling EPUB", [("chapter1.xhtml", "Chapter One", "ch1"),
                                       ("chapter2.xhtml", "Chapter Two", "ch2"),
                                       ("notes.xhtml", "Notes", "notes")])
    files = common_files(img_png(), img_png(), img_jpeg(), img_gif(),
                         {"OEBPS/chapter1.xhtml": chapter1(),
                          "OEBPS/chapter2.xhtml": chapter2(),
                          "OEBPS/notes.xhtml": notes_chapter()}, nav, ncx, opf_text)
    build(OUT / "latin.epub", files, {})


def make_cjk() -> None:
    chapters = [("ch1", "chapter1.xhtml"), ("notes", "notes.xhtml")]
    opf_text = opf("河边记事", "zh-CN", chapters)
    nav = nav_doc([("chapter1.xhtml", "河边记事", "ch1"),
                   ("notes.xhtml", "注释", "notes")])
    ncx = ncx_doc("河边记事", [("chapter1.xhtml", "河边记事", "ch1"),
                               ("notes.xhtml", "注释", "notes")])
    files = common_files(img_png(), img_png(), img_jpeg(), img_gif(),
                         {"OEBPS/chapter1.xhtml": cjk_chapter(),
                          "OEBPS/notes.xhtml": notes_chapter()}, nav, ncx, opf_text)
    build(OUT / "cjk.epub", files, {})


def make_images() -> None:
    ch = xhtml("Images", """
<h1>Images</h1>
<p><img src="../images/pic.png"/></p>
<p><img src="../images/photo.jpg"/></p>
<p><img src="../images/anim.gif"/></p>
<p><img src="../images/missing.png"/></p>
""")
    chapters = [("ch1", "chapter1.xhtml")]
    opf_text = opf("Image Fixture", "en", chapters)
    nav = nav_doc([("chapter1.xhtml", "Images", "ch1")])
    ncx = ncx_doc("Image Fixture", [("chapter1.xhtml", "Images", "ch1")])
    files = common_files(img_png(), img_png(), img_jpeg(), img_gif(),
                         {"OEBPS/chapter1.xhtml": ch}, nav, ncx, opf_text)
    build(OUT / "images.epub", files, {})


def make_malformed() -> None:
    # Truncated zip: copy a valid book and cut it in half.
    data = (OUT / "latin.epub").read_bytes()
    (OUT / "broken-truncated.epub").write_bytes(data[: len(data) // 2])

    # Broken OPF XML.
    opf_text = opf("Broken", "en", [("ch1", "chapter1.xhtml")]).replace("</manifest>", "</broken>")
    nav = nav_doc([("chapter1.xhtml", "Broken", "ch1")])
    ncx = ncx_doc("Broken", [("chapter1.xhtml", "Broken", "ch1")])
    files = common_files(img_png(), img_png(), img_jpeg(), img_gif(),
                         {"OEBPS/chapter1.xhtml": chapter2()}, nav, ncx, opf_text)
    build(OUT / "broken-opf.epub", files, {})

    # No spine entries.
    opf_text = opf("No Spine", "en", [])
    nav = nav_doc([("chapter1.xhtml", "Empty", "ch1")])
    ncx = ncx_doc("No Spine", [("chapter1.xhtml", "Empty", "ch1")])
    files = common_files(img_png(), img_png(), img_jpeg(), img_gif(),
                         {"OEBPS/chapter1.xhtml": chapter2()}, nav, ncx, opf_text)
    build(OUT / "no-spine.epub", files, {})


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    make_latin()
    make_cjk()
    make_images()
    make_malformed()
    for f in sorted(OUT.glob("*.epub")):
        print(f"{f.name:32s} {f.stat().st_size:8d} bytes")


if __name__ == "__main__":
    main()
