# 교재 그림 설명과 프롬프트

이 그림은 내장 이미지 생성 도구로 만든 교육용 개념도입니다. 실제 프로젝트 화면, 성능 측정 그래프, 특정 실차의 설계도는 아닙니다. 그림은 PPT와 HTML에 포함되어 있습니다. 저장소의 원본 위치는 `docs/study/pptx_textbook/assets`입니다.

## architecture

원본: `assets/architecture.png`

Use case: scientific-educational. Create one wide landscape textbook illustration, 16:9, on a clean warm-white background, for learning a C++ server and Unreal driving simulation. A monitor showing a simplified blue sedan in a grey virtual road scene at LEFT, a compact computer/server with a visible calculation grid at RIGHT. Two distinct broad curved paths connect them: TOP path points LEFT TO RIGHT labeled exactly 'ControlCommand'; BOTTOM path points RIGHT TO LEFT labeled exactly 'WorldState'. Small keyboard at lower-left. Center label 'WebSocket + Protobuf'. Other labels exactly 'Unreal' and 'C++ Server'. Friendly precise technical illustration, teal and deep navy with orange accents, large readable typography, generous space. No invented measurements, no logos, no code, no tiny labels, no extra text. This is a conceptual explanatory illustration, not a screenshot.

## protobuf

원본: `assets/protobuf.png`

Use case: scientific-educational. Single wide 16:9 conceptual textbook illustration on pure white. Three large stages arranged horizontally with clear rightward arrows: left a structured paper document with readable large label 'Object'; center a precise compact binary packet, label 'Bytes'; right a second structured paper document labeled 'Object'. Above center a blueprint sheet labeled 'Schema' with two guidance lines to the transformation arrows. Under left arrow exact word 'Serialize'; under right arrow exact word 'Parse'. This teaches that a schema describes data and both ends use it, while bytes travel. Tiny text inside the papers must be replaced by simple colored horizontal marks, not invented code or numbers. Elegant teal/navy/orange technical illustration, ample white margins, readable exact English labels, no logos, no extra labels, no source-code text. Concept image not screenshot.

## suspension

원본: `assets/suspension.png`

Use case: scientific-educational. Wide 16:9 engineering textbook cutaway illustration on clean white. A blue sedan chassis in three-quarter front view, translucent body so four wheels and spring/damper assemblies connecting wheel supports to chassis are clearly visible. One front wheel rests on a raised kerb and the other three on the lower road. Springs visibly compress by different amounts. One large downward arrow above chassis labeled exactly 'mg'. Four upward arrows at tire contact patches labeled 'Fz' (different lengths, no numerical values). Labels 'Body', 'Spring + Damper', 'Tire', 'Ground' with clear sparse leader lines. Physically plausible generic suspension schematic, not CAD manufacturing dimensions. Navy/teal/orange, precise but accessible, do not show wheels floating disconnected, no software logos, no tiny text. This is an educational illustration, not a screenshot.

## npc_bypass

원본: `assets/npc_bypass.png`

Use case: scientific-educational. Single wide 16:9 overhead textbook diagram of a road obstacle-avoidance decision. White background, broad grey road with dashed lane divider and curbs, all traffic travel upward. A blue following car at lower center is close behind one stopped orange car at center. A short orange curved reverse path starts at blue car rear and ends a small distance below, label 'Reverse'. A green smooth bypass path starts at that reverse endpoint, passes LEFT of stopped orange car with realistic turn radius, returns ahead of it, label 'Bypass'. One red too-tight path is shown dashed from original blue-car pose touching the stopped car, labeled 'Blocked'. Blue car does not turn in place. Label the orange car 'Obstacle'. Large simple English labels only, no arrows beyond clear path direction markers, no distance measurements, generous margins, navy/teal/orange and minimal explanatory engineering style. Diagram illustrates a candidate plan, not an actual recorded NPC route.

## actor_components

원본: `assets/actor_components-v2.png`

Use case: scientific-educational. Wide 16:9 textbook illustration on clean white of Unreal Actor component composition. Central large translucent outlined container labeled exactly 'Pawn'. Inside the container put a stylized small blue sedan labeled 'Mesh', a camera on a short boom labeled 'Camera', and a small network plug/circuit icon labeled 'Client Component'. They are separate aspects of one vehicle Actor, not three independent cars. One generic gamepad outside at top labeled 'Controller' points to the enclosing Pawn. No invented Unreal logos or source code, only these five exact labels, large readable letters, teal/navy/orange engineering illustration, generous white space. This is a conceptual composition diagram, not the exact engine inheritance or attachment tree. No browser UI frames, no buttons, no tiny text.
EDIT: Edit only the top object under the word 'Controller': replace the physical gamepad with a simple neutral software-object symbol, such as a small upright class-document sheet with three horizontal lines. The Controller here is a software Actor that possesses a Pawn, NOT a physical game controller. Keep the word 'Controller', orange connection arrow, Pawn container, mesh/camera/client-component objects, layout, background and all other labels unchanged. Remove the car brand emblem without altering the car. Do not add any text.

## coordinates

원본: `assets/coordinates.png`

Use case: scientific-educational. Single wide 16:9 top-down coordinate illustration on white, exactly TWO separated panels without panel borders. LEFT: small road map grid, origin at lower left, right-pointing horizontal arrow labeled 'East (X)', upward vertical arrow labeled 'North (Y)', a blue point at grid coordinates east2 north5 labeled '(2, 5) m'. RIGHT: matching road map grid, origin at lower left, right-pointing horizontal arrow labeled 'Y', upward vertical arrow labeled 'X', matching blue point at same physical spot labeled '(500, 200) cm'. Heading above LEFT exactly 'Server ENU'; heading above RIGHT exactly 'Unreal World'. A short connector between panels labeled 'swap + scale'. Axes correctly match: server X=east horizontal; Unreal Y=east horizontal; server Y=north vertical; Unreal X=north vertical. Z omitted because this is top view. Big exact labels, uncluttered teal/navy with orange scale cue, no extra equations, no logos, no perspective distortion.

## audio

원본: `assets/audio-v2.png`

Use case: scientific-educational. Wide 16:9 explanatory textbook illustration on white for procedural car audio. LEFT a stylized blue sedan with tiny steering wheel and speed dial icons near it. MIDDLE two clearly different illustrative waveform strips: upper smooth periodic sine wave labeled exactly 'Engine'; lower irregular aperiodic friction texture labeled exactly 'Tire'. RIGHT a speaker emitting a few curved sound waves. Connect car to both waveform strips and both strips to speaker with clean lines. Under the speaker show a smooth volume envelope rising then flat then falling, labeled exactly 'Envelope'. No numeric data, no frequency scales, no claim this is actual recorded sound. Spacious flat engineering textbook illustration, teal navy orange, readable English labels only, no logos. Scientifically sensible conceptual example, not a measurement chart.

보완: 스피커 아래의 화살표를 제거했습니다. Envelope 곡선은 출력 후 처리 단계가 아니라 시간에 따른 음량 변화를 뜻합니다. 곡선 아래에 Volume over time을 표기했습니다.

EDIT: Edit this educational audio illustration, preserving the car, Engine sine wave, Tire noise waveform, speaker, all main flow lines, colors and layout. Remove ONLY the short downward arrow between the speaker and the lower envelope curve. The lower curve is an illustration of volume over time, NOT a processing stage after the speaker. Keep the curve and its label 'Envelope'. Directly below that label add the small but clearly legible exact phrase 'Volume over time'. No new arrows. No other changes.

## server_tick

원본: `assets/server_tick.png`

Use case: scientific-educational. Landscape 16:9 textbook explanatory timeline on white. Exactly two horizontal timeline rows, each left-to-right. Top row heading 'Before': small teal segment labeled 'Input', one very long orange segment labeled 'NPC search', a small blue segment at far right labeled 'State'. Bottom row heading 'After': show three repeated groups, each containing three equal-medium-small segments labeled 'Input', 'One probe', 'State'; use teal, orange, navy consistently. Under top long segment a small hourglass illustration emphasizes waiting. Under lower groups a small green clock emphasizes scheduling. Do not include numeric time scales or measured values. These are qualitative timelines explaining amortized work, not benchmark charts. Clear large exact labels and arrows only from left to right, lots of white margins, precise editorial engineering style, no other text or logos.

## session

원본: `assets/session.png`

Use case: scientific-educational. Landscape 16:9 textbook metaphor illustration on white about two separate lifetimes. Top one long continuous teal road ribbon labeled exactly 'Play session'. Under it three disconnected navy cable segments in a row, labeled exactly 'Connection A', 'Connection B', 'Connection C', all entirely under same long road ribbon. Small unplugged gap between A and B, plugged gap between B and C. At far right, outside the long road ribbon, a new separate orange road ribbon labeled 'New Play' with a blue car returned to start. Convey reconnecting within same Play versus new play reset. Keep symbols conceptual, no timestamps, no fake code or server screenshot, very simple clean labels with whitespace. Minimal teal navy orange classroom illustration.

## collision

원본: `assets/collision.png`

Use case: scientific-educational. Single wide 16:9 engineering textbook illustration on white, TOP DOWN view of one blue car hitting the front-left corner of one orange car obliquely. Clear small contact point labeled exactly 'Contact'. A short thick arrow from contact pointing away along the contact normal labeled exactly 'Impulse'. On the orange car one curved arrow indicating possible angular motion labeled exactly 'Rotation'. A small center dot in orange car labeled exactly 'COM'. Show offset between contact and center of mass with a thin dotted line. Keep all cars fully inside frame with ample margins. No shattered glass, no injuries, no dramatic crash particles. This explains why contact location and direction matter, not exact numerical simulation. Precise stylized technical illustration, navy teal orange, large readable English labels, no logos, no other text.
EDIT: Correct only the force and rotation arrows for physics consistency, keeping cars, contact point, COM, dotted lever arm, background and label words intact. The teal Impulse arrow MUST start at Contact and point diagonally UPPER RIGHT (toward 1 o'clock), representing the impulse applied to the ORANGE car from the blue car. The curved navy Rotation arrow on the RIGHT END of orange car MUST point DOWNWARD along that right end, showing CLOCKWISE rotation in this top-down view. Reposition the two labels slightly only if needed. No second arrows, no extra text, no other changes.
