// Rebuild the offline reader from the adjacent Markdown source.
// Usage: node build_one_day_guide.mjs <path-to-marked/lib/marked.esm.js>
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const directory = path.dirname(fileURLToPath(import.meta.url));
const stem = path.join(directory, 'D01-one-day-project-guide');
if (!process.argv[2]) throw new Error('Pass the local marked.esm.js path. No download is required.');
const { marked } = await import(pathToFileURL(path.resolve(process.argv[2])).href);
const source = fs.readFileSync(`${stem}.md`, 'utf8');
if (source.includes('INCLUDE_CHAPTER')) throw new Error('Chapter assembly is incomplete.');
const chapters = [...source.matchAll(/^## (\d)\. (.+)$/gm)];
if (chapters.length !== 10) throw new Error(`Expected 10 chapters; found ${chapters.length}.`);
const escape = value => value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');
const localLinks = [...source.matchAll(/\]\((B:\/[^)]+)\)/g)];
for (const [, target] of localLinks) {
  const file = target.replace(/:\d+$/, '');
  if (file === `${stem.replaceAll('\\', '/')}.html`) continue;
  if (!fs.existsSync(file)) throw new Error(`Missing local source: ${file}`);
}
let body = marked.parse(source, { gfm: true });
body = body.replace(/href="(B:\/[^"#]+?)(?::(\d+))?"/g, (_, file, line) => {
  const url = pathToFileURL(file).href;
  const hint = `${file}${line ? ` · 작성 당시 ${line}행, 파일을 열고 본문의 함수 이름을 검색하세요.` : ''}`;
  return `href="${escape(url)}" title="${escape(hint)}"`;
});
body = body.replaceAll('<table>', '<div class="table-wrap"><table>').replaceAll('</table>', '</table></div>');
const toc = chapters.map(([, number, title]) => `<a href="#chapter-${number}"><span>${number}</span>${escape(title.replace(/ — .+$/, ''))}</a>`).join('\n');
const html = `<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light">
<title>Drive Integration | AI 없이 공부하는 하루</title>
<style>
:root{--ink:#172c3b;--muted:#576c79;--accent:#116f76;--line:#d8e3e5;--paper:#fff;--canvas:#f1f5f5}
*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:22px}body{margin:0;background:var(--canvas);color:var(--ink);font-family:"Malgun Gothic","Segoe UI",sans-serif;font-size:16px;line-height:1.85;word-break:keep-all;overflow-wrap:anywhere}
a{color:var(--accent);text-underline-offset:3px}a:hover{color:#073f45}a:focus-visible,button:focus-visible,summary:focus-visible{outline:3px solid #d59325;outline-offset:3px}
aside{position:fixed;inset:0 auto 0 0;width:280px;padding:30px 24px;overflow:auto;background:#102e3b;color:#f4f9f8}
.brand{font-size:12px;letter-spacing:2px;color:#9ac7ca}.rail-title{font-size:23px;line-height:1.5;margin:10px 0}.rail-note{font-size:13px;color:#c5d6da;margin:0 0 20px}
nav a{display:flex;gap:10px;padding:9px 0;font-size:13px;line-height:1.6;color:#deecee;text-decoration:none;border-bottom:1px solid #2f4b55}nav a:hover{color:white}nav span{color:#8acbd0;min-width:15px}
.tools{display:grid;gap:8px;margin:24px 0 12px}button{font:inherit;font-size:13px;cursor:pointer;border:1px solid #8faeb4;border-radius:5px;background:#e6f1f0;color:#143944;padding:8px 12px;text-align:left}button:hover{background:white}.offline{font-size:12px;color:#b4cbd0}
main{margin:34px 40px 60px 320px;max-width:980px;padding:48px 56px;background:var(--paper);border:1px solid var(--line);border-radius:10px;box-shadow:0 8px 40px #18384306}
h1,h2,h3,h4{line-height:1.45;letter-spacing:-.025em}h1{font-size:34px;margin:0 0 24px}h2{font-size:27px;margin:65px 0 22px;padding-top:20px;border-top:3px solid var(--accent)}h3{font-size:20px;margin:34px 0 12px}h4{font-size:17px;margin:24px 0 10px}
p{margin:14px 0}li{margin:7px 0}ul,ol{padding-left:25px}strong{font-weight:700}blockquote{margin:20px 0;padding:10px 20px;border-left:4px solid var(--accent);background:#eef7f5;color:#284f58}blockquote p{margin:5px 0}
code{font-family:Consolas,"Malgun Gothic",monospace;font-size:.88em;background:#edf2f4;border-radius:3px;padding:2px 5px;overflow-wrap:anywhere}pre{border:1px solid #dce6e8;border-radius:7px;padding:18px 20px;overflow:auto;background:#f3f7f8;line-height:1.7;white-space:pre-wrap;word-break:break-word}pre code{background:none;padding:0;font-size:13px}
.table-wrap{overflow:auto;margin:22px 0}table{width:100%;border-collapse:collapse;font-size:14px;line-height:1.7}th,td{border:1px solid var(--line);padding:11px 13px;text-align:left;vertical-align:top}th{background:#e9f2f2;color:#234a53}tr:nth-child(even) td{background:#fafcfc}table code{font-size:.86em}
details{margin:24px 0;border:1px solid #bbd7d5;background:#f6fbf9;border-radius:7px;padding:0 20px 10px}summary{cursor:pointer;font-weight:700;color:#176368;padding:15px 0 8px;line-height:1.6}details[open] summary{margin-bottom:12px;border-bottom:1px solid #cee1dc}details:not([open]){padding-bottom:7px}
input[type=checkbox]{accent-color:var(--accent)}footer{margin-top:55px;padding-top:18px;border-top:1px solid var(--line);font-size:12px;color:var(--muted)}noscript{display:block;padding:10px;background:#fff3d7}
@media(min-width:1500px){main{margin-left:calc(280px + (100vw - 280px - 980px)/2)}}
@media(max-width:1050px){aside{position:static;width:auto;padding:24px}nav{display:grid;grid-template-columns:1fr 1fr;gap:0 25px}.tools{display:flex;flex-wrap:wrap;margin:16px 0 5px}.rail-note{margin-bottom:10px}main{margin:20px;padding:32px;max-width:none}h1{font-size:29px}}
@media(max-width:560px){body{font-size:15px}main{margin:10px;padding:22px 17px}aside{padding:20px}nav{grid-template-columns:1fr}h1{font-size:26px}h2{font-size:23px}h3{font-size:18px}th,td{padding:8px;min-width:110px}pre{padding:12px}details{padding-left:13px;padding-right:13px}}
@media print{@page{size:A4;margin:15mm}html{scroll-behavior:auto}body{background:white;font-size:10.5pt;line-height:1.65}aside{display:none}main{max-width:none;margin:0;padding:0;border:0;border-radius:0;box-shadow:none}h1{font-size:24pt}h2{font-size:19pt;break-before:page;margin-top:0}h3{font-size:14pt;break-after:avoid}h4{break-after:avoid}p{orphans:3;widows:3}a{color:inherit;text-decoration:underline}table{font-size:9pt}th,td{min-width:0;padding:6px}thead{display:table-header-group}tr{break-inside:avoid}.table-wrap{overflow:visible}pre{font-size:9pt;overflow:visible}details{background:white;border-color:#aaa;break-inside:auto}summary{color:inherit}footer{font-size:9pt}}
</style>
</head>
<body>
<aside aria-label="교재 목차">
<div class="brand">DRIVE INTEGRATION</div>
<p class="rail-title">AI 없이 공부하는 하루</p>
<p class="rail-note">7시간 30분 학습 · 10개 장<br>Windows / Unreal Engine 5.6</p>
<nav>${toc}</nav>
<div class="tools"><button id="expand" type="button">모든 정답·해설 펼치기</button><button id="collapse" type="button">정답·해설 접기</button><button id="print" type="button">정답 포함 인쇄 / PDF 저장</button></div>
<p class="offline">이 파일은 인터넷과 AI 연결 없이 읽습니다. 코드 링크는 같은 PC의 프로젝트 파일을 엽니다. 줄 이동이 안 되면 본문의 함수 이름을 검색하세요.</p>
</aside>
<main>
<noscript>정답 제목을 눌러 개별 해설을 볼 수 있습니다. 인쇄 전 필요한 해설을 펼쳐 주세요.</noscript>
${body}
<footer>작성 기준 2026-09-08 · 옆의 Markdown 파일이 원문입니다. 학습 기록과 코드 변경은 자동으로 저장하거나 실행하지 않습니다.</footer>
</main>
<script>
const answers = Array.from(document.querySelectorAll('details'));
document.getElementById('expand').addEventListener('click', () => answers.forEach(item => item.open = true));
document.getElementById('collapse').addEventListener('click', () => answers.forEach(item => item.open = false));
document.getElementById('print').addEventListener('click', () => window.print());
let beforePrint = null;
window.addEventListener('beforeprint', () => { if (beforePrint) return; beforePrint = answers.map(item => item.open); answers.forEach(item => item.open = true); });
window.addEventListener('afterprint', () => { if (!beforePrint) return; answers.forEach((item, index) => item.open = beforePrint[index]); beforePrint = null; });
</script>
</body>
</html>`;
fs.writeFileSync(`${stem}.html`, html, 'utf8');
console.log(JSON.stringify({ output: `${stem}.html`, chapters: chapters.length, localLinks: localLinks.length, answers: (body.match(/<details>/g) || []).length, bytes: Buffer.byteLength(html) }));
