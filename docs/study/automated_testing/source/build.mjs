import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
import project from './project.mjs';

const ROOT='B:/Portfolio/Drive_Integration';
const BASE=path.join(ROOT,'docs/study/automated_testing');
const BOOK=path.join(ROOT,'docs/study/pptx_textbook');
const PREVIOUS=path.join(ROOT,'docs/study/interview_deck');
const TMP=path.join(ROOT,'runtime_tmp/automated_testing_20260908');
const SKILL='C:/Users/PC/.codex/plugins/cache/openai-primary-runtime/presentations/26.905.11957/skills/presentations';
const MODULES='C:/Users/PC/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const PYTHON='C:/Users/PC/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
const revision=process.argv.find(a=>a.startsWith('--revision='))?.split('=')[1]??'v1';
const preflight=process.argv.includes('--preflight');
const build=path.join(TMP,revision);
const stem='DriveIntegration_Automated_Testing_'+revision;
const output=path.join(BASE,'output',stem+'.pptx');
process.env.RUNTIME_NODE_MODULES=MODULES;
process.env.RUNTIME_NODE=process.execPath;
process.env.RUNTIME_PYTHON=PYTHON;
const require=createRequire(path.join(TMP,'probe.mjs'));
const {Presentation,PresentationFile}=await import(pathToFileURL(require.resolve('@oai/artifact-tool')).href);
const {createCanvas,GlobalFonts}=require('@napi-rs/canvas');
GlobalFonts.registerFromPath('C:/Windows/Fonts/malgun.ttf','Malgun Gothic');
GlobalFonts.registerFromPath('C:/Windows/Fonts/malgunbd.ttf','Malgun Gothic');
GlobalFonts.registerFromPath('C:/Windows/Fonts/consola.ttf','Consolas');
const ctx=createCanvas(1,1).getContext('2d'); // Text measurement only, never illustration.
const {finalizePresentation}=await import(pathToFileURL(path.join(SKILL,'container_tools/artifact_tool_utils.mjs')).href);
const FONT='Malgun Gothic',CODE='Consolas';
const C={ink:'#15374B',teal:'#087E87',muted:'#58717C',orange:'#A95718'};
const manifest={};
for(const name of ['image_manifest.json','revised_images.json']){
 const data=JSON.parse(await fs.readFile(path.join(BOOK,'source',name),'utf8'));
 for(const [key,value]of Object.entries(data))manifest[key]={...value,absolute:path.join(BOOK,value.path),promptSource:path.join(BOOK,'source',name)};
}
for(let n=1;n<=3;n++){
 const data=JSON.parse(await fs.readFile(path.join(BOOK,`source/illustrated_v${n}.json`),'utf8'));
 for(const value of data)manifest[value.imageKey]={...value,absolute:path.join(BOOK,value.path),promptSource:path.join(BOOK,`source/illustrated_v${n}.json`)};
}
for(const base of [PREVIOUS,BASE]){
 let data={};try{data=JSON.parse(await fs.readFile(path.join(base,'source/new_images.json'),'utf8'));}catch(e){if(e.code!=='ENOENT')throw e;}
 for(const [key,value]of Object.entries(data))manifest[key]={...value,absolute:path.join(base,value.path),promptSource:path.join(base,'source/new_images.json')};
}
manifest.curb_actual={absolute:path.join(PREVIOUS,'assets/curb_report_actual.png'),kind:'사용자 제공 수정 전 오류 화면',prompt:'이미지 생성 없이 원본을 복사했습니다.'};
const readJson=async name=>JSON.parse(await fs.readFile(path.join(BASE,'source',name),'utf8'));
const all=[...project.filter(s=>s.id.startsWith('A')),...await readJson('server.json'),...await readJson('unreal.json'),...project.filter(s=>s.id.startsWith('W'))];
if(all.length!==36||new Set(all.map(s=>s.id)).size!==36)throw Error('Expected 36 unique slides');
let items=all;
if(preflight){
 items=[];
 for(const item of all){try{await fs.access(manifest[item.imageKey].absolute);items.push(item);}catch{console.log('Waiting image '+item.id+' '+item.imageKey);}}
}
const hashes=new Set();
for(const item of items){
 const asset=manifest[item.imageKey];if(!asset)throw Error('Missing image '+item.imageKey);
 asset.data=await fs.readFile(asset.absolute);
 const hash=crypto.createHash('sha256').update(asset.data).digest('hex');
 if(hashes.has(hash))throw Error('Repeated image '+item.id);hashes.add(hash);
 if(item.codeRef){const ref=item.codeRef;const lines=(await fs.readFile(path.join(ROOT,ref.path),'utf8')).replaceAll('\r\n','\n').split('\n');
  if(ref.start<1||ref.end>lines.length)throw Error('Code range '+item.id);item.code={...ref,text:lines.slice(ref.start-1,ref.end).join('\n')};}
 if(item.type!=='cover')for(const field of ['why','answer','followup','notes'])if(!item[field])throw Error('Missing '+field+' '+item.id);
 for(const source of item.sources??[]){if(source.path){const absolute=path.isAbsolute(source.path)?source.path:path.join(ROOT,source.path);const lines=(await fs.readFile(absolute,'utf8')).split('\n');if(source.start<1||source.end>lines.length)throw Error('Source range '+item.id+' '+source.path+' '+source.end+' > '+lines.length);}}
}
await fs.mkdir(path.join(build,'rendered'),{recursive:true});await fs.mkdir(path.join(BASE,'output'),{recursive:true});
const deck=Presentation.create({slideSize:{width:1600,height:900}});
const review=[];
function measure(value,size,family=FONT,bold=false){ctx.font=`${bold?'bold ':''}${size}px "${family}"`;return ctx.measureText(value).width;}
function wrap(value,width,size,family=FONT,bold=false){return String(value).split('\n').flatMap(para=>{
 const out=[];let current='';for(const token of para.split(/(\s+)/)){
  if(measure(current+token,size,family,bold)<=width){current+=token;continue;}
  if(current.trim()){out.push(current.trimEnd());current='';}
  if(measure(token,size,family,bold)<=width){current=token.trimStart();continue;}
  for(const ch of token){if(measure(current+ch,size,family,bold)>width){out.push(current);current='';}current+=ch;}
 }if(current||!out.length)out.push(current.trimEnd());return out;}).join('\n');}
function text(slide,name,value,x,y,w,h,size,color=C.ink,bold=false,family=FONT){
 const shape=slide.shapes.add({name,geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});
 shape.text=value;shape.text.style={typeface:family,fontSize:size,color,bold,autoFit:'none',wrap:'none',insets:{left:0,right:0,top:0,bottom:0}};return shape;
}
function paragraph(slide,name,value,x,y,w,size=26,color=C.ink,bold=false,family=FONT){
 const rendered=wrap(value,w,size,family,bold),height=rendered.split('\n').length*size*1.32+5;
 text(slide,name,rendered,x,y,w,height,size,color,bold,family);return height;
}
function codeLines(value,width,size){const lines=value.replaceAll('\t','    ').split('\n');
 const indent=Math.min(...lines.filter(s=>s.trim()).map(s=>s.match(/^\s*/)[0].length));
 return lines.map(s=>s.slice(indent)).flatMap(line=>{
  if(measure(line,size,CODE)<=width)return[line];
  const out=[];let current='';for(const token of line.split(/(\s+|(?<=[,;({])|(?<=\.)(?=[A-Za-z_]))/)){
   if(measure(current+token,size,CODE)>width&&current.trim()){out.push(current.trimEnd());current='  ';}current+=token;
  }out.push(current);return out;}).join('\n');
}
function sourcesText(item){return(item.sources??[]).map(s=>s.url?`${s.label??'공식 문서'} ${s.url}`:`${s.path}${s.start?':'+s.start+(s.end?'-'+s.end:''):''}`).join('\n');}
function notes(item){let str=`학습 질문\n${item.question}\n\n사용 목적\n${item.why}\n\n핵심 설명\n${item.answer}\n\n${item.notes}\n\n확인 질문\n${item.followup}\n\n그림 읽기\n${(item.reading??[]).join('\n')}\n\n출처\n${sourcesText(item)}`;
 if(item.code)str+=`\n\n실제 코드 원문\n${item.code.path}:${item.code.start}-${item.code.end}\n${item.code.text}\n${item.code.explain}`;
 str+=`\n\n그림 출처\n${manifest[item.imageKey].absolute}\n${item.imageKey==='curb_actual'?'수정 전 사용자 제보 화면. 수정 결과를 보여 주는 사진이 아닙니다.':'교육용 개념도. 실제 게임 화면 또는 실측 결과가 아닙니다.'}`;return str;
}
for(const item of items){
 const index=all.findIndex(s=>s.id===item.id),slide=deck.slides.add();slide.background.fill='#FFFFFF';
 const asset=manifest[item.imageKey];const img=(x,y,w,h,fit='contain')=>slide.images.add({name:item.imageKey,blob:asset.data,contentType:'image/png',position:{left:x,top:y,width:w,height:h},fit,alt:item.title+' 설명 그림'});
 if(item.type==='cover'){
  img(0,0,1600,900,'cover');paragraph(slide,'title',item.title,64,96,680,68,C.ink,true);
  paragraph(slide,'subtitle',item.subtitle,69,322,670,30);paragraph(slide,'answer',item.answer,69,737,1410,29);
  text(slide,'disclosure','교육용 표지 일러스트',70,856,720,28,19,C.muted);
 }else{
  const title=wrap(item.title,1410,49,FONT,true);if(title.includes('\n'))review.push({id:item.id,issue:'two-line-title'});
  text(slide,'title',title,56,38,1430,76,49,C.ink,true);text(slide,'page',String(index+1).padStart(2,'0')+' / 36',1470,48,100,38,22,C.muted);
  if(paragraph(slide,'question',item.question,58,117,1480,29,C.teal)>67)review.push({id:item.id,issue:'long-question'});
  const rx=item.code?925:1120,rw=item.code?620:425;img(52,186,item.code?815:1015,505);
  let cursor=187;text(slide,'why-label','왜 필요한가',rx,cursor,rw,37,23,C.teal,true);cursor+=43;
  cursor+=paragraph(slide,'why',item.why,rx,cursor,rw,26)+22;
  if(item.code){
   text(slide,'code-label','실제 코드',rx,cursor,rw,35,23,C.teal,true);cursor+=41;
   const shown=codeLines(item.code.text,rw,23);for(const line of shown.split('\n'))if(measure(line,23,CODE)>rw)review.push({id:item.id,issue:'code-too-wide',line});
   const height=shown.split('\n').length*23*1.26+8;text(slide,'code',shown,rx,cursor,rw,height,23,C.ink,false,CODE);cursor+=height+18;
   cursor+=paragraph(slide,'code-explanation',item.code.explain,rx,cursor,rw,24);
  }else for(const [n,cue]of(item.reading??[]).entries())cursor+=paragraph(slide,'reading-'+n,cue,rx,cursor,rw,26)+24;
  if(cursor>708)review.push({id:item.id,issue:'right-overflow',bottom:cursor});
  const source=item.code??item.sources?.find(s=>s.path);const label=item.section+(source?'   '+path.basename(source.path)+(source.start?':'+source.start:''):'');
  text(slide,'source',label,57,704,1080,27,17,C.muted);
  text(slide,'image-disclosure',item.imageKey==='curb_actual'?'수정 전 사용자 오류 화면':'원리 설명 그림',1120,705,425,27,17,C.muted);
  const bottom=742+paragraph(slide,'answer','핵심  '+item.answer,58,742,1482,25);if(bottom>855)review.push({id:item.id,issue:'answer-overflow',bottom});
  const q=wrap('확인 질문  '+item.followup,1480,22);if(q.includes('\n'))review.push({id:item.id,issue:'followup-overflow'});
  text(slide,'followup',q,58,865,1482,33,22,C.orange);
 }slide.speakerNotes.textFrame.setText(notes(item));
}
await fs.writeFile(path.join(build,'expanded.json'),JSON.stringify({items,images:items.map(s=>({id:s.id,key:s.imageKey,path:manifest[s.imageKey].absolute,promptSource:manifest[s.imageKey].promptSource}))},null,2));
await fs.writeFile(path.join(build,'fit-review.json'),JSON.stringify(review,null,2));
await(await PresentationFile.exportPptx(deck)).save(path.join(build,'candidate.pptx'));
if(!review.length&&!preflight){
 await finalizePresentation({workspaceDir:ROOT,candidatePath:path.join(build,'candidate.pptx'),finalPath:output,pythonExecutable:PYTHON,
  integrityValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_package_integrity.py'),layoutValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_layout_geometry.py'),
  layoutArgs:['--expected-slide-size-emu','15240000,8572500','--validate-bullet-geometry','--validate-heading-fit'],requiredNativeTableOwnerSlides:[],
  fontPolicy:{basis:'design',families:[FONT,CODE],scriptFonts:{ea:FONT}},verifyArtifactToolImport:true,receiptPath:path.join(build,'validation.json')});
 console.log(JSON.stringify({status:'finalized',path:output,slides:items.length}));
 const md=['# Drive Integration 자동 테스트 기술 교재','',`[그림 중심 PPT](${output.replaceAll('\\','/')})`,'','기준일: 2026년 9월 8일. 프로젝트 코드와 과거 실행 기록을 설명하는 교재입니다. 이번 문서 작성 과정에서 게임 테스트를 새로 실행하지 않았습니다.','',
  '## 목차','','- 1장 학습 기초: PPT 1–7쪽','- 2장 C++ 서버: PPT 8–15쪽','- 3장 Unreal: PPT 16–23쪽','- 4장 통합 도구와 실행: PPT 24–36쪽','- 부록: Windows 실행 명령, 결과 해석, 검사 등록 목록과 학습 연습','','질문을 먼저 읽고 그림으로 검사 대상을 파악한 뒤, 사용 목적과 준비·실행·판정을 확인합니다. 각 주제의 효과와 한계를 같이 읽어 주세요.'];
 for(const [n,item]of items.entries()){
  md.push('',`## ${n+1}. ${item.title.replaceAll('\n',' ')}`,'',`**학습 질문:** ${item.question}`,'',`![${item.title.replaceAll('\n',' ')}](${manifest[item.imageKey].absolute.replaceAll('\\','/')})`,'',`*${item.imageKey==='curb_actual'?'사용자 제공 수정 전 오류 화면':'교육용 원리 그림. 실제 실행 결과나 실측 자료가 아닙니다.'}*`,'','### 사용 목적','',item.why,'','### 핵심 설명','',item.answer,'');
  for(const para of item.notes.split('\n\n')){const lines=para.split('\n');if(lines.length>1){md.push('### '+lines[0],'',lines.slice(1).join('\n'),'');}else md.push(para,'');}
  if(item.code){const ext=path.extname(item.code.path);const lang=ext==='.py'?'python':ext==='.ps1'?'powershell':ext==='.txt'?'cmake':'cpp';md.push('### 실제 사용 코드','',`[${item.code.path}:${item.code.start}](${path.join(ROOT,item.code.path).replaceAll('\\','/')}:${item.code.start})`,'','```'+lang,item.code.text,'```','',item.code.explain,'');}
  md.push('### 스스로 확인할 질문','',item.followup,'','### 근거','');
  for(const s of item.sources??[])md.push(s.url?`- [${s.label??'공식 문서'}](${s.url})`:`- [${s.path}${s.start?':'+s.start:''}](${(path.isAbsolute(s.path)?s.path:path.join(ROOT,s.path)).replaceAll('\\','/')}${s.start?':'+s.start:''})`);
 }
 md.push('',await fs.readFile(path.join(BASE,'source/appendix.md'),'utf8'));
 md.push('',await fs.readFile(path.join(BASE,'source/inventory.md'),'utf8'));
 await fs.writeFile(path.join(BASE,'output','자동_테스트_상세_교재.md'),md.join('\n')+'\n');
 const catalog=['# 자동 테스트 교재의 그림 출처','','신규 그림은 built-in imagegen으로 제작했습니다. 프롬프트와 수정 지시는 원문 JSON의 해당 키에서 확인할 수 있습니다. 기존 원교재와 면접 교재의 관련 개념 그림도 재사용했습니다. 실제 사용자 오류 화면은 수정 전 제보 자료이며 성공 증거로 사용하지 않습니다.','','| PPT 쪽 | 그림 키 | 원본 | 정확한 프롬프트 출처 |','| --- | --- | --- | --- |'];
 for(const [n,item]of items.entries()){const a=manifest[item.imageKey];catalog.push(`| ${n+1} | ${item.imageKey} | [PNG](${a.absolute.replaceAll('\\','/')}) | ${a.promptSource?`[JSON](${a.promptSource.replaceAll('\\','/')})의 ${item.imageKey}`:'사용자가 제공한 오류 제보 화면'} |`);}
 await fs.writeFile(path.join(BASE,'ILLUSTRATIONS.md'),catalog.join('\n')+'\n');
}
for(let n=0;n<deck.slides.items.length;n++){
 const slide=deck.slides.items[n],number=String(all.findIndex(s=>s.id===items[n].id)+1).padStart(3,'0');
 await fs.writeFile(path.join(build,'rendered',number+'.png'),new Uint8Array(await(await deck.export({slide,format:'png',scale:1})).arrayBuffer()));
 await fs.writeFile(path.join(build,'rendered',number+'.layout.json'),await(await slide.export({format:'layout'})).text());
}
console.log(JSON.stringify({status:review.length?'fit-review':'complete',slides:items.length,codes:items.filter(s=>s.code).length,uniqueImages:hashes.size,review}));
