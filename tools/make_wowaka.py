#!/usr/bin/env python3
"""Generate `tests/fixtures/epub/wowaka.epub`.

A dedicated, deterministic test book with a single theme: **wowaka**
(ヲワカ, 1987-2019), Vocaloid producer and vocalist/guitarist of the band
hitorie.  The prose is original commemorative fan writing (no lyrics), used
only to exercise the RollingEPUB reader.

The book deliberately contains every content feature the library supports:

  * EPUB 3 nav + EPUB 2 NCX, cover, series metadata, TOC nesting
  * h1-h6, paragraphs, blockquote, pre/code, ul/ol, table rows, hr, br
  * bold / italic / underline / strike / superscript / subscript
  * internal + external links, anchors, id/class CSS selectors
  * same-file and cross-file footnotes, text markers and an icon marker
  * ruby annotations, drop caps, shaded/bordered box, page breaks
  * display:none content, white-space: pre, inline styles
  * illustrations interleaved with text (mixed pages), image-only pages
  * PNG / JPEG / GIF / BMP image formats with known dimensions
  * a CJK vertical-writing chapter for `WRITING_VERTICAL_RL` tests

Run:  python tools/make_wowaka.py
"""
from __future__ import annotations

import io
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "tests" / "fixtures" / "epub" / "wowaka.epub"

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

CSS = """
html, body { margin: 0; padding: 0; }
body { font-family: serif; font-size: 100%; line-height: 165%; }
p { margin-top: 0; margin-bottom: 0.7em; text-indent: 2em; text-align: justify; }
p.noindent { text-indent: 0; }
h1 { font-size: 190%; font-weight: bold; text-align: center; margin: 0.8em 0 0.8em 0; text-indent: 0; }
h2 { font-size: 150%; font-weight: bold; margin: 0.8em 0 0.5em 0; text-indent: 0; }
h3 { font-size: 130%; font-weight: bold; margin: 0.6em 0 0.4em 0; text-indent: 0; }
h4 { font-size: 115%; font-weight: bold; margin: 0.5em 0 0.3em 0; text-indent: 0; }
h5 { font-size: 105%; font-weight: bold; margin: 0.4em 0 0.2em 0; text-indent: 0; }
h6 { font-size: 100%; font-weight: bold; margin: 0.3em 0 0.2em 0; text-indent: 0; }
blockquote { margin-left: 2em; margin-right: 2em; font-style: italic; }
pre { white-space: pre; font-family: monospace; font-size: 85%; }
code { font-family: monospace; }
ul, ol { margin-left: 1.6em; }
li { margin-bottom: 0.25em; }
.center { text-align: center; text-indent: 0; }
.right { text-align: right; text-indent: 0; }
.small { font-size: 80%; }
.large { font-size: 140%; }
.bold { font-weight: bold; }
.italic { font-style: italic; }
.underline { text-decoration: underline; }
.strike { text-decoration: line-through; }
.hidden { display: none; }
.pb { page-break-before: always; }
.shaded { background-color: #eeeeee; border: 1px solid black; padding: 0.5em; text-indent: 0; }
.dropcap::first-letter { initial-letter: 3; font-weight: bold; }
#timeline { font-size: 90%; }
table { margin-left: 1em; margin-bottom: 0.8em; }
th { font-weight: bold; }
caption { font-style: italic; font-size: 90%; }
.caption { text-align: center; text-indent: 0; font-size: 85%; }
a { text-decoration: underline; }
"""

LIFE = """
<h1>1987 — 2019</h1>
<p class="dropcap">ヲワカ，通常写作 wowaka，1987 年 11 月 4 日出生于日本东京。
他最初以 Vocaloid 制作人的身份被听众记住，后来成为乐队 hitorie 的主唱与吉他手。
这条创作线索从卧室里的音乐软件一直延伸到舞台的灯光下，始终没有真正中断。</p>
<p>在 Vocaloid 场景里，wowaka 的早期作品以密集的吉他切分、快速的鼓点和略带神经质的旋律著称。
“World's End Dancehall”“Rollin' Girl”“Unknown Mother Goose”等歌曲让许多听众第一次意识到：
电子合成的人声也可以承载摇滚乐队般的冲劲。</p>
<h2>时间线 <span class="small">（测试用表格）</span></h2>
<table id="timeline">
  <caption>wowaka 创作年表（节选）</caption>
  <tr><th>年份</th><th>事件</th><th>关联</th></tr>
  <tr><td>1987</td><td>11 月 4 日出生于东京</td><td>—</td></tr>
  <tr><td>2009</td><td>开始公开发表 Vocaloid 作品</td><td>初音未来</td></tr>
  <tr><td>2011</td><td>组建乐队 hitorie</td><td>主唱 / 吉他</td></tr>
  <tr><td>2019</td><td>4 月 5 日逝世</td><td>享年 31 岁</td></tr>
</table>
<h3>声音的形状</h3>
<p>他的编曲常常把声音切成短促的方块：吉他像节拍器一样反复敲击，贝斯在低处游走，
人声则被推到最前面。听久了会发现，那些看似凌乱的音符其实排列得非常整齐，
像一段被反复擦写又反复保留的铅笔草稿<a href="#note-life"><sup>1</sup></a>。</p>
<p class="noindent"><a href="hitorie.xhtml#band">下一章：hitorie 与舞台</a></p>
<h4>一份工作笔记</h4>
<pre>tempo = 196 bpm
pattern: kick - snare - kick kick - snare
guitar: short muted chords, no long sustains
vocal: keep the melody nervous, never too smooth</pre>
<h5>列表</h5>
<ul>
  <li>Vocaloid 时期的作品：World's End Dancehall、Rollin' Girl</li>
  <li>乐队时期的作品：hitorie 的专辑与巡演</li>
  <li>影响：许多后来的制作人开始尝试更快的节拍与更硬的吉他</li>
</ul>
<ol>
  <li>先听见鼓点</li>
  <li>再听见人声</li>
  <li>最后记住旋律</li>
</ol>
<hr/>
<h6>一段引用</h6>
<blockquote>“把想说的话写进歌里，然后把它交给愿意听的人。”</blockquote>
<p class="shaded">这个灰色边框盒子用于测试 CSS 背景与边框绘制。它同时包含
<span class="bold">粗体</span>、<span class="italic">斜体</span>、
<span class="underline">下划线</span> 和 <span class="strike">删除线</span>。</p>
<p>上下标测试：H<sub>2</sub>O、E = mc<sup>2</sup>；行内代码 <code>hitorie.play()</code>。
换行测试：第一行<br/>第二行。</p>
<p class="hidden">这段文字设置了 display:none，不应出现在排版结果里。</p>
<p class="pb">这一段设置了 page-break-before，应该从新的一页开始。</p>
<aside epub:type="footnote" id="note-life">注 1：本文中的时间与作品信息为测试用整理，欢迎读者自行查证。</aside>
"""

HITORIE = """
<h1>hitorie 与舞台</h1>
<p id="band">hitorie（ヒトリエ）成立于 2011 年，wowaka 担任主唱与吉他手。
和 Vocaloid 时期的单人创作不同，乐队编制让他的歌有了更直接的呼吸：
鼓、贝斯、吉他和人声在同一间排练室里互相推挤，最后形成一种既精密又冲动的声音。</p>
<p>舞台上的他常常只是站在麦克风前，话不多，灯亮起来就开始弹。
有观众说，看 hitorie 的现场像是看一个人把自己写下的草稿当场重写一遍。
那些已经发行过的歌，在现场会被拉长、加速，或者突然停在某个和弦上<a href="notes.xhtml#note-cross"><sup>2</sup></a>。</p>
<p>这一页还测试了<ruby>汉字<rt>hànzì</rt></ruby>注音、内部链接与图片混排。</p>
<p class="noindent center"><img src="../images/portrait.png" alt="abstract portrait"/></p>
<p class="caption">图 1：抽象的舞台灯光（测试插图，非真实照片）。</p>
<p>wowaka 在 2019 年 4 月 5 日去世，年仅 31 岁。消息公布后，
许多音乐人重新翻唱了他的作品，hitorie 的成员也继续以乐队的形式活动。
对这些歌的听众来说，作品本身比任何解释都更长久。</p>
<p><a href="gallery.xhtml">继续：影像与色彩</a> ·
<a href="https://example.com/wowaka">外部链接（示例）</a></p>
"""

GALLERY = """
<h1>影像与色彩</h1>
<p>这一章把文字与图片交错排列，用来测试图文混排、图片缩放与抖动：
文字在前，插图在后，下一段文字再继续。每张图都有已知尺寸，测试会逐一核对。</p>
<p>第一张是 1600×1000 的黑白棋盘格 PNG。它会在页面上被缩小，
细密的格子正好用来观察双线性重采样的灰度过渡。</p>
<p class="noindent center"><img src="../images/checkerboard.png" alt="checkerboard"/></p>
<p class="caption">图 2：棋盘格 PNG，1600×1000。</p>
<p>第二张是 1200×800 的 JPEG 渐变照片式插图，用来测试 JPEG 解码与 2× 放大查看。</p>
<p class="noindent center"><img src="../images/photo.jpg" alt="gradient photo"/></p>
<p class="caption">图 3：JPEG，1200×800。</p>
<p>第三张是 GIF（只取第一帧），第四张是 BMP。两种格式都在支持列表里。</p>
<p class="noindent center"><img src="../images/motion.gif" alt="animated gif"/></p>
<p class="caption">图 4：GIF，320×200，两帧动画取第一帧。</p>
<p class="noindent center"><img src="../images/outline.bmp" alt="bmp"/></p>
<p class="caption">图 5：BMP，800×600。</p>
<p>最后是一张 800×2000 的竖长 PNG，高度超过一页，应形成图片独占页。</p>
<p class="noindent center"><img src="../images/tall.png" alt="tall illustration"/></p>
<p>图文混排测试结束。下面进入竖排章节。<a href="notes.xhtml#note-icon"><img src="../images/icon.png" alt="note"/></a></p>
"""

VERTICAL = """
<h1>竖排：从右到左</h1>
<p>这一章用于测试竖排文字。汉字自上而下排列，行序自右向左。
标点遵循竖排禁则：句号与逗号不应出现在行首，左括号与左引号不应出现在行尾。</p>
<p>秋风起时，河边的树叶落了一地，水面上漂着几只纸船。排版引擎需要同时处理
逐字换行、标点位置、首行缩进与分页稳定性。竖排模式下的字距与行距由同一套字体度量决定。</p>
<p>「こんにちは、wowaka。」<ruby>声音<rt>sound</rt></ruby>从舞台上传来，
观众安静地听着。竖排的英文与括号会被旋转 90 度，以保持行气。</p>
<p>最后一段仍然使用中文标点：，。！？（）《》——用来检查竖排禁则与标点位置。</p>
"""

NOTES = """
<h1>注释</h1>
<aside epub:type="footnote" id="note-cross">注 2（跨文件）：hitorie 的现场编曲常把 Vocaloid 时期的歌曲改写成乐队版本，
节奏更直接，吉他也更突出。</aside>
<aside epub:type="footnote" id="note-icon">注 3（图标注号）：这个注释由正文中的小图标打开。</aside>
"""


def img_cover() -> bytes:
    im = Image.new("RGB", (600, 900), (16, 20, 28))
    d = ImageDraw.Draw(im)
    for y in range(900):
        d.line((0, y, 600, y), fill=(16 + y // 6, 20 + y // 9, 28 + y // 5))
    d.ellipse((120, 180, 480, 540), fill=(210, 180, 70))
    d.ellipse((200, 260, 400, 460), fill=(240, 220, 140))
    d.rectangle((60, 650, 540, 656), fill=(230, 230, 230))
    d.rectangle((60, 700, 400, 704), fill=(180, 180, 180))
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=88)
    return buf.getvalue()


def img_portrait() -> bytes:
    im = Image.new("RGBA", (480, 640), (255, 255, 255, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 479, 639), fill=(26, 28, 40, 255))
    for i in range(12):
        x = 30 + i * 38
        d.rectangle((x, 80, x + 18, 560), fill=(52, 58, 78, 255))
    d.ellipse((120, 180, 360, 460), fill=(222, 196, 120, 255))
    d.rectangle((60, 520, 420, 536), fill=(240, 240, 240, 255))
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()


def img_checkerboard() -> bytes:
    im = Image.new("L", (1600, 1000), 255)
    d = ImageDraw.Draw(im)
    cell = 20
    for y in range(0, 1000, cell):
        for x in range(0, 1600, cell):
            if ((x // cell) + (y // cell)) % 2 == 0:
                d.rectangle((x, y, x + cell - 1, y + cell - 1), fill=0)
    # A colored circle makes the downscale/bilinear path visible.
    im = im.convert("RGB")
    d = ImageDraw.Draw(im)
    d.ellipse((560, 260, 1040, 740), fill=(220, 70, 60))
    d.ellipse((700, 400, 900, 600), fill=(250, 220, 90))
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()


def img_photo() -> bytes:
    im = Image.new("RGB", (1200, 800), (240, 240, 240))
    d = ImageDraw.Draw(im)
    for y in range(800):
        d.line((0, y, 1200, y), fill=(40 + y // 5, 80, 140 - y // 7))
    d.ellipse((320, 120, 880, 680), fill=(250, 200, 80))
    d.rectangle((0, 620, 1200, 800), fill=(20, 40, 60))
    for i in range(9):
        x = 60 + i * 130
        d.rectangle((x, 560, x + 30, 800), fill=(30, 30, 36))
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=87)
    return buf.getvalue()


def img_gif() -> bytes:
    frames = []
    for offset in (0, 12):
        frame = Image.new("P", (320, 200), 0)
        palette = []
        for i in range(256):
            palette += [i, (i * 5) % 256, (i * 11) % 256]
        frame.putpalette(palette)
        d = ImageDraw.Draw(frame)
        d.rectangle((0, 0, 319, 199), fill=40)
        d.ellipse((40 + offset, 40, 280 + offset, 160), fill=200)
        d.rectangle((20, 150, 300, 180), fill=120)
        frames.append(frame)
    buf = io.BytesIO()
    frames[0].save(buf, format="GIF", save_all=True, append_images=frames[1:], duration=200, loop=0)
    return buf.getvalue()


def img_bmp() -> bytes:
    im = Image.new("RGB", (800, 600), (18, 24, 34))
    d = ImageDraw.Draw(im)
    for i in range(8):
        d.rectangle((40 + i * 90, 80, 100 + i * 90, 520), fill=(40 + i * 20, 90, 160 - i * 10))
    d.ellipse((300, 180, 500, 380), fill=(240, 190, 70))
    buf = io.BytesIO()
    im.save(buf, format="BMP")
    return buf.getvalue()


def img_tall() -> bytes:
    im = Image.new("RGB", (800, 2000), (255, 255, 255))
    d = ImageDraw.Draw(im)
    for y in range(0, 2000, 50):
        shade = 240 - (y * 200 // 2000)
        d.rectangle((0, y, 799, y + 49), fill=(shade, shade, shade))
    d.rectangle((100, 200, 700, 1800), outline=(30, 30, 30), width=8)
    for i in range(10):
        d.ellipse((150, 300 + i * 140, 650, 400 + i * 140), outline=(200, 60, 60), width=6)
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()


def img_icon() -> bytes:
    im = Image.new("RGBA", (24, 24), (255, 255, 255, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 23, 23), fill=(30, 30, 30, 255))
    d.rectangle((6, 5, 17, 7), fill=(240, 240, 240, 255))
    d.rectangle((6, 10, 17, 12), fill=(240, 240, 240, 255))
    d.rectangle((6, 15, 17, 17), fill=(240, 240, 240, 255))
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()


def xhtml(title: str, body: str) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>{title}</title><link rel="stylesheet" type="text/css" href="../styles/book.css"/></head>
<body>
{body}
</body>
</html>
"""


def opf() -> str:
    return """<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">urn:uuid:rolling-epub-wowaka-fixture</dc:identifier>
    <dc:title>wowaka 与 hitorie：一条没有终点的旋律</dc:title>
    <dc:creator>RollingEPUB 测试书</dc:creator>
    <dc:language>zh-CN</dc:language>
    <dc:publisher>RollingEPUB</dc:publisher>
    <dc:date>2026-01-01</dc:date>
    <dc:description>为 RollingEPUB 功能测试制作的原创纪念性文字，非官方资料，不含歌词。</dc:description>
    <meta property="belongs-to-collection">RollingEPUB Fixtures</meta>
    <meta property="group-position">1</meta>
    <meta name="cover" content="cover-image"/>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="../styles/book.css" media-type="text/css"/>
    <item id="cover-image" href="../images/cover.jpg" media-type="image/jpeg" properties="cover-image"/>
    <item id="title" href="text/title.xhtml" media-type="application/xhtml+xml"/>
    <item id="life" href="text/life.xhtml" media-type="application/xhtml+xml"/>
    <item id="hitorie" href="text/hitorie.xhtml" media-type="application/xhtml+xml"/>
    <item id="gallery" href="text/gallery.xhtml" media-type="application/xhtml+xml"/>
    <item id="vertical" href="text/vertical.xhtml" media-type="application/xhtml+xml"/>
    <item id="notes" href="text/notes.xhtml" media-type="application/xhtml+xml"/>
    <item id="img-portrait" href="../images/portrait.png" media-type="image/png"/>
    <item id="img-checker" href="../images/checkerboard.png" media-type="image/png"/>
    <item id="img-photo" href="../images/photo.jpg" media-type="image/jpeg"/>
    <item id="img-gif" href="../images/motion.gif" media-type="image/gif"/>
    <item id="img-bmp" href="../images/outline.bmp" media-type="image/bmp"/>
    <item id="img-tall" href="../images/tall.png" media-type="image/png"/>
    <item id="img-icon" href="../images/icon.png" media-type="image/png"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="title"/>
    <itemref idref="life"/>
    <itemref idref="hitorie"/>
    <itemref idref="gallery"/>
    <itemref idref="vertical"/>
    <itemref idref="notes"/>
  </spine>
</package>
"""


def nav() -> str:
    return """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>目录</title></head>
<body>
  <nav epub:type="toc" id="toc">
    <h1>目录</h1>
    <ol>
      <li><a href="text/title.xhtml">扉页</a></li>
      <li><a href="text/life.xhtml">1987 — 2019</a>
        <ol>
          <li><a href="text/life.xhtml#timeline">时间线</a></li>
        </ol>
      </li>
      <li><a href="text/hitorie.xhtml">hitorie 与舞台</a></li>
      <li><a href="text/gallery.xhtml">影像与色彩</a></li>
      <li><a href="text/vertical.xhtml">竖排：从右到左</a></li>
      <li><a href="text/notes.xhtml">注释</a></li>
    </ol>
  </nav>
</body>
</html>
"""


def ncx() -> str:
    return """<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:uuid:rolling-epub-wowaka-fixture"/></head>
  <docTitle><text>wowaka 与 hitorie</text></docTitle>
  <navMap>
    <navPoint id="np1" playOrder="1"><navLabel><text>扉页</text></navLabel><content src="text/title.xhtml"/></navPoint>
    <navPoint id="np2" playOrder="2"><navLabel><text>1987 — 2019</text></navLabel><content src="text/life.xhtml"/></navPoint>
    <navPoint id="np3" playOrder="3"><navLabel><text>hitorie 与舞台</text></navLabel><content src="text/hitorie.xhtml"/></navPoint>
    <navPoint id="np4" playOrder="4"><navLabel><text>影像与色彩</text></navLabel><content src="text/gallery.xhtml"/></navPoint>
    <navPoint id="np5" playOrder="5"><navLabel><text>竖排：从右到左</text></navLabel><content src="text/vertical.xhtml"/></navPoint>
    <navPoint id="np6" playOrder="6"><navLabel><text>注释</text></navLabel><content src="text/notes.xhtml"/></navPoint>
  </navMap>
</ncx>
"""


def title_chapter() -> str:
    return xhtml("扉页", """
<span epub:type="title">wowaka</span>
<h1>一条没有终点的旋律</h1>
<p class="noindent center"><img src="../images/cover.jpg" alt="cover"/></p>
<p class="caption">ヲワカ / wowaka（1987—2019）</p>
<p class="noindent center">这是一本用于 RollingEPUB 功能测试的原创纪念册，<br/>
不是官方资料，也不包含歌词。</p>
""")


def build() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    files = {
        "mimetype": b"application/epub+zip",
        "META-INF/container.xml": CONTAINER.encode(),
        "OEBPS/content.opf": opf().encode(),
        "OEBPS/nav.xhtml": nav().encode(),
        "OEBPS/toc.ncx": ncx().encode(),
        "OEBPS/styles/book.css": CSS.encode(),
        "OEBPS/text/title.xhtml": title_chapter().encode(),
        "OEBPS/text/life.xhtml": xhtml("1987 — 2019", LIFE).encode(),
        "OEBPS/text/hitorie.xhtml": xhtml("hitorie 与舞台", HITORIE).encode(),
        "OEBPS/text/gallery.xhtml": xhtml("影像与色彩", GALLERY).encode(),
        "OEBPS/text/vertical.xhtml": xhtml("竖排：从右到左", VERTICAL).encode(),
        "OEBPS/text/notes.xhtml": xhtml("注释", NOTES).encode(),
        "OEBPS/images/cover.jpg": img_cover(),
        "OEBPS/images/portrait.png": img_portrait(),
        "OEBPS/images/checkerboard.png": img_checkerboard(),
        "OEBPS/images/photo.jpg": img_photo(),
        "OEBPS/images/motion.gif": img_gif(),
        "OEBPS/images/outline.bmp": img_bmp(),
        "OEBPS/images/tall.png": img_tall(),
        "OEBPS/images/icon.png": img_icon(),
    }
    with zipfile.ZipFile(OUT, "w") as z:
        # EPUB requires an uncompressed mimetype entry first.
        z.writestr(zipfile.ZipInfo("mimetype"), files.pop("mimetype"),
                   compress_type=zipfile.ZIP_STORED)
        for name, data in files.items():
            z.writestr(name, data, compress_type=zipfile.ZIP_DEFLATED)
    print(f"{OUT}  {OUT.stat().st_size} bytes")


if __name__ == "__main__":
    build()
