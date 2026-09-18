import fs from 'node:fs/promises';
import path from 'node:path';

// Keep the previously delivered explanations and code excerpts intact.
// This revision inserts diagrams, adds reading cues, and recalculates the TOC.
const sourceStems = ['01_Foundations_Protobuf_Textbook_r2',
  '02_CPP_Server_Textbook_r3', '03_Unreal_Client_Textbook_r2'];

export const existingReading = {
  architecture: ['위쪽 명령은 가속·조향 요청입니다. 새 위치를 직접 지정하지 않습니다.',
    '오른쪽 서버가 입력과 지면 조건으로 차량 상태를 계산합니다.',
    '아래쪽 상태가 돌아오면 Unreal이 차체와 화면을 갱신합니다.'],
  cpp_memory: ['Original과 Copy는 별도 저장 공간입니다. 값은 같을 수 있습니다.',
    'Reference는 기존 값을 다른 이름으로 읽습니다. 새 소유자가 생기지 않습니다.',
    'Lifetime의 원본이 사라지면 빌린 참조나 뷰도 계속 사용할 수 없습니다.'],
  packet_layer: ['왼쪽은 값을 직렬화한 Protobuf 바이트입니다. 스키마 소스가 아닙니다.',
    '가운데 WebSocket은 바이트에 메시지 경계를 제공합니다.',
    '오른쪽 TCP는 순서 있는 바이트 흐름을 전달합니다. 각 계층의 일이 다릅니다.'],
  protobuf: ['공통 스키마는 필드 번호와 자료형을 정하는 약속입니다.',
    '송신 측은 실제 값을 바이트로 쓰고, 수신 측은 같은 약속으로 읽습니다.',
    '해석 성공만으로 조작을 허용하지 않습니다. 지도·Play·제어권도 검사합니다.'],
  session: ['연결은 메시지가 오가는 통로이고, Play는 한 번의 게임 실행입니다.',
    '같은 Play에서 통로만 바뀌면 기존 차량 상태를 보존할 수 있습니다.',
    '새 Play에는 이전 실행의 상태가 섞이지 않도록 별도 초기화가 필요합니다.'],
  lease_time: ['Client stamp는 생성 시각, Server arrival은 수신 시각입니다.',
    '서로 다른 시계의 절대값 대신 각각의 변화량을 비교합니다.',
    'Silence duration은 정상 입력의 공백입니다. 안전 정지와 연결 폐기에 사용합니다.'],
  ground_query: ['아래로 향한 Ray는 지면을 찾는 질의입니다. 차량을 누르는 힘이 아닙니다.',
    '주황색 Ground hit는 도로 또는 연석에서 찾은 실제 접점입니다.',
    '위쪽 Normal은 표면 방향입니다. 접점과 함께 바퀴 지지 계산에 사용합니다.'],
  suspension: ['바퀴마다 지면 높이와 접촉 여부가 다를 수 있습니다.',
    '스프링은 눌린 정도, 댐퍼는 눌리는 속도에 반응합니다.',
    '각 바퀴의 지지력을 합치면 차체의 위아래 운동과 기울기가 달라집니다.'],
  tire_force: ['타이어 접촉부가 종방향 힘 Fx와 횡방향 힘 Fy를 전달합니다.',
    '오른쪽 원은 두 힘이 함께 사용하는 접지 한도를 설명하는 개념도입니다.',
    '가속에 힘을 많이 쓰면 회전에 쓸 여유가 줄어듭니다. 조향각 자체와 구분합니다.'],
  collision: ['접촉점의 위치와 힘의 방향을 먼저 확인합니다.',
    '무게중심을 향한 충격과 중심에서 벗어난 충격의 회전 효과는 다릅니다.',
    '밀림은 선운동, 돌아가는 정도는 접촉점의 지렛팔과 관성까지 사용합니다.'],
  npc_space: ['가는 경로선만 비어 있어도 트럭 전체가 지나간다는 뜻은 아닙니다.',
    'Clear corridor는 차체 폭과 회전 중 모서리가 차지하는 공간까지 비어 있습니다.',
    'Blocked corridor는 선이 통과해도 차체가 장애물과 겹쳐서 사용할 수 없습니다.'],
  npc_bypass: ['바로 꺾을 공간이 부족하면 먼저 뒤쪽 통과 공간을 확인합니다.',
    '후진은 조향 곡선에 필요한 여유를 만드는 과정입니다.',
    '앞으로 진행하는 우회 경로도 차체 전체의 충돌과 회전 반경을 검사합니다.'],
  server_tick: ['위쪽의 긴 탐색은 다른 입력 처리와 상태 발행을 늦출 수 있습니다.',
    '아래쪽은 후보 검사를 여러 틱에 나누어 다음 입력도 처리하는 방식입니다.',
    '계산을 나눠도 차량마다 탐색 기회가 돌아가도록 순서를 관리해야 합니다.'],
  input_trip: ['W는 운전 의도입니다. 클라이언트가 보낸 명령에 담깁니다.',
    '서버는 명령을 검사하고 그 틱의 차량 운동을 계산합니다.',
    '화면은 수신한 결과 상태를 표시합니다. 송신 완료와 이동 완료는 다릅니다.'],
  actor_components: ['Pawn은 입력과 차량의 대표 위치를 연결하는 소유 객체입니다.',
    '차체·바퀴·카메라 부품은 자신이 맡은 표시와 동작을 처리합니다.',
    '위치가 있는 SceneComponent는 부모 기준 변환으로 함께 움직입니다.'],
  client_gap: ['위쪽은 상태의 도착 시간, 아래쪽은 차량 표시 위치입니다.',
    '수신이 끊겨도 상태 나이는 계속 늘지만 위치 예측에는 한도가 있습니다.',
    '예측을 계속하면 서버에서 멈춘 차도 화면에서 전진할 수 있습니다.'],
  coordinates: ['서버의 동·북·위 방향과 Unreal의 축 대응을 먼저 확인합니다.',
    '같은 지점도 미터를 센티미터로 바꾸면 숫자가 100배가 됩니다.',
    '위치 변환과 회전 부호는 별도로 검사합니다. 그림 방향만 보고 부호를 추측하지 않습니다.'],
  component_space: ['World 축은 고정되어 있고 Local 축은 차체와 함께 돌아갑니다.',
    '그림은 고정 부착 예시입니다. 실제 Orbit 카메라는 회전 상속을 끕니다.',
    '서버의 월드 접점을 바퀴 부품에 넣기 전에 차체 상대 좌표로 변환합니다.'],
  audio: ['입력은 차량 속도·회전·슬립 등의 상태입니다. 소리의 완성 파형이 아닙니다.',
    '서로 다른 소리 성분을 합성하고 상태에 맞게 음량과 변화를 조절합니다.',
    'Play 종료·오래된 상태·경적 해제에 맞춰 해당 소리를 정리합니다.'],
};

export async function loadIllustratedVolume(root, number, allowIncomplete = false) {
  const base = path.join(root, 'docs/study/pptx_textbook');
  const frozen = JSON.parse(await fs.readFile(path.join(root,
    'runtime_tmp/study_revised_20260908', sourceStems[number - 1], 'expanded.json'), 'utf8'));
  let additions;
  try { additions = JSON.parse(await fs.readFile(path.join(base, `source/illustrated_v${number}.json`), 'utf8')); }
  catch (error) { if (!allowIncomplete || error.code !== 'ENOENT') throw error; additions = []; }
  const manifest = Object.fromEntries(additions.map(entry => [entry.imageKey, entry]));
  const items = [];
  const inserted = new Set();
  for (const original of frozen.items) {
    const item = structuredClone(original);
    if (item.illustration) {
      item.visualReading = existingReading[item.illustration];
      if (!item.visualReading) throw Error(`Missing image reading: ${item.illustration}`);
    }
    items.push(item);
    for (const entry of additions) {
      const anchor = entry.afterPageId.startsWith(entry.lessonId + '--')
        ? entry.afterPageId : `${entry.lessonId}--${entry.afterPageId}`;
      if (item.id !== anchor) continue;
      if (inserted.has(entry.lessonId)) throw Error(`Duplicate inserted lesson ${entry.lessonId}`);
      inserted.add(entry.lessonId);
      items.push({id:`${entry.lessonId}--VISUAL`,type:'picture',title:entry.title,
        chapterId:item.chapterId,chapter:item.chapter,lessonId:entry.lessonId,
        lessonTitle:item.lessonTitle,illustration:entry.imageKey,imageKey:entry.imageKey,
        body:[],visualReading:entry.reading,notes:entry.reading.join('\n'),sources:entry.sources??[]});
    }
  }
  if (inserted.size !== additions.length) throw Error('Some diagram anchors were not found');
  for (const lesson of frozen.volume.lessons) {
    const pictures = items.filter(item => item.lessonId === lesson.id && item.illustration);
    if (!allowIncomplete && pictures.length < 1) throw Error(`No lesson diagram: ${number}/${lesson.id}`);
  }
  const pageNumber = id => {
    const index = items.findIndex(item => item.id === id);
    if (index < 0) throw Error(`Missing TOC target ${id}`);
    return index + 1;
  };
  const volume = frozen.volume;
  for (const item of items) {
    if (item.tocChapters) item.body = item.tocChapters.map(id => {
      const c = volume.chapters.find(chapter => chapter.id === id);
      return `${volume.chapters.indexOf(c)+1}장 ${c.title} · ${pageNumber(id)}쪽\n${c.purpose}`;
    });
    if (item.tocLessons) item.body = item.tocLessons.map(id => {
      const l = volume.lessons.find(lesson => lesson.id === id);
      return `${id} ${l.title} · ${pageNumber(id)}쪽\n${l.goal}`;
    });
  }
  return {volume, items, manifest, sourceStem:sourceStems[number - 1]};
}
