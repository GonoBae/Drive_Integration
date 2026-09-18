import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const ROOT = 'B:/Portfolio/Drive_Integration';
const BASE = path.join(ROOT, 'docs/study/pptx_textbook');
const BUILD = path.join(ROOT, 'runtime_tmp/study_decks_20260908');
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
const imageManifest = JSON.parse(await fs.readFile(path.join(BASE, 'source/image_manifest.json'), 'utf8'));
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
  if(item.imageKey&&imageManifest[item.imageKey]&&!usedImages.has(item.imageKey)){
   usedImages.add(item.imageKey);
   const title=imageDescriptions[item.imageKey]??item.title;
   result.push({id:item.id+'-FIG',type:'picture',title,chapter:item.chapter,illustration:item.imageKey,body:[...(item.body??[]).slice(0,2)],notes:`${item.notes??''}\n\n그림은 이 개념을 눈으로 연결하기 위한 설명입니다. 영어 표기는 앞 페이지의 용어와 대응해서 읽으세요. 실제 데이터 값과 함수의 처리 순서는 인용한 코드에서 확인합니다.`,sources:item.sources??[]});
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
 addText(slide,'footer',`${volume.id}권  ${item.chapter??volume.title}     ${index+1} / ${count}`,64,680,1152,24,15,C.muted);
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
   else if(ch===','||ch==='('||/\s/.test(ch)){if(remaining.slice(0,i).trim())lastBreak=i+1;}
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
  slide.images.add({blob:new Uint8Array(await fs.readFile(file)),contentType:'image/png',alt:item.title,fit:'contain',position:{left:64,top:y,width:1152,height:385}});
  y+=391;
  if(item.body?.length)y+=paragraph(slide,'image-caption',item.body.join(' '),64,y,1152,23)+6;
  addText(slide,'image-disclosure','원리 설명용 개념도',64,647,1152,22,14,C.muted);
 }else if(item.excerpt){codeSlide(slide,item,y);}
 else if(item.type==='table'){tableSlide(slide,item,y);}
 else {
  for(let i=0;i<(item.body??[]).length;i++){
   y+=paragraph(slide,'paragraph-'+i,item.body[i],64,y,1120,30,i===0?C.teal:C.ink,i===0)+27;
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
async function build(number){
 const volume=(await import(pathToFileURL(path.join(BASE,`source/volume${number}.mjs`)).href)).default;
 const lessons=await expand(volume);
 const chapters=[...new Set(lessons.map(s=>s.chapter).filter(Boolean))];
 const items=[
  {id:`V${number}-COVER`,type:'cover',title:volume.title,chapter:'교재 안내',body:[`${volume.id}권  Drive Integration`,volume.subtitle??'코드를 읽고 동작을 설명하는 독학 교재','Windows / Unreal Engine 5.6'],notes:'이 교재는 2026년 9월 8일의 프로젝트 작업 폴더를 기준으로 합니다. 한 장씩 본문과 코드 발췌를 읽은 다음 노트의 상세 해설로 이해를 대조합니다. 생성된 개념 그림과 현재 프로젝트 구현을 구분하고, 실제로 학습한 부분만 노트에 기록하세요.',sources:[]},
  {id:`V${number}-HOW`,type:'lesson',title:'교재를 읽는 방법',chapter:'교재 안내',body:['처음 보는 용어는 정의와 예제를 함께 읽습니다. 코드 발췌 옆의 경로와 줄 번호로 원본을 찾을 수 있습니다.','PowerPoint 아래쪽 노트 창에 상세 풀이가 있습니다. 같은 내용을 동봉한 HTML 교재에서도 읽을 수 있습니다.','교육용 예시와 실제 코드 발췌를 구분합니다. 실습 문제는 먼저 풀고 다음 정답 페이지와 대조합니다.','전체를 하루에 끝내지 않아도 됩니다. 이 권의 마지막 복습 과제까지 자신의 속도로 진행합니다.'],notes:'한 장에서 모든 줄을 암기하려 하지 마세요. 입력, 바뀌는 상태, 출력, 실패 조건을 네 칸으로 메모하면 됩니다. 코드 슬라이드는 전체 함수 중 이번 개념에 필요한 연속 구간을 발췌했습니다. 공통 들여쓰기와 화면 줄바꿈은 읽기 좋게 조정하지만 원본 파일은 변경하지 않습니다. PowerPoint의 기본 보기에서 하단 노트 버튼을 누르면 상세 해설이 보입니다. HTML 파일에는 본문과 코드 아래에 같은 해설을 함께 넣었습니다. 제공된 코드 발췌는 교재 안에 있으므로 AI 연결 없이 읽을 수 있습니다. 실습 실행이 막히면 코드와 테스트 기대값을 손으로 추적하는 과제로 대체합니다.',sources:[]},
 ];
 for(let i=0;i<chapters.length;i+=4)items.push({id:`V${number}-TOC-${i}`,type:'lesson',title:'학습 순서'+(chapters.length>4?` ${i/4+1}`:''),chapter:'교재 안내',body:chapters.slice(i,i+4),notes:'앞 단계의 용어가 뒤 단계 코드에서 다시 나옵니다. 모르는 이름이 생기면 해당 장으로 돌아가 정의와 예제를 대조하세요. 화면의 장 제목과 각 페이지 아래의 장 이름을 이용해 위치를 찾을 수 있습니다.',sources:[]});
 items.push(...lessons);
 const revision=process.argv.find(arg=>/^--revision=[a-z0-9]+$/.test(arg))?.split('=')[1];
 const stem=outputNames[volume.id]+(revision?'_'+revision:'');
 const dir=path.join(BUILD,stem);await fs.mkdir(dir,{recursive:true});
 const p=Presentation.create({slideSize:{width:1280,height:720}});
 const tables=[];
 for(let i=0;i<items.length;i++){await renderSlide(p,volume,items[i],i,items.length);if(items[i].type==='table')tables.push(i+1);}
 const candidate=path.join(dir,'candidate.pptx');
 await(await PresentationFile.exportPptx(p)).save(candidate);
 await fs.writeFile(path.join(dir,'expanded.json'),JSON.stringify({volume:{id:volume.id,title:volume.title},items},null,2));
 await fs.writeFile(path.join(dir,'author-fit-review.json'),JSON.stringify(review,null,2));
 if(review.length){console.log(JSON.stringify({volume:volume.id,status:'fit-review',issues:review}));if(!process.argv.includes('--allow-review'))return;}
 const finalPath=path.join(BASE,'output',stem+'.pptx');
 const result=await finalizePresentation({workspaceDir:ROOT,candidatePath:candidate,finalPath,pythonExecutable:PYTHON,integrityValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_package_integrity.py'),layoutValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_layout_geometry.py'),layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-bullet-geometry','--validate-heading-fit',...tables.flatMap(n=>['--require-native-table-slide',String(n)])],requiredNativeTableOwnerSlides:tables,fontPolicy,verifyArtifactToolImport:true,receiptPath:path.join(dir,'validation.json')});
 console.log(JSON.stringify({volume:volume.id,status:'finalized',slides:items.length,finalPath,validation:result.schemaVersion}));
 await companion(volume,items,stem);
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
await fs.mkdir(path.join(BASE,'output'),{recursive:true});
await build(Number(process.argv[2]??1));
