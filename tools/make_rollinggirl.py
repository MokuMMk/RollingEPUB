#!/usr/bin/env python3
"""Build the local "ローリンガール" introduction book.

The book is a personal, non-commercial introduction to wowaka's 2010
Vocaloid song "ローリンガール" (Rollin' Girl). Facts are taken from the Chinese
Wikipedia article and Moegirlpedia; the prose is original commentary and
contains no lyrics. All reader features are woven into the article:

  * EPUB 3 nav + EPUB 2 NCX, nested TOC, cover, series metadata
  * h1-h6, table, pre/code-style credits, lists, blockquote, hr, br
  * bold / italic / underline / strike / superscript / subscript
  * links, anchors, same-file / cross-file / icon-marker footnotes
  * ruby, drop caps, shaded info boxes, page breaks, display:none
  * PNG / JPEG / GIF / BMP artwork crops, mixed pages, image-only page
  * a CJK vertical-writing chapter

Run:  python tools/make_rollinggirl.py
"""
from __future__ import annotations

import io
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageOps

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "tools" / "assets" / "rollinggirl"
OUT = ROOT / "tests" / "fixtures" / "epub" / "rollinggirl.epub"

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

CSS = """
html, body { margin: 0; padding: 0; }
body { font-family: serif; font-size: 100%; line-height: 170%; }
p { margin-top: 0; margin-bottom: 0.7em; text-indent: 2em; text-align: justify; }
p.noindent { text-indent: 0; }
p.center { text-align: center; text-indent: 0; }
p.caption { text-align: center; text-indent: 0; font-size: 84%; color: #333333; }
p.subtitle { text-align: center; text-indent: 0; font-size: 112%; }
p.epigraph { text-indent: 0; text-align: center; font-size: 96%; }
h1 { font-size: 185%; font-weight: bold; text-align: center; margin: 0.7em 0 0.9em 0; text-indent: 0; }
h2 { font-size: 150%; font-weight: bold; margin: 1em 0 0.5em 0; text-indent: 0; }
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
.small { font-size: 82%; }
.bold { font-weight: bold; }
.italic { font-style: italic; }
.underline { text-decoration: underline; }
.strike { text-decoration: line-through; }
.hidden { display: none; }
.pb { page-break-before: always; }
.shaded { background-color: #eeeeee; border: 1px solid black; padding: 0.55em; margin: 0.8em 0; }
.dropcap::first-letter { initial-letter: 3; font-weight: bold; }
a { text-decoration: underline; }
"""

TITLE_BODY = """
<p class="subtitle">VOCALOID 名曲档案 · 01</p>
<h1>ローリンガール</h1>
<p class="subtitle">Rollin' Girl · 翻滚少女</p>
<p class="noindent center"><img src="../images/cover.jpg" alt="cover"/></p>
<p class="epigraph">再一次，是这首歌里最轻、也最沉的三个字。</p>
<p class="noindent center small">wowaka feat. 初音ミク · 2010</p>
"""

OVERVIEW_BODY = """
<h1>歌曲档案</h1>
<p class="dropcap">《ローリンガール》（Rollin' Girl，常译作《翻滚少女》）是日本 VOCALOID
制作人 wowaka 使用初音未来创作的歌曲，于 2010 年 2 月 14 日投稿至 Niconico 动画，
2017 年 9 月 5 日上传至 YouTube。歌曲时长 3 分 16 秒。</p>
<div class="shaded">
<p class="noindent"><span class="bold">ローリンガール / Rollin' Girl</span><br/>
演唱：初音未来（V<sub>2</sub>）<br/>
作词、作曲、插画：wowaka<br/>
投稿：2010 年 2 月 14 日（Niconico）<br/>
时长：3 分 16 秒</p>
</div>
<pre>title: ローリンガール
music: wowaka
illustration: wowaka
vocal: 初音ミク
length: 3:16</pre>
<h2 id="song-info">基本信息</h2>
<table>
  <caption>歌曲档案</caption>
  <tr><th>项目</th><th>内容</th></tr>
  <tr><td>日文标题</td><td>ローリンガール</td></tr>
  <tr><td>英文标题</td><td>Rollin' Girl</td></tr>
  <tr><td>中文通译</td><td>翻滚少女</td></tr>
  <tr><td>演唱</td><td>初音未来</td></tr>
  <tr><td>作词 / 作曲</td><td>wowaka</td></tr>
  <tr><td>插画</td><td>wowaka</td></tr>
  <tr><td>投稿日期</td><td>2010 年 2 月 14 日</td></tr>
</table>
<h3 id="upload">投稿</h3>
<p>本曲最初发表于 Niconico 动画，投稿者为 wowaka。根据中文维基百科的记录，
视频编号为 <code>sm9714351</code>；2017 年 9 月 5 日，官方版本被上传至 YouTube，
视频编号为 <code>vnw8zURAxkU</code>。两个版本的内容一致，后者为较晚建立的官方频道版本。</p>
<h4>标题的写法</h4>
<p>日文标题使用片假名“ローリンガール”，英文通常写作 <span class="italic">Rollin' Girl</span>。
中文语境里也有人把它译作《翻滚少女》或《滚动少女》，但这些都不是官方译名。
标题中的 <span class="underline">Rolling</span> 同时指向“滚动”和“不断重复”的动作，这一点与歌词的主题有关<a href="#note-1"><sup>1</sup></a>。</p>
<h5 id="author">作者</h5>
<p>wowaka（ヲワカ）是这首歌的作词、作曲与插画作者。他在 Vocaloid 场景中还有一个
更早使用的名义“現実逃避P”。2010 年前后，他一边以个人身份发表 Vocaloid 作品，
一边筹备后来的乐队 hitorie。</p>
<p class="noindent center"><img src="../images/author.jpg" alt="wowaka"/></p>
<p class="caption">wowaka</p>
<h6>同时期的作品</h6>
<p>2010 年，wowaka 还发表了《ワールズエンド・ダンスホール》（World's End Dancehall）。
这两首歌后来都被视为他的代表作，也经常被放在一起讨论。</p>
<p class="hidden">资料核对：投稿日期、收录信息与榜单数据以中文维基百科和萌娘百科的记载为准。</p>
<hr/>
<aside epub:type="footnote" id="note-1">注 1：本文的事实信息参考中文维基百科《Rollin Girl》条目与萌娘百科《ローリンガール》条目，
文字为原创整理，不包含歌词。</aside>
<p><a href="background.xhtml">下一章：2010 年的房间</a></p>
"""

BACKGROUND_BODY = """
<h1>2010 年的房间</h1>
<p class="dropcap">要理解《ローリンガール》，需要先回到 2010 年的 Vocaloid 场景。
那是一个投稿门槛很低的时期：一台电脑、一套音源、一个想法，就可以把歌传到网上。
wowaka 在 2009 年已经发表了多首作品，逐渐形成了自己的节奏感与旋律写法<a href="#note-2"><sup>2</sup></a>。</p>
<h2>之前的作品</h2>
<ul>
  <li>《グレーゾーンにて。》</li>
  <li>《テノヒラ》</li>
  <li>《ラインアート》</li>
  <li>《とおせんぼ》</li>
  <li>《僕のサイノウ》</li>
  <li>《裏表ラバーズ》</li>
  <li>《ずれていく》</li>
  <li>《積み木の人形》</li>
</ul>
<table>
  <caption>2009—2010 年的部分投稿</caption>
  <tr><th>年份</th><th>作品</th></tr>
  <tr><td>2009</td><td>グレーゾーンにて。 / テノヒラ / ラインアート</td></tr>
  <tr><td>2009</td><td>とおせんぼ / 僕のサイノウ / 裏表ラバーズ</td></tr>
  <tr><td>2009</td><td>ずれていく / 積み木の人形</td></tr>
  <tr><td>2010</td><td>ローリンガール</td></tr>
  <tr><td>2010</td><td>ワールズエンド・ダンスホール</td></tr>
</table>
<h3>声音的倾向</h3>
<p>wowaka 这一时期的作品普遍有较快的节奏和密集的歌词密度。人声被当成一件
可以不断加速的乐器，句子短、重复多，情绪在重复中逐渐堆高。
《ローリンガール》延续了这种写法，但主题比此前的作品更集中。</p>
<h4>“現実逃避P”</h4>
<p>“現実逃避P”是 wowaka 早期使用的名义之一。这个名义后来不再频繁出现，
但在 Vocaloid 爱好者的讨论中，仍然被用来指代他 2009 年前后的创作阶段。</p>
<blockquote>在一个所有人都可以投稿的房间里，真正让人记住的并不是技术，
而是一首歌里那股不肯停下来的劲。</blockquote>
<h5>从个人到乐队</h5>
<p>2011 年，wowaka 与贝斯手イガラシ、鼓手ゆーまお组成前身乐队；
2012 年シノダ加入，hitorie 的四人体制成形。Vocaloid 时期那种一个人完成全部环节的
工作方式，也在乐队里变成了另一种分工。</p>
<h6>一句补充</h6>
<p>本曲与《ワールズエンド・ダンスホール》的投稿时间只隔了几个月，
两首歌共享同一种高速、紧绷的气质，却走向了不同的情绪终点。</p>
<hr/>
<p>参考：<a href="https://zh.wikipedia.org/wiki/Rollin_Girl">中文维基百科：Rollin Girl</a> ·
<a href="https://zh.moegirl.org.cn/%E3%83%AD%E3%83%BC%E3%83%AA%E3%83%B3%E3%82%AC%E3%83%BC%E3%83%AB">萌娘百科：ローリンガール</a></p>
<aside epub:type="footnote" id="note-2">注 2：2009 年的作品列表与“現実逃避P”名义参考萌娘百科《ローリンガール》条目中的作品年表。</aside>
<p><a href="theme.xhtml">下一章：再一次</a></p>
"""

THEME_BODY = """
<h1>再一次</h1>
<p class="dropcap">《ローリンガール》的歌词围绕一个不断失败的少女展开。
她一次次说“再一次”，一次次重新开始，却始终没有抵达想去的方向。
歌名里的 Rolling 既指身体上的翻滚，也指这种循环：跌倒、爬起、再跌倒。</p>
<p>这首歌没有给出明确的结局。它停在一种悬而未决的状态里：
少女还在继续，听众却已经知道，所谓的“再一次”可能不会有终点。
这种留白让歌曲在听完之后仍然停在脑子里，而不是随着最后一个音符结束。</p>
<h2>重复</h2>
<p>重复是这首歌最显眼的结构。短句反复出现，旋律在相似的位置回到相似的音高，
像一个人在同一段路上来回走。重复并不等于停滞，它让情绪一点点累积，
直到听众开始分不清这是坚持还是消耗。</p>
<h3>失败</h3>
<p><ruby>失敗<rt>しっぱい</rt></ruby>是歌词里反复出现的词。少女并不掩饰自己的失败，
反而把它当成继续下去的理由。这种态度让歌曲避免了单纯的悲伤，
多了一层近乎固执的力气。</p>
<h4>留白</h4>
<p>歌曲没有解释少女为什么失败，也没有说明她最终去了哪里。
正是这些没有说出的部分，让不同的听众可以把自己的经历放进去。
有人在里面听见考试、工作，也有人听见一段没有结果的关系。</p>
<h5>标题</h5>
<p>“ローリンガール”这个标题本身就是一个矛盾：少女在 rolling，
但 rolling 也可以是被动的、失去控制的。她究竟是在向前，还是在原地打转，
歌曲没有回答。</p>
<h6>一点听感</h6>
<p>整首歌只有 3 分 16 秒，却几乎没有真正放松的段落。人声和伴奏一起往前推，
让“再一次”听起来既像请求，也像命令。<s>停下来</s>从来不是选项。</p>
<blockquote>这首歌最有力的地方，不是告诉听众“会好起来”，
而是承认“可能不会好起来”，然后仍然把下一句唱完。</blockquote>
<hr/>
<p>主题分析参考萌娘百科的歌词页与中文维基百科的介绍。
<a href="pv.xhtml">下一章：画面</a></p>
"""

PV_BODY = """
<h1>画面</h1>
<p class="dropcap">《ローリンガール》的官方插画由 wowaka 本人完成。
画面使用黑、白、灰三种颜色，圆月和弧线在画面里不断重复，
像一组没有起点也没有终点的轨道。它没有直接画出歌词里的少女，
却把“循环”的感觉留在了构图上。</p>
<p class="noindent center"><img src="../images/art.png" alt="Rollin Girl artwork"/></p>
<p class="caption">《ローリンガール》插画</p>
<p>画面中央的圆形与两侧不断重复的月牙形，构成了一个几乎可以无限延长的图案。
这也是这幅插画最常被截取、重绘和二次创作的部分。</p>
<p class="noindent center"><img src="../images/detail.jpg" alt="artwork detail"/></p>
<p class="caption">插画局部</p>
<h2>视频</h2>
<p>本曲在 Niconico 的投稿编号为 <code>sm9714351</code>，YouTube 官方版本编号为
<code>vnw8zURAxkU</code>。早期观众主要通过 Niconico 的投稿页面接触这首歌，
弹幕文化也让“再一次”这句重复的呼喊变成了集体记忆的一部分。</p>
<ul>
  <li><a href="https://www.nicovideo.jp/watch/sm9714351">Niconico：初音ミク オリジナル曲「ローリンガール」</a></li>
  <li><a href="https://www.youtube.com/watch?v=vnw8zURAxkU">YouTube：wowaka『ローリンガール』feat. 初音ミク</a></li>
</ul>
<h3>二次创作</h3>
<p>据萌娘百科记载，秋赤音曾为本曲制作 PV。除此之外，这首歌还有大量翻唱、
演奏与插画二次创作，并被收录进《初音未來 -名伶計畫- extend》等游戏。
同一段旋律在不同媒介里被反复重新解释，这本身也延续了歌曲“再一次”的主题<a href="notes.xhtml#ref-pv"><sup>3</sup></a>。</p>
<ol>
  <li>2010 年：Niconico 投稿</li>
  <li>2011 年：收录于《初音未來 -名伶計畫- extend》</li>
  <li>2017 年：YouTube 官方版本上传</li>
  <li>2023 年：Niconico 播放量突破一千万</li>
</ol>
<hr/>
<p><a href="records.xhtml">下一章：记录</a></p>
"""

RECORDS_BODY = """
<h1>记录</h1>
<p>《ローリンガール》在投稿后的十多年里一直被反复播放。
2023 年 2 月 4 日，它在 Niconico 动画的播放量突破一千万次，
成为“VOCALOID 神话曲”之一。据萌娘百科统计，它是第十二首达成这一数字的曲目，
也是 wowaka 的第二首神话曲。</p>
<table>
  <caption>主要时间点</caption>
  <tr><th>时间</th><th>事件</th></tr>
  <tr><td>2010 年 2 月 14 日</td><td>投稿至 Niconico 动画</td></tr>
  <tr><td>2010 年 3 月 3 日</td><td>收录于《EXIT TUNES PRESENTS Supernova 2》</td></tr>
  <tr><td>2010 年 5 月 19 日</td><td>收录于《EXIT TUNES PRESENTS Vocalogenesis feat.初音未來》</td></tr>
  <tr><td>2010 年 11 月 14 日</td><td>收录于迷你专辑《SEVEN GIRLS' DISCORD》</td></tr>
  <tr><td>2011 年 5 月 2 日</td><td>收录于专辑《Unhappy Refrain》</td></tr>
  <tr><td>2011 年 11 月 10 日</td><td>收录于游戏《初音未來 -名伶計畫- extend》</td></tr>
  <tr><td>2017 年 9 月 5 日</td><td>YouTube 官方版本上传</td></tr>
  <tr><td>2023 年 2 月 4 日</td><td>Niconico 播放量突破一千万</td></tr>
  <tr><td>2023 年 2 月 8 日</td><td>Billboard Japan“Niconico VOCALOID SONGS TOP 20”第二位</td></tr>
</table>
<ol>
  <li>第十二首 VOCALOID 神话曲。</li>
  <li>wowaka 的第二首神话曲，也使成为第三位拥有两首神话曲的 P 主。</li>
  <li>与《World is Mine》达成神话的时间相隔约 12 小时，是当时间隔最短的两首。</li>
  <li>据萌娘百科统计，本曲在 Niconico 的 VOCALOID 歌曲播放量中位列第七。</li>
</ol>
<p>2019 年 4 月 5 日，wowaka 因急性心力衰竭去世。此后的一周，
《ローリンガール》《ワールズエンド・ダンスホール》与《裏表ラバーズ》
一同进入 Billboard Japan Hot 100，分别位列第 42、39 与 40 位<a href="notes.xhtml#ref-record"><sup>4</sup></a>。</p>
<hr/>
<p>参考：<a href="https://zh.wikipedia.org/wiki/Rollin_Girl">中文维基百科：Rollin Girl</a> ·
<a href="https://zh.moegirl.org.cn/%E3%83%AD%E3%83%BC%E3%83%AA%E3%83%B3%E3%82%AC%E3%83%BC%E3%83%AB">萌娘百科：ローリンガール</a></p>
<p><a href="albums.xhtml">下一章：收录</a></p>
"""

ALBUMS_BODY = """
<h1>收录</h1>
<p>《ローリンガール》最初以单曲视频的形式出现，随后被收录进多张合辑与专辑。
它的传播路径很典型：先在 Niconico 被听见，再进入商业合辑，
最后成为游戏和现场演出里的常客。</p>
<table>
  <caption>主要收录</caption>
  <tr><th>载体</th><th>名称</th><th>时间</th></tr>
  <tr><td>合辑</td><td>EXIT TUNES PRESENTS Supernova 2</td><td>2010 年 3 月 3 日</td></tr>
  <tr><td>合辑</td><td>EXIT TUNES PRESENTS Vocalogenesis feat.初音未來</td><td>2010 年 5 月 19 日</td></tr>
  <tr><td>迷你专辑</td><td>SEVEN GIRLS' DISCORD</td><td>2010 年 11 月 14 日</td></tr>
  <tr><td>专辑</td><td>Unhappy Refrain</td><td>2011 年 5 月 2 日</td></tr>
  <tr><td>游戏</td><td>初音未來 -名伶計畫- extend</td><td>2011 年 11 月 10 日</td></tr>
</table>
<h2>专辑版本</h2>
<p>《Unhappy Refrain》是 wowaka 的第一张个人专辑，收录了包括本曲在内的多首作品。
专辑版本在保留原曲结构的同时，让整张唱片的情绪更完整：
《ローリンガール》不再是一首孤立的歌，而是整张专辑叙事的一部分。</p>
<h3>游戏收录</h3>
<p>在《初音未來 -名伶計畫- extend》中，本曲被改编为可游玩的节奏游戏曲目，
并配有新的 3D 演出。对很多没有经历过 2010 年 Niconico 投稿期的玩家来说，
这是他们第一次接触这首歌。</p>
<hr/>
<p>参考：<a href="https://zh.wikipedia.org/wiki/Rollin_Girl">中文维基百科：Rollin Girl</a></p>
<p><a href="gallery.xhtml">下一章：影像</a></p>
"""

GALLERY_BODY = """
<h1 class="pb">影像</h1>
<p class="dropcap">这首歌留下的画面并不多，但足够建立一个完整的世界：
黑白的月牙、重复的弧线、没有面孔的少女，以及一条似乎永远不会结束的轨道。
下面这些画面都取自同一幅官方插画，只是以不同的方式被截取、放慢和重新观看。</p>
<p class="noindent center"><img src="../images/art.png" alt="artwork"/></p>
<p class="caption">插画全图</p>
<p>画面没有明确的透视，也没有可以站稳的地面。圆形和月牙叠在一起，
让人很难判断哪里是起点、哪里是终点——这正是歌曲想表达的感觉。</p>
<p class="noindent center"><img src="../images/detail.jpg" alt="detail"/></p>
<p class="caption">局部：重复的月牙形</p>
<p>把画面放大之后，会发现每一道弧线的粗细和方向都不完全相同。
它们并不是机械复制，而是在重复中缓慢变化，像同一段旋律的许多次变奏。</p>
<p class="noindent center"><img src="../images/motion.gif" alt="animated artwork"/></p>
<p class="caption">两帧的动态处理</p>
<p>把插画做成两帧的轻微明暗变化，是为了模拟旧式动画里那种呼吸般的闪动。
它不改变画面内容，只让静止的图案多了一点时间感。</p>
<p class="noindent center"><img src="../images/texture.bmp" alt="texture"/></p>
<p class="caption">局部：黑白灰的层次</p>
<p>最后是一张竖长的画面：月牙从上方一直延伸到页底，像一条被拉长的轨道。</p>
<p class="noindent center"><img src="../images/tall.png" alt="tall artwork"/></p>
<p class="noindent center">插画的来源与使用说明，见书末<a href="notes.xhtml#ref-art"><img src="../images/icon.png" alt="注"/></a>。</p>
"""

VERTICAL_BODY = """
<h1>竖排：再一次的方向</h1>
<p>文字从上往下，行从右往左。这样的阅读方向和这首歌的节奏正好相反：
歌一直在往前赶，阅读却要求人慢下来。</p>
<p>「再一次」在歌里出现了很多次。每一次都比上一次更用力，
也更像一句没有出口的话。竖排把这些字排成一列，
它们之间的距离忽然变得很清楚。</p>
<p><ruby>転<rt>ころ</rt></ruby>がる、<ruby>失敗<rt>しっぱい</rt></ruby>する、
また<ruby>起<rt>お</rt></ruby>きる。这些动作在横排里是连续的，
在竖排里却像一格一格的停顿。</p>
<p>也许这首歌真正想说的，不是“一定会成功”，
而是“即使不会成功，也还可以再来一次”。</p>
"""

NOTES_BODY = """
<h1>资料与图片</h1>
<h2>注释</h2>
<aside epub:type="footnote" id="ref-art">注 6：本册使用的《ローリンガール》插画与 wowaka 街拍来自网络公开资料，
仅用于个人阅读与非商业介绍；著作权归原权利人所有。请勿公开传播本册或图片。</aside>
<aside epub:type="footnote" id="ref-pv">注 3：秋赤音 PV、游戏收录等信息参考萌娘百科《ローリンガール》条目；二次创作数量众多，未能尽录。</aside>
<aside epub:type="footnote" id="ref-record">注 4：Billboard Japan 的数据参考中文维基百科条目所引用的 2019 年 4 月 22 日 Hot 100 榜单。</aside>
<h2>文字资料</h2>
<ul class="small">
  <li><a href="https://zh.wikipedia.org/wiki/Rollin_Girl">中文维基百科：Rollin Girl</a></li>
  <li><a href="https://zh.moegirl.org.cn/%E3%83%AD%E3%83%BC%E3%83%AA%E3%83%B3%E3%82%AC%E3%83%BC%E3%83%AB">萌娘百科：ローリンガール</a></li>
  <li><a href="https://www.nicovideo.jp/watch/sm9714351">Niconico：初音ミク オリジナル曲「ローリンガール」</a></li>
  <li><a href="https://www.youtube.com/watch?v=vnw8zURAxkU">YouTube：wowaka『ローリンガール』feat. 初音ミク</a></li>
</ul>
<h2>图片与声明</h2>
<p class="small">本册为个人编辑的非官方读物，仅供个人阅读，不用于商业传播。
文字为原创整理，不包含歌词；插画与人物照片的著作权归原权利人所有。</p>
"""


def _load(name: str) -> Image.Image:
    return Image.open(ASSETS / name).convert("RGB")


def _cover() -> bytes:
    art = _load("art.jpg")
    canvas = Image.new("RGB", (600, 900), (12, 12, 14))
    fitted = ImageOps.fit(art, (600, 450), method=Image.Resampling.LANCZOS)
    canvas.paste(fitted, (0, 225))
    d = ImageDraw.Draw(canvas)
    d.rectangle((24, 24, 575, 875), outline=(235, 235, 235), width=1)
    buf = io.BytesIO()
    canvas.save(buf, format="JPEG", quality=90)
    return buf.getvalue()


def _art_png() -> bytes:
    im = ImageOps.fit(_load("art.jpg"), (1200, 900), method=Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def _detail_jpg() -> bytes:
    im = ImageOps.fit(_load("art.jpg"), (800, 600), method=Image.Resampling.LANCZOS)
    im = im.crop((100, 120, 700, 480)).resize((800, 600), Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=88)
    return buf.getvalue()


def _motion_gif() -> bytes:
    art = ImageOps.fit(_load("art.jpg"), (800, 600), method=Image.Resampling.LANCZOS)
    dark = ImageEnhance.Brightness(art).enhance(0.72)
    frames = [art.convert("P", palette=Image.Palette.ADAPTIVE, colors=96),
              dark.convert("P", palette=Image.Palette.ADAPTIVE, colors=96)]
    buf = io.BytesIO()
    frames[0].save(buf, format="GIF", save_all=True, append_images=frames[1:],
                   duration=600, loop=0)
    return buf.getvalue()


def _texture_bmp() -> bytes:
    art = ImageOps.fit(_load("art.jpg"), (800, 600), method=Image.Resampling.LANCZOS)
    im = art.crop((100, 0, 900, 600))
    buf = io.BytesIO()
    im.save(buf, format="BMP")
    return buf.getvalue()


def _tall_png() -> bytes:
    art = _load("art.jpg")
    slice_ = art.crop((320, 0, 704, 768))
    tall = slice_.resize((800, 2048), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (800, 2048), (255, 255, 255))
    canvas.paste(tall, (0, 0))
    buf = io.BytesIO()
    canvas.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def _icon_png() -> bytes:
    art = ImageOps.fit(_load("art.jpg"), (48, 48), method=Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    art.save(buf, format="PNG")
    return buf.getvalue()


def _author_jpg() -> bytes:
    im = ImageOps.fit(_load("wowaka.jpg"), (600, 400), method=Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=88)
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
    <dc:identifier id="bookid">urn:uuid:2a7e4c9b-3d61-4f08-b5c7-9a10e8d4f632</dc:identifier>
    <dc:title>ローリンガール：再一次</dc:title>
    <dc:creator>音乐档案</dc:creator>
    <dc:language>zh-CN</dc:language>
    <dc:publisher>个人出版</dc:publisher>
    <dc:date>2026-11-05</dc:date>
    <dc:description>关于 wowaka 的 Vocaloid 歌曲《ローリンガール》的个人介绍册；原创文字，不含歌词。</dc:description>
    <meta property="belongs-to-collection">VOCALOID 名曲档案</meta>
    <meta property="group-position">1</meta>
    <meta name="cover" content="cover-image"/>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="../styles/book.css" media-type="text/css"/>
    <item id="cover-image" href="../images/cover.jpg" media-type="image/jpeg" properties="cover-image"/>
    <item id="title" href="text/title.xhtml" media-type="application/xhtml+xml"/>
    <item id="overview" href="text/overview.xhtml" media-type="application/xhtml+xml"/>
    <item id="background" href="text/background.xhtml" media-type="application/xhtml+xml"/>
    <item id="theme" href="text/theme.xhtml" media-type="application/xhtml+xml"/>
    <item id="pv" href="text/pv.xhtml" media-type="application/xhtml+xml"/>
    <item id="records" href="text/records.xhtml" media-type="application/xhtml+xml"/>
    <item id="albums" href="text/albums.xhtml" media-type="application/xhtml+xml"/>
    <item id="gallery" href="text/gallery.xhtml" media-type="application/xhtml+xml"/>
    <item id="vertical" href="text/vertical.xhtml" media-type="application/xhtml+xml"/>
    <item id="notes" href="text/notes.xhtml" media-type="application/xhtml+xml"/>
    <item id="art" href="../images/art.png" media-type="image/png"/>
    <item id="detail" href="../images/detail.jpg" media-type="image/jpeg"/>
    <item id="motion" href="../images/motion.gif" media-type="image/gif"/>
    <item id="texture" href="../images/texture.bmp" media-type="image/bmp"/>
    <item id="tall" href="../images/tall.png" media-type="image/png"/>
    <item id="icon" href="../images/icon.png" media-type="image/png"/>
    <item id="author" href="../images/author.jpg" media-type="image/jpeg"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="title"/>
    <itemref idref="overview"/>
    <itemref idref="background"/>
    <itemref idref="theme"/>
    <itemref idref="pv"/>
    <itemref idref="records"/>
    <itemref idref="albums"/>
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
      <li><a href="text/overview.xhtml">歌曲档案</a>
        <ol>
          <li><a href="text/overview.xhtml#song-info">基本信息</a></li>
          <li><a href="text/overview.xhtml#upload">投稿</a></li>
          <li><a href="text/overview.xhtml#author">作者</a></li>
        </ol>
      </li>
      <li><a href="text/background.xhtml">2010 年的房间</a></li>
      <li><a href="text/theme.xhtml">再一次</a></li>
      <li><a href="text/pv.xhtml">画面</a></li>
      <li><a href="text/records.xhtml">记录</a></li>
      <li><a href="text/albums.xhtml">收录</a></li>
      <li><a href="text/gallery.xhtml">影像</a></li>
      <li><a href="text/vertical.xhtml">竖排：再一次的方向</a></li>
      <li><a href="text/notes.xhtml">资料与图片</a></li>
    </ol>
  </nav>
</body>
</html>
"""


def ncx() -> str:
    points = [
        ("扉页", "text/title.xhtml"),
        ("歌曲档案", "text/overview.xhtml"),
        ("2010 年的房间", "text/background.xhtml"),
        ("再一次", "text/theme.xhtml"),
        ("画面", "text/pv.xhtml"),
        ("记录", "text/records.xhtml"),
        ("收录", "text/albums.xhtml"),
        ("影像", "text/gallery.xhtml"),
        ("竖排：再一次的方向", "text/vertical.xhtml"),
        ("资料与图片", "text/notes.xhtml"),
    ]
    body = "\n".join(
        f'    <navPoint id="np{i}" playOrder="{i}"><navLabel><text>{t}</text></navLabel>'
        f'<content src="{h}"/></navPoint>'
        for i, (t, h) in enumerate(points, 1)
    )
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:uuid:2a7e4c9b-3d61-4f08-b5c7-9a10e8d4f632"/></head>
  <docTitle><text>ローリンガール：再一次</text></docTitle>
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
        "OEBPS/text/overview.xhtml": xhtml("歌曲档案", OVERVIEW_BODY).encode(),
        "OEBPS/text/background.xhtml": xhtml("2010 年的房间", BACKGROUND_BODY).encode(),
        "OEBPS/text/theme.xhtml": xhtml("再一次", THEME_BODY).encode(),
        "OEBPS/text/pv.xhtml": xhtml("画面", PV_BODY).encode(),
        "OEBPS/text/records.xhtml": xhtml("记录", RECORDS_BODY).encode(),
        "OEBPS/text/albums.xhtml": xhtml("收录", ALBUMS_BODY).encode(),
        "OEBPS/text/gallery.xhtml": xhtml("影像", GALLERY_BODY).encode(),
        "OEBPS/text/vertical.xhtml": xhtml("竖排：再一次的方向", VERTICAL_BODY).encode(),
        "OEBPS/text/notes.xhtml": xhtml("资料与图片", NOTES_BODY).encode(),
        "OEBPS/images/cover.jpg": _cover(),
        "OEBPS/images/art.png": _art_png(),
        "OEBPS/images/detail.jpg": _detail_jpg(),
        "OEBPS/images/motion.gif": _motion_gif(),
        "OEBPS/images/texture.bmp": _texture_bmp(),
        "OEBPS/images/tall.png": _tall_png(),
        "OEBPS/images/icon.png": _icon_png(),
        "OEBPS/images/author.jpg": _author_jpg(),
    }
    with zipfile.ZipFile(OUT, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), files.pop("mimetype"),
                   compress_type=zipfile.ZIP_STORED)
        for name, data in files.items():
            z.writestr(name, data, compress_type=zipfile.ZIP_DEFLATED)
    print(f"{OUT}  {OUT.stat().st_size} bytes")


if __name__ == "__main__":
    build()
