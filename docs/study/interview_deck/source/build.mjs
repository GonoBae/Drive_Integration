import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import {pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';
import project from './project.mjs';

const ROOT='B:/Portfolio/Drive_Integration';
const BASE=path.join(ROOT,'docs/study/interview_deck');
const BOOK=path.join(ROOT,'docs/study/pptx_textbook');
const SKILL='C:/Users/PC/.codex/plugins/cache/openai-primary-runtime/presentations/26.905.11957/skills/presentations';
const MODULES='C:/Users/PC/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const PYTHON='C:/Users/PC/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
const revision=process.argv.find(a=>a.startsWith('--revision='))?.split('=')[1]??'v1';
const preview=process.argv.includes('--preview');
const preflight=process.argv.includes('--preflight');
const build=path.join(ROOT,'runtime_tmp/interview_deck_20260908',revision);
const stem='DriveIntegration_Interview_Visual_'+revision;
const output=path.join(BASE,'output',stem+'.pptx');
process.env.RUNTIME_NODE_MODULES=MODULES;
process.env.RUNTIME_NODE=process.execPath;
process.env.RUNTIME_PYTHON=PYTHON;
const require=createRequire(path.join(ROOT,'runtime_tmp/interview_deck_20260908/probe.mjs'));
const {Presentation,PresentationFile}=await import(pathToFileURL(require.resolve('@oai/artifact-tool')).href);
const {createCanvas,GlobalFonts}=require('@napi-rs/canvas');
GlobalFonts.registerFromPath('C:/Windows/Fonts/malgun.ttf','Malgun Gothic');
GlobalFonts.registerFromPath('C:/Windows/Fonts/malgunbd.ttf','Malgun Gothic');
GlobalFonts.registerFromPath('C:/Windows/Fonts/consola.ttf','Consolas');
const measureContext=createCanvas(1,1).getContext('2d'); // Text measurement only.
const {finalizePresentation}=await import(pathToFileURL(path.join(SKILL,'container_tools/artifact_tool_utils.mjs')).href);
const FONT='Malgun Gothic', CODE='Consolas';
const C={ink:'#15374B',teal:'#087E87',muted:'#58717C',orange:'#A95718'};
const manifest={};
for(const name of ['image_manifest.json','revised_images.json']){
 const entries=JSON.parse(await fs.readFile(path.join(BOOK,'source',name),'utf8'));
 for(const [key,value] of Object.entries(entries))manifest[key]={...value,absolute:path.join(BOOK,value.path)};
}
for(let number=1;number<=3;number++){
 const entries=JSON.parse(await fs.readFile(path.join(BOOK,`source/illustrated_v${number}.json`),'utf8'));
 for(const value of entries)manifest[value.imageKey]={...value,absolute:path.join(BOOK,value.path)};
}
for(const [key,value] of Object.entries(JSON.parse(await fs.readFile(path.join(BASE,'source/new_images.json'),'utf8')))){
 manifest[key]={...value,absolute:path.join(BASE,value.path)};
}
manifest.curb_actual={absolute:path.join(BASE,'assets/curb_report_actual.png'),path:'assets/curb_report_actual.png',kind:'사용자가 제공한 실제 오류 제보 화면',prompt:'생성하지 않음. 사용자 제공 PNG를 편집 없이 복사했습니다.'};
const readJson=async name=>JSON.parse(await fs.readFile(path.join(BASE,'source',name),'utf8'));
let items;
if(preview){
 items=[...project.filter(s=>s.id.startsWith('I')), ...(await readJson('foundations.json')).slice(0,4), ...project.filter(s=>['S02','D04'].includes(s.id))];
}else{
 items=[...project.filter(s=>s.id.startsWith('I')), ...await readJson('foundations.json'), ...await readJson('unreal.json'),
  ...project.filter(s=>s.id.startsWith('S')), ...project.filter(s=>s.id.startsWith('D')), ...project.filter(s=>s.id.startsWith('C'))];
 if(items.length!==50)throw Error(`Expected50slides, got${items.length}`);
}
const imageHashes=new Set();
if(preflight){
 const ready=[];
 for(const item of items){try{await fs.access(manifest[item.imageKey].absolute);ready.push(item);}catch{console.log('Waiting image '+item.id+' '+item.imageKey);}}
 items=ready;
}
for(const item of items){
 const asset=manifest[item.imageKey];
 if(!asset)throw Error('Missing manifest '+item.imageKey);
 const bytes=await fs.readFile(asset.absolute);
 asset.data=bytes;
 const hash=crypto.createHash('sha256').update(bytes).digest('hex');
 if(imageHashes.has(hash))throw Error('Repeated image '+item.id);
 imageHashes.add(hash);
 if(item.codeRef){
  const ref=item.codeRef;
  const text=(await fs.readFile(path.join(ROOT,ref.path),'utf8')).replaceAll('\r\n','\n').split('\n').slice(ref.start-1,ref.end).join('\n');
  item.code={...ref,text};
 }
 if(item.code){
  const actual=(await fs.readFile(path.join(ROOT,item.code.path),'utf8')).replaceAll('\r\n','\n').split('\n').slice(item.code.start-1,item.code.end).join('\n');
  if(actual!==item.code.text)throw Error('Excerpt differs from current source '+item.id);
 }
}
await fs.mkdir(path.join(build,'rendered'),{recursive:true});
const deck=Presentation.create({slideSize:{width:1600,height:900}});
const review=[];
function measure(value,size,family=FONT,bold=false){measureContext.font=`${bold?'bold ':''}${size}px "${family}"`;return measureContext.measureText(value).width;}
function wrap(value,width,size,family=FONT,bold=false){
 return String(value).split('\n').flatMap(para=>{
  const result=[];let current='';
  for(const token of para.split(/(\s+)/)){
   if(measure(current+token,size,family,bold)<=width){current+=token;continue;}
   if(current.trim()){result.push(current.trimEnd());current='';}
   if(measure(token,size,family,bold)<=width){current=token.trimStart();continue;}
   for(const ch of token){if(measure(current+ch,size,family,bold)>width){result.push(current);current='';}current+=ch;}
  }
  if(current||!result.length)result.push(current.trimEnd());return result;
 }).join('\n');
}
function text(slide,name,value,x,y,w,h,size,color=C.ink,bold=false,family=FONT){
 const shape=slide.shapes.add({name,geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});
 shape.text=value;shape.text.style={typeface:family,fontSize:size,color,bold,autoFit:'none',wrap:'none',insets:{left:0,right:0,top:0,bottom:0}};
 return shape;
}
function paragraph(slide,name,value,x,y,w,size=26,color=C.ink,bold=false,family=FONT){
 const rendered=wrap(value,w,size,family,bold);
 const height=rendered.split('\n').length*size*1.32+5;
 text(slide,name,rendered,x,y,w,height,size,color,bold,family);
 return height;
}
function dedent(value){
 const lines=value.replaceAll('\t','    ').split('\n');
 const indent=Math.min(...lines.filter(s=>s.trim()).map(s=>s.match(/^\s*/)[0].length));
 return lines.map(s=>s.slice(indent)).join('\n');
}
function codeLines(value,width,size){
 return dedent(value).split('\n').flatMap(line=>{
  if(measure(line,size,CODE)<=width)return [line];
  const split=[];let current='';
  for(const token of line.split(/(\s+|(?<=[,;({])|(?<=\.)(?=[A-Za-z_]))/)){
   if(measure(current+token,size,CODE)>width&&current.trim()){
    split.push(current.trimEnd());current='  ';
   }
   current+=token;
  }
  split.push(current);return split;
 }).join('\n');
}
function sourceLabel(item){
 const source=item.code??item.sources?.find(s=>s.path);
 const original=(item.textbooks??[]).map(s=>`${s.volume}권 ${s.lesson}`).join(', ');
 return `${original}${source?'   '+path.basename(source.path)+(source.start?`:${source.start}${source.end!==source.start?'-'+source.end:''}`:''):''}`;
}
function addNotes(slide,item){
 const sources=(item.sources??[]).map(s=>s.url?`${s.label??'공식 문서'} ${s.url}`:`${s.path}${s.start?':'+s.start+(s.end?'-'+s.end:''):''}${s.symbol?' '+s.symbol:''}`).join('\n');
 let notes=`질문\n${item.question??''}\n\n사용 이유\n${item.why??''}\n\n답변\n${item.answer??''}\n\n보충 설명\n${item.notes??''}\n\n꼬리 질문\n${item.followup??''}\n\n그림 읽기\n${(item.reading??[]).join('\n')}\n\n출처\n${sources}`;
 if(item.code)notes+=`\n\n실제 코드 원문\n${item.code.path}:${item.code.start}-${item.code.end}\n${item.code.text}\n${item.code.explain}`;
 notes+=`\n\n원교재\n${(item.textbooks??[]).map(b=>`${b.volume}권 ${b.lesson}`).join(', ')}\n\n그림\n${manifest[item.imageKey].kind??'교육용 개념도'}\n${manifest[item.imageKey].path}\n`;
 if(item.imageKey!=='curb_actual')notes+='개념 그림이며 실제 프로젝트 화면·실차 측정 그래프가 아닙니다.\n';
 slide.speakerNotes.textFrame.setText(notes);
}
for(const [index,item]of items.entries()){
 const slide=deck.slides.add();slide.background.fill='#FFFFFF';
 const asset=manifest[item.imageKey];
 const img=(x,y,w,h,fit='contain')=>slide.images.add({name:item.imageKey,blob:asset.data,contentType:'image/png',position:{left:x,top:y,width:w,height:h},fit,alt:item.title+' 설명 그림'});
 if(item.type==='cover'){
  img(0,0,1600,900,'cover');
  paragraph(slide,'title',item.title,64,96,680,68,C.ink,true);
  paragraph(slide,'subtitle',item.subtitle,69,322,670,30);
  paragraph(slide,'answer',item.answer,69,737,1410,29);
  text(slide,'cover-disclosure','교육용 표지 일러스트',70,856,720,28,19,C.muted);
 }else{
  const title=wrap(item.title,1410,49,FONT,true);
  if(title.includes('\n'))review.push({id:item.id,issue:'two-line-title'});
  text(slide,'title',title,56,38,1430,76,49,C.ink,true);
  text(slide,'page',String(index+1).padStart(2,'0')+' / '+items.length,1470,48,100,38,22,C.muted);
  const questionHeight=paragraph(slide,'question',item.question,58,117,1480,29,C.teal);
  if(questionHeight>67)review.push({id:item.id,issue:'question-too-long'});
  const rightX=item.code?1010:1120, rightW=item.code?535:425;
  img(52,186,item.code?915:1015,505);
  let cursor=187;
  text(slide,'why-label','왜 필요한가',rightX,cursor,rightW,37,23,C.teal,true);cursor+=43;
  cursor+=paragraph(slide,'why',item.why,rightX,cursor,rightW,26)+22;
  if(item.code){
   text(slide,'code-label','실제 코드',rightX,cursor,rightW,35,23,C.teal,true);cursor+=41;
   const shown=codeLines(item.code.text,rightW,23);
   for(const line of shown.split('\n'))if(measure(line,23,CODE)>rightW)review.push({id:item.id,issue:'code-line-too-wide',line});
   const height=shown.split('\n').length*23*1.26+8;
   text(slide,'code',shown,rightX,cursor,rightW,height,23,C.ink,false,CODE);cursor+=height+18;
   cursor+=paragraph(slide,'code-explanation',item.code.explain,rightX,cursor,rightW,24);
  }else{
   for(const [n,line]of (item.reading??[]).entries()){
    cursor+=paragraph(slide,'reading-'+n,line,rightX,cursor,rightW,26)+24;
   }
  }
  if(cursor>708)review.push({id:item.id,issue:'right-column-overflow',bottom:cursor});
  text(slide,'source',sourceLabel(item),57,704,1080,27,17,C.muted);
  const prefix=item.imageKey==='curb_actual'?'사용자 제공 오류 화면':'원리 설명 그림';
  text(slide,'image-disclosure',prefix,1120,705,425,27,17,C.muted);
  const answerHeight=paragraph(slide,'answer','답변  '+item.answer,58,742,1482,25);
  if(742+answerHeight>855)review.push({id:item.id,issue:'answer-overflow',bottom:742+answerHeight});
  const question=wrap('꼬리 질문  '+item.followup,1480,22);
  if(question.includes('\n'))review.push({id:item.id,issue:'followup-overflow'});
  text(slide,'followup',question,58,865,1482,33,22,C.orange);
 }
 addNotes(slide,item);
}
await fs.writeFile(path.join(build,'expanded.json'),JSON.stringify({items,images:items.map(s=>({id:s.id,key:s.imageKey,path:manifest[s.imageKey].path}))},null,2));
await fs.writeFile(path.join(build,'fit-review.json'),JSON.stringify(review,null,2));
await(await PresentationFile.exportPptx(deck)).save(path.join(build,'candidate.pptx'));
if(!review.length&&!preview&&!preflight){
 await finalizePresentation({workspaceDir:ROOT,candidatePath:path.join(build,'candidate.pptx'),finalPath:output,
  pythonExecutable:PYTHON,integrityValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_package_integrity.py'),
  layoutValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_layout_geometry.py'),
  layoutArgs:['--expected-slide-size-emu','15240000,8572500','--validate-bullet-geometry','--validate-heading-fit'],
  requiredNativeTableOwnerSlides:[],fontPolicy:{basis:'design',families:[FONT,CODE],scriptFonts:{ea:FONT}},
  verifyArtifactToolImport:true,receiptPath:path.join(build,'validation.json')});
 console.log(JSON.stringify({status:'finalized',path:output,slides:items.length}));
}
for(let i=0;i<deck.slides.items.length;i++){
 const slide=deck.slides.items[i], number=String(i+1).padStart(3,'0');
 const png=await deck.export({slide,format:'png',scale:1});
 await fs.writeFile(path.join(build,'rendered',number+'.png'),new Uint8Array(await png.arrayBuffer()));
 await fs.writeFile(path.join(build,'rendered',number+'.layout.json'),await(await slide.export({format:'layout'})).text());
}
console.log(JSON.stringify({status:review.length?'fit-review':'complete',slides:items.length,codes:items.filter(s=>s.code).length,uniqueImages:imageHashes.size,review}));
