# 설명 그림과 제작 기준

그림은 내장 이미지 생성 도구로 만든 교육용 개념도입니다. 실제 화면이나 실차 측정 자료가 아닙니다. 본문·표·코드는 편집할 수 있고 그림은 PNG입니다. 그림 원본은 저장소의 `docs/study/pptx_textbook/assets`에 있으며 PPT와 HTML에도 포함되어 있습니다.

## 01권 L01 왕복 그림을 읽는 기준

원본: assets/architecture.png

1. 위쪽 명령은 가속·조향 요청입니다. 새 위치를 직접 지정하지 않습니다.
2. 오른쪽 서버가 입력과 지면 조건으로 차량 상태를 계산합니다.
3. 아래쪽 상태가 돌아오면 Unreal이 차체와 화면을 갱신합니다.

제작 프롬프트:

Use case: scientific-educational. Create one wide landscape textbook illustration, 16:9, on a clean warm-white background, for learning a C++ server and Unreal driving simulation. A monitor showing a simplified blue sedan in a grey virtual road scene at LEFT, a compact computer/server with a visible calculation grid at RIGHT. Two distinct broad curved paths connect them: TOP path points LEFT TO RIGHT labeled exactly 'ControlCommand'; BOTTOM path points RIGHT TO LEFT labeled exactly 'WorldState'. Small keyboard at lower-left. Center label 'WebSocket + Protobuf'. Other labels exactly 'Unreal' and 'C++ Server'. Friendly precise technical illustration, teal and deep navy with orange accents, large readable typography, generous space. No invented measurements, no logos, no code, no tiny labels, no extra text. This is a conceptual explanatory illustration, not a screenshot.

## 01권 L02 콜백 등록과 실행이 이어지는 시간선

원본: assets/illustrated_v3/v1_L02.png

1. 등록은 동작을 저장하고, run 이후 준비된 콜백이 실행됩니다.
2. 입력이 20ms에 준비돼도 80ms의 틱 종료까지 기다립니다.
3. 교육용 시간선이며 길이는 실제 시간 비율·측정값이 아닙니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: Korean C++ networking textbook explanatory image, raster, wide 16:9.
Primary request: show a SINGLE event loop with callback registration followed by a precise timeline that demonstrates why async input can wait behind a long tick callback. White background; crisp flat textbook diagram; navy typography, teal execution blocks, orange waiting; large sparse English labels. No tiny text, no extra decorations, no code screenshot.
Top slim conceptual strip: a box "REGISTER CALLBACK" then arrow to "ioc.run()" then arrow to "RUN READY WORK". Registration is storage of a future action, not immediate execution.
Main timeline occupies most of image. Exactly labeled ticks "0 ms", "20 ms", "80 ms" on a shared horizontal time axis. A single navy lane labeled "ONE EVENT LOOP". Its teal rectangle labeled "TICK CALLBACK" starts at 0 and ends at 80. At 80 the next separate teal rectangle begins labeled "INPUT CALLBACK"; do not overlap these executing rectangles.
At exactly the 20 ms position show an orange input-ready marker above/below and text "INPUT READY". Draw an orange dashed waiting arrow from 20 to 80, parallel to axis, labeled "WAITING". Clearly connect its right endpoint to the beginning of INPUT CALLBACK at 80.
Footer exact small-but-readable label "ILLUSTRATIVE TIMING". Additional concise insight label "ASYNC DOES NOT IMPLY PARALLEL". No second worker thread. Keep 20 located at one quarter of the distance from 0 to 80, all timings precisely aligned. No screenshot, watermark or fabricated measured data.

## 01권 L03 원본, 내부 복사본과 값의 존재

원본: assets/illustrated_v3/v1_L03.png

1. const 참조는 원본을 읽고, 별도 복사본만 1.0으로 제한합니다.
2. error가 출력 문자열을 가리킬 때만 쓰고 nullptr이면 건너뜁니다.
3. 높이 0m는 유효한 값입니다. nullopt에는 결과 자체가 없습니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: textbook raster diagram, wide16:9, white background, flat technical explanatory style, navy/teal/orange palette, large English labels only, no fine print.
Explain three distinct C++ value contracts using a clean three-panel landscape diagram. Minimal visual syntax and strong boundaries.
LEFT panel title "CONST REFERENCE". An original object box "input" contains "throttle 1.2". A label "const &" points to this SAME original object (an alias, not a second stored object). A separate arrow "COPY" leads from original to a clearly distinct stored box "input_" containing "throttle 1.0". Put "CLAMP" above the stored box. Label original "READ ONLY THROUGH THIS REFERENCE". It remains 1.2 while stored copy is clamped to1.0.
MIDDLE panel title "OPTIONAL OUTPUT POINTER". Two simple branches: "error" pointer arrow to an existing output string box, marked "WRITE"; below "nullptr" leads to a stop/bypass symbol labeled "SKIP". No arrow from nullptr to an object. This depicts selecting an output destination.
RIGHT panel title "OPTIONAL RESULT". Two separate result cards: teal "VALUE" containing "height = 0 m" and orange outlined "nullopt" containing "NO RESULT". The zero-height card contains a clear solid ground surface at height0; the no-result card is empty, no ground invented. Under these exact short words "ZERO IS A VALUE".
All three panels must be readable when embedded in a textbook page. Do not equate nullopt with zero, do not mutate original input through const reference, no arbitrary numeric memory addresses, no people or decorative icons, no watermark.

## 01권 L04 값 복사와 빌린 메모리의 비유

원본: assets/cpp_memory-revised.png

1. Original과 Copy는 별도 저장 공간입니다. 값은 같을 수 있습니다.
2. Reference는 기존 값을 다른 이름으로 읽습니다. 새 소유자가 생기지 않습니다.
3. Lifetime의 원본이 사라지면 빌린 참조나 뷰도 계속 사용할 수 없습니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 clean white programming textbook illustration with three small related scenes, left-to-right. LEFT two differently colored document sheets labeled 'Original' and 'Copy', each with its own separate small storage tray. MIDDLE one document in one tray, with TWO paper name tags connected to the SAME document, caption 'Reference'. RIGHT a bookmark pointing to a document that is fading away, caption 'Lifetime'. Show that a copy owns separate data, a reference is another name for existing data, and a borrowed view cannot keep an owner alive. Do not show real memory addresses, code, pointer arithmetic, or claim reference is physically stored as a pointer. Clean teal navy orange editorial illustration, readable short labels, no logos, no decorative panels.

## 01권 L05 통신 계층 그림의 읽는 순서

원본: assets/packet_layer-revised.png

1. 왼쪽은 값을 직렬화한 Protobuf 바이트입니다. 스키마 소스가 아닙니다.
2. 가운데 WebSocket은 바이트에 메시지 경계를 제공합니다.
3. 오른쪽 TCP는 순서 있는 바이트 흐름을 전달합니다. 각 계층의 일이 다릅니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 textbook illustration of logical network layering on white. On the LEFT a small structured document labeled 'Protobuf' goes inside a clearly larger open envelope labeled 'WebSocket'. On the RIGHT the closed envelope is carried by a cable-like path labeled 'TCP'. Use one simple rightward flow; keep Protobuf payload visibly INSIDE WebSocket envelope, TCP as transport rather than an additional payload schema. No UDP, JSON, IP labels, no numeric sizes, no logos. Explain conceptually that schema encoding, message framing, and ordered byte transport have different jobs. Readable teal navy orange technical illustration.
EDIT: Edit the provided network illustration. Keep the three main objects: a document at left, an envelope in the center, a cable at right. Keep only these labels: 'Protobuf bytes', 'WebSocket message', 'TCP stream'. Remove ALL paragraphs, the entire bottom legend box, the slogan, and ALL source code inside both documents. Inside the documents use short rows of tiny square teal/orange blocks with NO letters or numbers, to represent already serialized bytes. Make the document visibly slide into the envelope. The image must NOT suggest .proto schema source text is sent as payload. Big clean simple objects on white, no extra text. Preserve the main teal/navy/orange palette.

## 01권 L06 공통 스키마로 값의 의미를 복원

원본: assets/protobuf.png

1. 공통 스키마는 필드 번호와 자료형을 정하는 약속입니다.
2. 송신 측은 실제 값을 바이트로 쓰고, 수신 측은 같은 약속으로 읽습니다.
3. 해석 성공만으로 조작을 허용하지 않습니다. 지도·Play·제어권도 검사합니다.

제작 프롬프트:

Use case: scientific-educational. Single wide 16:9 conceptual textbook illustration on pure white. Three large stages arranged horizontally with clear rightward arrows: left a structured paper document with readable large label 'Object'; center a precise compact binary packet, label 'Bytes'; right a second structured paper document labeled 'Object'. Above center a blueprint sheet labeled 'Schema' with two guidance lines to the transformation arrows. Under left arrow exact word 'Serialize'; under right arrow exact word 'Parse'. This teaches that a schema describes data and both ends use it, while bytes travel. Tiny text inside the papers must be replaced by simple colored horizontal marks, not invented code or numbers. Elegant teal/navy/orange technical illustration, ample white margins, readable exact English labels, no logos, no extra labels, no source-code text. Concept image not screenshot.

## 01권 L07 비트 조각을 연결하여 150 쓰기

원본: assets/illustrated_v3/v1_L07.png

1. 태그의 위쪽 비트는 필드 2, 아래 세 비트는 저장 방식 0입니다.
2. 주황 계속 비트가 1이면 다음 바이트를 읽고 0이면 끝냅니다.
3. 교육용 조각 10 96 01에서 96 01은 22＋1×128＝150입니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: raster Protobuf textbook explainer, wide16:9, pure white, navy text, teal value bits, orange continuation bits, generous whitespace, large English labels and exact monospace digits.
Main lesson: a sequence value150 in unsigned field2 becomes the educational byte fragment "10 96 01". Build three horizontal stages.
Top stage titled "TAG". A byte bit row "00010 | 000" with the five field bits teal and three wire bits orange. Below the groups respectively "FIELD 2" and "WIRE 0". Beside it show "2 × 8 + 0 = 16" then "0x10".
Middle stage titled "VALUE 150". Show two byte groups in actual encoded order left-to-right. First group digits exactly "1 | 0010110" and second exactly "0 | 0000001". The first bit of each byte is orange and seven value bits teal. Below first "0x96" and "22"; below second "0x01" and "1 × 128". Label above the orange1 "CONTINUE"; above orange0 "STOP". Join the two lower contributions with exact formula "22 + 1 × 128 = 150". Do not reverse group order. Each group must have exactly8 bits.
Bottom stage titled "ENCODED BYTES". Exactly three large adjacent byte cells: "10", "96", "01". Under first a navy brace labeled "TAG"; under last two a teal brace labeled "VALUE". Small footer "HEX BYTES • EDUCATIONAL FRAGMENT".
Must teach exact correct bit pattern for0x96 and0x01. No implication that this is a complete accepted control packet. No extra binary digits, no watermark, no art ornament.

## 01권 L08 안쪽 메시지의 범위와 문자열 바이트 수

원본: assets/illustrated_v3/v1_L08.png

1. 주황 괄호는 내부 태그와 값을 합친 5바이트입니다.
2. 마지막 네 바이트 00 00 00 3F가 float 0.5입니다.
3. U+AC00은 ‘가’이며 3바이트입니다. 그림은 교육용 조각입니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: raster Protobuf textbook figure, wide16:9 white canvas, crisp flat diagram, navy text and outlines, teal data bytes, orange length boundaries. Large monospace hex bytes, minimal English labels; no screenshot.
Primary request: correctly visualize nested length-delimited bytes and UTF-8 byte counting. Main diagram upper two thirds is exactly seven adjacent byte cells left-to-right:
"5A" | "05" | "15" | "00" | "00" | "00" | "3F".
Above first cell "ENVELOPE FIELD 11" and smaller "WIRE 2". Above second cell "LENGTH 5". Under the LAST FIVE cells only (15 00 00 00 3F), one orange enclosing bracket labeled "CONTROL COMMAND • 5 BYTES". The bracket must not include5A or05. Inside this scope below first internal cell15 put "FIELD 2 • WIRE 5". Under the final FOUR cells00 00 00 3F only, a teal bracket labeled "FLOAT 0.5". Add a short explanatory chain "0x3F000000 → 00 00 00 3F" labeled "LOW BYTE FIRST". Those bytes preserve float bits; do not integer-cast0.5.
Lower third separated by fine divider, title "UTF-8 LENGTH = BYTE COUNT". Two small equal examples with text: left "'car'" pointing to three byte cells "63" "61" "72", bottom "3 CHARACTERS • 3 BYTES"; right "U+AC00" pointing to three byte cells "EA" "B0" "80", bottom "1 CHARACTER • 3 BYTES". Each character example gets its own count; do not confuse length with code points.
Footer "EDUCATIONAL FRAGMENTS". All digits exactly verbatim. Never show this fragment as a complete control packet. No fake measured output, no extra payload fields, no watermark.

## 01권 L09 바퀴 위치 식별과 메시지 종류 선택

원본: assets/illustrated_v3/v1_L09.png

1. 21은 목록 필드 번호이고 wheel_index는 원소의 바퀴 위치입니다.
2. 두 번째 예시 원소의 값 3은 뒤오른쪽 바퀴로 연결됩니다.
3. 선택된 종류는 WorldState 하나이며, 그 안에 여러 엔티티를 담습니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: wide16:9 raster textbook diagram. White background, navy outlines/text, teal data and selected paths, orange emphasis, high legibility, sparse English labels. Two panels with no unnecessary decoration.
LEFT two-thirds: title "REPEATED WHEELS". Show a vertical list box "EntityState • wheels = 21" with exactly three visibly separate sample rows: "entry 1: wheel_index = 0", "entry 2: wheel_index = 3", "entry 3: wheel_index = 1". Emphasize second row in orange. Beside list show top-down simplified vehicle rectangle, front facing up, arrow upward labelled "FRONT". Four wheels at corners labeled at front-left "0", front-right "1", rear-left "2", rear-right "3". Important: top-left0, top-right1, bottom-left2, bottom-right3. A bold orange arrow routes from the SECOND sample row directly to the bottom-right wheel3, not wheel1. Include exact bottom labels "21 = FIELD NUMBER" and "wheel_index = POSITION". This is a sample subset/reordered list, not a complete four-wheel observation; add "EXAMPLE ENTRIES". Do not add steering arrows or physical measurements.
RIGHT one-third: title "ONEOF PAYLOAD". A large box "Envelope" contains three alternative inset boxes stacked vertically: "Hello", "ControlCommand", "WorldState". Only WorldState highlighted teal with a clear selection dot; Hello and ControlCommand light grey inactive. Inside the selected WorldState inset show a small bracket and three tiny entity boxes labelled "entities" to demonstrate one selected kind can contain multiple items. Caption below "ONE ACTIVE KIND" and below that "NONE IS ALSO POSSIBLE". Never connect wheel entries to payload selector. This panel is a static choice, not three simultaneously active payloads. No wire bytes, no watermark.

## 01권 L10 필드 부재와 명시적 값, 건너뛰기의 한계

원본: assets/illustrated_v3/v1_L10.png

1. gear 부재는 기본 Drive를 유지하고 명시적 0은 Neutral로 바꿉니다.
2. 모르는 LEN 필드는 길이와 남은 범위를 확인한 뒤 건너뜁니다.
3. 건너뛴 필드의 기능은 여전히 모르며, 파싱 성공 뒤 수락 검사가 남습니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: textbook diagram, raster landscape16:9. Clean white backdrop, navy text and outlines, teal accepted values, orange branch emphasis. Large minimal English labels. Goal: distinguish field presence from0, then structure skip from feature support.
Upper half title "PRESENCE CHANGES THE RESULT". Exactly two clear horizontal rows.
Row1: dashed empty field card "gear ABSENT" → decision text "has_gear = false" → teal result "KEEP DEFAULT DRIVE".
Row2: solid filled field card "gear = 0" → decision text "has_gear = true" → orange result "SET NEUTRAL".
Do not show a shared0 value in row1. Add a small shared left setup label "INITIAL: DRIVE" so both cases start from same internal default.
Lower half title "UNKNOWN FIELD: SKIP ITS STRUCTURE". A horizontal byte-stream schematic has three blocks "KNOWN A", "UNKNOWN LEN", "KNOWN B". The unknown length-delimited block includes three subparts "TAG", "LENGTH", "BYTES". A curved orange forward arrow routes parser position from before the unknown block to immediately after it, labelled "CHECK BOUNDS → SKIP". Navy forward arrows show reading knownA then knownB. Under unknown block exact statement "MEANING STILL UNKNOWN". Below this main line use compact support note "SUPPORTED WIRE TYPES: 0, 1, 2, 5".
Bottom note "PARSING SUCCESS ≠ COMMAND ACCEPTANCE". Diagram is conceptual teaching, not a captured packet. Do not imply all unknown formats are supported or skipped data implements new feature. No decorative art, no watermark.

## 01권 L11 Play와 연결의 서로 다른 생명주기

원본: assets/session.png

1. 연결은 메시지가 오가는 통로이고, Play는 한 번의 게임 실행입니다.
2. 같은 Play에서 통로만 바뀌면 기존 차량 상태를 보존할 수 있습니다.
3. 새 Play에는 이전 실행의 상태가 섞이지 않도록 별도 초기화가 필요합니다.

제작 프롬프트:

Use case: scientific-educational. Landscape 16:9 textbook metaphor illustration on white about two separate lifetimes. Top one long continuous teal road ribbon labeled exactly 'Play session'. Under it three disconnected navy cable segments in a row, labeled exactly 'Connection A', 'Connection B', 'Connection C', all entirely under same long road ribbon. Small unplugged gap between A and B, plugged gap between B and C. At far right, outside the long road ribbon, a new separate orange road ribbon labeled 'New Play' with a blue car returned to start. Convey reconnecting within same Play versus new play reset. Keep symbols conceptual, no timestamps, no fake code or server screenshot, very simple clean labels with whitespace. Minimal teal navy orange classroom illustration.

## 01권 L12 입력 시각과 무입력 기간의 구분

원본: assets/lease_time-revised.png

1. Client stamp는 생성 시각, Server arrival은 수신 시각입니다.
2. 서로 다른 시계의 절대값 대신 각각의 변화량을 비교합니다.
3. Silence duration은 정상 입력의 공백입니다. 안전 정지와 연결 폐기에 사용합니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 explanatory textbook illustration on a clean white background. Three distinct clocks in a left-to-right technical story. LEFT a small client laptop stamps a packet and a small clock labeled exactly 'Client stamp'. CENTER a server receives the packet, with a separate clock labeled exactly 'Server arrival'. RIGHT the same server waits with no new packet, a third clock labeled exactly 'Silence duration'. Show a dotted message path left to center, then a simple time ruler with a short orange safe-stop marker and a more distant red disconnect marker, labeled 'Safe stop' and 'Disconnect'. Do NOT align the numerical hands of client and server clocks as if they share an epoch. No numbers, no formulas, no data charts, no extra words. Precise teal navy orange educational illustration, big labels and generous white margins. This teaches sender timestamps, arrival time, and elapsed silence are distinct, not actual measured thresholds.

## 01권 L13 검증을 통과한 입력에서 화면의 상태까지

원본: assets/illustrated_v3/v1_L13.png

1. 위쪽은 운전 의도의 송신이며, 서버 검사를 통과해야 물리에 적용됩니다.
2. 아래쪽은 계산된 WorldState의 귀환과 클라이언트 재검증입니다.
3. 현재 연결·지도·Play가 정상인 예시입니다. 스로틀 0.5가 속도를 정하지 않습니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: wide16:9 raster diagram for a detailed networking and vehicle-simulation textbook. White background; navy typography/outlines; teal successful flow; orange validation gates and rejection branch. Clean flat technical diagram, large English text, arrows must make direction unambiguous.
Create a command-to-state roundtrip flow with SIX MAIN BOXES arranged as three columns and two rows. Broad top headings: first two columns "UNREAL", third column "SERVER". Use a divider before server.
TOP ROW from left to right:
box1 "CONTROL INTENT" with three small exact data lines "throttle 0.5", "steering 0", "gear Drive".
arrow right to box2 "SERIALIZE + SEND", a small outgoing byte-strip icon, compact note "Envelope".
arrow right across server boundary to box3 orange outlined "PARSE + VALIDATE" with three compact lines "Hello / map", "session / sequence", "time".
TOP RIGHT box3 connects DOWN via a thick teal arrow labeled "ACCEPTED ONLY" to the bottom right box6. Add a short orange SIDE branch from box3 to a small "REJECT" endpoint; it stops and has no path to simulation.
BOTTOM ROW read RIGHT TO LEFT:
bottom right box6 "SIMULATE + SERIALIZE" with simple vehicle state diagram and lines "WorldState" and "state + Health".
arrow LEFT across boundary labeled "RESULT BYTES" to bottom middle box5 "VALIDATE WORLDSTATE", with lines "map / Play", "sequence".
arrow LEFT to bottom left box4 "STORE + DISPLAY" with a small monitor showing a car silhouette and label "LatestState".
Include tiny clear reading order numbers1,2,3 then4 at simulate(bottom right),5 at validate(bottom middle),6 at store(bottom left), matching the arrows.
Small footer "ILLUSTRATIVE INPUT • NO FIXED SPEED GUARANTEE". Do not draw a direct link from throttle to a numerical speed. Do not imply Send or parsing alone guarantees acceptance. WorldState holds computed outcome, not raw request. No perfect measured behavior, no real packet dump, no watermark.

## 02권 L01 시작 준비가 끝난 뒤 이벤트 루프가 틱을 호출합니다

원본: assets/illustrated_v3/v2_L01.png

1. 위는 한 번 수행하는 준비이며 아직 차를 이동시키지 않습니다.
2. 아래 틱은 입력·NPC·물리·발행을 차례로 연결합니다.
3. 타이머와 소켓이 같은 주 이벤트 루프를 공유합니다.

제작 프롬프트:

Use case: scientific-educational. Create one clean 16:9 landscape raster textbook diagram on white background, navy text and outlines, teal data paths, orange key callback accents. Flat technical illustration with large legible English labels, generous whitespace. No title, no decorative elements, no branding. Explain a C++ simulation server from one-time startup to repeated tick. Top band labeled "STARTUP": three boxes "main" -> "run_simcore" -> "ioc.run"; under main small label "Options"; under run_simcore "Files + objects". Bottom major rounded container labeled "MAIN EVENT LOOP" shows two kinds of callback sharing a single loop: a small socket/packet icon labeled "Socket callback" and a timer icon labeled "Timer callback". Timer callback arrow enters a four-step horizontal sequence inside a light teal container labeled "run_tick": "Validate input" -> "Prepare NPCs" -> "Vehicle physics" -> "Publish state". Publish state ends in a small outgoing snapshot packet. Thin return arrow from completed run_tick to Timer callback labeled "Schedule next". Arrow from ioc.run down to the loop boundary means loop execution. Illustrate callbacks as sequential opportunities, never parallel execution threads. Do not connect Socket callback directly to physics or imply ioc.run runs once per tick. Only use listed English labels. All text large enough for slide reading.
Final accuracy edits:
Keep the entire image, palette, all text, and layout unchanged except DELETE the teal right-pointing arrow immediately to the right of 'Socket callback' (the arrow at about x=450,y=495 pointing into run_tick). Leave white background in its place. Socket callback must NOT have an arrow into run_tick. Keep the Timer callback arrow to run_tick and every other arrow unchanged.

## 02권 L02 물리 틱 한 번의 시간과 실제 기다린 시간은 다릅니다

원본: assets/illustrated_v3/v2_L02.png

1. 60Hz의 한 틱은 물리 시간 약 16.67ms입니다.
2. 아래 30ms는 한 틱 계산에 걸린 실제 시간의 교육용 예입니다.
3. 놓친 마감을 건너뛰어도 완료한 물리 틱은 하나입니다.

제작 프롬프트:

Use case: scientific-educational. One accurate 16:9 landscape raster textbook diagram, white background, navy typography, teal simulation, orange wall-time delay. Flat technical illustration, spacious, no title or decoration. Compare two time quantities using separate clearly non-shared scales. Upper half labeled "SIMULATION TIME" has one large teal tile labeled "One completed tick" with a small car moving one short step; next to it the exact equation "60 Hz: dt = 1/60 s" and below "16.67 ms". Lower half labeled "WALL TIME" shows an orange task-duration bar from "Start" to "Finish", with label "30 ms" centered inside the bar and "Illustrative example" nearby. At the end of the bar, arrow to a clock icon labeled "Next future deadline". Gray dashed ticks inside the wall-time bar are crossed out lightly, with large label "Skip missed deadlines". A brace with text "Still one physics tick" connects the single upper tile conceptually to the full lower duration. Do not add catch-up ticks, extra physics tiles, numerical wall deadline positions or imply dt becomes 30 ms. Clear distinction: actual compute time can exceed fixed dt without adding completed physics ticks. Only listed labels and equations; no tiny text.
Final accuracy edits:
Keep entire image, all large text, palette, layout unchanged except correct missed deadline marks inside the orange 30 ms wall-time bar. Remove ALL FOUR gray dashed tick marks and X marks. Add exactly ONE gray dashed mark with X at the midpoint of the bar. This is a schematic indicator of a missed deadline, not four separate deadlines. Do not alter any other part.

## 02권 L03 새 입력의 검증과 입력 부재의 만료를 따로 읽습니다

원본: assets/illustrated_v3/v2_L03.png

1. 위 명령은 세션·순서·시계·추가 지연을 모두 검사합니다.
2. 유효 입력 부재가 250ms를 초과하면 안전 정지하고 권한은 남습니다.
3. 같은 기준에서 1000ms를 초과하면 기존 권한을 폐기합니다.

제작 프롬프트:

Use case: scientific-educational. Create one 16:9 landscape raster textbook diagram, clean white background, navy large text, teal valid path, orange safety state. Flat precise diagram with generous whitespace. Upper band is a left-to-right chain labeled "ARRIVING COMMAND": packet -> four gates "Session" -> "Sequence" -> "Client clock" -> "Extra delay" -> green box "Accept". One shared downward red branch from gates labeled "Reject" clearly means failing any gate; no numerical values in upper band. Lower band labeled "NO NEW VALID INPUT" is a three-state horizontal timeline: "Active" -> "SafeStop" -> "Retired". Arrow Active to SafeStop labeled "> 250 ms". Arrow SafeStop to Retired labeled "> 1000 ms". A long bracket underneath both arrows labeled "Wall time since last valid input" makes clear both thresholds share the same origin, not cumulative durations. SafeStop box subtitle "Keep session"; Retired box subtitle "Remove session". Thin teal arrow from upper Accept to Active labeled "Valid current session" shows recovery only before retirement; do not connect Retired back to Active. Include small exact bottom label "Transitions occur when checked". Threshold comparison must be strictly greater than, never greater-or-equal. No extra icons except command packet and small clock. No watermarks or decorative objects.
Final accuracy edits:
Keep entire image, all text, palette and layout unchanged except fix ONE arrow: the teal line from 'Accept' currently incorrectly ends at the top of 'SafeStop'. Extend its horizontal segment farther left and place its downward arrowhead at the TOP CENTER of the 'Active' box instead. The Accept arrow must end at Active, not SafeStop. Label 'Valid current session' stays. Do not add any arrow from Retired. Leave timeout arrows exactly as they are.
Edit only the bottom bracket-like teal line UNDER the three state boxes. Completely DELETE that entire U-shaped teal line from beneath Active all the way to beneath Retired, including the erroneous upward arrowhead at Active. Leave white empty space in place. Keep the text 'Wall time since last valid input' unchanged. Preserve all other arrows and text unchanged, especially Accept to Active, Active to SafeStop and SafeStop to Retired. There must be no bottom line connecting Retired to Active.

## 02권 L04 최신 대기는 교체해도 전송 중인 버퍼는 보존합니다

원본: assets/illustrated_v3/v2_L04.png

1. A가 전송 중일 때 대기 B는 C, D로 교체됩니다.
2. A 바이트는 쓰기 완료까지 보존하며 그 뒤 최신 D를 보냅니다.
3. 오른쪽 message와 self 캡처가 완료까지 객체 수명을 잇습니다.

제작 프롬프트:

Use case: scientific-educational. One 16:9 landscape raster educational diagram, white background, navy outlines, teal active data, orange replacement arrows, large sans-serif English labels. No title or decoration. Explain latest-wins state queue and async buffer lifetime. Left two-thirds show three sequential rows with columns labeled "IN FLIGHT" and "WAITING": first row teal packet "A" and pale orange packet "B"; second row same A and "C"; third row same A and "D". Vertical orange replacement arrow between B/C and C/D labeled once "Replace waiting". All A packets are within one long teal outline labeled "Keep A unchanged". Beneath rows a right-pointing arrow reads "A completes" leading to a new in-flight teal packet "D". The right third is a simple lifetime strip headed "ASYNC LIFETIME": line beginning "async_write", midpoint marker "Function returns", endpoint "Callback completes". Two solid horizontal ownership bands labeled "message" and "self" both span from start through callback completion, with caption "Captured until completion". Use no pointer syntax or tiny text. Do not suggest copying three A messages or modifying A; row arrangement is a progression over time, with small downward label "Time". Accurate queue policy is for driving state only, so put exact small label "Driving state messages" at top. No additional text.

## 02권 L05 격자 표본에서 바퀴 아래 접점을 찾기

원본: assets/ground_query-revised.png

1. 아래로 향한 Ray는 지면을 찾는 질의입니다. 차량을 누르는 힘이 아닙니다.
2. 주황색 Ground hit는 도로 또는 연석에서 찾은 실제 접점입니다.
3. 위쪽 Normal은 표면 방향입니다. 접점과 함께 바퀴 지지 계산에 사용합니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 white textbook side-view cross-section diagram. A wheel on the left rests on a flat grey road, a second wheel on the right rests on a raised curb top. Above each wheel, a blue origin dot sends one vertical dashed ray DOWN to the corresponding ground surface, with a clearly downward arrowhead. At each actual ground hit show a small orange hit point, and a short green surface normal pointing UP from the surface, independent from the ray. Labels only 'Ray origin', 'Ground hit', 'Normal', 'Road', 'Curb'. Show ground-height measurement, NOT a force or a spring. Tire lower edges touch ground without penetrating. The ray represents query geometry and may pass through a non-colliding visualization wheel. No numeric data, no code, navy teal orange, crisp readable technical illustration, no shadow that suggests floating wheels.

## 02권 L06 바퀴별 지지력이 차체를 받치는 모습

원본: assets/suspension.png

1. 바퀴마다 지면 높이와 접촉 여부가 다를 수 있습니다.
2. 스프링은 눌린 정도, 댐퍼는 눌리는 속도에 반응합니다.
3. 각 바퀴의 지지력을 합치면 차체의 위아래 운동과 기울기가 달라집니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 engineering textbook cutaway illustration on clean white. A blue sedan chassis in three-quarter front view, translucent body so four wheels and spring/damper assemblies connecting wheel supports to chassis are clearly visible. One front wheel rests on a raised kerb and the other three on the lower road. Springs visibly compress by different amounts. One large downward arrow above chassis labeled exactly 'mg'. Four upward arrows at tire contact patches labeled 'Fz' (different lengths, no numerical values). Labels 'Body', 'Spring + Damper', 'Tire', 'Ground' with clear sparse leader lines. Physically plausible generic suspension schematic, not CAD manufacturing dimensions. Navy/teal/orange, precise but accessible, do not show wheels floating disconnected, no software logos, no tiny text. This is an educational illustration, not a screenshot.

## 02권 L07 바퀴가 뜨면 구동 토크는 어떻게 달라질까요?

원본: assets/illustrated_v3/v2_L07.png

1. 무하중 바퀴가 개방형 차동의 양쪽 토크를 제한합니다.
2. 주황색 제동 반력은 접지·제동 한도 안에서 전달을 돕습니다.
3. D·R은 요구 방향을 바꾸며 공중에 접지력을 만들지 않습니다.

제작 프롬프트:

Use case: scientific-educational. Create one technically accurate wide 16:9 raster textbook diagram on white, navy outlines, teal drive torque paths, orange brake reaction. Large English labels, no title, no numerical values. Two equal side-by-side panels display a schematic rear axle viewed from behind, with central differential, two half-shafts, and two tires. In BOTH panels left tire is slightly above ground with clear gap, labeled "Unloaded"; right tire rests on a raised curb platform and is labeled "Supported". Upper input arrow into differential labeled "Drive torque". Left panel heading "OPEN DIFFERENTIAL": both half-shaft torque arrows tiny and equal, annotation "Weak side limits both". Right panel heading "BRAKE ASSIST": orange brake pad touches LEFT unloaded tire and curved orange torque arrow labeled "Brake reaction"; equal medium teal torque arrows on both half-shafts, annotation "Limited shared torque". RIGHT supported tire alone shows a ground contact patch; unloaded tire has NO contact patch or ground force arrow. Show a simple separate bottom strip with gear selector "D / R" and opposed horizontal arrows labeled "Demand direction", teaching forward/reverse demand, without implying lateral car force. Under right panel include "Within tire grip + brake capacity". No force arrows from air, no shafts connecting wheels across panels, no unlimited force, no mass values, no decorative details.

## 02권 L08 한 타이어가 나누어 사용하는 종·횡방향 힘

원본: assets/tire_force-revised.png

1. 타이어 접촉부가 종방향 힘 Fx와 횡방향 힘 Fy를 전달합니다.
2. 오른쪽 원은 두 힘이 함께 사용하는 접지 한도를 설명하는 개념도입니다.
3. 가속에 힘을 많이 쓰면 회전에 쓸 여유가 줄어듭니다. 조향각 자체와 구분합니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 white textbook illustration, two objects only. LEFT a close-up generic tire contact patch on asphalt, small arrow along travel labeled 'Longitudinal' and perpendicular arrow to the side labeled 'Lateral'. RIGHT a simple friction-budget circle diagram, no numeric scale: horizontal axis to right labeled 'Fx', vertical axis up labeled 'Fy', a teal vector from circle center diagonally upper right ending INSIDE the circle, a second thin orange dashed continuation beyond circle boundary. Label circle boundary exactly 'Grip limit'. This is a conceptual equal-friction circle model, not measured tire data. No equations, no paragraphs, no additional objects, precise clean navy teal orange, large labels, generous margins.
EDIT: Edit only the left half of this illustration: remove BOTH horizontal arrows and the words 'Lateral' and 'Longitudinal' around the tire. Keep the tire itself and its contact with asphalt. Preserve the ENTIRE right-side friction circle diagram, axes Fx and Fy, Grip limit label, teal vector, dashed orange extension exactly unchanged. There must be no arrows on or beside the physical tire, because this front view cannot correctly show the forward direction with a horizontal arrow. No other changes.

## 02권 L09 차종 선택은 외형과 물리 자료를 함께 연결합니다

원본: assets/illustrated_v3/v2_L09.png

1. 하나의 차종에서 시각 모델과 물리 프로필이 갈라집니다.
2. 주황색 점은 자동차 네 접점과 오토바이 중심선 두 접점입니다.
3. 휠베이스는 앞뒤 축 사이, 반지름은 휠 중심부터 잽니다.

제작 프롬프트:

Use case: scientific-educational. One clean 16:9 raster textbook diagram, white background, navy labels, teal arrows, orange contact points. A large centered top box "Vehicle class" branches downward into two clearly separated equal columns. Left heading "VISUAL MODEL": small clean side-view silhouettes of sedan, compact car, truck, motorcycle; no specific brand or logos. Right heading "PHYSICS PROFILE": four large labeled parameter cards "Mass", "Wheelbase", "Tire radius", "Yaw inertia" with meaningful minimal pictograms: balance weight; distance between front/rear axle center lines; wheel with radius line from center to rim not diameter; circular angular response arrow. Lower right shows two top-down schematic contact layouts with labels "Car: four contacts" and "Motorcycle: centerline contacts": car has four orange points forming a rectangle; motorcycle exactly two orange points in one fore-aft centerline, no duplicate side points. A wide subtle linking brace under the two columns labeled "Consistent dimensions and class". No numerical data, no performance claims, no wheel count confusion. Generous white space, all labels large, simple flat engineering illustration, no decorative elements or title.
Final accuracy edits:
Make one localized technical correction. ONLY replace the pictogram inside the small 'Wheelbase' parameter card. It currently looks like two tires joined across an axle, misleadingly showing track width. Replace it with a tiny unmistakable SIDE VIEW outline of a car, with two CIRCULAR wheel profiles at front and rear, and a horizontal double-ended distance arrow below measuring from the front wheel CENTER vertical dashed line to the rear wheel CENTER vertical dashed line. Keep the word Wheelbase, its card boundaries, all other cards, diagrams, silhouettes, labels, colors, and composition unchanged. Do not depict tire treads viewed from the front. Wheelbase must unambiguously be fore-aft axle spacing in side view.

## 02권 L10 무게중심에서 벗어난 접촉이 만드는 회전

원본: assets/collision.png

1. 접촉점의 위치와 힘의 방향을 먼저 확인합니다.
2. 무게중심을 향한 충격과 중심에서 벗어난 충격의 회전 효과는 다릅니다.
3. 밀림은 선운동, 돌아가는 정도는 접촉점의 지렛팔과 관성까지 사용합니다.

제작 프롬프트:

Use case: scientific-educational. Single wide 16:9 engineering textbook illustration on white, TOP DOWN view of one blue car hitting the front-left corner of one orange car obliquely. Clear small contact point labeled exactly 'Contact'. A short thick arrow from contact pointing away along the contact normal labeled exactly 'Impulse'. On the orange car one curved arrow indicating possible angular motion labeled exactly 'Rotation'. A small center dot in orange car labeled exactly 'COM'. Show offset between contact and center of mass with a thin dotted line. Keep all cars fully inside frame with ample margins. No shattered glass, no injuries, no dramatic crash particles. This explains why contact location and direction matter, not exact numerical simulation. Precise stylized technical illustration, navy teal orange, large readable English labels, no logos, no other text.
EDIT: Correct only the force and rotation arrows for physics consistency, keeping cars, contact point, COM, dotted lever arm, background and label words intact. The teal Impulse arrow MUST start at Contact and point diagonally UPPER RIGHT (toward 1 o'clock), representing the impulse applied to the ORANGE car from the blue car. The curved navy Rotation arrow on the RIGHT END of orange car MUST point DOWNWARD along that right end, showing CLOCKWISE rotation in this top-down view. Reposition the two labels slightly only if needed. No second arrows, no extra text, no other changes.

## 02권 L11 경로 선택과 신호 앞 감속은 서로 다른 판단입니다

원본: assets/illustrated_v3/v2_L11.png

1. 교육용 그래프는 길이 합이 작은 A→B→D의 80m를 고릅니다.
2. 오른쪽 추종 제어는 경로가 있어도 빨간 신호 앞에서 감속합니다.
3. Turn intent는 예정한 교차로 회전이며 일반 곡률과 다릅니다.

제작 프롬프트:

Use case: scientific-educational. One clear wide 16:9 raster textbook diagram, white background, navy outlines/text, teal selected route, orange controls. Left half labeled "ROUTE PLANNER" and subtitle "Illustrative lane graph". Four circular lane nodes in diamond graph: A at left labeled "A: 20 m", B at upper middle "B: 35 m", C at lower middle "C: 50 m", D at right "D: 25 m". Directed links only A->B, A->C, B->D, C->D. Teal highlights exactly A->B->D; gray alternate A->C->D. Below graph two clean lines "A → B → D = 80 m" and "A → C → D = 95 m". Weights are node lengths, never edge weights. Arrow across center labeled "Lane sequence" leads to right half "LANE FOLLOWER". Right half is top-down T junction with small teal car approaching from bottom; the selected route bends left at junction. Car clearly stops BEFORE a transverse stop line below the junction. A traffic signal beside stop line has red lamp lit, labeled "Stop line". Orange directional turn icon near car labeled "Turn intent"; it indicates the planned left turn, not current yaw. Include one label "Brake before red signal". Do not portray car already crossing red signal, no spurious nodes/edges, no traffic timing claims. Large labels and whitespace, no title or decorative city.

## 02권 L12 회전하는 차체가 필요한 공간

원본: assets/npc_space-revised.png

1. 가는 경로선만 비어 있어도 트럭 전체가 지나간다는 뜻은 아닙니다.
2. Clear corridor는 차체 폭과 회전 중 모서리가 차지하는 공간까지 비어 있습니다.
3. Blocked corridor는 선이 통과해도 차체가 장애물과 겹쳐서 사용할 수 없습니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 top-down driving textbook diagram on white. Broad grey paved road with one stationary orange car blocking the center. A blue truck behind it has two potential curved forward routes around the car. LEFT route has a wide translucent teal corridor representing truck swept width and stays clear of the orange obstacle and roadside. RIGHT route looks clear for a thin line but its translucent ORANGE truck-width corridor overlaps a small roadside barrier, marked with a small orange X at overlap. Label only 'Vehicle width', 'Clear corridor', 'Blocked corridor'. Both paths bend smoothly with no in-place pivot, no traffic-law claims, no pedestrians, no numerical widths. Explain why empty centerline alone does not guarantee vehicle clearance. Clean precise navy teal orange engineering illustration.

## 02권 L12 후진으로 공간을 만든 뒤 우회하는 개념

원본: assets/npc_bypass.png

1. 바로 꺾을 공간이 부족하면 먼저 뒤쪽 통과 공간을 확인합니다.
2. 후진은 조향 곡선에 필요한 여유를 만드는 과정입니다.
3. 앞으로 진행하는 우회 경로도 차체 전체의 충돌과 회전 반경을 검사합니다.

제작 프롬프트:

Use case: scientific-educational. Single wide 16:9 overhead textbook diagram of a road obstacle-avoidance decision. White background, broad grey road with dashed lane divider and curbs, all traffic travel upward. A blue following car at lower center is close behind one stopped orange car at center. A short orange curved reverse path starts at blue car rear and ends a small distance below, label 'Reverse'. A green smooth bypass path starts at that reverse endpoint, passes LEFT of stopped orange car with realistic turn radius, returns ahead of it, label 'Bypass'. One red too-tight path is shown dashed from original blue-car pose touching the stopped car, labeled 'Blocked'. Blue car does not turn in place. Label the orange car 'Obstacle'. Large simple English labels only, no arrows beyond clear path direction markers, no distance measurements, generous margins, navy/teal/orange and minimal explanatory engineering style. Diagram illustrates a candidate plan, not an actual recorded NPC route.

## 02권 L12 한 번의 긴 탐색을 여러 틱으로 나누기

원본: assets/server_tick.png

1. 위쪽의 긴 탐색은 다른 입력 처리와 상태 발행을 늦출 수 있습니다.
2. 아래쪽은 후보 검사를 여러 틱에 나누어 다음 입력도 처리하는 방식입니다.
3. 계산을 나눠도 차량마다 탐색 기회가 돌아가도록 순서를 관리해야 합니다.

제작 프롬프트:

Use case: scientific-educational. Landscape 16:9 textbook explanatory timeline on white. Exactly two horizontal timeline rows, each left-to-right. Top row heading 'Before': small teal segment labeled 'Input', one very long orange segment labeled 'NPC search', a small blue segment at far right labeled 'State'. Bottom row heading 'After': show three repeated groups, each containing three equal-medium-small segments labeled 'Input', 'One probe', 'State'; use teal, orange, navy consistently. Under top long segment a small hourglass illustration emphasizes waiting. Under lower groups a small green clock emphasizes scheduling. Do not include numeric time scales or measured values. These are qualitative timelines explaining amortized work, not benchmark charts. Clear large exact labels and arrows only from left to right, lots of white margins, precise editorial engineering style, no other text or logos.

## 02권 L13 저장 상태를 보여 주는 재생과 물리를 다시 푸는 검증

원본: assets/illustrated_v3/v2_L13.png

1. 위 ghost는 저장 snapshot으로 당시 경로를 표시합니다.
2. 아래는 입력·외부 proxy·고정 조건으로 ego 물리를 다시 풉니다.
3. 계산 결과와 기록 결과를 비교하며 전체 NPC 판단 재현은 아닙니다.

제작 프롬프트:

Use case: scientific-educational. Create one accurate 16:9 raster textbook comparison diagram, white background, navy large labels, teal dataflow, orange validation. Two horizontal lanes with generous whitespace. Top lane heading "GHOST PLAYBACK": archive icon "Saved snapshots" -> simple translucent car at three past positions "Display saved path". No physics engine in upper lane. Bottom lane heading "EGO PHYSICS RERUN": two input boxes stacked "Recorded inputs" and "External proxies" plus third compact box "Same identity + fixed dt", all three arrows feed one large box "Vehicle physics". Vehicle physics arrow to box "Recomputed state". That box and separate archive box "Recorded result" both have arrows entering a final orange comparison box "Compare error". Crucial: Recorded result must go ONLY to Compare error, never into Vehicle physics. Small schematic of ego car and one outlined obstacle car in the external proxies input clarifies recorded collision surroundings. Bottom footnote exact "Ego under recorded external conditions" in readable medium text. No numbers, no claim of whole-city determinism, no extra labels, no decorative gear cogs or title.

## 02권 L14 같은 시점의 증거에서 검증 가능한 진단으로 이어 갑니다

원본: assets/illustrated_v3/v2_L14.png

1. 같은 시점의 입력·health·접촉·Fz·Fx를 모읍니다.
2. 가설을 구별할 검사를 고르고 같은 시작 상태에서 반복합니다.
3. 첫 틱 힘과 이후 이동을 확인하고 결과와 한계를 남깁니다.

제작 프롬프트:

Use case: scientific-educational. One 16:9 clean raster textbook diagram on white, navy text, teal evidence and tests, orange hypotheses. No title, decorative filler or invented numeric measurements. Four large panels left-to-right joined by arrows, with clear headings "OBSERVE", "HYPOTHESIZE", "TEST", "REPORT". OBSERVE shows a small car stopped on curb next to a single aligned log card labeled "Same instant"; readable log field names only "Input", "Health", "Contact", "Fz / Fx" and explicit "Unknown ≠ zero". HYPOTHESIZE shows three candidate cards "Expired input", "Wall contact", "Torque limit"; connect observation arrow to all three rather than asserting one is true. TEST shows a compact two-stage sequence "First-tick forces" -> "Motion + contact" inside one panel and a loop arrow with label "Same initial state". REPORT shows a simple notebook card containing exactly "Evidence", "Result", "Limits". One thin teal feedback arrow from TEST back to HYPOTHESIZE beneath panels labeled "Revise". Distinguish facts from possible causes visually. Do not imply a passed distance check alone proves a physical fix, no success check mark on an untested hypothesis, all four headings and field text large and readable.

## 03권 L01 W 입력이 화면에 돌아오는 왕복

원본: assets/input_trip-revised.png

1. W는 운전 의도입니다. 클라이언트가 보낸 명령에 담깁니다.
2. 서버는 명령을 검사하고 그 틱의 차량 운동을 계산합니다.
3. 화면은 수신한 결과 상태를 표시합니다. 송신 완료와 이동 완료는 다릅니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 classroom storyboard on white with FOUR left-to-right scenes, numbered only 1 2 3 4. 1 a finger pressing a keyboard key clearly marked W, caption 'Input'. 2 a small addressed binary packet, caption 'Command'. 3 a server beside a car with a short movement trail, caption 'Simulation'. 4 a monitor displays the car at its new position, caption 'Display'. Connect adjacent scenes with simple rightward arrows. The packet represents requested throttle, not a precomputed new position. No code, no numeric telemetry, no brand logos, no extra labels, navy teal orange, well-spaced exact readable words. Conceptual explanation, not actual screenshot.

## 03권 L02 Pawn과 부품의 역할

원본: assets/actor_components-v2.png

1. Pawn은 입력과 차량의 대표 위치를 연결하는 소유 객체입니다.
2. 차체·바퀴·카메라 부품은 자신이 맡은 표시와 동작을 처리합니다.
3. 위치가 있는 SceneComponent는 부모 기준 변환으로 함께 움직입니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 textbook illustration on clean white of Unreal Actor component composition. Central large translucent outlined container labeled exactly 'Pawn'. Inside the container put a stylized small blue sedan labeled 'Mesh', a camera on a short boom labeled 'Camera', and a small network plug/circuit icon labeled 'Client Component'. They are separate aspects of one vehicle Actor, not three independent cars. One generic gamepad outside at top labeled 'Controller' points to the enclosing Pawn. No invented Unreal logos or source code, only these five exact labels, large readable letters, teal/navy/orange engineering illustration, generous white space. This is a conceptual composition diagram, not the exact engine inheritance or attachment tree. No browser UI frames, no buttons, no tiny text.
EDIT: Edit only the top object under the word 'Controller': replace the physical gamepad with a simple neutral software-object symbol, such as a small upright class-document sheet with three horizontal lines. The Controller here is a software Actor that possesses a Pawn, NOT a physical game controller. Keep the word 'Controller', orange connection arrow, Pawn container, mesh/camera/client-component objects, layout, background and all other labels unchanged. Remove the car brand emblem without altering the car. Do not add any text.

## 03권 L03 Play 정리와 새 실행의 경계

원본: assets/illustrated_v3/v3_L03.png

1. 위쪽은 실행 순서입니다. EndPlay에서 연결과 소리를 정리합니다.
2. 아래 A→B는 연결 교체입니다. A의 늦은 사건은 세대로 거릅니다.
3. Play ID는 실행을, SocketGeneration은 연결을 구분합니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: explanatory raster diagram for a Korean programming textbook, with Korean explanatory prose outside this image. Produce a wide 16:9 composition, white background, clean flat illustration, navy text and outlines, teal active flows, orange caution paths, large readable sans-serif English labels. Precise, spacious, practical teaching diagram, no decoration, no logos, no watermark. All arrows must clearly connect the intended nodes. Not a real software screenshot; no invented measurement results. Primary request: show why every new Play needs its own identity and cleanup. Main horizontal lifecycle has five stages, each with a small meaningful icon: "Constructor" with subtitle "Parts + defaults" -> "BeginPlay" with "New Play ID + Connect" -> "Tick" with "Update" -> "EndPlay" with "Disconnect + Stop sound" -> "Next Play" with "New Play ID". Under the middle stages show a second small connection-lifetime inset: "Socket A" grey -> "Socket B" teal. An orange delayed arrow from Socket A goes to an orange gate labeled "Generation check", then ends at a barred stop labeled "Old event rejected"; it must not enter Socket B. The lifecycle arrows depict order only, never imply EndPlay creates the next Play. Use only the exact quoted labels; 2 rows, generous margins.

## 03권 L04 같은 S도 차량 상태에 따라 뜻이 달라집니다

원본: assets/illustrated_v3/v3_L04.png

1. 첫 행은 방향 전환 허용 속도보다 빠른 전진에서 S로 제동합니다.
2. 저속·신선한 상태에서는 Reverse를 고르고 후진 가속합니다.
3. W와 S를 함께 누른 마지막 행은 추진보다 제동을 우선합니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: explanatory raster diagram for a Korean programming textbook, with Korean explanatory prose outside this image. Produce a wide 16:9 composition, white background, clean flat illustration, navy text and outlines, teal active flows, orange caution paths, large readable sans-serif English labels. Precise, spacious, practical teaching diagram, no decoration, no logos, no watermark. All arrows must clearly connect the intended nodes. Not a real software screenshot; no invented measurement results. Primary request: teach W/S inputs become different commands based on vehicle state. Title "Pedals need context". Four clearly separated horizontal example rows, each with state at left, key capsule at middle, output at right connected by arrow. Row 1: "Drive + moving forward" then "S" then orange brake icon "Brake first". Row 2: "Drive + low speed + fresh state" then "S" then teal gear icon "Reverse + throttle". Row 3: "Reverse + moving backward" then "S" then "Reverse throttle". Row 4: "Any gear" then "W + S" then orange brake icon "Brake priority". Above rows a small flow "Pedals" + "Gear" + "Fresh speed" feeding "Resolver" illustrates inputs, without being connected as a temporal chain. Do not print numerical thresholds; no gear shift at high forward speed; no claim stale state zeroes all inputs. Use only quoted labels, simple arrow and car icons. Large readable text, educational infographic.

## 03권 L05 조향 변화와 최신 명령 보관·송신

원본: assets/illustrated_v3/v3_L05.png

1. 왼쪽 선은 키보드 중간 명령입니다. 누르면 접근하고 놓으면 복귀합니다.
2. 가운데 한 칸만 최신 값입니다. 흐린 옛 값은 대기열에 쌓지 않습니다.
3. 송신은 기본 20Hz, 변경 시 최대 30Hz이며 Tick 횟수와 다릅니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: explanatory raster diagram for a Korean programming textbook, Korean explanations added outside the image. Wide 16:9 white background, clean flat illustration, navy labels, teal accepted flow, orange warnings, large readable sans-serif English text. Generous whitespace; no fake software screenshot, no invented measured data, no logos, no watermark. Exact labels only, arrows clearly connect nodes. Primary request: show smooth keyboard steering feeding a latest-only command slot and timed sender. Compose 3 connected teaching panels. Left panel labeled "A / D" shows keycap icons and a rising ramp toward a dashed target, then a ramp back to zero after release; axes have only "Time" and "Command" (no measured tick values). The panel labels are "Hold: approach target" and "Release: return to zero". Arrow to middle panel labeled "PendingControl" containing ONE highlighted card "Latest command"; faded prior cards outside the slot with orange crossed-out arrow labeled "Replace old value". Do not draw a queue. Arrow from this one slot to a gate labeled "Send timing", with two clear captions under the gate: "Base: 20 Hz" and "Changed: up to 30 Hz". Last arrow to a small server labeled "Server". Small separate bottom note "Tick rate is separate" with a clock icon, no implication each Tick is a send. The line plot is a conceptual ramp, not a real captured trace. Keep meaning: latest value overwrites; send rate is independent from input callbacks.

## 03권 L06 받은 바이트가 안전한 상태가 되는 과정

원본: assets/illustrated_v3/v3_L06.png

1. Copy는 데이터 수명, Game thread는 엔진 적용 위치를 지킵니다.
2. Assemble은 마지막 조각까지 누적합니다. 미완성은 기다립니다.
3. 잘못된 크기·형식은 거절하며, 객체와 연결 수명도 따로 검사합니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: explanatory raster diagram for a Korean programming textbook, Korean explanations added outside the image. Wide 16:9 white background, clean flat illustration, navy labels, teal accepted flow, orange warnings, large readable sans-serif English text. Generous whitespace; no fake software screenshot, no invented measured data, no logos, no watermark. Exact labels only, arrows clearly connect nodes. Primary request: teach four independent receive safety responsibilities as a pipeline. Layout from left to right with five numbered panels labeled "1 Copy", "2 Game thread", "3 Assemble", "4 Parse + validate", "5 Apply". Panel 1 shows a borrowed byte strip copied into an owned strip with text "Owned bytes". Panel 2 shows a thread-boundary gate with label "Safe execution context". Panel 3 shows three complementary byte fragments joined into ONE rectangular envelope, the final piece orange, caption "Wait for final fragment". Panel 4 shows the complete envelope at a shield/checkpoint, small labels "Size + format" and "Object + connection" (these guard concepts also remain required during the pipeline). Panel 5 shows an accepted state card labeled "LatestState". A narrow orange lower rail under panels 3–4 ends at a stop sign labeled "Incomplete / invalid: reject"; only a dashed rejection arrow from panel 4 to this rail, no arrow applying invalid bytes. Diagram is conceptual: copying preserves lifetime, scheduling chooses context, assembling restores message boundary, validation decides acceptance. No unsupported claim that each network callback is a full message. Use exact quoted labels only.
Targeted correction: Use case: text-localization. Edit this image with one targeted text correction only. In the bottom-right orange caption next to STOP, replace 'Incomplete / invalid: reject' with exactly two lines: 'Incomplete: wait' on first line and 'Invalid: reject' on second line. This distinction is essential: ordinary unfinished fragments are accumulated while waiting; only malformed or oversized messages are rejected. Preserve every other label, node, arrow, color, and the 16:9 canvas. Keep large readable text and no additional material.

## 03권 L07 연결 뒤에도 맵·Play·순서 관문이 남습니다

원본: assets/illustrated_v3/v3_L07.png

1. 위 관문은 메시지 계약→맵→Play→순서의 서로 다른 조건입니다.
2. 현재 M2/P2/40에서 41은 받지만 같은 번호 40은 중복입니다.
3. 900이 더 커도 P1은 지난 실행입니다. 다른 지도 M1도 거절합니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: explanatory raster diagram for a Korean programming textbook; Korean reading notes added outside this image. White background, wide 16:9 composition. Crisp flat illustration, navy text and outlines, teal accepted flows, orange rejected paths. Large readable sans-serif English labels, exact quoted text only. No fake screenshot, no invented measurements, no logos, no watermark. Diagrams must convey causality and boundaries clearly, ample whitespace. Primary request: show that an open socket is only a transport, accepted state needs identity and order. Top main flow left to right: "Connected" with open-link icon -> "Hello" shield labeled "Schema + features" -> "Map identity" gate -> "Play ID" gate -> "Sequence" gate -> "LatestState" accepted card. Keep top six nodes readable. Bottom panel caption "Example: current M2 / P2 / 40". Under it show four wide packet cards in a 2-by-2 grid, not a dense table. Card1 "M2 / P2 / 41" followed by teal check and "Accept". Card2 "M2 / P2 / 40" followed by orange stop and "Duplicate". Card3 "M2 / P1 / 900" followed by orange stop and "Old Play". Card4 "M1 / P2 / 42" followed by orange stop and "Wrong map". Example labels are illustrative given current state, not measured data. Large sequence 900 must visibly still be rejected. Do not suggest Sequence overrides identity.

## 03권 L08 새 패킷이 없어도 화면 프레임은 계속됩니다

원본: assets/client_gap-revised.png

1. 위쪽은 상태의 도착 시간, 아래쪽은 차량 표시 위치입니다.
2. 수신이 끊겨도 상태 나이는 계속 늘지만 위치 예측에는 한도가 있습니다.
3. 예측을 계속하면 서버에서 멈춘 차도 화면에서 전진할 수 있습니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 white textbook illustration showing a server update gap and bounded visual extrapolation. One straight road across image. At LEFT a solid blue car at the last received position, labeled exactly 'Last state'. Slightly to its right a paler car labeled exactly 'Prediction limit'. A distinct vertical orange marker beside the second car indicates the display stops extending the prediction beyond this point. A long grey dotted road continues farther to the right but NO more cars there. Above the road, three arriving little packets then a visible gap labeled exactly 'No new state'. A small separate stopwatch labeled 'State age' continues during the gap. Only these five labels, no numeric values, no other paragraphs. Clarify visually that time since receipt can grow while predicted displacement is capped. Teal navy orange, large clean exact words, spacious pedagogical illustration, not actual game screenshot.
EDIT: Edit only the three downward dashed teal arrows that run from the packet timeline to the cars: remove those three arrows entirely. Keep all packets, the upper timeline and gap bracket, all labels, both cars, the orange prediction limit marker, road and state-age stopwatch exactly as they are. The upper arrival timeline and lower car-position schematic are separate conceptual views and must not be linked by arrows. No other changes.

## 03권 L09 동·북 축 교환과 단위 변환

원본: assets/coordinates.png

1. 서버의 동·북·위 방향과 Unreal의 축 대응을 먼저 확인합니다.
2. 같은 지점도 미터를 센티미터로 바꾸면 숫자가 100배가 됩니다.
3. 위치 변환과 회전 부호는 별도로 검사합니다. 그림 방향만 보고 부호를 추측하지 않습니다.

제작 프롬프트:

Use case: scientific-educational. Single wide 16:9 top-down coordinate illustration on white, exactly TWO separated panels without panel borders. LEFT: small road map grid, origin at lower left, right-pointing horizontal arrow labeled 'East (X)', upward vertical arrow labeled 'North (Y)', a blue point at grid coordinates east2 north5 labeled '(2, 5) m'. RIGHT: matching road map grid, origin at lower left, right-pointing horizontal arrow labeled 'Y', upward vertical arrow labeled 'X', matching blue point at same physical spot labeled '(500, 200) cm'. Heading above LEFT exactly 'Server ENU'; heading above RIGHT exactly 'Unreal World'. A short connector between panels labeled 'swap + scale'. Axes correctly match: server X=east horizontal; Unreal Y=east horizontal; server Y=north vertical; Unreal X=north vertical. Z omitted because this is top view. Big exact labels, uncluttered teal/navy with orange scale cue, no extra equations, no logos, no perspective distortion.

## 03권 L10 도로 표면 측정에서 서버 접촉까지

원본: assets/illustrated_v3/v3_L10.png

1. 주황선은 조명선이 아니라 충돌 표면의 높이·법선을 측정하는 선입니다.
2. 측정 결과를 맵으로 내보내고 서버는 그 지면으로 접촉을 계산합니다.
3. 아래는 양쪽 맵의 정체성 비교입니다. 외형 수정만으로 지면은 안 바뀝니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: explanatory raster diagram for a Korean programming textbook; Korean reading notes added outside this image. White background, wide 16:9 composition. Crisp flat illustration, navy text and outlines, teal accepted flows, orange rejected paths. Large readable sans-serif English labels, exact quoted text only. No fake screenshot, no invented measurements, no logos, no watermark. Diagrams must convey causality and boundaries clearly, ample whitespace. Primary request: explain the relationship between road appearance, collision measurement, exported ground, and server contact. Upper two-thirds three teaching stages left to right. Stage1 caption "Unreal world" shows a cutaway road curb: the visible asphalt skin navy-grey labeled "Visible road", and a distinct teal contour representing query collision surface labeled "Collision surface". Orange vertical downward measurement rays from above terminate on teal contact dots on that collision contour; label "Sample height + normal". Do not show these rays as lighting. Stage2 caption "Exported ground" shows a small grid terrain tile with heights as simple dots, beside a file icon labeled "Map package". Arrow from stage1 samples to stage2 then to stage3. Stage3 caption "Server contact" shows a car tire touching the matching sampled curb profile with upward contact normal arrow. Lower teaching strip has two linked identity cards: "Client map" -> a teal equality gate "Same identity" <- "Server map". Bottom plain note "Appearance alone does not update ground". This is a conceptual cutaway, no measured coordinates, no claim server queries Unreal every frame.
Targeted correction: Use case: precise-object-edit. Edit this educational diagram. Correct ONLY the road contact geometry and collision callout; preserve all wording, colors, layout, the map pipeline, and all other contents. In the right 'Server contact' panel, replace the tire and local ground drawing with a clean side-view tire whose bottom visibly touches a flat horizontal teal ground segment at exactly one teal contact dot directly beneath tire center. Draw an upward VERTICAL teal normal arrow starting exactly at that contact dot; it may overlay the tire as a diagram annotation. No floating tire, no detached dot, no angled normal on flat ground. Keep curb profile to the left as context, with the wheel on the flat lower surface to its right. In the left panel, point the 'Collision surface' callout tip directly onto the teal contour itself, not inside concrete. No other changes. Maintain wide16:9 white/navy/teal/orange style.
Targeted correction: Use case: precise-object-edit. Make ONLY this tiny callout correction. In the upper-left Unreal world diagram, the label 'Collision surface' currently points to a teal circle inside the concrete. DELETE that interior teal circle and redirect the teal callout line from the same label so its endpoint lands exactly ON the top teal ground contour, preferably at the first sample point on the lower horizontal ground just to the right of the curb. Do not modify the contour, orange sampling rays, image text, correct right-side tire and vertical contact normal, middle map package, or bottom identity comparison. Preserve all else exactly.

## 03권 L11 월드 접촉점과 자식 부품의 상대 위치

원본: assets/component_space-revised.png

1. World 축은 고정되어 있고 Local 축은 차체와 함께 돌아갑니다.
2. 그림은 고정 부착 예시입니다. 실제 Orbit 카메라는 회전 상속을 끕니다.
3. 서버의 월드 접점을 바퀴 부품에 넣기 전에 차체 상대 좌표로 변환합니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 textbook illustration on white. Two top-down views of the SAME simple blue car: at LEFT car facing up, at RIGHT car rotated clockwise 45 degrees. Each car has a tiny orange camera icon attached behind it and one labeled point at a front wheel. A small triad at each car center rotates WITH the car, label 'Local'. A larger fixed reference axis in each scene stays identical, label 'World'. The camera and wheel keep their relative position to the car as the car rotates. Only labels 'Local', 'World', 'Camera', 'Wheel'; no X/Y because software axis conventions are explained outside this generic picture. Precise engineering illustration, simple flat colors, no force arrows, no code, no formulas or extra text.

## 03권 L12 HUD 화면 좌표와 카메라 시선 기준

원본: assets/illustrated_v3/v3_L12.png

1. 왼쪽 원점은 화면 좌상단입니다. HUD는 우하단 여백을 남깁니다.
2. 오른쪽 카메라는 차의 위치를 따르며 시선과 차체 회전을 구분합니다.
3. 마우스는 이동량, 스틱은 속도입니다. 스틱에만 시간을 곱합니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: explanatory raster diagram for a Korean programming textbook, Korean reading notes added outside image. White background wide 16:9, navy text and outlines, teal arrows and key shapes, orange caution accents. Clean purposeful illustration and large readable sans-serif English labels with generous margins. No fake software screenshot, no measured results, no logos, no watermark. Use quoted labels exactly. Primary request: teach that HUD uses screen coordinates while orbit camera follows vehicle position with its own view rotation. Two large upper panels with useful spatial diagrams. Left panel "Screen space" shows a landscape viewport outline with screen-origin dot at TOP LEFT labeled "(0, 0)", right-pointing X arrow and downward Y arrow. Draw a simple road perspective within viewport but label "Clear driving view" near open center. Small outlined rectangle at BOTTOM RIGHT labeled "HUD" with small margin arrows to right and bottom edges. No numeric gauge data or pretend screenshot. Right panel "Camera space" shows top-down car centered, a camera icon outside it connected by a boom to the car pivot, a teal curved orbit arrow around pivot labelled "View rotation", and a separate orange curved arrow beside car labelled "Body rotation"; do not merge these two arrows. Caption "Follow position; independent rotation". Lower strip has two separated simple formulas, with mouse icon and gamepad icon: "Mouse delta x sensitivity" and "Stick x degrees/second x DeltaSeconds". This teaches mouse delta already accumulates movement per frame whereas stick is a rate; no time multiplier on mouse. No invented axes or numeric limits.

## 03권 L13 상태를 파형과 음량으로 바꾸는 구성

원본: assets/audio-v2.png

1. 입력은 차량 속도·회전·슬립 등의 상태입니다. 소리의 완성 파형이 아닙니다.
2. 서로 다른 소리 성분을 합성하고 상태에 맞게 음량과 변화를 조절합니다.
3. Play 종료·오래된 상태·경적 해제에 맞춰 해당 소리를 정리합니다.

제작 프롬프트:

Use case: scientific-educational. Wide 16:9 explanatory textbook illustration on white for procedural car audio. LEFT a stylized blue sedan with tiny steering wheel and speed dial icons near it. MIDDLE two clearly different illustrative waveform strips: upper smooth periodic sine wave labeled exactly 'Engine'; lower irregular aperiodic friction texture labeled exactly 'Tire'. RIGHT a speaker emitting a few curved sound waves. Connect car to both waveform strips and both strips to speaker with clean lines. Under the speaker show a smooth volume envelope rising then flat then falling, labeled exactly 'Envelope'. No numeric data, no frequency scales, no claim this is actual recorded sound. Spacious flat engineering textbook illustration, teal navy orange, readable English labels only, no logos. Scientifically sensible conceptual example, not a measurement chart.
EDIT: Edit this educational audio illustration, preserving the car, Engine sine wave, Tire noise waveform, speaker, all main flow lines, colors and layout. Remove ONLY the short downward arrow between the speaker and the lower envelope curve. The lower curve is an illustration of volume over time, NOT a processing stage after the speaker. Keep the curve and its label 'Envelope'. Directly below that label add the small but clearly legible exact phrase 'Volume over time'. No new arrows. No other changes.

## 03권 L14 기록 재생과 검사로 관찰을 근거로 바꾸기

원본: assets/illustrated_v3/v3_L14.png

1. 위 경로는 저장된 상태를 Ghost로 재표시하며 물리를 재계산하지 않습니다.
2. 아래 순환은 조건과 기대를 정하고 자동 검사·F3·HUD로 비교합니다.
3. Evidence note에 기대·실제·한계를 적고 다음 확인으로 이어갑니다.

제작 프롬프트:

Use case: scientific-educational. Asset type: explanatory raster diagram for a Korean programming textbook, Korean reading notes added outside image. White background wide 16:9, navy text and outlines, teal arrows and key shapes, orange caution accents. Clean purposeful illustration and large readable sans-serif English labels with generous margins. No fake software screenshot, no measured results, no logos, no watermark. Use quoted labels exactly. Primary request: teach a concrete record-and-test evidence loop while making Ghost replay scope clear. Upper horizontal flow three cards: "Authoritative states" with small timestamped stacked state-card icon -> "Track / CSV" with saved-file icon -> "Ghost display" with translucent outline car on a short dotted path. Under Ghost include prominent orange caption "Re-displays saved states"; do not draw a connection back to server physics. Lower half a separate loop of four spacious nodes: "Question + conditions" -> "Expected behavior" -> "Test + observe" -> "Evidence note", and a return arrow from Evidence note back to Question + conditions labeled "Next check". Under Test + observe show two small side-by-side useful icons labelled "Automated checks" and "F3 + HUD"; under Evidence note small three-line caption "Expected / actual / limits". A thin arrow goes down from Track / CSV to Test + observe, indicating records can support comparison. The loop is learning/evidence, no claim Ghost proves deterministic physics or that sensor metadata is captured camera video. No numbers or fake passes, no green PASS badges; only conceptual evidence workflow.
Targeted correction: Use case: text-localization. Edit this image with a targeted factual correction. The three numeric calendar timestamp lines in the 'Authoritative states' stacked card are invented data and must be replaced with exactly three abstract lines 'State at t0', 'State at t1', 'State at t2'. Remove every numeric calendar date and clock timestamp. Preserve all other contents including pipeline, Ghost, arrows, evidence loop, colors, and 16:9 layout. Render abstract t0 t1 t2 labels clearly; no new numbers beyond these symbolic indexes.
