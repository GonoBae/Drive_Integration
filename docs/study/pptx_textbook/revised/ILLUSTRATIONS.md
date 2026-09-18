# 개정 교재 그림 안내

내장 이미지 생성 도구로 만든 원리 설명용 개념도입니다. 실제 프로젝트 화면이나 실차 측정 자료가 아닙니다. 각 그림의 뜻과 한계는 해당 단원의 본문에서 설명합니다. 그림 파일은 PPT와 HTML 안에 포함되어 있으므로 아래 저장소 경로에 접근하지 않아도 읽을 수 있습니다.

## architecture

저장소 원본: `docs/study/pptx_textbook/assets/architecture.png`

Use case: scientific-educational. Create one wide landscape textbook illustration, 16:9, on a clean warm-white background, for learning a C++ server and Unreal driving simulation. A monitor showing a simplified blue sedan in a grey virtual road scene at LEFT, a compact computer/server with a visible calculation grid at RIGHT. Two distinct broad curved paths connect them: TOP path points LEFT TO RIGHT labeled exactly 'ControlCommand'; BOTTOM path points RIGHT TO LEFT labeled exactly 'WorldState'. Small keyboard at lower-left. Center label 'WebSocket + Protobuf'. Other labels exactly 'Unreal' and 'C++ Server'. Friendly precise technical illustration, teal and deep navy with orange accents, large readable typography, generous space. No invented measurements, no logos, no code, no tiny labels, no extra text. This is a conceptual explanatory illustration, not a screenshot.

## protobuf

저장소 원본: `docs/study/pptx_textbook/assets/protobuf.png`

Use case: scientific-educational. Single wide 16:9 conceptual textbook illustration on pure white. Three large stages arranged horizontally with clear rightward arrows: left a structured paper document with readable large label 'Object'; center a precise compact binary packet, label 'Bytes'; right a second structured paper document labeled 'Object'. Above center a blueprint sheet labeled 'Schema' with two guidance lines to the transformation arrows. Under left arrow exact word 'Serialize'; under right arrow exact word 'Parse'. This teaches that a schema describes data and both ends use it, while bytes travel. Tiny text inside the papers must be replaced by simple colored horizontal marks, not invented code or numbers. Elegant teal/navy/orange technical illustration, ample white margins, readable exact English labels, no logos, no extra labels, no source-code text. Concept image not screenshot.

## suspension

저장소 원본: `docs/study/pptx_textbook/assets/suspension.png`

Use case: scientific-educational. Wide 16:9 engineering textbook cutaway illustration on clean white. A blue sedan chassis in three-quarter front view, translucent body so four wheels and spring/damper assemblies connecting wheel supports to chassis are clearly visible. One front wheel rests on a raised kerb and the other three on the lower road. Springs visibly compress by different amounts. One large downward arrow above chassis labeled exactly 'mg'. Four upward arrows at tire contact patches labeled 'Fz' (different lengths, no numerical values). Labels 'Body', 'Spring + Damper', 'Tire', 'Ground' with clear sparse leader lines. Physically plausible generic suspension schematic, not CAD manufacturing dimensions. Navy/teal/orange, precise but accessible, do not show wheels floating disconnected, no software logos, no tiny text. This is an educational illustration, not a screenshot.

## npc_bypass

저장소 원본: `docs/study/pptx_textbook/assets/npc_bypass.png`

Use case: scientific-educational. Single wide 16:9 overhead textbook diagram of a road obstacle-avoidance decision. White background, broad grey road with dashed lane divider and curbs, all traffic travel upward. A blue following car at lower center is close behind one stopped orange car at center. A short orange curved reverse path starts at blue car rear and ends a small distance below, label 'Reverse'. A green smooth bypass path starts at that reverse endpoint, passes LEFT of stopped orange car with realistic turn radius, returns ahead of it, label 'Bypass'. One red too-tight path is shown dashed from original blue-car pose touching the stopped car, labeled 'Blocked'. Blue car does not turn in place. Label the orange car 'Obstacle'. Large simple English labels only, no arrows beyond clear path direction markers, no distance measurements, generous margins, navy/teal/orange and minimal explanatory engineering style. Diagram illustrates a candidate plan, not an actual recorded NPC route.

## actor_components

저장소 원본: `docs/study/pptx_textbook/assets/actor_components-v2.png`

Use case: scientific-educational. Wide 16:9 textbook illustration on clean white of Unreal Actor component composition. Central large translucent outlined container labeled exactly 'Pawn'. Inside the container put a stylized small blue sedan labeled 'Mesh', a camera on a short boom labeled 'Camera', and a small network plug/circuit icon labeled 'Client Component'. They are separate aspects of one vehicle Actor, not three independent cars. One generic gamepad outside at top labeled 'Controller' points to the enclosing Pawn. No invented Unreal logos or source code, only these five exact labels, large readable letters, teal/navy/orange engineering illustration, generous white space. This is a conceptual composition diagram, not the exact engine inheritance or attachment tree. No browser UI frames, no buttons, no tiny text.
EDIT: Edit only the top object under the word 'Controller': replace the physical gamepad with a simple neutral software-object symbol, such as a small upright class-document sheet with three horizontal lines. The Controller here is a software Actor that possesses a Pawn, NOT a physical game controller. Keep the word 'Controller', orange connection arrow, Pawn container, mesh/camera/client-component objects, layout, background and all other labels unchanged. Remove the car brand emblem without altering the car. Do not add any text.

## coordinates

저장소 원본: `docs/study/pptx_textbook/assets/coordinates.png`

Use case: scientific-educational. Single wide 16:9 top-down coordinate illustration on white, exactly TWO separated panels without panel borders. LEFT: small road map grid, origin at lower left, right-pointing horizontal arrow labeled 'East (X)', upward vertical arrow labeled 'North (Y)', a blue point at grid coordinates east2 north5 labeled '(2, 5) m'. RIGHT: matching road map grid, origin at lower left, right-pointing horizontal arrow labeled 'Y', upward vertical arrow labeled 'X', matching blue point at same physical spot labeled '(500, 200) cm'. Heading above LEFT exactly 'Server ENU'; heading above RIGHT exactly 'Unreal World'. A short connector between panels labeled 'swap + scale'. Axes correctly match: server X=east horizontal; Unreal Y=east horizontal; server Y=north vertical; Unreal X=north vertical. Z omitted because this is top view. Big exact labels, uncluttered teal/navy with orange scale cue, no extra equations, no logos, no perspective distortion.

## audio

저장소 원본: `docs/study/pptx_textbook/assets/audio-v2.png`

Use case: scientific-educational. Wide 16:9 explanatory textbook illustration on white for procedural car audio. LEFT a stylized blue sedan with tiny steering wheel and speed dial icons near it. MIDDLE two clearly different illustrative waveform strips: upper smooth periodic sine wave labeled exactly 'Engine'; lower irregular aperiodic friction texture labeled exactly 'Tire'. RIGHT a speaker emitting a few curved sound waves. Connect car to both waveform strips and both strips to speaker with clean lines. Under the speaker show a smooth volume envelope rising then flat then falling, labeled exactly 'Envelope'. No numeric data, no frequency scales, no claim this is actual recorded sound. Spacious flat engineering textbook illustration, teal navy orange, readable English labels only, no logos. Scientifically sensible conceptual example, not a measurement chart.
EDIT: Edit this educational audio illustration, preserving the car, Engine sine wave, Tire noise waveform, speaker, all main flow lines, colors and layout. Remove ONLY the short downward arrow between the speaker and the lower envelope curve. The lower curve is an illustration of volume over time, NOT a processing stage after the speaker. Keep the curve and its label 'Envelope'. Directly below that label add the small but clearly legible exact phrase 'Volume over time'. No new arrows. No other changes.

## server_tick

저장소 원본: `docs/study/pptx_textbook/assets/server_tick.png`

Use case: scientific-educational. Landscape 16:9 textbook explanatory timeline on white. Exactly two horizontal timeline rows, each left-to-right. Top row heading 'Before': small teal segment labeled 'Input', one very long orange segment labeled 'NPC search', a small blue segment at far right labeled 'State'. Bottom row heading 'After': show three repeated groups, each containing three equal-medium-small segments labeled 'Input', 'One probe', 'State'; use teal, orange, navy consistently. Under top long segment a small hourglass illustration emphasizes waiting. Under lower groups a small green clock emphasizes scheduling. Do not include numeric time scales or measured values. These are qualitative timelines explaining amortized work, not benchmark charts. Clear large exact labels and arrows only from left to right, lots of white margins, precise editorial engineering style, no other text or logos.

## session

저장소 원본: `docs/study/pptx_textbook/assets/session.png`

Use case: scientific-educational. Landscape 16:9 textbook metaphor illustration on white about two separate lifetimes. Top one long continuous teal road ribbon labeled exactly 'Play session'. Under it three disconnected navy cable segments in a row, labeled exactly 'Connection A', 'Connection B', 'Connection C', all entirely under same long road ribbon. Small unplugged gap between A and B, plugged gap between B and C. At far right, outside the long road ribbon, a new separate orange road ribbon labeled 'New Play' with a blue car returned to start. Convey reconnecting within same Play versus new play reset. Keep symbols conceptual, no timestamps, no fake code or server screenshot, very simple clean labels with whitespace. Minimal teal navy orange classroom illustration.

## collision

저장소 원본: `docs/study/pptx_textbook/assets/collision.png`

Use case: scientific-educational. Single wide 16:9 engineering textbook illustration on white, TOP DOWN view of one blue car hitting the front-left corner of one orange car obliquely. Clear small contact point labeled exactly 'Contact'. A short thick arrow from contact pointing away along the contact normal labeled exactly 'Impulse'. On the orange car one curved arrow indicating possible angular motion labeled exactly 'Rotation'. A small center dot in orange car labeled exactly 'COM'. Show offset between contact and center of mass with a thin dotted line. Keep all cars fully inside frame with ample margins. No shattered glass, no injuries, no dramatic crash particles. This explains why contact location and direction matter, not exact numerical simulation. Precise stylized technical illustration, navy teal orange, large readable English labels, no logos, no other text.
EDIT: Correct only the force and rotation arrows for physics consistency, keeping cars, contact point, COM, dotted lever arm, background and label words intact. The teal Impulse arrow MUST start at Contact and point diagonally UPPER RIGHT (toward 1 o'clock), representing the impulse applied to the ORANGE car from the blue car. The curved navy Rotation arrow on the RIGHT END of orange car MUST point DOWNWARD along that right end, showing CLOCKWISE rotation in this top-down view. Reposition the two labels slightly only if needed. No second arrows, no extra text, no other changes.

## lease_time

저장소 원본: `docs/study/pptx_textbook/assets/lease_time-revised.png`

Use case: scientific-educational. Wide 16:9 explanatory textbook illustration on a clean white background. Three distinct clocks in a left-to-right technical story. LEFT a small client laptop stamps a packet and a small clock labeled exactly 'Client stamp'. CENTER a server receives the packet, with a separate clock labeled exactly 'Server arrival'. RIGHT the same server waits with no new packet, a third clock labeled exactly 'Silence duration'. Show a dotted message path left to center, then a simple time ruler with a short orange safe-stop marker and a more distant red disconnect marker, labeled 'Safe stop' and 'Disconnect'. Do NOT align the numerical hands of client and server clocks as if they share an epoch. No numbers, no formulas, no data charts, no extra words. Precise teal navy orange educational illustration, big labels and generous white margins. This teaches sender timestamps, arrival time, and elapsed silence are distinct, not actual measured thresholds.

## cpp_memory

저장소 원본: `docs/study/pptx_textbook/assets/cpp_memory-revised.png`

Use case: scientific-educational. Wide 16:9 clean white programming textbook illustration with three small related scenes, left-to-right. LEFT two differently colored document sheets labeled 'Original' and 'Copy', each with its own separate small storage tray. MIDDLE one document in one tray, with TWO paper name tags connected to the SAME document, caption 'Reference'. RIGHT a bookmark pointing to a document that is fading away, caption 'Lifetime'. Show that a copy owns separate data, a reference is another name for existing data, and a borrowed view cannot keep an owner alive. Do not show real memory addresses, code, pointer arithmetic, or claim reference is physically stored as a pointer. Clean teal navy orange editorial illustration, readable short labels, no logos, no decorative panels.

## packet_layer

저장소 원본: `docs/study/pptx_textbook/assets/packet_layer-revised.png`

Use case: scientific-educational. Wide 16:9 textbook illustration of logical network layering on white. On the LEFT a small structured document labeled 'Protobuf' goes inside a clearly larger open envelope labeled 'WebSocket'. On the RIGHT the closed envelope is carried by a cable-like path labeled 'TCP'. Use one simple rightward flow; keep Protobuf payload visibly INSIDE WebSocket envelope, TCP as transport rather than an additional payload schema. No UDP, JSON, IP labels, no numeric sizes, no logos. Explain conceptually that schema encoding, message framing, and ordered byte transport have different jobs. Readable teal navy orange technical illustration.
EDIT: Edit the provided network illustration. Keep the three main objects: a document at left, an envelope in the center, a cable at right. Keep only these labels: 'Protobuf bytes', 'WebSocket message', 'TCP stream'. Remove ALL paragraphs, the entire bottom legend box, the slogan, and ALL source code inside both documents. Inside the documents use short rows of tiny square teal/orange blocks with NO letters or numbers, to represent already serialized bytes. Make the document visibly slide into the envelope. The image must NOT suggest .proto schema source text is sent as payload. Big clean simple objects on white, no extra text. Preserve the main teal/navy/orange palette.

## client_gap

저장소 원본: `docs/study/pptx_textbook/assets/client_gap-revised.png`

Use case: scientific-educational. Wide 16:9 white textbook illustration showing a server update gap and bounded visual extrapolation. One straight road across image. At LEFT a solid blue car at the last received position, labeled exactly 'Last state'. Slightly to its right a paler car labeled exactly 'Prediction limit'. A distinct vertical orange marker beside the second car indicates the display stops extending the prediction beyond this point. A long grey dotted road continues farther to the right but NO more cars there. Above the road, three arriving little packets then a visible gap labeled exactly 'No new state'. A small separate stopwatch labeled 'State age' continues during the gap. Only these five labels, no numeric values, no other paragraphs. Clarify visually that time since receipt can grow while predicted displacement is capped. Teal navy orange, large clean exact words, spacious pedagogical illustration, not actual game screenshot.
EDIT: Edit only the three downward dashed teal arrows that run from the packet timeline to the cars: remove those three arrows entirely. Keep all packets, the upper timeline and gap bracket, all labels, both cars, the orange prediction limit marker, road and state-age stopwatch exactly as they are. The upper arrival timeline and lower car-position schematic are separate conceptual views and must not be linked by arrows. No other changes.

## component_space

저장소 원본: `docs/study/pptx_textbook/assets/component_space-revised.png`

Use case: scientific-educational. Wide 16:9 textbook illustration on white. Two top-down views of the SAME simple blue car: at LEFT car facing up, at RIGHT car rotated clockwise 45 degrees. Each car has a tiny orange camera icon attached behind it and one labeled point at a front wheel. A small triad at each car center rotates WITH the car, label 'Local'. A larger fixed reference axis in each scene stays identical, label 'World'. The camera and wheel keep their relative position to the car as the car rotates. Only labels 'Local', 'World', 'Camera', 'Wheel'; no X/Y because software axis conventions are explained outside this generic picture. Precise engineering illustration, simple flat colors, no force arrows, no code, no formulas or extra text.

## input_trip

저장소 원본: `docs/study/pptx_textbook/assets/input_trip-revised.png`

Use case: scientific-educational. Wide 16:9 classroom storyboard on white with FOUR left-to-right scenes, numbered only 1 2 3 4. 1 a finger pressing a keyboard key clearly marked W, caption 'Input'. 2 a small addressed binary packet, caption 'Command'. 3 a server beside a car with a short movement trail, caption 'Simulation'. 4 a monitor displays the car at its new position, caption 'Display'. Connect adjacent scenes with simple rightward arrows. The packet represents requested throttle, not a precomputed new position. No code, no numeric telemetry, no brand logos, no extra labels, navy teal orange, well-spaced exact readable words. Conceptual explanation, not actual screenshot.

## tire_force

저장소 원본: `docs/study/pptx_textbook/assets/tire_force-revised.png`

Use case: scientific-educational. Wide 16:9 white textbook illustration, two objects only. LEFT a close-up generic tire contact patch on asphalt, small arrow along travel labeled 'Longitudinal' and perpendicular arrow to the side labeled 'Lateral'. RIGHT a simple friction-budget circle diagram, no numeric scale: horizontal axis to right labeled 'Fx', vertical axis up labeled 'Fy', a teal vector from circle center diagonally upper right ending INSIDE the circle, a second thin orange dashed continuation beyond circle boundary. Label circle boundary exactly 'Grip limit'. This is a conceptual equal-friction circle model, not measured tire data. No equations, no paragraphs, no additional objects, precise clean navy teal orange, large labels, generous margins.
EDIT: Edit only the left half of this illustration: remove BOTH horizontal arrows and the words 'Lateral' and 'Longitudinal' around the tire. Keep the tire itself and its contact with asphalt. Preserve the ENTIRE right-side friction circle diagram, axes Fx and Fy, Grip limit label, teal vector, dashed orange extension exactly unchanged. There must be no arrows on or beside the physical tire, because this front view cannot correctly show the forward direction with a horizontal arrow. No other changes.

## ground_query

저장소 원본: `docs/study/pptx_textbook/assets/ground_query-revised.png`

Use case: scientific-educational. Wide 16:9 white textbook side-view cross-section diagram. A wheel on the left rests on a flat grey road, a second wheel on the right rests on a raised curb top. Above each wheel, a blue origin dot sends one vertical dashed ray DOWN to the corresponding ground surface, with a clearly downward arrowhead. At each actual ground hit show a small orange hit point, and a short green surface normal pointing UP from the surface, independent from the ray. Labels only 'Ray origin', 'Ground hit', 'Normal', 'Road', 'Curb'. Show ground-height measurement, NOT a force or a spring. Tire lower edges touch ground without penetrating. The ray represents query geometry and may pass through a non-colliding visualization wheel. No numeric data, no code, navy teal orange, crisp readable technical illustration, no shadow that suggests floating wheels.

## npc_space

저장소 원본: `docs/study/pptx_textbook/assets/npc_space-revised.png`

Use case: scientific-educational. Wide 16:9 top-down driving textbook diagram on white. Broad grey paved road with one stationary orange car blocking the center. A blue truck behind it has two potential curved forward routes around the car. LEFT route has a wide translucent teal corridor representing truck swept width and stays clear of the orange obstacle and roadside. RIGHT route looks clear for a thin line but its translucent ORANGE truck-width corridor overlaps a small roadside barrier, marked with a small orange X at overlap. Label only 'Vehicle width', 'Clear corridor', 'Blocked corridor'. Both paths bend smoothly with no in-place pivot, no traffic-law claims, no pedestrians, no numerical widths. Explain why empty centerline alone does not guarantee vehicle clearance. Clean precise navy teal orange engineering illustration.

