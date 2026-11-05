#!/usr/bin/env python3
"""Build the detailed wowaka test book `tests/fixtures/epub/wowaka.epub`.

The whole book is about **wowaka** (ヲワカ, 1987-2019), Vocaloid producer and
vocalist/guitarist of hitorie.  All library test features are woven into the
biography and discography prose instead of being labelled as tests:

  * EPUB 3 nav + EPUB 2 NCX, nested TOC, cover, series metadata
  * h1-h6 hierarchy, paragraphs, sections, blockquote, pre, ul/ol, hr, br
  * bold / italic / underline / strike / superscript / subscript
  * internal + external links, anchors, CSS classes and ids
  * same-file, cross-file and icon-marker footnotes
  * ruby readings, drop caps, shaded info box, page breaks, display:none
  * PNG / JPEG / GIF / BMP illustrations with known dimensions
  * mixed text + image pages and an image-only tall page
  * a CJK vertical-writing chapter

Photos are freely licensed context images (see tools/assets/wowaka/CREDITS.md);
there is no free-licensed portrait of wowaka, so none is embedded.

Run:  python tools/make_wowaka.py
"""
from __future__ import annotations

import io
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageOps

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "tools" / "assets" / "wowaka"
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
body { font-family: serif; font-size: 100%; line-height: 168%; }
p { margin-top: 0; margin-bottom: 0.7em; text-indent: 2em; text-align: justify; }
p.noindent { text-indent: 0; }
p.center { text-align: center; text-indent: 0; }
p.caption { text-align: center; text-indent: 0; font-size: 85%; color: #333333; }
p.verse { text-indent: 0; text-align: center; font-style: italic; }
p.subtitle { text-align: center; text-indent: 0; font-size: 115%; }
p.epigraph { text-indent: 0; text-align: center; font-size: 95%; }
h1 { font-size: 185%; font-weight: bold; text-align: center; margin: 0.7em 0 0.9em 0; text-indent: 0; }
h2 { font-size: 150%; font-weight: bold; margin: 1.0em 0 0.5em 0; text-indent: 0; }
h3 { font-size: 130%; font-weight: bold; margin: 0.8em 0 0.4em 0; text-indent: 0; }
h4 { font-size: 115%; font-weight: bold; margin: 0.6em 0 0.3em 0; text-indent: 0; }
h5 { font-size: 105%; font-weight: bold; margin: 0.5em 0 0.2em 0; text-indent: 0; }
h6 { font-size: 100%; font-weight: bold; margin: 0.4em 0 0.2em 0; text-indent: 0; }
blockquote { margin-left: 2.2em; margin-right: 2.2em; font-style: italic; }
pre { white-space: pre; font-family: monospace; font-size: 82%; margin-left: 1em; }
code { font-family: monospace; }
ul, ol { margin-left: 1.8em; }
li { margin-bottom: 0.25em; }
hr { margin: 1.2em 2em; }
table { margin-left: 1em; margin-bottom: 0.9em; }
th { font-weight: bold; }
caption { font-style: italic; font-size: 90%; }
.small { font-size: 80%; }
.large { font-size: 140%; }
.bold { font-weight: bold; }
.italic { font-style: italic; }
.underline { text-decoration: underline; }
.strike { text-decoration: line-through; }
.hidden { display: none; }
.pb { page-break-before: always; }
.shaded { background-color: #eeeeee; border: 1px solid black; padding: 0.55em; margin: 0.8em 0; }
.dropcap::first-letter { initial-letter: 3; font-weight: bold; }
#members { font-size: 92%; }
a { text-decoration: underline; }
"""

# ---------------------------------------------------------------------------
# Book text (original commemorative prose; no lyrics, no official claim)
# ---------------------------------------------------------------------------

TITLE_BODY = """
<p class="subtitle">ヲワカ · wowaka</p>
<h1>把噪音写成旋律的人</h1>
<p class="noindent center"><img src="../images/cover.jpg" alt="cover"/></p>
<p class="epigraph">有些声音离开之后，反而在更多人的耳机里留下来。</p>
<p class="noindent center small">1987 — 2019</p>
"""

BIO_BODY = """
<h1>1987 — 2019</h1>
<p class="dropcap">ヲワカ，通常写作 wowaka。1987 年 11 月 4 日，他出生在东京。
很多年后，人们记住的是那个在舞台上抱着吉他、把身体微微前倾的歌手，
以及更早以前，那个在电脑屏幕前把一个个音符拖进时间线的年轻人。</p>
<p>他的创作起步于网络。大约从 2009 年开始，wowaka 以 Vocaloid 制作人的身份
发表作品，用初音未来等虚拟歌手的声音写歌。那时的投稿文化没有门槛，
一首歌可以在几天内穿过整个互联网，被素未谋面的人听见。</p>
<h2 id="youth">少年时代</h2>
<p>关于他的少年时期，公开资料并不多。可以确认的是，他从小在东京长大，
后来学会了弹吉他。城市、房间、耳机和深夜，这些词反复出现在关于他的叙述里，
也构成了他音乐最初的底色。</p>
<h3>从一个人到一支乐队</h3>
<p>Vocaloid 时期的作品大多由一个人完成：作曲、编曲、调校、投稿。
这种工作方式给了他绝对的自由，也让他习惯把每一层声音都握在自己手里。
但随着作品变多，他开始想要另一种东西——真实的鼓、真实的贝斯，
以及在同一个房间里同时响起的乐器。</p>
<h4 id="turn">转折</h4>
<p>2011 年，wowaka 与贝斯手イガラシ、鼓手ゆーまお组成了前身乐队「ひとりアトリエ」。
2012 年，吉他手シノダ加入，四人体制的 hitorie 正式开始活动。
从个人项目到乐队，变化的不只是编制，还有歌的呼吸方式<a href="#note-1"><sup>1</sup></a>。</p>
<h5>舞台上的位置</h5>
<p>在 hitorie 里，wowaka 是主唱和吉他手。他常常站在舞台中央偏左的位置，
灯亮起来之后话不多，第一首歌往往直接开始。熟悉现场的人会知道，
他喜欢把录音室里已经很快的节奏再往前推一点，让整个乐队跟在后面追。</p>
<h6 id="spring">春天之后</h6>
<p>2019 年 4 月 5 日，wowaka 去世，年仅 31 岁。那一年 hitorie 正在专辑巡演途中。
有些计划停在了春天，<s>以后</s>两个字忽然变得很轻。但歌还在，
乐队也在同年 6 月举行了追悼演出，之后继续以三人体制活动。</p>
<p class="verse">凌晨三点的屏幕<br/>未保存的和弦<br/>还在循环的副歌</p>
<blockquote>有人说，他的歌像一列不肯减速的电车：窗外的风景是模糊的，
座位却意外地稳。</blockquote>
<h2>年表</h2>
<table>
  <caption>wowaka 与 hitorie 的主要节点</caption>
  <tr><th>年份</th><th>事件</th></tr>
  <tr><td>1987</td><td>11 月 4 日出生于东京</td></tr>
  <tr><td>2009</td><td>以 Vocaloid 制作人身份开始发表作品</td></tr>
  <tr><td>2010</td><td>《World's End Dancehall》等作品在网络上流传</td></tr>
  <tr><td>2011</td><td>与イガラシ、ゆーまお组成前身乐队「ひとりアトリエ」</td></tr>
  <tr><td>2012</td><td>シノダ加入，hitorie 四人体制开始</td></tr>
  <tr><td>2014</td><td>hitorie 发行首张单曲《センスレス・ワンダー》</td></tr>
  <tr><td>2017</td><td>发表约六年来的 Vocaloid 新作《アンノウン・マザーグース》</td></tr>
  <tr><td>2019</td><td>4 月 5 日去世；6 月举行追悼演出</td></tr>
</table>
<hr/>
<div class="shaded">
<p class="noindent"><span class="bold">基本资料</span></p>
<p class="noindent">姓名：wowaka（ヲワカ）<br/>
出生：1987 年 11 月 4 日，东京<br/>
身份：Vocaloid 制作人、hitorie 主唱 / 吉他手<br/>
乐队：hitorie（イガラシ、シノダ、ゆーまお）</p>
</div>
<ul>
  <li>代表作：《World's End Dancehall》《Rollin' Girl》《Unknown Mother Goose》</li>
  <li>乐队作品：hitorie 的专辑与巡演</li>
  <li>影响：让更多听众接受“虚拟歌手也能唱摇滚”这件事</li>
</ul>
<p class="hidden">资料核对：生卒年、乐队成员与发行信息以官方公开资料为准。</p>
<aside epub:type="footnote" id="note-1">注 1：本文为原创纪念性文字，依据官方简介与公开资料整理；不包含歌词，也不代表官方立场。</aside>
<p><a href="vocaloid.xhtml">下一章：初音未来与网络上的房间</a></p>
"""

VOCALOID_BODY = """
<h1>初音未来与网络上的房间</h1>
<p class="dropcap">在 Vocaloid 的场景里，wowaka 的声音很容易被认出来：
密集的吉他切分、偏快的鼓点、略带神经质的旋律，以及一种始终向前冲的劲。
虚拟歌手的声音本来偏冷，他却用编曲把它推成了一支摇滚乐队<a href="#note-2"><sup>2</sup></a>。</p>
<p>这种反差是很多听众记住他的原因。屏幕里的歌手没有体温，
歌里却有真实的急躁、犹豫和不甘心。早期的投稿作品因此迅速传播，
也让“Vocaloid 摇滚”成为一个越来越常被提起的说法。</p>
<h2>代表曲</h2>
<ul>
  <li><ruby>ワールズエンド・ダンスホール<rt>World's End Dancehall</rt></ruby>：快节奏与不断堆叠的音层。</li>
  <li><ruby>ローリンガール<rt>Rollin' Girl</rt></ruby>：一条几乎不肯停下的旋律线。</li>
  <li><ruby>アンノウン・マザーグース<rt>Unknown Mother Goose</rt></ruby>：2017 年发表的回归之作。</li>
</ul>
<table>
  <caption>Vocaloid 时期的代表作品</caption>
  <tr><th>作品</th><th>年份</th><th>备注</th></tr>
  <tr><td>World's End Dancehall</td><td>2010</td><td>高速的吉他与鼓点</td></tr>
  <tr><td>Rollin' Girl</td><td>2011</td><td>旋律不断向上推进</td></tr>
  <tr><td>Unknown Mother Goose</td><td>2017</td><td>初音未来十周年企划</td></tr>
</table>
<p>第一张图是虚拟歌手演唱会现场。灯光扫过观众席的时候，
那种“屏幕里的声音站在舞台上”的错觉，正是他这一代制作人共同参与制造的现场。</p>
<p class="noindent center"><img src="../images/miku.png" alt="virtual singer concert"/></p>
<p class="caption">虚拟歌手的演唱会现场（公开授权图片）。</p>
<p>他的作品里经常出现一种结构：先用一段几乎重复的吉他动机把人带进去，
再让人声从缝隙里挤出来。等听众习惯了速度，歌才真正开始。
这种写法后来也被他带进了 hitorie。</p>
<p>参考：<a href="https://ja.wikipedia.org/wiki/Wowaka">wowaka - Wikipedia</a> ·
<a href="https://www.hitorie.jp/">hitorie 官方网站</a></p>
<aside epub:type="footnote" id="note-2">注 2：文中提到的作品名称使用通行的英文/日文写法，发行时间以公开资料为准。</aside>
<p><a href="hitorie.xhtml">下一章：hitorie：四个人的乐队</a></p>
"""

HITORIE_BODY = """
<h1>hitorie：四个人的乐队</h1>
<p class="dropcap">如果说 Vocaloid 时期是一个人的房间，那么 hitorie 就是四个人共用的排练室。
声音不再只由鼠标和键盘决定：鼓槌落下的力度、贝斯弦的震动、吉他音箱的反馈，
都成为歌的一部分。</p>
<h2 id="members">成员</h2>
<ul>
  <li>wowaka：主唱 / 吉他</li>
  <li>イガラシ：贝斯</li>
  <li>シノダ：吉他 / 和声（2012 年加入）</li>
  <li>ゆーまお：鼓</li>
</ul>
<p>2011 年，wowaka 与イガラシ、ゆーまお组成前身乐队；2012 年シノダ加入后，
hitorie 正式以四人体制活动。2014 年，他们发行首张单曲《センスレス・ワンダー》，
随后是首张完整专辑《WONDER and WONDER》<a href="notes.xhtml#ref-live"><sup>3</sup></a>。</p>
<p class="noindent center"><img src="../images/stage.jpg" alt="live stage"/></p>
<p class="caption">摇滚现场的灯光（公开授权图片）。</p>
<p>乐队的现场和录音室版本往往不太一样。同一个副歌，在现场会被拉长，
吉他会更响，鼓会更快，wowaka 的声音也会更靠前。
有观众说，看 hitorie 的演出像是在看一个人把自己的草稿当场重写。</p>
<blockquote>四个人站在同一个舞台上，谁也不能真正躲起来。
这大概就是他从一个人的房间走向乐队的原因。</blockquote>
<hr/>
<p class="noindent center"><img src="../images/logo.png" alt="hitorie logo"/></p>
<p class="caption">hitorie 的标识（公有领域图片）。</p>
<p>乐队在 2019 年之后继续活动。シノダ开始担任主唱，イガラシ和ゆーまお留在原来的位置。
对很多听众来说，这是一种很难但很诚实的继续。</p>
<p><a href="sound.xhtml">下一章：声音的方法</a></p>
"""

SOUND_BODY = """
<h1 class="pb">声音的方法</h1>
<p class="dropcap">wowaka 的歌听起来总是很快，但“快”并不只是 BPM 的数字。
更准确地说，是每一层声音都在往前赶：鼓点密集，贝斯几乎不停，
吉他切成短促的方块，人声则在最上面不断变换重心。</p>
<h2>速度与吉他</h2>
<p>以标准音高来说，A<sub>4</sub> 是 440 Hz。他的歌里，吉他经常不是用来铺底，
而是当作节奏乐器：短促、干净、几乎没有延音。这样的写法让空间显得拥挤，
也让主唱的声音必须更用力才能穿出来。</p>
<pre>tempo: around 190 bpm
meter: 4/4
guitar: short muted strokes, no long sustain
bass: eighth-note drive
drums: kick on the grid, snare slightly ahead
vocal: keep the line restless, never too smooth</pre>
<h3>编曲的层次</h3>
<p>他的编曲很少留大片空白。即使是一段相对安静的段落，也往往有细碎的吉他
或鼓的装饰音在下面动。听久了会发现，那些看似凌乱的音符其实排列得非常整齐，
像一段被反复擦写又反复保留的铅笔草稿。</p>
<h4>歌词与声音</h4>
<p>他不常把话说得很直。歌里的句子有时像自言自语，有时像对着一个不在场的人喊。
<span class="bold">重复</span>、<span class="italic">停顿</span>、
<span class="underline">突然提高的音量</span>，都是他常用的手法。
有些句子被<s>删掉</s>又捡回来，最后留在录音里。</p>
<h5>专辑</h5>
<table>
  <caption>hitorie 的主要专辑</caption>
  <tr><th>专辑</th><th>年份</th></tr>
  <tr><td>WONDER and WONDER</td><td>2014</td></tr>
  <tr><td>DEEPER</td><td>2016</td></tr>
  <tr><td>IKI</td><td>2016</td></tr>
  <tr><td>HOWLS</td><td>2019</td></tr>
</table>
<div class="shaded">
<p class="noindent"><span class="bold">聆听提示</span></p>
<p class="noindent">先听鼓，再听贝斯，最后才听人声。会更容易发现他的歌里
那些一直向前滚动的层次。</p>
</div>
<hr/>
<p class="noindent center"><img src="../images/guitar.bmp" alt="guitar"/></p>
<p class="caption">琴颈与琴身（公开授权图片）。</p>
<p>他使用的吉他并不复杂，声音也不追求过度的失真。真正让歌变得锋利的是节奏，
以及那种“下一个音马上就要来了”的预感<a href="notes.xhtml#ref-sound"><sup>4</sup></a>。</p>
<p><a href="legacy.xhtml">下一章：之后</a></p>
"""

LEGACY_BODY = """
<h1>之后</h1>
<p>wowaka 去世后，hitorie 没有停下。2019 年 6 月，乐队在新木场 STUDIO COAST
举行追悼演出，シノダ站在了主唱的位置上。那之后的巡演、专辑和纪念活动，
让这些歌继续被听见。</p>
<p>他的影响也不只留在自己的乐队里。很多后来开始写歌的人，都提到过
第一次听到《World's End Dancehall》或《Rollin' Girl》时的震动：
原来虚拟歌手可以这样唱，原来一首歌可以一直往前跑，不给自己留退路。</p>
<ol>
  <li>2019 年：hitorie 举行追悼演出，之后以三人体制继续。</li>
  <li>2024 年：收录未发表歌词的《wowaka 歌词集》出版。</li>
  <li>2025 年：初音未来交响音乐会演奏了 wowaka 作品组曲。</li>
</ol>
<blockquote>作品比人更长寿，这句话在他身上显得格外具体。
只要还有人按下播放键，歌里的速度就不会停下来。</blockquote>
<hr/>
<p class="noindent center"><img src="../images/tokyo.gif" alt="Tokyo at night"/></p>
<p class="caption">东京的夜（公开授权图片）。</p>
<p>他出生在这座城市，也在这里写下最早的那些歌。城市夜晚的灯光、
电车的声音、便利店的白光，后来都变成了他音乐里那种持续不断的背景噪音<a href="#note-legacy"><sup>5</sup></a>。</p>
<p>参考：<a href="https://www.hitorie.jp/">hitorie 官方网站</a> ·
<a href="https://ja.wikipedia.org/wiki/Wowaka">wowaka - Wikipedia</a></p>
<aside epub:type="footnote" id="note-legacy">注 5：文中关于追悼演出、歌词集与音乐会的描述依据公开报道与官方信息整理。</aside>
"""

IMAGES_BODY = """
<h1 class="pb">影像</h1>
<p class="dropcap">关于他的影像资料并不多。这里收集的，是与他音乐相伴的场景：
虚拟歌手的舞台、摇滚现场的灯光、他手中的吉他、以及他出生的城市。
它们不是他的肖像，却拼出了一种属于他的光线。</p>
<p>第一张图是虚拟歌手演唱会现场。屏幕上的光与观众席的荧光棒连成一片，
那是他最早被听见的地方。</p>
<p class="noindent center"><img src="../images/miku.png" alt="virtual singer concert"/></p>
<p class="caption">虚拟歌手的演唱会现场（公开授权图片）。</p>
<p>第二张图是摇滚现场的灯光。hitorie 的演出常常从第一首歌开始就把音量推到很前面，
灯光在鼓点和吉他之间切换，像另一件乐器。</p>
<p class="noindent center"><img src="../images/stage.jpg" alt="live stage"/></p>
<p class="caption">摇滚现场的灯光（公开授权图片）。</p>
<p>第三张是吉他。短促的切分、几乎不留空白的节奏，是他最容易被辨认的声音记号。</p>
<p class="noindent center"><img src="../images/guitar.bmp" alt="guitar"/></p>
<p class="caption">琴颈与琴身（公开授权图片）。</p>
<p>第四张是东京的夜。他在这里出生、写歌，也在这里离开。</p>
<p class="noindent center"><img src="../images/tokyo.gif" alt="Tokyo at night"/></p>
<p class="caption">东京的夜（公开授权图片）。</p>
<p>下一张是竖长的画面：舞台的灯从下往上延伸，像一条没有尽头的通道。</p>
<p class="noindent center"><img src="../images/tall.png" alt="tall stage"/></p>
<p>图片与资料说明见<a href="notes.xhtml#ref-photo"><img src="../images/logo-icon.png" alt="note"/></a>。</p>
"""

VERTICAL_BODY = """
<h1>竖排：声音的方向</h1>
<p>文字从上往下，行从右往左。这样的阅读方向和音乐并不冲突：
一首歌也可以从右边开始，向左生长。</p>
<p>wowaka 的旋律常常从一句很短的话开始，然后不断重复、加速，
像一条被拉长的线。竖排时，这条线变成一列一列的字，
读者从上往下看，正好和听歌时那种“一直往前”的感觉相反——
阅读需要放慢，而他的歌总是很快。</p>
<p>「まだ終わらない。」<ruby>声音<rt>sound</rt></ruby>在舞台上停了一拍，
又继续。标点按竖排的规则落在字的右侧：，。！？（）《》——
这些细小的位置，决定了阅读的呼吸。</p>
<p>最后一段仍然使用中文标点：，。！？（）《》——用来结束这一章，
也用来检查竖排中的禁则与标点位置。</p>
"""

NOTES_BODY = """
<h1>注释与来源</h1>
<aside epub:type="footnote" id="ref-live">注 3：hitorie 于 2011 年由 wowaka、イガラシ、ゆーまお组成前身乐队，
2012 年シノダ加入后正式以四人体制活动；2014 年发行首张单曲《センスレス・ワンダー》。</aside>
<aside epub:type="footnote" id="ref-sound">注 4：关于速度与编曲的描述来自对公开作品的听感整理，BPM 与配器仅为近似说明。</aside>
<aside epub:type="footnote" id="ref-photo">注 6：本书图片来自 Wikimedia Commons 的公开授权照片与公有领域 hitorie 标识；
未使用 wowaka 本人的受版权保护照片。具体署名与许可证见下。</aside>
<aside epub:type="footnote" id="ref-legacy">注 5：追悼演出、歌词集与初音未来交响音乐会的相关信息依据公开报道整理。</aside>
<p>文字资料参考：<a href="https://www.hitorie.jp/">hitorie 官方网站</a>、
<a href="https://ja.wikipedia.org/wiki/Wowaka">Wikipedia：wowaka</a>。
本文为原创纪念性文字，不包含歌词，也不代表官方立场。</p>
<h2>图片来源</h2>
<ul class="small">
  <li>初音未来演唱会照片：MIKI Yoshihito，CC BY 2.0，Wikimedia Commons。</li>
  <li>摇滚现场照片：Shixart1985，CC BY 2.0，Wikimedia Commons。</li>
  <li>吉他照片：Shixart1985，CC BY 2.0，Wikimedia Commons。</li>
  <li>东京夜景照片：Nalilord，CC BY-SA 3.0，Wikimedia Commons。</li>
  <li>hitorie 标识：公有领域，Wikimedia Commons。</li>
</ul>
"""


def _load(name: str) -> Image.Image:
    return Image.open(ASSETS / name).convert("RGB")


def _white_logo(size: tuple[int, int]) -> Image.Image:
    """hitorie logo recoloured to white while keeping its alpha channel."""
    logo = Image.open(ASSETS / "hitorie-logo.png").convert("RGBA")
    r, g, b, a = logo.split()
    logo = Image.merge("RGBA", (ImageOps.invert(r), ImageOps.invert(g), ImageOps.invert(b), a))
    return ImageOps.fit(logo, size, method=Image.Resampling.LANCZOS)


def _cover() -> bytes:
    base = ImageOps.fit(_load("tokyo.jpg"), (600, 900), method=Image.Resampling.LANCZOS)
    dark = ImageEnhance.Brightness(base).enhance(0.62)
    logo = _white_logo((360, 84))
    dark.paste(logo, (120, 720), logo)
    buf = io.BytesIO()
    dark.save(buf, format="JPEG", quality=88)
    return buf.getvalue()


def _miku_png() -> bytes:
    im = ImageOps.fit(_load("miku-live.jpg"), (1200, 675), method=Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def _stage_jpg() -> bytes:
    im = ImageOps.fit(_load("live-stage.jpg"), (1200, 800), method=Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=86)
    return buf.getvalue()


def _guitar_bmp() -> bytes:
    im = ImageOps.fit(_load("guitar.jpg"), (800, 1200), method=Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, format="BMP")
    return buf.getvalue()


def _tokyo_gif() -> bytes:
    base = ImageOps.fit(_load("tokyo.jpg"), (800, 510), method=Image.Resampling.LANCZOS)
    second = ImageEnhance.Color(base).enhance(0.35)
    second = ImageEnhance.Brightness(second).enhance(0.8)
    frames = [base.convert("P", palette=Image.Palette.ADAPTIVE, colors=128),
              second.convert("P", palette=Image.Palette.ADAPTIVE, colors=128)]
    buf = io.BytesIO()
    frames[0].save(buf, format="GIF", save_all=True, append_images=frames[1:],
                   duration=350, loop=0)
    return buf.getvalue()


def _logo_png() -> bytes:
    logo = Image.open(ASSETS / "hitorie-logo.png").convert("RGBA")
    canvas = Image.new("RGBA", (960, 224), (255, 255, 255, 0))
    logo.thumbnail((920, 200), Image.Resampling.LANCZOS)
    canvas.paste(logo, ((960 - logo.width) // 2, (224 - logo.height) // 2), logo)
    buf = io.BytesIO()
    canvas.save(buf, format="PNG")
    return buf.getvalue()


def _logo_icon() -> bytes:
    logo = Image.open(ASSETS / "hitorie-logo.png").convert("RGBA")
    icon = Image.new("RGBA", (48, 48), (255, 255, 255, 0))
    logo.thumbnail((40, 24), Image.Resampling.LANCZOS)
    icon.paste(logo, ((48 - logo.width) // 2, (48 - logo.height) // 2), logo)
    buf = io.BytesIO()
    icon.save(buf, format="PNG")
    return buf.getvalue()


def _tall_png() -> bytes:
    base = _load("live-stage.jpg")
    # A tall vertical crop of the stage; a soft dark fade only at the bottom
    # keeps the photograph readable instead of covering it.
    slice_ = ImageOps.fit(base, (400, 1200), method=Image.Resampling.LANCZOS)
    tall = slice_.resize((800, 2400), Image.Resampling.LANCZOS).convert("RGBA")
    fade = Image.new("RGBA", (800, 2400), (0, 0, 0, 0))
    d = ImageDraw.Draw(fade)
    for y in range(1600, 2400):
        alpha = min(190, (y - 1600) // 3)
        d.line((0, y, 800, y), fill=(10, 12, 18, alpha))
    tall = Image.alpha_composite(tall, fade)
    logo = _white_logo((560, 140))
    tall.paste(logo, ((800 - logo.width) // 2, 2160), logo)
    buf = io.BytesIO()
    tall.convert("RGB").save(buf, format="PNG", optimize=True)
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
    <dc:title>wowaka：把噪音写成旋律的人</dc:title>
    <dc:creator>RollingEPUB 音乐档案</dc:creator>
    <dc:language>zh-CN</dc:language>
    <dc:publisher>RollingEPUB</dc:publisher>
    <dc:date>2026-01-01</dc:date>
    <dc:description>关于 wowaka（ヲワカ，1987—2019）与 hitorie 的资料性纪念册；原创文字，不含歌词。</dc:description>
    <meta property="belongs-to-collection">音乐人档案</meta>
    <meta property="group-position">1</meta>
    <meta name="cover" content="cover-image"/>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="../styles/book.css" media-type="text/css"/>
    <item id="cover-image" href="../images/cover.jpg" media-type="image/jpeg" properties="cover-image"/>
    <item id="title" href="text/title.xhtml" media-type="application/xhtml+xml"/>
    <item id="bio" href="text/bio.xhtml" media-type="application/xhtml+xml"/>
    <item id="vocaloid" href="text/vocaloid.xhtml" media-type="application/xhtml+xml"/>
    <item id="hitorie" href="text/hitorie.xhtml" media-type="application/xhtml+xml"/>
    <item id="sound" href="text/sound.xhtml" media-type="application/xhtml+xml"/>
    <item id="legacy" href="text/legacy.xhtml" media-type="application/xhtml+xml"/>
    <item id="images" href="text/images.xhtml" media-type="application/xhtml+xml"/>
    <item id="vertical" href="text/vertical.xhtml" media-type="application/xhtml+xml"/>
    <item id="notes" href="text/notes.xhtml" media-type="application/xhtml+xml"/>
    <item id="miku" href="../images/miku.png" media-type="image/png"/>
    <item id="stage" href="../images/stage.jpg" media-type="image/jpeg"/>
    <item id="guitar" href="../images/guitar.bmp" media-type="image/bmp"/>
    <item id="tokyo" href="../images/tokyo.gif" media-type="image/gif"/>
    <item id="logo" href="../images/logo.png" media-type="image/png"/>
    <item id="logo-icon" href="../images/logo-icon.png" media-type="image/png"/>
    <item id="tall" href="../images/tall.png" media-type="image/png"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="title"/>
    <itemref idref="bio"/>
    <itemref idref="vocaloid"/>
    <itemref idref="hitorie"/>
    <itemref idref="sound"/>
    <itemref idref="legacy"/>
    <itemref idref="images"/>
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
      <li><a href="text/bio.xhtml">1987 — 2019</a>
        <ol>
          <li><a href="text/bio.xhtml#youth">少年时代</a></li>
          <li><a href="text/bio.xhtml#turn">转折</a></li>
          <li><a href="text/bio.xhtml#spring">春天之后</a></li>
        </ol>
      </li>
      <li><a href="text/vocaloid.xhtml">初音未来与网络上的房间</a></li>
      <li><a href="text/hitorie.xhtml">hitorie：四个人的乐队</a></li>
      <li><a href="text/sound.xhtml">声音的方法</a></li>
      <li><a href="text/legacy.xhtml">之后</a></li>
      <li><a href="text/images.xhtml">影像</a></li>
      <li><a href="text/vertical.xhtml">竖排：声音的方向</a></li>
      <li><a href="text/notes.xhtml">注释与来源</a></li>
    </ol>
  </nav>
</body>
</html>
"""


def ncx() -> str:
    points = [
        ("扉页", "text/title.xhtml"),
        ("1987 — 2019", "text/bio.xhtml"),
        ("初音未来与网络上的房间", "text/vocaloid.xhtml"),
        ("hitorie：四个人的乐队", "text/hitorie.xhtml"),
        ("声音的方法", "text/sound.xhtml"),
        ("之后", "text/legacy.xhtml"),
        ("影像", "text/images.xhtml"),
        ("竖排：声音的方向", "text/vertical.xhtml"),
        ("注释与来源", "text/notes.xhtml"),
    ]
    body = "\n".join(
        f'    <navPoint id="np{i}" playOrder="{i}"><navLabel><text>{t}</text></navLabel>'
        f'<content src="{h}"/></navPoint>'
        for i, (t, h) in enumerate(points, 1)
    )
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:uuid:rolling-epub-wowaka-fixture"/></head>
  <docTitle><text>wowaka：把噪音写成旋律的人</text></docTitle>
  <navMap>
{body}
  </navMap>
</ncx>
"""


def build() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    files: dict[str, bytes | str] = {
        "mimetype": b"application/epub+zip",
        "META-INF/container.xml": CONTAINER.encode(),
        "OEBPS/content.opf": opf().encode(),
        "OEBPS/nav.xhtml": nav().encode(),
        "OEBPS/toc.ncx": ncx().encode(),
        "OEBPS/styles/book.css": CSS.encode(),
        "OEBPS/text/title.xhtml": xhtml("扉页", TITLE_BODY).encode(),
        "OEBPS/text/bio.xhtml": xhtml("1987 — 2019", BIO_BODY).encode(),
        "OEBPS/text/vocaloid.xhtml": xhtml("初音未来与网络上的房间", VOCALOID_BODY).encode(),
        "OEBPS/text/hitorie.xhtml": xhtml("hitorie：四个人的乐队", HITORIE_BODY).encode(),
        "OEBPS/text/sound.xhtml": xhtml("声音的方法", SOUND_BODY).encode(),
        "OEBPS/text/legacy.xhtml": xhtml("之后", LEGACY_BODY).encode(),
        "OEBPS/text/images.xhtml": xhtml("影像", IMAGES_BODY).encode(),
        "OEBPS/text/vertical.xhtml": xhtml("竖排：声音的方向", VERTICAL_BODY).encode(),
        "OEBPS/text/notes.xhtml": xhtml("注释与来源", NOTES_BODY).encode(),
        "OEBPS/images/cover.jpg": _cover(),
        "OEBPS/images/miku.png": _miku_png(),
        "OEBPS/images/stage.jpg": _stage_jpg(),
        "OEBPS/images/guitar.bmp": _guitar_bmp(),
        "OEBPS/images/tokyo.gif": _tokyo_gif(),
        "OEBPS/images/logo.png": _logo_png(),
        "OEBPS/images/logo-icon.png": _logo_icon(),
        "OEBPS/images/tall.png": _tall_png(),
    }
    with zipfile.ZipFile(OUT, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), files.pop("mimetype"),
                   compress_type=zipfile.ZIP_STORED)
        for name, data in files.items():
            z.writestr(name, data, compress_type=zipfile.ZIP_DEFLATED)
    print(f"{OUT}  {OUT.stat().st_size} bytes")


if __name__ == "__main__":
    build()
