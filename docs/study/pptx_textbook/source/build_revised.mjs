import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const ROOT = 'B:/Portfolio/Drive_Integration';
const BASE = path.join(ROOT, 'docs/study/pptx_textbook');
const ILLUSTRATED = process.argv.includes('--illustrated');
const OUTPUT = path.join(BASE, ILLUSTRATED ? 'illustrated' : 'revised');
const BUILD = path.join(ROOT, ILLUSTRATED ? 'runtime_tmp/study_illustrated_20260908' : 'runtime_tmp/study_revised_20260908');
const MODULES = 'C:/Users/PC/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const SKILL = 'C:/Users/PC/.codex/plugins/cache/openai-primary-runtime/presentations/26.905.11957/skills/presentations';
const PYTHON = 'C:/Users/PC/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
process.env.RUNTIME_NODE_MODULES = MODULES;
process.env.RUNTIME_NODE = process.execPath;
process.env.RUNTIME_PYTHON = PYTHON;
const { Presentation, PresentationFile, FileBlob } = await import(pathToFileURL(path.join(MODULES, '@oai/artifact-tool/dist/artifact_tool.mjs')).href).catch(async () => {
  const { createRequire } = await import('node:module');
  const require = createRequire(path.join(BUILD, 'probe.mjs'));
  return import(pathToFileURL(require.resolve('@oai/artifact-tool')).href);
});
const { createRequire } = await import('node:module');
const require = createRequire(path.join(BUILD, 'probe.mjs'));
const { createCanvas, GlobalFonts } = require('@napi-rs/canvas');
GlobalFonts.registerFromPath('C:/Windows/Fonts/malgun.ttf', 'Malgun Gothic');
GlobalFonts.registerFromPath('C:/Windows/Fonts/malgunbd.ttf', 'Malgun Gothic');
GlobalFonts.registerFromPath('C:/Windows/Fonts/consola.ttf', 'Consolas');
const ctx = createCanvas(1, 1).getContext('2d'); // Font measurement only, no image drawing.
const { finalizePresentation } = await import(pathToFileURL(path.join(SKILL, 'container_tools/artifact_tool_utils.mjs')).href);
const C = { ink:'#173648', muted:'#526B79', teal:'#147D82', pale:'#EFF6F5', line:'#D0DDE0', warm:'#B16A22', code:'#EDF3F5' };
const FONT = 'Malgun Gothic', CODE = 'Consolas';
const fontPolicy = { basis:'design', families:[FONT, CODE], scriptFonts:{ea:FONT} };
const imageManifest = { ...JSON.parse(await fs.readFile(path.join(BASE, 'source/image_manifest.json'), 'utf8')), ...JSON.parse(await fs.readFile(path.join(BASE, 'source/revised_images.json'), 'utf8')) };
const outputNames = { '01':'01_Foundations_Protobuf', '02':'02_CPP_Server', '03':'03_Unreal_Client' };
const imageDescriptions = {
 architecture:'명령과 상태가 오가는 왕복 구조', protobuf:'객체와 바이트 사이의 직렬화',
 suspension:'접지 하중과 서스펜션의 역할', npc_bypass:'후진 공간과 우회 곡선',
 actor_components:'Pawn을 구성하는 기능', coordinates:'ENU와 Unreal의 축과 단위', audio:'엔진음과 타이어음의 합성',
 server_tick:'한 틱에 몰리는 탐색과 분할 처리', session:'연결과 플레이 세션의 구분', collision:'접촉점에서 작용하는 충격량',
};
const sourceCache = new Map();
const review = [];

function measure(value, size, family=FONT, bold=false) {
 ctx.font=`${bold?'bold ':''}${size}px "${family}"`;
 return ctx.measureText(value).width;
}
function wrap(value, width, size, family=FONT, bold=false) {
 return String(value).split('\n').flatMap(paragraph => {
  if (!paragraph) return [''];
  const out=[]; let current='';
  for (const word of paragraph.split(/(\s+)/)) {
   if (measure(current+word,size,family,bold)<=width) { current+=word; continue; }
   if(current.trim()){out.push(current.trimEnd()); current='';}
   if(measure(word,size,family,bold)<=width){current=word.trimStart();continue;}
   for(const ch of word){if(measure(current+ch,size,family,bold)>width){out.push(current);current='';}current+=ch;}
  }
  if(current || !out.length)out.push(current.trimEnd());
  return out;
 }).join('\n');
}
function addText(slide, name, value, x,y,w,h,size=28,color=C.ink,bold=false,family=FONT,background='none') {
 const t=slide.shapes.add({ name, geometry:'textbox', position:{left:x,top:y,width:w,height:h}, fill:background, line:{fill:'none',width:0} });
 t.text=value;
 t.text.style={typeface:family,fontSize:size,bold,color,autoFit:'none',wrap:'none',insets:{left:0,right:0,top:0,bottom:0}};
 return t;
}
function paragraph(slide,name,value,x,y,w,size=29,color=C.ink,bold=false) {
 const wrapped=wrap(value,w,size,FONT,bold);
 const height=wrapped.split('\n').length*size*1.42+5;
 addText(slide,name,wrapped,x,y,w,height,size,color,bold);
 return height;
}
function addNotes(slide, item) {
 const sources=(item.sources??[]).map(s => s.url ? `${s.label??'공식 문서'}: ${s.url}` : `${s.path}${s.start?`:${s.start}${s.end?'-'+s.end:''}`:''}${s.symbol?'  '+s.symbol:''}`).join('\n');
 let notes=`${item.chapter??''}\n\n${item.notes??''}`;
 if(item.excerpt)notes+=`\n\n[코드 원문]\n${item.excerpt}\n\n${item.excerptSource}`;
 if(item.illustration)notes+=`\n\n[그림 설명]\n${imageManifest[item.illustration]?.kind??'교육용 개념도입니다.'}\n실제 프로젝트의 화면, 측정 그래프 또는 기계 설계도가 아닙니다. 코드와 수치는 본문의 원문을 기준으로 읽습니다.`;
 if(sources)notes+=`\n\n[읽을 코드와 출처]\n${sources}`;
 slide.speakerNotes.textFrame.setText(notes);
}
async function expand(volume) {
 const result=[]; const usedImages=new Set();
 for(const original of volume.slides){
  const item={...original};
  if(item.code){
   const resolved=path.resolve(ROOT,item.code.path);
   if(!resolved.toLowerCase().startsWith(path.resolve(ROOT).toLowerCase()+path.sep))throw Error('Code outside project');
   if(!sourceCache.has(resolved)){const text=await fs.readFile(resolved,'utf8');sourceCache.set(resolved,{text,sha256:crypto.createHash('sha256').update(text).digest('hex')});}
   const file=sourceCache.get(resolved), lines=file.text.replace(/\r\n/g,'\n').split('\n');
   if(item.code.start<1||item.code.end>lines.length||item.code.end<item.code.start)throw Error(`Invalid excerpt ${item.id}`);
   item.excerpt=lines.slice(item.code.start-1,item.code.end).join('\n');
   item.excerptSource=`${item.code.path}:${item.code.start}-${item.code.end}`;
   item.sourceSha256=file.sha256;
  } else if(item.codeText) {item.excerpt=item.codeText;item.excerptSource=item.codeLabel??'교육용 예시';}
  for(const s of item.sources??[]){if(s.path)await fs.access(path.resolve(ROOT,s.path));}
  result.push(item);
  if(item.imageKey){
   if(!imageManifest[item.imageKey])throw Error('Missing image '+item.imageKey);
   if(usedImages.has(item.imageKey))throw Error('Repeated image '+item.imageKey);
   usedImages.add(item.imageKey);item.illustration=item.imageKey;
  }
 }
 return result;
}
function title(slide,item) {
 const wrapped=wrap(item.title,1152,44,FONT,true);
 const lines=wrapped.split('\n').length;
 if(lines>2)throw Error(`Title too long: ${item.id}`);
 addText(slide,'title',wrapped,64,47,1152,lines*59+10,44,C.ink,true);
 return 47+lines*59+29;
}
function footer(slide, volume, item, index, count) {
 addText(slide,'footer',`${volume.id}권  ${item.chapterId??'학습 안내'}${item.lessonId?'  '+item.lessonId:''}     ${index+1} / ${count}`,64,680,1152,24,15,C.muted);
}
function wrapCodeLine(value, width) {
 const output=[];let remaining=value;
 while(measure(remaining,23,CODE)>width){
  let quote='',escaped=false,lastBreak=-1;
  for(let i=0;i<remaining.length;i++){
   if(measure(remaining.slice(0,i+1),23,CODE)>width)break;
   const ch=remaining[i];
   if(/\s/.test(ch)&&remaining.slice(0,i).trim())lastBreak=i+1;
   if(quote){if(escaped)escaped=false;else if(ch==='\\')escaped=true;else if(ch===quote)quote='';}
   else if(ch==='"'||ch==="'")quote=ch;
   else if(ch===','||ch==='{'||/\s/.test(ch)){if(remaining.slice(0,i).trim())lastBreak=i+1;}
  }
  if(lastBreak<1)throw Error('A code token is too long for the slide: '+remaining);
  output.push(remaining.slice(0,lastBreak).trimEnd());
  remaining='    '+remaining.slice(lastBreak).trimStart();
 }
 output.push(remaining);return output;
}
function codeSlide(slide,item,y) {
 const raw=item.excerpt.replace(/\t/g,'    ');
 const nonempty=raw.split('\n').filter(line=>line.trim());
 const indent=nonempty.length?Math.min(...nonempty.map(line=>line.match(/^ */)[0].length)):0;
 const lines=raw.split('\n').map(line=>line.slice(indent));
 const displayLines=[];
 for(let i=0;i<lines.length;i++){
  const n=item.code?item.code.start+i:i+1;
  const line=lines[i];
  if(measure(line,23,CODE)>1060) {
   const wrapped=wrapCodeLine(line,1060);
   for(let j=0;j<wrapped.length;j++)displayLines.push({number:j===0?String(n):'',value:wrapped[j]});
  }else displayLines.push({number:String(n),value:line});
 }
 const source=wrap(item.excerptSource,1152,16,CODE);
 addText(slide,'code-source',source,64,y,1152,44,16,C.muted,false,CODE);y+=44;
 const codeHeight=displayLines.length*29+16;
 if(codeHeight>420)review.push({id:item.id,issue:'long-code',lines:displayLines.length});
 // One flat code surface. The code and line numbers remain editable text.
 const block=slide.shapes.add({name:'code-background',geometry:'textbox',position:{left:64,top:y,width:1152,height:codeHeight},fill:C.code,line:{fill:'none',width:0}});
 block.text='';
 addText(slide,'line-numbers',displayLines.map(line=>line.number).join('\n'),76,y+8,61,codeHeight,23,C.muted,false,CODE);
 addText(slide,'source-code',displayLines.map(line=>line.value).join('\n'),147,y+8,1060,codeHeight,23,C.ink,false,CODE);
 y+=codeHeight+19;
 for(let i=0;i<(item.body??[]).length;i++){y+=paragraph(slide,'code-explanation-'+i,item.body[i],64,y,1152,25)+10;}
 if(y>665)review.push({id:item.id,issue:'code-overflow',bottom:y});
}
function tableSlide(slide,item,y) {
 const values=[item.headers,...item.rows];
 const cols=item.headers.length;
 const widths=item.columnWidths??Array(cols).fill(1152/cols);
 const font=24;
 const rowHeights=values.map(row=>Math.max(...row.map((cell,c)=>wrap(String(cell),widths[c]-26,font).split('\n').length))*font*1.4+20);
 const height=rowHeights.reduce((a,b)=>a+b,0);
 const table=slide.tables.add({rows:values.length,columns:cols,left:64,top:y,width:1152,height,columnWidths:widths,values:values.map(row=>row.map((cell,c)=>wrap(String(cell),widths[c]-26,font)))});
 for(let r=0;r<values.length;r++){
  table.rows[r].height=rowHeights[r];
  for(let c=0;c<cols;c++){const cell=table.getCell(r,c);cell.fill=r===0?C.pale:'#FFFFFF';cell.text.style={typeface:FONT,fontSize:font,bold:r===0,color:C.ink,autoFit:'none',insets:{left:12,right:12,top:8,bottom:8}};}
 }
 table.borders.assign({style:'solid',fill:C.line,width:1});
 y+=height+26;
 for(let i=0;i<(item.body??[]).length;i++)y+=paragraph(slide,'table-explanation-'+i,item.body[i],64,y,1152,25)+13;
 if(y>665)review.push({id:item.id,issue:'table-overflow',bottom:y});
}
async function renderSlide(p,volume,item,index,total) {
 const slide=p.slides.add();slide.background.fill='#FFFFFF';
 let y=title(slide,item);
 if(item.type==='cover'){
  // Covers use the same quiet typographic system as the lessons.
  let cursor=230;
  for(const line of item.body??[])cursor+=paragraph(slide,'cover-'+cursor,line,64,cursor,1070,34,C.teal)+30;
 }else if(item.type==='picture'){
  const file=path.join(BASE,imageManifest[item.illustration].path);
  if(item.visualReading){
   const available=635-y;
   slide.images.add({blob:new Uint8Array(await fs.readFile(file)),contentType:'image/png',
    alt:item.title,fit:'contain',position:{left:48,top:y,width:808,height:available}});
   let readingY=y+15;
   addText(slide,'reading-title','그림을 읽는 순서',885,readingY,331,40,26,C.teal,true);
   readingY+=57;
   for(const [i,value] of item.visualReading.entries()){
    readingY+=paragraph(slide,'reading-'+i,`${i+1}. ${value}`,885,readingY,331,24)+23;
   }
   if(readingY>642)review.push({id:item.id,issue:'picture-reading-overflow',bottom:readingY});
  }else{
  const imageHeight=Math.min(500,635-y);
  slide.images.add({blob:new Uint8Array(await fs.readFile(file)),contentType:'image/png',alt:item.title,fit:'contain',position:{left:64,top:y,width:1152,height:imageHeight}});
  y+=imageHeight;
  if(item.body?.length)y+=paragraph(slide,'image-caption',item.body.join(' '),64,y,1152,24)+6;
  if(y>641)review.push({id:item.id,issue:'picture-overflow',bottom:y});
  }
  addText(slide,'image-disclosure','원리 설명용 개념도',64,647,1152,22,14,C.muted);
 }else if(item.excerpt){codeSlide(slide,item,y);}
 else if(item.type==='table'){tableSlide(slide,item,y);}
 else {
  for(let i=0;i<(item.body??[]).length;i++){
   y+=paragraph(slide,'paragraph-'+i,item.body[i],64,y,1152,27,i===0?C.teal:C.ink,i===0)+19;
  }
  if(y>658)review.push({id:item.id,issue:'body-overflow',bottom:y});
 }
 footer(slide,volume,item,index,total);addNotes(slide,item);return slide;
}
function htmlEscape(value){return String(value??'').replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');}
async function companion(volume,items,stem){
 const parts=[];
 for(let i=0;i<items.length;i++){
  const s=items[i];
  const picture=s.illustration?`<img alt="${htmlEscape(s.title)}" src="data:image/png;base64,${(await fs.readFile(path.join(BASE,imageManifest[s.illustration].path))).toString('base64')}"><p class="caption">원리 설명용 개념도입니다. 실제 프로젝트 화면은 아닙니다.</p>`:'';
  const code=s.excerpt?`<p class="source">${htmlEscape(s.excerptSource)}</p><pre><code>${htmlEscape(s.excerpt)}</code></pre>`:'';
  const table=s.type==='table'?`<div class="table"><table><thead><tr>${s.headers.map(v=>`<th>${htmlEscape(v)}</th>`).join('')}</tr></thead><tbody>${s.rows.map(row=>`<tr>${row.map(v=>`<td>${htmlEscape(v)}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`:'';
  const sources=(s.sources??[]).map(x=>x.url?`<li><a href="${htmlEscape(x.url)}">${htmlEscape(x.label??x.url)}</a></li>`:`<li>${htmlEscape(x.path)}${x.start?':'+x.start:''} ${htmlEscape(x.symbol??'')}</li>`).join('');
  parts.push(`<article id="s${i+1}"><p class="eyebrow">${htmlEscape(s.chapter??volume.title)} / 슬라이드 ${i+1} / ${htmlEscape(s.id)}</p><h2>${htmlEscape(s.title)}</h2>${s.body.map(v=>`<p>${htmlEscape(v)}</p>`).join('')}${picture}${code}${table}<section class="explanation"><h3>상세 해설</h3>${String(s.notes??'').split(/\n+/).filter(Boolean).map(v=>`<p>${htmlEscape(v)}</p>`).join('')}</section>${sources?`<details><summary>읽을 코드와 출처</summary><ul>${sources}</ul></details>`:''}</article>`);
 }
 const html=`<!doctype html><html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${htmlEscape(volume.title)}</title><style>
 *{box-sizing:border-box}body{margin:0;background:#F2F5F5;color:#173648;font:17px/1.9 "Malgun Gothic","Segoe UI",sans-serif;word-break:keep-all;overflow-wrap:anywhere}a{color:#147D82}header,article{max-width:1000px;margin:26px auto;padding:42px 52px;background:white;border:1px solid #D5E0E1}h1{font-size:34px;line-height:1.5}h2{font-size:29px;line-height:1.5;margin:8px 0 24px}h3{font-size:20px;color:#147D82}.eyebrow,.caption,.source{font-size:13px;color:#536B79}.explanation{margin-top:30px;border-top:2px solid #147D82}img{display:block;max-width:100%;height:auto}pre{padding:20px;background:#EDF3F5;white-space:pre-wrap;font:16px/1.8 Consolas,"Malgun Gothic",monospace;overflow-wrap:anywhere}.table{overflow:auto}table{width:100%;border-collapse:collapse;font-size:15px}th,td{border:1px solid #CEDBDF;padding:11px;text-align:left;vertical-align:top}th{background:#EFF6F5}summary{cursor:pointer;color:#147D82}li{margin:5px 0}.toc{columns:2;column-gap:35px;font-size:14px}.toc a{display:block;break-inside:avoid;margin:4px 0}button{font:inherit;background:#173648;color:white;border:0;padding:9px 18px;cursor:pointer}@media(max-width:700px){header,article{margin:12px;padding:24px}.toc{columns:1}h2{font-size:24px}}@media print{@page{size:A4;margin:15mm}body{background:white;font-size:11pt}header,article{max-width:none;margin:0;padding:0;border:0;break-after:page}button,.toc{display:none}h2{font-size:19pt}pre{font-size:9pt}.explanation{break-inside:auto}img{max-height:95mm;object-fit:contain}details{display:block}}
 </style></head><body><header><p>${htmlEscape(volume.id)}권 / 2026-09-08 현재 코드 기준</p><h1>${htmlEscape(volume.title)}</h1><p>슬라이드 본문, 코드 원문, PowerPoint 노트의 해설을 한곳에서 읽는 오프라인 교재입니다. 코드 링크를 누르지 않아도 발췌와 설명을 읽을 수 있습니다. 공식 문서 링크는 추가 확인용이며 인터넷이 없어도 학습할 수 있습니다.</p><p><a href="${stem}.pptx">PowerPoint 파일</a>　<a href="START_HERE.html">전체 교재 안내</a></p><button onclick="window.print()">인쇄 / PDF 저장</button><nav class="toc">${items.map((s,i)=>`<a href="#s${i+1}">${i+1}. ${htmlEscape(s.title)}</a>`).join('')}</nav></header>${parts.join('\n')}</body></html>`;
 await fs.writeFile(path.join(BASE,'output',stem+'.html'),html,'utf8');
}
async function companionCourse(volume,items,stem){
 const esc=htmlEscape;
 const num=id=>items.findIndex(x=>x.id===id)+1;
 const toc=volume.chapters.map((c,ci)=>`<section class="toc-chapter"><h3><a href="#${c.id}">${ci+1}장 ${esc(c.title)}</a> <small>${num(c.id)}쪽</small></h3><p>${esc(c.purpose)}</p><ol>${volume.lessons.filter(l=>l.chapter===c.id).map(l=>`<li><a href="#${l.id}">${esc(l.title)}</a> <small>${l.id} · ${num(l.id)}쪽</small></li>`).join('')}</ol></section>`).join('');
 const parts=[];
 for(let i=0;i<items.length;i++){
  const item=items[i];
  const image=item.illustration?`<figure><img alt="${esc(item.title)}" src="data:image/png;base64,${(await fs.readFile(path.join(BASE,imageManifest[item.illustration].path))).toString('base64')}"><figcaption>원리 설명용 개념도입니다. 실제 화면·측정값과 구분해서 읽습니다.</figcaption></figure>`:'';
  const code=item.excerpt?`<p class="source">${esc(item.excerptSource)}${item.code?' · 실제 프로젝트 소스':''}</p><pre><code>${esc(item.excerpt)}</code></pre>`:'';
  const table=item.type==='table'?`<div class="table"><table><thead><tr>${item.headers.map(c=>`<th>${esc(c)}</th>`).join('')}</tr></thead><tbody>${item.rows.map(r=>`<tr>${r.map(c=>`<td>${esc(c)}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`:'';
  const source=(item.sources??[]).map(s=>s.url?`<li><a href="${esc(s.url)}">${esc(s.label??s.url)}</a></li>`:`<li>${esc(s.path)}${s.start?':'+s.start:''}${s.symbol?' — '+esc(s.symbol):''}</li>`).join('');
  const nav=item.outlineType==='unit-end'?`<nav class="unit-nav"><a href="#${item.lessonId}">이 단원 처음</a> · <a href="#toc">목차</a>${items[i+1]?` · <a href="#${items[i+1].id}">다음 학습</a>`:''}</nav>`:'';
  const context=item.lessonId&&item.outlineType!=='unit'?`<a href="#${item.lessonId}">${esc(item.lessonTitle)}</a>`:esc(item.chapter??'교재 안내');
  const reading=item.visualReading?`<section class="picture-reading"><h3>그림을 읽는 순서</h3><ol>${item.visualReading.map(value=>`<li>${esc(value)}</li>`).join('')}</ol></section>`:'';
  parts.push(`<article id="${item.id}" class="${item.outlineType??item.type}"><p class="location">${context} · ${i+1}쪽</p><h2>${esc(item.title)}</h2>${image}${reading}${code}${table}${item.body.map(v=>`<p>${esc(v).replaceAll('\n','<br>')}</p>`).join('')}${item.notes?`<details class="supplement"><summary>보충 설명</summary>${item.notes.split(/\n+/).filter(Boolean).map(v=>`<p>${esc(v)}</p>`).join('')}</details>`:''}${source?`<details><summary>사용 소스코드와 공식 자료</summary><ul>${source}</ul></details>`:''}${nav}</article>`);
 }
 const html=`<!doctype html><html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${esc(volume.title)} · 단원형 개정판</title><style>
*{box-sizing:border-box}html{scroll-padding-top:24px}body{margin:0;background:#f2f5f4;color:#173648;font:18px/1.95 "Malgun Gothic","Segoe UI",sans-serif;word-break:keep-all;overflow-wrap:anywhere}header,article{max-width:1040px;margin:26px auto;background:white;padding:42px 58px;border:1px solid #d2dfe0}h1{font-size:37px;line-height:1.5;margin:10px 0 25px}h2{font-size:29px;line-height:1.5;margin:12px 0 24px}h3{font-size:21px;line-height:1.7;margin-bottom:7px}p{margin:15px 0}a{color:#126e75;text-underline-offset:4px}.location,.source,figcaption,small{color:#55707a;font-size:14px}.toc-chapter{margin:25px 0;padding-top:16px;border-top:1px solid #d2dfe0}.toc-chapter p{font-size:16px;margin:6px 0}.toc-chapter ol{margin:10px 0;font-size:17px}.toc-chapter li{margin:5px 0}article.chapter{border-top:5px solid #147d82}article.unit{border-top:3px solid #147d82}article.answer h2{color:#146959}pre{background:#edf3f5;padding:22px;font:16px/1.8 Consolas,"Malgun Gothic",monospace;white-space:pre-wrap;overflow-wrap:anywhere}.table{overflow-x:auto;margin:22px 0}table{width:100%;border-collapse:collapse;font-size:16px}th,td{border:1px solid #ccdadd;padding:12px;text-align:left;vertical-align:top}th{background:#eff6f5}figure{margin:24px 0}img{max-width:100%;height:auto;display:block}figcaption{margin-top:12px}details{margin:20px 0;font-size:16px}summary{cursor:pointer;color:#126e75}details p{line-height:1.9}.unit-nav{border-top:1px solid #d2dfe0;padding-top:17px;margin-top:23px;font-size:16px}.intro{border-left:3px solid #147d82;padding-left:20px}.print{font:inherit;background:white;border:1px solid #147d82;padding:6px 15px;cursor:pointer;color:#126e75} @media(max-width:720px){header,article{margin:12px;padding:25px}h1{font-size:29px}h2{font-size:25px}body{font-size:17px}table{font-size:14px}td,th{padding:9px}pre{font-size:14px}} @media print{@page{size:A4;margin:15mm}body{background:white;font-size:11pt}header,article{padding:0;margin:0;border:0;max-width:none;break-after:page}.print,.unit-nav{display:none}h1{font-size:26pt}h2{font-size:19pt}pre{font-size:9pt}table{font-size:9pt}figure img{max-height:90mm;object-fit:contain}details{font-size:10pt}}
</style></head><body><header id="toc"><small>${volume.id}권 · 2026-09-08 코드 기준 · 단원형 개정판</small><h1>${esc(volume.title)}</h1><p class="intro">${esc(volume.subtitle)}<br>각 단원에서 문제와 목적을 확인하고 개념, 예시, 실제 코드, 값 추적, 확인 문제 순서로 읽습니다. 핵심 설명은 본문에 있으며 보충 설명만 접어 두었습니다.</p><p><a href="${stem}.pptx">PPT 교재</a> · <a href="START_HERE.html">전체 학습 안내</a></p><button class="print" onclick="window.print()">인쇄 / PDF 저장</button><h2>목차</h2>${toc}</header>${parts.join('\n')}</body></html>`;
 await fs.writeFile(path.join(OUTPUT,stem+'.html'),html);
}
function coursePages(volume){
 const items=[{id:'COVER',type:'cover',title:volume.title,body:[`${volume.id}권  Drive Integration`,volume.subtitle,'단원별 해설과 소스코드 실습'],notes:'2026-09-08 작업 폴더 기준. Windows와 Unreal Engine 5.6 환경의 실제 코드, 교육용 예제, 개념도를 구분하여 읽습니다.',sources:[]},
 {id:'HOW',type:'lesson',title:'단원 학습과 확인 방법',body:['각 단원은 먼저 실제 운전·통신 문제를 제시합니다. 왜 그 문제가 생기며 어떤 개념이 해결에 필요한지 확인한 뒤 코드를 읽습니다. 사용 목적과 핵심 해설은 슬라이드 본문에 있습니다.','코드 페이지의 경로와 줄 번호는 현재 소스의 연속 구간을 가리킵니다. 바로 다음 풀이에서 입력, 조건, 상태 변경과 결과를 연결합니다. 교육용 계산은 실제 설정값·측정값과 구분합니다.','확인 문제는 답을 보지 않고 먼저 풀어 보세요. 결과 숫자뿐 아니라 왜 그 분기를 선택했는지 설명해야 합니다. 틀린 경우 같은 단원의 예시와 코드 풀이를 다시 읽고 다음 단원으로 넘어갑니다.','목차의 시작 쪽과 각 장의 학습 순서로 현재 위치를 찾습니다. HTML 교재에는 장·단원별 바로가기와 같은 본문이 있습니다. 노트의 추가 설명은 보충 자료이므로 본문만으로도 핵심 수업을 따라갈 수 있습니다.'],notes:'예제를 실제 서버에 실행하지 않아도 종이 위의 값 추적과 코드 읽기만으로 필수 문제를 풀 수 있습니다. 필수 학습에는 AI나 인터넷 연결이 필요하지 않습니다.',sources:[]}];
 for(let i=0;i<volume.chapters.length;i+=3)items.push({id:'TOC-'+i,type:'lesson',title:`전체 목차 ${Math.floor(i/3)+1}`,body:[],tocChapters:volume.chapters.slice(i,i+3).map(c=>c.id),notes:'목차는 장 제목과 시작 쪽, 해당 장에서 풀 문제를 함께 보여 줍니다. 아래 장의 내용을 사용하려면 앞 장의 완료 조건을 먼저 확인하세요.',sources:[]});
 for(const [ci,chapter] of volume.chapters.entries()){
  const group=volume.lessons.filter(l=>l.chapter===chapter.id);
  if(!group.length)throw Error('Empty chapter '+chapter.id);
  const shared={chapterId:chapter.id,chapter:`${ci+1}장 ${chapter.title}`};
  items.push({...shared,id:chapter.id,type:'lesson',outlineType:'chapter',title:`${ci+1}장 ${chapter.title}`,body:[`이 장의 목적: ${chapter.purpose}`,`먼저 익힐 내용: ${chapter.prerequisite}`,`학습 목표: ${chapter.objectives.join(' ')}`,`이 장의 완료 기준: ${chapter.outcome}`],notes:chapter.next,sources:[]});
  items.push({...shared,id:chapter.id+'-ORDER',type:'lesson',title:'이 장의 학습 순서',body:[],tocLessons:group.map(l=>l.id),notes:chapter.next,sources:[]});
  for(const lesson of group){
   const common={...shared,lessonId:lesson.id,lessonTitle:lesson.title};
   items.push({...common,id:lesson.id,type:'lesson',outlineType:'unit',title:lesson.title,body:[`문제 상황: ${lesson.problem}`,`사용 목적과 이유: ${lesson.why}`,`이 단원에서 할 일: ${lesson.goal}`,`필요한 앞 내용: ${lesson.prerequisite}`],notes:lesson.next,sources:[]});
   for(const page of lesson.pages){
    const expanded={...common,...page,id:lesson.id+'--'+page.id,body:page.body??[],notes:page.notes??'',sources:page.sources??[]};
    if(page.type==='picture'){
     items.push({...expanded,body:[]});
     const {imageKey,...reading}=expanded;
     items.push({...reading,id:expanded.id+'-READ',type:'lesson',title:'그림 해설: '+page.title});
    }else items.push(expanded);
   }
   items.push({...common,id:lesson.id+'-NEXT',type:'lesson',outlineType:'unit-end',title:'단원 정리와 다음 학습',body:[`이제 설명할 수 있어야 하는 것: ${lesson.goal}`,`문제와 연결: ${lesson.why}`,`다음 단원으로 이어지는 이유: ${lesson.next}`],notes:'확인 문제의 결과를 맞혀도 코드의 조건을 설명하지 못한다면 앞의 코드 풀이를 다시 읽습니다. 해당 단원에서 아직 모르는 변수나 함수 이름을 적고 넘어가세요.',sources:[]});
  }
 }
 const pageNumber=id=>{const n=items.findIndex(x=>x.id===id);if(n<0)throw Error('Missing target '+id);return n+1;};
 for(const item of items){
  if(item.tocChapters)item.body=item.tocChapters.map(id=>{const c=volume.chapters.find(x=>x.id===id);return `${volume.chapters.indexOf(c)+1}장 ${c.title} · ${pageNumber(id)}쪽\n${c.purpose}`;});
  if(item.tocLessons)item.body=item.tocLessons.map(id=>{const l=volume.lessons.find(x=>x.id===id);return `${id} ${l.title} · ${pageNumber(id)}쪽\n${l.goal}`;});
 }
 return items;
}
async function build(number){
 let volume,items;
 if(ILLUSTRATED){
  const {loadIllustratedVolume}=await import('./illustrated_content.mjs');
  const data=await loadIllustratedVolume(ROOT,number,process.argv.includes('--layout-only'));
  volume=data.volume;items=data.items;Object.assign(imageManifest,data.manifest);
 }else{
  volume=(await import(pathToFileURL(path.join(BASE,`source/revised${number}.mjs`)).href)).default;
  items=await expand({slides:coursePages(volume)});
 }
 const revision=process.argv.find(arg=>/^--revision=[a-z0-9]+$/.test(arg))?.split('=')[1];
 const stem=outputNames[volume.id]+'_Textbook_'+(revision??'r1');
 const dir=path.join(BUILD,stem);await fs.mkdir(dir,{recursive:true});
 const p=Presentation.create({slideSize:{width:1280,height:720}});
 const tables=[];
 for(let i=0;i<items.length;i++){await renderSlide(p,volume,items[i],i,items.length);if(items[i].type==='table')tables.push(i+1);}
 const candidate=path.join(dir,'candidate.pptx');
 await(await PresentationFile.exportPptx(p)).save(candidate);
 await fs.writeFile(path.join(dir,'expanded.json'),JSON.stringify({volume,items},null,2));
 await fs.writeFile(path.join(dir,'author-fit-review.json'),JSON.stringify(review,null,2));
 if(process.argv.includes('--layout-only')){
  await fs.mkdir(path.join(dir,'preview'),{recursive:true});
  for(const i of [...new Set([0,2,5,8,12,18,items.findIndex(x=>x.illustration),items.findIndex(x=>x.code),items.findIndex(x=>x.type==='table')])].filter(x=>x>=0&&x<items.length)){
   const png=await p.export({slide:p.slides.items[i],format:'png',scale:1});
   await fs.writeFile(path.join(dir,'preview',String(i+1).padStart(3,'0')+'.png'),new Uint8Array(await png.arrayBuffer()));
  }
  console.log(JSON.stringify({volume:volume.id,status:'draft-review',slides:items.length,issues:review}));return;
 }
 if(review.length){console.log(JSON.stringify({volume:volume.id,status:'fit-review',issues:review}));if(!process.argv.includes('--allow-review'))return;}
 const finalPath=path.join(OUTPUT,stem+'.pptx');
 const result=await finalizePresentation({workspaceDir:ROOT,candidatePath:candidate,finalPath,pythonExecutable:PYTHON,integrityValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_package_integrity.py'),layoutValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_layout_geometry.py'),layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-bullet-geometry','--validate-heading-fit',...tables.flatMap(n=>['--require-native-table-slide',String(n)])],requiredNativeTableOwnerSlides:tables,fontPolicy,verifyArtifactToolImport:true,receiptPath:path.join(dir,'validation.json')});
 console.log(JSON.stringify({volume:volume.id,status:'finalized',slides:items.length,finalPath,validation:result.schemaVersion}));
 await companionCourse(volume,items,stem);
 const finalDeck=await PresentationFile.importPptx(await FileBlob.load(finalPath));
 await fs.mkdir(path.join(dir,'rendered'),{recursive:true});
 for(let i=0;i<items.length;i++){
  const slide=finalDeck.slides.items[i];
  const png=await finalDeck.export({slide,format:'png',scale:1});
  await fs.writeFile(path.join(dir,'rendered',String(i+1).padStart(3,'0')+'.png'),new Uint8Array(await png.arrayBuffer()));
  const layout=await slide.export({format:'layout'});
  await fs.writeFile(path.join(dir,'rendered',String(i+1).padStart(3,'0')+'.layout.json'),await layout.text());
  if((i+1)%10===0)console.log(`${volume.id} rendered ${i+1}/${items.length}`);
 }
 console.log(JSON.stringify({volume:volume.id,status:'complete',slides:items.length,codes:items.filter(x=>x.code).length,pictures:items.filter(x=>x.illustration).length,tables:tables.length}));
}
await fs.mkdir(OUTPUT,{recursive:true});
await build(Number(process.argv[2]??1));
