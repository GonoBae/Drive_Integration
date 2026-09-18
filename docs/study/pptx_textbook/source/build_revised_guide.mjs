import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root='B:/Portfolio/Drive_Integration';
const base=path.join(root,'docs/study/pptx_textbook');
const illustrated=process.argv.includes('--illustrated');
const output=path.join(base,illustrated?'illustrated':'revised');
const build=path.join(root,illustrated?'runtime_tmp/study_illustrated_20260908':'runtime_tmp/study_revised_20260908');
const names=['01_Foundations_Protobuf','02_CPP_Server','03_Unreal_Client'];
const revisions=(process.argv[2]??'r1,r1,r1').split(',');
const esc=s=>String(s??'').replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
const volumes=[];
for(let n=1;n<=3;n++){
 const v=(await import(pathToFileURL(path.join(base,`source/revised${n}.mjs`)))).default;
 const stem=names[n-1]+'_Textbook_'+revisions[n-1];
 const {items}=JSON.parse(await fs.readFile(path.join(build,stem,'expanded.json'),'utf8'));
 volumes.push({v,items,stem});
}
const count=volumes.reduce((n,v)=>n+v.items.length,0);
const all=volumes.flatMap(v=>v.items);
const imageCount=all.filter(s=>s.illustration).length;
const codeCount=all.filter(s=>s.code).length;
const lessons=volumes.reduce((n,{v})=>n+v.lessons.length,0);
const chapterCount=volumes.reduce((n,{v})=>n+v.chapters.length,0);
const toc=volumes.map(({v,items,stem})=>`<h2>${v.id}권 ${esc(v.title)}</h2><p>${v.chapters.length}개 장, ${v.lessons.length}개 단원, ${items.length}쪽 · <a href="${stem}.pptx">PPT</a> · <a href="${stem}.html">본문과 예제 HTML</a></p>${v.chapters.map((c,i)=>`<h3>${i+1}장 ${esc(c.title)}</h3><p>${esc(c.purpose)}</p><ol>${v.lessons.filter(l=>l.chapter===c.id).map(l=>`<li><a href="${stem}.html#${l.id}">${l.id} ${esc(l.title)}</a><br><span class="muted">${esc(l.goal)}</span></li>`).join('')}</ol>`).join('')}`).join('');
const lease=volumes[0];const client=volumes[2];
const leaseStart=lease.items.findIndex(s=>s.id==='L12')+1;
const leaseEnd=lease.items.findIndex(s=>s.id==='L12-NEXT')+1;
const clientStart=client.items.findIndex(s=>s.id==='L08')+1;
const clientEnd=client.items.findIndex(s=>s.id==='L08-NEXT')+1;
const style=`*{box-sizing:border-box}body{margin:0;background:#f2f5f4;color:#173648;font:18px/1.95 "Malgun Gothic","Segoe UI",sans-serif;word-break:keep-all;overflow-wrap:anywhere}main{max-width:1100px;margin:30px auto;padding:46px 58px;background:white;border:1px solid #cfdddf}h1{font-size:36px;line-height:1.45}h2{font-size:27px;margin-top:44px}h3{font-size:22px;margin:27px 0 10px;color:#147d82}a{color:#126f78;text-underline-offset:4px}p{margin:15px 0}li{margin:10px 0}.muted,small{color:#546d78;font-size:15px}.table{overflow-x:auto}table{width:100%;border-collapse:collapse;font-size:16px}td,th{border:1px solid #cedcde;padding:12px;text-align:left;vertical-align:top}th{background:#eff6f5}.intro{padding-left:20px;border-left:3px solid #147d82} @media(max-width:720px){main{margin:12px;padding:25px}body{font-size:17px}h1{font-size:29px}table{font-size:14px}} @media print{@page{size:A4;margin:15mm}body{background:white;font-size:11pt}main{padding:0;margin:0;border:0}h1{font-size:25pt}h2{font-size:19pt}h3{font-size:15pt}tr{break-inside:avoid}}`;
const html=`<!doctype html><html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Drive Integration 단원형 교재 안내</title><style>${style}</style></head><body><main><small>2026-09-08 코드 기준 · Windows · Unreal Engine 5.6 · 단원형 개정판</small><h1>Drive Integration 독학 교재</h1><p class="intro">${chapterCount}개 장과 ${lessons}개 단원으로 구성한 ${count}쪽 교재입니다. 현재 프로젝트의 실제 코드 ${codeCount}개와 원리 그림 ${imageCount}개를 사용합니다. 각 단원은 왜 이 기능을 쓰는지부터 설명하고, 개념·예시·소스코드·값 추적·확인 문제로 이어집니다.</p><p>핵심 해설은 모두 본문에서 읽습니다. PPT의 노트를 열어야만 수업이 완성되는 구성은 아닙니다. 같은 내용을 HTML에서는 장·단원 목차와 함께 스크롤하며 읽을 수 있습니다. 그림·발췌 코드는 파일에 들어 있으며 공식 자료 링크만 추가 확인용입니다.</p><h2>처음 열 파일</h2><div class="table"><table><thead><tr><th>교재</th><th>PPT</th><th>정독용 HTML</th></tr></thead><tbody>${volumes.map(({v,items,stem})=>`<tr><td>${v.id}권 ${esc(v.title)}</td><td><a href="${stem}.pptx">${items.length}쪽</a></td><td><a href="${stem}.html">목차부터 읽기</a></td></tr>`).join('')}</tbody></table></div><p>압축 파일 전체를 푼 뒤 이 안내를 여세요. 코드를 직접 실행하거나 서버를 변경하지 않아도 본문과 손계산 문제를 학습할 수 있습니다.</p><h2>앞 내용이 다음 단계에 필요한 이유</h2><ol><li><strong>1권:</strong> 함수 입력과 값의 수명을 읽고, 프로세스 사이에서 왜 바이트 계약이 필요한지 배웁니다. Protobuf를 읽은 뒤 메시지 해석 성공과 현재 제어권 수락을 구분합니다.</li><li><strong>3권:</strong> 1권의 계약을 실제 입력과 화면에 연결합니다. 객체·입력·수신 검증을 먼저 배운 다음 수신 공백과 좌표 변환, 바퀴·HUD·소리로 확장합니다. Unreal 클라이언트 학습을 우선할 때의 순서입니다.</li><li><strong>2권:</strong> 이미 구분한 명령과 상태 사이에 어떤 계산이 들어가는지 읽습니다. 시간과 제어권을 먼저 정하고 지면 질의, 접촉과 힘, 충돌, NPC, 재실행 검증으로 이어갑니다.</li></ol><p>서버 계산이 더 궁금하면 1권 이후 2권을 먼저 읽어도 됩니다. 각 장 첫 페이지의 선수학습과 완료 기준을 확인하고, 아직 배우지 않은 계산을 안다고 가정하지 마세요.</p><h2>지적하신 시간 기준을 배우는 위치</h2><h3>ControlLease</h3><p><a href="${lease.stem}.html#L12">1권 L12, ${leaseStart}~${leaseEnd}쪽</a>에서 다룹니다. 마지막 스로틀이 계속 남으면 생기는 문제를 시작으로 제어권의 목적을 배웁니다. 두 시계의 간격으로 지연을 추정하는 계산, 100ms 거부 한도, 250ms 안전 정지, 1000ms 연결 폐기 코드와 회복 조건을 각각 따라갑니다. 숫자는 기본 설정과 교육용 시각 예제를 구분합니다.</p><h3>Unreal의 신선도와 예측 한도</h3><p><a href="${client.stem}.html#L08">3권 L08, ${clientStart}~${clientEnd}쪽</a>에서 다룹니다. 화면 프레임·서버 틱·수신 시각을 먼저 배운 뒤, 새 상태가 잠시 없을 때 어디까지 표시 위치를 예측할지 묻습니다. 예측 한도와 상태 나이 판정, 반대 방향 명령에 적용하는 서버 타임아웃을 서로 다른 목적과 코드로 연결합니다.</p><p>이 두 단원만 먼저 살펴보며 개정된 설명 방식을 확인할 수 있습니다. 처음 공부할 때는 각 단원에 적힌 선수학습부터 진행하는 편이 좋습니다.</p><h2>하루 학습 계획</h2><p>이 전체 교재를 하루에 완독하는 일정은 아닙니다. 첫날은 뒤의 코드를 혼자 읽을 기반을 만들고, 맞힌 문제보다 설명할 수 있는 범위를 남깁니다. 시간이 부족하면 같은 순서로 다음 날 이어가세요.</p><div class="table"><table><thead><tr><th>시간</th><th>범위</th><th>직접 남길 결과</th></tr></thead><tbody><tr><td>09:00~10:00</td><td>1권 L01~L02</td><td>W 입력 왕복과 main 콜백의 등록·실행 구분</td></tr><tr><td>10:00~10:15</td><td>휴식</td><td>화면에서 눈을 떼세요</td></tr><tr><td>10:15~12:00</td><td>1권 L03~L04</td><td>복사·참조·값 없음·수명을 자신의 말로 설명</td></tr><tr><td>12:00~13:00</td><td>점심</td><td>휴식</td></tr><tr><td>13:00~14:30</td><td>1권 L05~L06</td><td>TCP·WebSocket·Protobuf의 목적과 단위 계약</td></tr><tr><td>14:30~14:45</td><td>휴식</td><td>휴식</td></tr><tr><td>14:45~16:00</td><td>1권 L07</td><td>태그와 varint를 직접 계산한 메모</td></tr><tr><td>16:00~16:15</td><td>휴식</td><td>휴식</td></tr><tr><td>16:15~17:10</td><td>오늘 범위 확인 문제 재풀이</td><td>틀린 이유와 해당 코드 줄을 연결</td></tr><tr><td>17:10~18:00</td><td>입력 왕복 3분 설명</td><td>실제 파일 이름, 아는 부분, 아직 모르는 경계</td></tr></tbody></table></div><p>다음 학습은 1권 L08~L13입니다. 바이트 구조와 실행 정체성 검사를 익힌 뒤 ControlLease를 읽으면 시간 기준이 갑자기 등장하지 않습니다. 이후 3권을 장 순서대로 진행합니다.</p><h2>한 단원을 완료했다고 보는 기준</h2><ol><li>어떤 사용자 문제를 해결하기 위해 이 기능이 필요한지 설명할 수 있습니다.</li><li>주요 변수의 뜻과 단위, 함수가 받는 값과 내보내는 결과를 구분합니다.</li><li>예제 숫자를 바꾸어도 어느 조건이 달라지는지 직접 계산합니다.</li><li>잘못된 값·늦은 값·값 없음 중 해당 단원의 실패 조건을 설명합니다.</li><li>다음 단원에서 방금 배운 개념을 어디에 사용하는지 말할 수 있습니다.</li></ol><h2>전체 장별 목차</h2>${toc}<h2>자료의 기준</h2><p>그림은 원리 설명용이며 현재 화면·실차 설계·성능 측정 결과를 대신하지 않습니다. 교육용 숫자는 가정을 표시했습니다. 실차 타이어 모델, 사람처럼 영상을 보는 NPC, 완전한 센서 구현 등 현재 범위를 넘어서는 기능으로 해석하지 마세요.</p><p><a href="TOPIC_MAP.html">이전 주제와 새 단원 대응표</a> · <a href="ILLUSTRATIONS.md">그림과 제작 프롬프트</a></p></main></body></html>`;
await fs.writeFile(path.join(output,'START_HERE.html'),illustrated?html.replace(
 '<h2>처음 열 파일</h2>',
 '<p>41개 단원 모두에 설명 그림이 있습니다. 그림 옆의 한국어 읽기 순서와 함께 확인하세요. <a href="VISUAL_INDEX.html">단원별 그림 목차</a></p><h2>처음 열 파일</h2>'):html);
const coverage=[];
for(let n=1;n<=3;n++){
 const old=(await import(pathToFileURL(path.join(base,`source/volume${n}.mjs`)))).default;
 const {v,stem,items}=volumes[n-1];
 coverage.push(`<h2>${n}권 주제 대응</h2><div class="table"><table><thead><tr><th>이전 학습 ID</th><th>이전 주제</th><th>새 단원</th></tr></thead><tbody>${old.slides.map(s=>{const l=v.lessons.find(l=>l.covers.includes(s.id));if(!l)throw Error('Uncovered '+s.id);return `<tr><td>${s.id}</td><td>${esc(s.title)}</td><td><a href="${stem}.html#${l.id}">${l.id} ${esc(l.title)}</a><br>${items.findIndex(i=>i.id===l.id)+1}쪽부터</td></tr>`;}).join('')}</tbody></table></div>`);
}
await fs.writeFile(path.join(output,'TOPIC_MAP.html'),`<!doctype html><html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>이전 주제와 새 단원</title><style>${style}</style></head><body><main><a href="START_HERE.html">전체 학습 안내</a><h1>이전 주제와 새 단원 대응표</h1><p>이전 교재의 개별 주제를 어떤 문제 중심 단원에서 다시 배우는지 찾는 표입니다. 정독할 때는 해당 단원의 문제 상황과 목적부터 읽고 코드·예시로 진행하세요.</p>${coverage.join('')}</main></body></html>`);
if(illustrated){
 const index=volumes.map(({v,items,stem})=>`<h2>${v.id}권 ${esc(v.title)}</h2>${v.lessons.map(l=>{
  const pictures=items.filter(s=>s.lessonId===l.id&&s.illustration);
  if(!pictures.length)throw Error('Missing illustration '+v.id+'/'+l.id);
  return `<h3>${l.id} ${esc(l.title)}</h3><p>${esc(l.why)}</p><ul>${pictures.map(s=>`<li><a href="${stem}.html#${s.id}">${esc(s.title)}</a> (${items.indexOf(s)+1}쪽)</li>`).join('')}</ul>`;
 }).join('')}`).join('');
 await fs.writeFile(path.join(output,'VISUAL_INDEX.html'),`<!doctype html><html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>단원별 설명 그림</title><style>${style}</style></head><body><main><a href="START_HERE.html">전체 학습 안내</a><h1>단원별 설명 그림</h1><p>41개 단원의 그림을 바로 찾아볼 수 있습니다. 그림 옆의 읽기 순서를 확인한 다음 해당 단원의 코드와 예제로 이어가세요. 단원 제목 아래에는 이 내용을 배우는 이유를 적었습니다.</p>${index}</main></body></html>`);
 const manifests=[JSON.parse(await fs.readFile(path.join(base,'source/image_manifest.json'),'utf8')),
  JSON.parse(await fs.readFile(path.join(base,'source/revised_images.json'),'utf8'))];
 for(let n=1;n<=3;n++)manifests.push(Object.fromEntries(JSON.parse(await fs.readFile(
  path.join(base,`source/illustrated_v${n}.json`),'utf8')).map(e=>[e.imageKey,e])));
 const manifest=Object.assign({},...manifests);
 const pictureSources=['# 설명 그림과 제작 기준','','그림은 내장 이미지 생성 도구로 만든 교육용 개념도입니다. 실제 화면이나 실차 측정 자료가 아닙니다. 본문·표·코드는 편집할 수 있고 그림은 PNG입니다. 그림 원본은 저장소의 `docs/study/pptx_textbook/assets`에 있으며 PPT와 HTML에도 포함되어 있습니다.',''];
 for(const {v,items} of volumes)for(const item of items.filter(s=>s.illustration)){
  const entry=manifest[item.illustration];
  pictureSources.push(`## ${v.id}권 ${item.lessonId} ${item.title}`,'',`원본: ${entry.path}`,'',
   ...item.visualReading.map((line,i)=>`${i+1}. ${line}`),'','제작 프롬프트:','',entry.prompt,'');
 }
 await fs.writeFile(path.join(output,'ILLUSTRATIONS.md'),pictureSources.join('\n'));
}
await fs.writeFile(path.join(build,'delivery-summary.json'),JSON.stringify({count,chapterCount,lessons,codeCount,imageCount,volumes:volumes.map(({v,stem,items})=>({id:v.id,title:v.title,stem,pages:items.length,lessons:v.lessons.length})),leaseStart,leaseEnd,clientStart,clientEnd},null,2));
console.log(JSON.stringify({count,chapterCount,lessons,codeCount,imageCount,leaseStart,leaseEnd,clientStart,clientEnd}));
