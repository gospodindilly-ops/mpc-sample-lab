# Функции MPC 3, которые мы берём: полная спецификация

Цель: описать функции MPC 3 **из нашего плана** (§5 [дорожной карты](firmware-roadmap.md), решения в [architecture.md](architecture.md)) со всеми их подфункциями заранее. Тогда модель данных, таблица параметров и режимы проектируются один раз, а не дописываются по одной функции. То, что нашлось в MPC 3 сверх плана, перечислено в конце одной строкой, без разбора.

**Источники:**
- MPC Software v3.9 Beta User Guide — основной, модель MPC 3;
- MPC Standalone OS User Guide v3.4 — для функций автономного режима;
- шаблон проекта штатной прошивки (`Temp-Project.xpj`) — точные имена полей и значения по умолчанию;
- строки приложения `MPC Sample` — перечни значений (типы фильтров, источники модуляции, доли синхронизации, эффекты).

Текст своими словами, параметры — фактами из источников.

**Обозначения решений:**
- ✅ берём как в MPC 3;
- 🔧 берём с упрощением под железо MPC Sample;
- ❌ не берём, с причиной.

**Ограничения железа, к которым всё сводится:**
- экран 320×240 без сенсора;
- 16 пэдов с силой удара и послекасанием;
- 24 кнопки, ручки K1–K3 и MAIN VOLUME, фейдер, энкодер с нажатием;
- 32 стереоголоса плюс стриминг с диска до 32 голосов, память 2 ГБ;
- стереовыход, вход Mic/TRS/USB;
- MIDI по TRS и USB, SYNC OUT;
- 4 ядра A55, без GPU.

---

## A. Треки и звук

### A.1 Треки

MPC 3: в проекте до 128 треков шести типов (Drum, Keygroup, Plugin, MIDI, CV, Audio). У нас **16 треков четырёх типов: Drum, Sample по нотам (как Keygroup), Audio и MIDI** (решения 2026-10-23 и 2026-10-24).

**Настройки трека** (Track Settings и Inspector):

| Параметр | Значения | Решение |
|---|---|---|
| Имя, цвет, позиция в списке | — | ✅ |
| Rec Arm | вкл/выкл | ✅ |
| MIDI Input Port / Channel | все порты / порт; Any / 1–16 | ✅ TRS, USB, панель |
| MIDI Send | трек-получатель MIDI | 🔧 один трек может слать ноты другому |
| Monitor | Off / In / Auto / Merge | ✅ |
| Length | в долях, или Seq — как секвенция | ✅ **основа «своего секвенсора» у трека** |
| Velo | масштаб силы удара трека | ✅ |
| Transpose | ± полутоны | ✅ |
| Mute / Solo | — | ✅ |
| Reset Strip, Delete, Duplicate (без событий) | — | ✅ |

В шаблоне проекта трек хранит: `name`, `colour`, `mute`, `muteGroup`, `pan`, `recordArm`, `length`, `lengthFollowsSequenceLength`, `cvPort`, `gatePort`, `padsFollowTrackColour`. Программа трека лежит в `program`, микшер — в `program.mixable`.

### A.2 Drum-программа: 128 пэдов (8 банков × 16)

**Global** (на весь трек):
- Polyphony: Mono / Poly;
- Semi ±36, Fine ±99 центов.

**Pad** (на пэд):
- Level, Pan;
- Vel Scale — масштаб силы удара.

**Simultaneous Play:** до 4 пэдов, которые звучат вместе с этим пэдом. Это Pad Link у MPC Sample. Поле `simultPlayTargets.value0..3`.

**Pad Play Modes:**

| Параметр | Значения | Поле | Решение |
|---|---|---|---|
| Sample Play | One-Shot / Note-Off / Note-On | `triggerMode` | ✅ (сейчас у нас One-Shot и Note-On) |
| Mute Group | Off, 1–32 (только внутри трека) | `whichMuteGroup` | ✅ (сейчас 1–16) |
| Pad Poly | Mono / Poly / 2–64 | `monophonic`, `polyphony` | ✅ |

**Сэмпл пэда:** один на пэд, как у MPC Sample. В формате MPC 3 это `layersv[0]`; из проектов MPC 3 берём первый слой. Поля:

| Параметр | Поле | Решение |
|---|---|---|
| Сэмпл | `sampleName`, `sampleFile` | ✅ |
| Slice: All / Pad / Slice N | `sliceIndex` (129 = Pad), `sliceInfo.*` | ✅ |
| Semi ±36, Fine ±99 | `coarseTune`, `fineTune`, `pitch` | ✅ |
| Level, Pan | `volume.{gainCoefficient, controlValue, law}`, `pan` | ✅ |
| Root Note (SMP = определить автоматически) | `rootNote` | ✅ |
| Key Track | `keyTrackEnable` | ✅ |
| Start / End | `sampleStart`, `sampleEnd`, `sliceInfo.Start/End` | ✅ |
| Loop: Off / Fwd / Rev / Alt, Loop Lock | `loop`, `loopStart`, `loopMode`, `sliceInfo.LoopMode`, `layerLoopModeOverridesSliceLoopMode` | ✅ (сейчас только Fwd) |
| Кроссфейд лупа: длина, Linear / Equal Power | `loopCrossfadeLength`, `sliceInfo.LoopCrossfadeType` | ✅ |
| Tail Length, Tail Start | `sliceInfo.TailLength`, `TailLoopPosition` | ✅ |
| Reverse | `direction` | ✅ |
| Offset (+ — начать позже, − — задержка) | `offset` | ✅ |
| Repeats (0 — бесконечно при Note-On) | — | ✅ |
| Motion: Increment / Random по слайсам, Cycles | — | ✅ |

**Synth Section** (`synthSection`):

| Блок | Параметры и поля | Решение |
|---|---|---|
| **Фильтр × 2** (`filterData.value0/1`) | Type (`filterType`), Cutoff, Reso, Env Amount, Keytrack, Velocity → Cutoff, Velocity → Env, Aftertouch → Cutoff, Pressure → Filter, Output Level, разброс Cutoff и Reso; общий Blend (`filterBlend`) и Serial / Parallel (`filterSerialRouting`) | ✅ |
| **Типы фильтров MPC** | Lowpass, Highpass и Bandpass (1/2/4/6/8 полюсов), Bandstop и Bandboost (2/4/6/8), Model1–3, Vocal1–3, MPC3000 LPF; в v3.9 ещё Jura LP и Iona LP | ✅ Список MPC Sample (Classic, LPF2/4, HPF2/4, BPF2/4) — подмножество. Jura и Iona — модели плагинов AIR: ❌ |
| **LFO × 2 на голос** (`lfoData.value0/1`) | Shape: Sine, Tri, S&H, Saw, SawD, Sqr, Noise; Rate или Rate [Sync]; Level; Delay / Delay [Sync]; Fade In / Fade In [Sync]; Reset (перезапуск по ноте); Free Running; цели: Pitch, Filter 1/2, Amp, Pan | ✅ |
| **Огибающие × 3:** Amp, Filter, Pitch (`ampEnvelope`, `filterEnvelope`, `pitchEnvelope`) | Delay, Attack, Hold, Decay, Sustain, Release; кривые A / D / R (`AttackCurve`, `DecayCurve`, `ReleaseCurve`); тип AD (Decay From Start / End) / AHDS / ADSR (при Note-On); Looped; Tempo Sync; Time Scaling; Velocity → Attack; глубина огибающей высоты `pitchEnvelopeAmount` | ✅ |
| **Velocity Sensitivity** | → Start (`velocityToStart`), Filter Attack, Filter Depth, Filter Cutoff, Pitch (`velocityToPitch`), Amp Attack, Amp (`velocitySensitivity`), Pan (`velocityToPan`) | ✅ |

**Доли синхронизации** (для LFO, огибающих, задержек): None; 8, 6, 4, 3, 2 целых; целая с точкой; целая, половинная (с точкой и триолью), четверть (с точкой и триолью), восьмая (с точкой и триолью), шестнадцатая (с триолью), тридцать вторая.

**Mod Matrix:** 32 строки. В каждой:
- Source, Amount, Target;
- Direction: однополярная или двухполярная;
- Min / Max;
- Via Source с Amount;
- Shaper с Amount.

| Что | Значения | Решение |
|---|---|---|
| Источники (из плана) | LFO 1/2, Filter Env, Pitch Env, Key Track (88 / 61 / 49 клавиш), Velocity, Poly Aftertouch, Random 1/2; с внешнего MIDI — Pitch Wheel, Channel Pressure, Breath Control | ✅ Poly Aftertouch дают наши пэды. Остальные источники MPC 3 относятся к генераторам вне плана |
| Цели | любой параметр синт-секции и слоя (Play Offset, Slice Index), Filter Blend, глубины огибающих, параметры LFO | ✅ Цели берём из общей таблицы параметров |
| Формы (Shaper) | Square, Inv Square, Cubic, Inv Cubic, Square Root, Cube Root, Double Sine, Quantize (0–25, 0–50, 0–75, 25–50, 50–75, 75–100 %) | ✅ |

Примечание. В шаблоне штатной прошивки у пэда mod matrix нет. Скорее всего, это только в Keygroup Advanced и в новых версиях формата. Хранение строк матрицы уточним при разборе формата `.xpj` у MPC 3.9.

### A.3 Sample-трек по нотам (Keygroup MPC 3)

- **До 128 зон** (keygroups), в каждой один сэмпл. По умолчанию одна зона. Пэды играют ноты хроматически: в банке D пэд 13 — исходная высота.
- **Два режима правки в MPC 3:** Legacy (как в старых MPC) и Advanced. 🔧 Берём одну модель: синт-секция как у Drum-пэда (§A.2), с двумя фильтрами.

**Global:**
- Semi ±36, Fine ±99;
- Trans — сдвиг входящих нот ±36;
- Polyphony: Mono / Poly;
- #KG — число зон, от 1 до 128.

**Зона** (Keygroup):

| Параметр | Значения | Поле в шаблоне |
|---|---|---|
| Keygroup Select (All — все зоны сразу) | — | — |
| Semi, Fine, Level, Pan | — | `coarseTune`, `fineTune`, `mixable` |
| Key Track | вкл/выкл | `keyTrackEnable` сэмпла |
| Note Range Lo / Hi | 0–127 | `lowNote`, `highNote` |
| KG Poly | Mono / Poly / 2–64 | `polyphony` |
| Mute Group | 1–32 | `whichMuteGroup` |
| Sample Play | One-Shot / Note-Off / Note-On | `triggerMode` |
| Root Note сэмпла | SMP или нота | `rootNote` |

**Решение:** ✅ зоны, Key Ranges, Root Note, Key Track, Trans и полифония зоны.

### A.4 Audio-трек (Audio MPC 3) и MIDI-трек (MIDI MPC 3)

- **Запись** с входа (Input: стерео или моно-пара) или перенос аудиофайла.
- **Monitor:** Off / In / Auto / Merge.
- **Выход:** сабмикс или мастер.
- **Регион на линии времени:**
  - громкость, вкл/выкл;
  - Reverse;
  - Fade In / Fade Out;
  - Repeats;
  - Coarse / Fine;
  - Warp с алгоритмом и темпом;
  - Split, Move, Duplicate, Trim, Insert / Delete Time.
- **Алгоритмы Warp:** Default, Basic (Least CPU), Pro Ten, Repitch, Elastique Pro.
  - 🔧 Elastique — чужая платная библиотека, не берём. Делаем свой простой растягиватель (гранулярный или WSOLA) и Repitch (меняет и высоту).
  - Руководство само предупреждает, что Warp тяжёл для CPU. Лимит регионов с Warp подбираем замером.
- **MIDI-трек:** порт (External = TRS, или USB, как MIDI Port в руководстве MPC Sample) и канал 1–16. Пэды, Note Repeat и события секвенции уходят наружу нотами; своего звука у трека нет. Ноты пэдов: A01 = C1 (36), дальше по полутонам. ✅

### A.5 Параметры сэмпла в Wave Editor

Это сэмпл пэда или зоны, поля — в таблице A.2.
- **Режимы маркеров:**
  - Zero Snap — Start, End и Loop прилипают к переходам через ноль;
  - Link Slices — конец слайса двигает начало следующего;
  - Slice Preview — Before / After / Off: что звучит, пока двигаешь маркер.
- **Темп сэмпла:** Warp (вкл/выкл), Stretch, BPM (кнопки ×2 и /2), BPM Sync. Поля пэда: `warpEnable`, `stretchPercentage`, `tempo`, `bpmLock`.
- **Шкала:** секунды, сэмплы или доли.
- **Process Sample:** те же процессы, что в Sample Edit, режим Track (§B).

### A.6 Микшер и маршрутизация

| Канал | Что есть | Решение |
|---|---|---|
| **Пэд / зона** | громкость, панорама, M / S, 4 посыла, вставки, выход: трек, Submix 1–8 или выход; Monitor | ✅ Выход — только трек или сабмикс |
| **Трек** | громкость, панорама, M / S, 4 посыла, вставки, выход: Submix 1–8 или мастер; автоматизация R / W / Off; Monitor; Rec Arm | ✅ |
| **Submix 1–8** | вставки, 4 посыла, выход, M, громкость, панорама, автоматизация | ✅ |
| **Return 1–4** (эффекты посылов) | вставки, выход, M, громкость, панорама, автоматизация | ✅ |
| **Main** | вставки, M, громкость, автоматизация | ✅ |
| Выходы 3/4 … 31/32, моно-выходы | — | ❌ физически только стерео |

- **В шаблоне** у каждого канала (`mixable`) есть:
  - `volume`, `pan`, `mute`, `solo`;
  - `sends[4]`;
  - `inserts {insertsEnabled, effects[]}`;
  - `audioRoute {destination, audioRouteSubIndex, channelBitmap}`;
  - `automationFilter`.
- **Коды назначения по шаблону:** у пэда 1 — в трек; у трека и сабмикса 2 — на выход, пара 0, каналы 1+2.
- **Новый проект в MPC 3** по умолчанию открывается с AIR Reverb на Return 1 и AIR Delay на Return 2. У нас — свои реверберация и задержка.
- **Слотов вставок** в руководстве нет числом. У MPC их обычно 4 на канал; в строках приложения есть «Insert 3» и «Insert 4». ✅ Берём 4.

---

## B. Сэмплы: правка, запись, Looper, Auto Sampler

### B.1 Sample Edit — общее для трёх режимов

- **Экран:** форма волны с обзором всего сэмпла сверху. Шкала — время, сэмплы или доли. Есть прокрутка и масштаб.
  - 🔧 Масштаб у нас: SHIFT + K1/K2/K3 — Zoom Start / End / Loop, как в руководстве MPC Sample.
- **Маркеры:**
  - зелёный — Start, красный — End, отдельный — Loop (если Loop Lock выключен);
  - грубая и точная подстройка: ×1000 и ×1 сэмпла.
  - 🔧 У нас K1–K3 плюс энкодер для точной подстройки.
- **Режимы маркеров:** Zero Snap и Link Slices (смысл — в A.5).
- **Переключение режимов:** Trim ↔ Chop ↔ Track. Выбранный слайс из Chop можно открыть в Trim, чтобы править подробно.

### B.2 Trim

- **Пэды прослушивания:**
  - 1–4 — на один удар: от Start, до Start, до End, от End;
  - 5–8 — то же, но звучит, пока держишь пэд;
  - 9 — весь сэмпл;
  - 10 — Start..End на один удар, 11 — Start..End, пока держишь;
  - 13 — луп, пока держишь, 16 — луп непрерывно;
  - 14 — до точки лупа, 15 — от точки лупа до конца сэмпла.
- **Параметры:**
  - Slice;
  - Start, End, Loop и Lock;
  - режим лупа Off / Fwd / Rev / Alt;
  - X-Fade: длина в сэмплах, Linear или Equal Power; нужно минимум 10 сэмплов между Start и Loop;
  - Tune ±24.00 полутона, Root Nt;
  - From BPM: Beats → Match, или To Sequence — подстроить и сменить темп секвенции;
  - BPM вручную, ×2 и /2, Detect BPM, Tap Tempo;
  - Key, Scale (Major / Minor / —), Detect Key.
- **Assign:**
  - Assign Slice to Pad: ссылка на слайс, тип Non-Destructive Slice или Pad Parameters;
  - New Sample to Pad: новый сэмпл, флажок Crop Sample;
  - назначение заменяет сэмпл пэда.
- ✅ всё. 🔧 Detect BPM, Detect Key и Tap Tempo делаем своими простыми алгоритмами в потоке файлов.

### B.3 Chop

- **Chop To:**
  - Manual;
  - Threshold: порог и Min Time в мс;
  - Regions: число регионов;
  - BPM: Bars, Beats, Time Div 1/4, 1/8, 1/16, 1/32.
- **Слайсы:**
  - выбор пэдом; больше 16 слайсов — на следующих банках;
  - Start / End слайса;
  - +Slice по позиции воспроизведения;
  - разметка ударами по незанятым пэдам на ходу: последний поставленный маркер светится зелёным, уже поставленные — жёлтым;
  - «−» склеить с предыдущим, «+» разрезать пополам, «−All» удалить все.
- **Курсор Cue:**
  - позиция, кнопки >Play и +Slice;
  - Cue Audition: One Shot / Toggle;
  - Pad Audition: One Shot / Note On.
- **Convert / Assign:**
  - новая программа на слайсах: Non-Destructive или Pad Parameters; флажок Create Events с числом тактов создаёт трек с событиями по порядку;
  - новая программа с новыми сэмплами: суффикс SI-#, флажки Crop, Create New Program, Create Events;
  - назначить слайс на пэд;
  - новый сэмпл на пэд;
  - Patched Phrase: сэмпл, который следует темпу секвенции, задаются такты;
  - ✅ всё. New clip program из MPC 3 (слайсы как клипы) ❌ — клипов у нас нет.
- **Chop не разрушает сэмпл:** слайсы — закладки внутри сэмпла. Поля `chopProperties` в шаблоне: `chopMode`, `chopThreshold`, `chopMinSliceTime`, `chopRegions`, `chopBars`, `chopBeats`, `chopTimeSignature`. ✅
- **У MPC Sample** своя форма: Chop Type Threshold / Regions 4·8·16 / Manual; Extract, Split, Merge через SHIFT + B1/B2/B3. Это подмножество, ложится на ту же модель.

### B.4 Track

Правка сэмпла прямо в звуковом тракте трека, с параметрами пэда:
- Slice: Pad / All, кнопка To Pad;
- Start, End, Loop, Lock;
- режимы лупа, X-Fade;
- Tune, Root;
- From BPM, Detect BPM;
- Assign.

✅

### B.5 Процессы обработки

Действуют на участок Start..End. Bit Reduce и Stereo→Mono — на весь сэмпл. Выполняются в потоке файлов, результат — новый сэмпл (шаг 1 нашей архитектуры).

| Процесс | Параметры | Решение |
|---|---|---|
| Discard | — оставить только Start..End | ✅ (это наш Trim Sample) |
| Delete | вырезать Start..End и сомкнуть | ✅ |
| Silence | тишина на Start..End | ✅ |
| Extract | Start..End в новый сэмпл, имя | ✅ |
| Normalize | — | ✅ (есть) |
| Reverse | участок задом наперёд | ✅ |
| Fade In / Fade Out | кривая: Linear, Square Root, Square, Log, Exp, Sine (в Chop и Track — только Linear, Log, Exp) | ✅ |
| Pitch Shift | ±12.00 полутона, длина не меняется | 🔧 свой алгоритм |
| Time Stretch | Number of Beats, New BPM или Ratio | 🔧 свой алгоритм |
| Gain Change | ±18 дБ, можно выше 0 дБ | ✅ |
| Copy | имя | ✅ |
| Bit Reduce | 1–16 бит | ✅ |
| Stereo > Mono | Left / Right / Sum, имя | ✅ |

### B.6 Sampler: запись

**Объединяем запись MPC Sample и MPC 3:**
- **Источник:**
  - по MPC Sample: Mic, Rear, Rear L, Rear R, Resample, USB, USB L, USB R;
  - по MPC 3: стерео или моно-вход, Resample L / R / L+R.
- **Вставные эффекты на входе** (Rec Input Effects): они записываются в сэмпл.
- **Mono / Stereo, Monitor:**
  - Monitor In — слушаем до сэмплера, без задержки;
  - Monitor Off — слушаем после сэмплера;
  - у MPC Sample: Off / Auto / On.
- **Порог:** запись стартует, когда сигнал выше порога.
- **Длина:**
  - максимум 19:59 на сэмпл;
  - по MPC Sample: Free или Seq.
- **Arm → Record**, Add Slice во время записи.

**Способы записи:**

| Способ | Как работает |
|---|---|
| Sample | обычная запись; Add Slice ставит маркеры |
| Slice | каждый удар по пэду ставит маркер. Пэд, который поставит следующий маркер, мигает красным; уже поставленные горят жёлтым. После записи можно создать трек на слайсах: выкл, Non-Destructive или Pad Parameters |
| Pad Tap | удар по пэду начинает запись в этот пэд, удар по другому переключает. Записывающий мигает красным, готовые горят зелёным. Только Drum-трек |
| Pad Hold | запись в пэд, пока он зажат. Только Drum-трек |

**После записи — окно Keep / Discard:**
- New Name, Track, Assign to Pad, Root Note;
- если играла секвенция: Add Event — At Start или At Trigger, и в какой трек;
- кнопки Save, Play, Discard, Keep, Edit.

**Recall из MPC Sample:** последние 25 записей со входа — кольцевой буфер.

Всё ✅.
- Потоку звука нужен путь записи без выделения памяти: кольцевой буфер, который поток файлов сливает на диск.
- Пэды MPC Sample светятся в цвет, поэтому подсветка из MPC 3 переносится как есть.

### B.7 Looper

- **Вход:**
  - Input Source: стерео / моно или Resample L / R / L+R;
  - вставные эффекты, они записываются;
  - Mono / Stereo, Monitor, порог.
- **Запись:**
  - Rplc — перезапись;
  - Ovr — наложение каждым кругом;
  - Play, Rev — проигрывание задом наперёд;
  - Record To: Play или Overdub.
- **Синхронизация и длина:**
  - Seq Sync: старт с первого такта секвенции, Rev на конце круга;
  - Bars — длина лупа.
- **Выход и экспорт:**
  - Output Gain, Clear;
  - Export — в Audio-трек.

✅ Экспорт идёт в Audio-трек. Память: петля в 8 тактов при 90 BPM — около 21 с стерео, примерно 7,5 МБ.

### B.8 Auto Sampler

Сэмплирует внешний инструмент по MIDI в Sample-трек по нотам:
- **Источник:** Record From — вход или Resample.
- **Ноты:** Min / Max note, Note stride (сколько нот покрывает один сэмпл), Extend min/max.
- **Сила удара:** одна. В MPC 3 до 8 слоёв по силе удара, у нас один сэмпл на зону.
- **Длина:** Note length и Tail в секундах, Base name.
- **Луп:** Off / Forwards / Alternating / Reverse, Loop start / end, Crossfade в секундах, Linear / Equal Power.
- **Ход работы:** оценка длительности сеанса, прогресс, отмена; готовые сэмплы сохраняются.

✅ MIDI наружу и вход у нас есть. Плагин-источника нет, поэтому только внешние инструменты и Resample.

---

## C. Секвенсор: 16 треков, у каждого своя длина

### C.1 Что из этого следует для модели событий

Это главный вывод раздела: событие должно сразу хранить всё ниже, иначе формат придётся менять.

| Тип события | Поля |
|---|---|
| Нота | тик (960 на четверть), пэд или нота, сила удара, длина в тиках, **вероятность в %**, **ratchet** (число долей), **модификатор**: тип и значение |
| Типы модификаторов ноты | Filter (Cutoff, Reso, Env Amount), Envelope (Attack, Decay, Release), Sample (слой, слайс), Tuning (Coarse, Fine), Mix (Pan, Level). Режим на проект: Absolute (заменяет значение пэда) или Relative (смещение; для Level — множитель) |
| Автоматизация | параметр (трек, пэд или зона, вставной эффект, микшер, посылы 1–4), значение. Хранится как точки на линии времени |
| Mute / Solo | трека или пэда, вкл/выкл (если включена запись mute в автоматизацию) |
| Темп | новый BPM в точке секвенции (вкладка Tempo в List Edit) |
| MIDI | Aftertouch (полифонический), Channel Pressure, Pitch Bend, CC, Program Change — для входящего MIDI и для отправки наружу |

- **Темп меняется внутри секвенции.** Поэтому часы пересчитывают тики в кадры по карте темпа, а не по одному BPM.
- **Размер события:** около 24 Б вместо 16 Б, заложенных в архитектуре. Обновлено в [architecture.md](architecture.md) §5.
- **Сверка с форматом** (строки сериализатора в приложении `MPC Sample`, факт). Поля, которые штатная прошивка уже знает:
  - событие: `len`, `mod`, `modVal`, `prob`, `ratchet`, `muted` (у ноты свой mute), `selected`; регион звука: `reverse`, `fadeIn`, `fadeOut`, `sliceStart`, `sliceEnd`, `warpMarkers`;
  - клипы (у нас не используются, при записи пустые): `startPulses`, `endPulses`, `loopStartPulses`, `loopEndPulses`, `legato`, `colour`, `eventList`, `perClipParameterValues`;
  - секвенция: `lengthBars`, `lengthPulses`, `loopStartBar`, `loopEndBar`, `tempoEnable`, `transposition`, `smpteStart`, `timeSignatureTrack.timeSignatures[] {beatsPerBar, beatLength, barStart}`, `trackClipMaps`, `seqEventList`, `locators`, `autoSelectTrackIndex`;
  - трек: `velocityScale`, `muteGroup`, `lengthFollowsSequenceLength`, `arrangementClipMap`, `sharedClipMap`, `sequenceClipMap`, `skipFromRowLaunch`, `midiInputRoute`, `midiOutputRoute`;
  - проект: `masterTempoEnabled`, `masterTempo`, `quantiser {timeDivision, swingLevel}`, `arpeggiatorProperties`, `padPerformSettings`, `clipPlayerData`, `currentClipRow`, `sharedClipMatrixData`;
  - шаг песни: `clips`, `optionalTempo`, `optionalColour`, `optionalWhen`, `optionalAction`.
- **Размер такта может меняться по ходу секвенции** (`timeSignatureTrack` — список «с такта N»), как и темп.

### C.2 Секвенция

- **Поля:**
  - имя;
  - BPM — свой или общий (Seq / Global);
  - Bars — длина в тактах;
  - Loop: вкл/выкл, Start, End;
  - Transpose;
  - размер такта: 1–16 / 4, 8, 16, 32;
  - Auto-Select Track — какой трек выбрать при переходе на секвенцию.
- **Сколько:** до 128 секвенций. Сколько из них в шаблоне MPC Sample — в §2 архитектуры.
- **Операции:**
  - Erase на отрезке: All / Automation (выбранный параметр) / Note (выбранные пэды) / Except Note;
  - Clear;
  - Trim до длины;
  - Half / Double Length (с копией событий);
  - Insert Bars (сколько, перед каким тактом), Delete Bars (с–по);
  - Copy Sequence (с переименованием);
  - Copy Bars: откуда, с–по, куда, после какого такта, сколько копий; Replace или Merge;
  - Copy Events: последовательность, трек, отрезок, все или выбранные, куда, сколько копий; Replace или Merge;
  - Transpose на отрезке: в Drum — события одного пэда на другой, в Sample по нотам — диапазон нот и сдвиг;
  - Bounce to Sample;
  - Bounce to Audio-трек.
- ✅ всё.

### C.3 Трек в секвенции

- **Поля трека:**
  - Length в долях; Seq — как секвенция. Это и есть разная длина треков, например барабан на 1 такт и бас на 4;
  - Velocity 50–200 %;
  - Transpose;
  - Monitor, Rec Arm, Mute, Solo;
  - Key Range — какие входящие ноты принимает трек.
- **Операции:**
  - Copy Track — поверх другого;
  - Duplicate — с событиями и без;
  - Delete, Clear, Rename;
  - Double / Half Speed — растянуть или сжать события, не меняя высоту и темп;
  - Split Events — каждую ноту на 2–32 части, все или выбранные;
  - Reset Channel Strip;
  - Bounce to Sample, Bounce to Sample-трек.
- ✅ всё.

### C.4 Своя длина трека

Это и есть «свой секвенсор» у каждого из 16 треков. Клипов нет (решение 2026-10-23).

- **Length трека** — в долях. Значение Seq: трек идёт вместе с секвенцией. В формате это `lengthFollowsSequenceLength` и длина трека.
- **Короткий трек крутится по кругу** внутри секвенции. Например, барабан на 1 такт под басом на 4 такта.
- **Треки разной длины** расходятся и сходятся сами (полиметрия). Общий у всех только темп.
- **Где задаётся:** в настройках трека (Main Mode MPC 3) и в Step Sequencer (Track Length).
- **Velocity %** и **Transpose** трека — там же (§C.3).

✅

### C.5 Next Sequence

- **Пэды — секвенции:** пэд A01 — секвенция 1 и далее. Пустые пэды — неиспользуемые секвенции, текущая мигает зелёным.
- **Удар по пэду** назначает следующую секвенцию. Если ничего не выбрано, текущая повторяется.
- **Как переключать:**
  - на конце секвенции;
  - Next Bar — со следующего такта;
  - Sudden — сразу.
- **Кнопки:**
  - Hold — повторять текущую и не реагировать на пэды;
  - Clear — убрать секвенцию из списка.
- **Перенос в песню:** список можно скопировать в Song.

✅

### C.6 Song

- **Объём:** до 32 песен, в каждой до 999 шагов.
- **Шаг:** секвенция, сколько раз играет (1–999 или Hold — бесконечно), её темп, сколько тактов занимает.
- **Правка:**
  - Insert, Delete, Clear;
  - смена секвенции в шаге;
  - запись песни ударами по пэдам: Rec, затем пэд — шаг, повторный удар — +1 повтор.
- **Во время игры:** Next (со следующего такта), Sudden (сразу).
- **Темп:** Seq / Gbl.
- **Вывод:**
  - Convert → Seq: в выбранную секвенцию, приглушённые треки пропустить или включить;
  - Export → Audio Mixdown.

✅

### C.7 Arrange — линейная аранжировка

- **Запись:**
  - треки на линии времени, запись MIDI и звука;
  - MIDI Rec Erase — заменять или накладывать;
  - Loop с началом и концом: запись с Loop Start, после Loop End — наложение;
  - Auto Record: Off / In (Punch In) / Loop Start, точки Punch In и Out.
- **Навигация:**
  - 6 локаторов;
  - окно Locate — перейти к позиции.
- **Шины:** дорожки сабмиксов, возвратов и мастера — для их автоматизации.
- **Правка по рамке лупа:**
  - Cut, Copy;
  - Paste (Merge) или Paste (Insert со сдвигом);
  - Duplicate (рамка переезжает на копию);
  - Clear, Delete (такты вместе с содержимым);
  - Insert Blank Bars;
  - Copy Bars.
- **Операции над аранжировкой:** Erase, Transpose, Half / Double Length, Trim, Double / Half Speed.
- **Редактор трека внизу:** вкладки Grid и Auto; Select All, Cut, Copy, Paste, Delete, Duplicate.

🔧 Экран 320×240: один трек крупно или 16 полос по одной строке. Удобство проверим на устройстве.

### C.8 Timing Correct (TC)

- **Тип:**
  - Start — сдвинуть начало;
  - End — подогнать конец;
  - Length — длина кратна доле;
  - Legato — ноты тянутся до следующей.
- **Параметры:**
  - Time Division, с триолями;
  - Swing 50–75 %;
  - Shift Timing — сдвиг в тиках;
  - Window — какие события вокруг доли затрагивать;
  - Strength — насколько тянуть к доле.
- **Область:** все, выбранные или отрезок (с, по, пэды или ноты).
- **Где действует:**
  - глобальный TC при записи — вкл/выкл;
  - применить к треку или ко всем трекам;
  - к регионам звука — только Start.

✅ У MPC Sample свой короткий набор (Q, Shift, Swing) — подмножество.

### C.9 Метроном

- **Count-In:** Off / Record / Record + Play.
- **Enable:** Off / Play / Record / Record + Play.
- **Rate:** 1/4, 1/8, 1/16, 1/32, каждое и с триолью.
- **Звук:** в MPC 3 это Sidestick 1, Sidestick 2, Clap, Metroclick, Shake, Tambourine, MPC Click. 🔧 Звуки клика свои: сэмплы Akai не берём.
- **Громкость, выход.**

✅

### C.10 Track Mute, Pad Mute, Solo

- **Track Mute:**
  - пэды — треки;
  - приглушённые горят оранжевым, остальные жёлтым.
- **Event Mute и Audio Mute:**
  - Event — новые ноты не играют, звучащие дозвучивают;
  - Audio — глушит звук трека.
- **Квантование mute:** вкл/выкл, доля от 1/16 до 2 тактов — mute срабатывает на её границе.
- **Mute / Solo** переключателем, Clear Solos.
- **Track Group:** до 16 групп треков, один пэд глушит всю группу.
- **Настройки:**
  - свои mute у каждой секвенции;
  - mute пишется в автоматизацию;
  - mute попадает в Undo;
  - цвета треков на пэдах;
  - Solo — Single или Multi.
- **Pad Mute** (только Drum):
  - то же внутри трека;
  - приглушённые горят красным, остальные жёлтым — так же, как у MPC Sample;
  - до 16 групп пэдов;
  - то же квантование.

✅

### C.11 Автоматизация

- **Состояния:**
  - Read / Write / Off — глобально и у каждого трека, сабмикса, возврата, мастера;
  - глобальная кнопка выставляет всем одно состояние.
- **Запись:** в Write ручками и фейдером во время записи.
- **Правка:** в Grid (полоса автоматизации, рисование, стирание, удалить параметр), в List, в Step Sequencer.
- **Стирание:** Erase → Automation, выбранный параметр на отрезке.
- **Что автоматизируется:**
  - микшер: громкость, панорама, Mute, Solo, посылы 1–4;
  - параметры пэда или зоны;
  - вставные эффекты трека и пэда;
  - темп секвенции;
  - MIDI CC, Aftertouch, Pitch Bend, Channel Pressure, Program Change.

✅ Список целей берётся из общей таблицы параметров.

### C.12 Grid

- **Вид:**
  - Drum — пэды по вертикали;
  - Sample по нотам — клавиатура;
  - Audio-трек — волна.
- **Настройки:**
  - Snap: Absolute или Relative (сохранить смещение от доли);
  - Hitting Pad Selects All Events;
  - Auto-Scroll: Follow / Page / Off;
  - шаг курсора: 1/16 или доля TC.
- **Инструменты нот:**
  - рисовать, стирать, выделять, навигация;
  - Nudge — по сетке, или Don't Snap — по 4 тика;
  - Edit Start / End, Transpose;
  - Velocity и Automation для выделенного;
  - Select All, Copy, Cut, Paste, Delete, Duplicate;
  - Undo / Redo.
- **Полосы:** сила удара, автоматизация, **вероятность**, **ratchet**.
- **Регионы звука** (Audio-трек):
  - выделить, двигать, резать;
  - Edit Start;
  - Fade In / Out, Level, Reverse, Mute;
  - Warp: BPM, Semi, Fine;
  - Copy, Cut, Paste, Duplicate.
- **Меню Edit:**
  - Clear;
  - Double Length with Events, Double / Half Speed, Trim to Length;
  - Pitch Quantize, Humanize, Generate Random Events;
  - Split Note Events;
  - Convert to Progression;
  - Flatten, Bounce.
  - Описание этих операций — в §D.

🔧 Сенсора нет. Сделано (2026-10-24, `mode_roll.c`): Grid совмещён со Step Edit из руководства MPC Sample (SHIFT + SEQ). Курсор — энкодером, выбор — пэдом и −/+, сдвиг — фейдером, сила удара и длина — K3 и SHIFT + K3, масштаб — B2. Пока нет: выделения нескольких нот, Copy / Paste, Transpose, полос вероятности и ratchet, регионов звука.

### C.13 List Edit

- **Фильтр вида:** All, Notes, Aftertouch, MIDI Automation, Track Automation, Pitch Bend, CC, Program Change, Ch Pressure, Solo/Mute, Other (параметры пэдов).
- **Вкладка Tempo:** события смены темпа.
- **Колонки:** №, время (такт:доля:тик), тип, пэд или нота, длина в тиках, сила удара, тип модификатора, его значение, вероятность, ratchet.
- **Note Modifier Settings:** для групп Filter, Envelope, Sample, Tuning, Mix — Absolute или Relative; Make Default.
- **Настройки:**
  - Hitting Pad Selects All Events;
  - Auto-Advance on Step Record — после удара курсор сдвигается на долю TC;
  - Mod Type Selection — менять у выбранного или у всех;
  - Auto-Scroll.
- **Ввод:**
  - пошаговая запись: Rec, затем удары по пэдам;
  - Insert Event: время, тип (нота, автоматизация пэда, микшера, MIDI), параметр.
- **Правка:**
  - значение энкодером, длина и время по сетке TC с SHIFT;
  - сдвиг ноты по банкам с SHIFT;
  - Mute / Solo-событие вкл/выкл;
  - Nudge, Play, Delete.

✅ Таблица хорошо ложится на экран 320×240: около 10 строк мелким шрифтом.

### C.14 Step Sequencer

- **Поля:**
  - трек, длина трека (Seq = как секвенция), BPM Seq / Gbl;
  - сила удара по умолчанию;
  - Step Size (сколько шагов в такте, с триолями) — заодно меняет TC;
  - Auto-Scroll: Off / Page.
- **Ввод:**
  - выбрать пэд: Pad −/+ или Pad Sel (удержанием или с фиксацией);
  - выбрать такт: Bar −/+;
  - 16 пэдов — 16 шагов; при большем числе шагов — следующие банки;
  - пэды светятся цветом по силе удара.
- **Сила удара по шагам:**
  - у каждого шага ручкой; 0 удаляет шаг;
  - общий ползунок для всех шагов.
- **Операции над тактом:**
  - Nudge рисунка на шаг влево или вправо;
  - Flip Steps — пустые становятся нотами со силой 127, и наоборот;
  - Presets для силы удара: рост к 1, 2 или 4 пикам и спад; чередование 1/127 по половинам, четвертям и восьмым; горка или яма по такту, по половинам или по четвертям; поменять половины такта местами; инвертировать силу (сумма 127).
- **Step Automation:**
  - параметр на каждом шаге — рисовать или ручками;
  - Clear и Clear Bar;
  - Nudge;
  - общий ползунок;
  - Presets.

✅ Это самый «родной» режим для MPC Sample: 16 пэдов, 16 шагов.

---

## D. Инструменты исполнения и правки нот

### D.1 Pad Perform

Для Sample-трека по нотам.

- **Тип раскладки:**
  - Off;
  - Chromatic: пэды по полутонам, ноты лада подсвечены;
  - Notes: по ступеням лада;
  - Chords: аккорд от каждой ступени;
  - Chromatic Chords: любой тип аккорда вне лада;
  - Progressions: пэды — аккорды прогрессии;
  - Custom: своя карта нот, пресеты Chromatic C1, Chromatic C-2, Classic MPC.
- **Root Note и Octave;** пэд с тоникой подсвечен иначе.
- **Лады:**
  - мажор, натуральный и гармонический минор;
  - мажорная и минорная пентатоника;
  - блюзовый минор, фламенко, цыганский, венгерский цыганский, персидский;
  - мажорный бибоп, целотонный, хроматический;
  - лидийский, миксолидийский, эолийский, дорийский, локрийский, фригийский.
- **Аккорды:**
  - в Chords: 1-3-5, 1-4-5 (sus4), 1-2-5 (sus2), 1-3-5-7, 1-3-5-7b;
  - в Chromatic Chords: Major, Minor, Sus2, Sus4, Major7, Minor7, Major9, Minor9, Augmented, Diminished.
- **Банки:** Continuous (банк продолжает предыдущий) или Start on Root (каждый банк с тоники).
- **Velocity:** As Played или фиксированная — 12 … 93 %, Half, Full.
- **Filter Input** (входящие ноты): As Played / Snap to Scale / Filter to Scale.
- **Use Aftertouch.**
- **Быстрые клавиши в автономном MPC 3:** Notes, Chords, Progressions; Octave −/+; SHIFT + аккорд; SHIFT + Note −/+ (тоника на полутон).

✅ У нас вход в режим — SHIFT + 16 LEVELS, как у автономных MPC.

### D.2 Арпеджиатор

Для Sample-трека по нотам.

- **Включение:** Enable, Latch.
- **Action:**
  - Arp: Pattern, Octaves, Variation;
  - Note Repeat;
  - Rhythm: Rhythm Pattern;
  - Pattern: мелодическая фраза от одной нажатой ноты, транспонируется нотой.
- **Step Size** 1/1–1/64 с триолями, **Note Length** 1–100 %.
- **Velocity:** As Played, From First Note, Accented, Full, 3/4, Half, 1/4.
- **Swing** 50–75 %.
- **Latch Mode:** Reset или Add. Педаль удержания тоже фиксирует.
- **Probability:** Accent %, Normal %.
- **Модификаторы с внешнего MIDI:**
  - источники: Channel Pressure, Mod Wheel, Aftertouch, Expression;
  - что меняют: Time Division (+1–4 ступени), Octaves (+1–4), Note Length (+1–100 %), Swing (+1–50 %), Variation (+1–4), Probability (+1–100 %).

🔧 Списки рисунков (Pattern, Rhythm) — контент Akai, делаем свои. Модификаторы работают только с внешней MIDI-клавиатуры, кроме Aftertouch: его дают наши пэды.

### D.3 16 Level

Выбранный пэд копируется на все 16 пэдов. Параметр растёт от пэда 1 к пэду 16.

| Тип | Что меняется | Решение |
|---|---|---|
| Velocity | сила удара 7 → 127 | ✅ (есть в плане MPC Sample) |
| Tune | высота по ладу; Original Pad — где исходная высота, Scale | ✅ |
| Filter | Cutoff | ✅ |
| Layer | по 4 пэда на диапазон силы удара | ❌ один сэмпл на пэд |
| Slice | слайсы 1–16 | ✅ |
| Attack, Decay | огибающая громкости | ✅ |
| Probability | вероятность ноты | ✅ |
| Ratchet | число ratchet-долей | ✅ |
| Articulation | — | ❌ артикуляции вне плана |

### D.4 Правка нот

- **Humanize:**
  - время: Amount в тиках; Eagerness — минус играет раньше доли, плюс позже;
  - длина: Length %;
  - сила удара: Strength %;
  - только выбранные;
  - Apply или Do It.
- **Pitch Quantize:** Root Note, Scale, только выбранные, Start / End Note (ноты за пределами подтягиваются внутрь).
- **Generate Random Events:**
  - Event Type: Drum или Melodic;
  - Replace: все, в диапазоне нот, добавить к существующим;
  - Pattern Size в тактах, Note Length;
  - для Drum: Bank или Range (Start / End Pad), Density %, Rhythm Variation;
  - для Melodic: Start / End Note, Legato, Polyphony 1–8, Constrain to Scale, Density %, Root, Scale.
- **Convert to Progression:**
  - находит аккорды на треке;
  - Name, Root Note, Scale Type;
  - у каждого аккорда: имя, тип (Root / Normal / AboveRoot / BelowRoot), число нот;
  - результат сохраняется как прогрессия для Pad Perform.
- **Note Mapping:** у каждого пэда Drum-трека своя MIDI-нота, пресеты Classic MPC, Chromatic C-2, Chromatic C1. У Sample-трека по нотам вместо этого Pad Perform.
- **Pad Color:**
  - Display As: Off, Classic Velocity (жёлтый → красный), Fixed, Off → Velocity, Dim → Velocity, Bright → Velocity;
  - один пэд или все;
  - Empty Pads: off / dim / normal;
  - Pads Follow Track Color, Make Default.

✅ всё. Pad Color ложится прямо на RGB-пэды MPC Sample.

---

## E. Эффекты

### E.1 Устройство (MPC 3)

- **Вставки:** до **4** на каждом канале — пэд или зона, трек, сабмикс (8), возврат (4), мастер.
- **Входные вставки:** у Sampler и Looper своих 4 вставки на входе. Они записываются в звук и потом не снимаются.
- **Посылы:** **4** на пэде или зоне, треке и сабмиксе, в любые из 4 возвратов. Возврат идёт на мастер.
- **Внутри цепочки эффектов:**
  - эффект вкл/выкл; вся секция вставок вкл/выкл, и при включении восстанавливается прежнее состояние;
  - перенос и копирование эффекта между слотами вместе с параметрами.
- **Сохранение:**
  - свои пресеты у каждого эффекта;
  - **FX Rack** — сохранить и загрузить цепочку из 4 вставок.
- **Микширование в файл:** можно включать или исключать эффекты мастера и эффекты посылов.
- **У MPC Sample** поверх этого есть свои Pad FX, Knob FX и Flex Beat — они в §2 дорожной карты.

✅ всё. На каждом шаге тракта обработка идёт блоком по 128 кадров.

### E.2 Наш набор эффектов

Эффекты AIR — чужие плагины: ❌ их код и звук не берём. Основа нашего набора — классические эффекты MPC (без приставки AIR). У них простые и полностью описанные параметры. Реализация своя.

**Совместимость проектов.** Если в проекте MPC 3 стоит эффект AIR, мы показываем его имя и пропускаем звук мимо, а не ломаем проект.

Ниже: параметры, диапазон, значение по умолчанию. Dry/Wet у всех 0–100.

**Задержки:**

| Эффект | Параметры |
|---|---|
| Delay Mono | Time 2–2000 мс (100), Feedback 0–100 (25), Damping 0–100 (100) |
| Delay Mono Sync | Time 1 такт … 1/16T (1/4), Feedback (50), Damping (100) |
| Delay Stereo | как Delay Mono, стерео |
| Delay Sync (Stereo) | как Delay Mono Sync, стерео |
| Delay Ping Pong | Time Left, Time Right 2–2000 мс (100), Feedback (25), Damping (100) |
| Delay Multi-Tap | Time 1/2/3 2–2000 мс, Pan 1/2/3, Gain 1/2/3 (25), Feedback (25), Damping (100) |
| Delay Tape Sync | Time (1/4), Feedback (50), Ramp (50), Head 1–4 (100/0/0/0), Tone (50), Spread (50), Wow & Flutter (50) |
| Sample Delay | Left / Right 0–11025 сэмплов или 0–250 мс, Samples / ms, Link |

**Реверберации:**

| Эффект | Параметры |
|---|---|
| Reverb Small, Medium | Pre-Delay 1–100 (50), Early Reflection (50), Density (50), Diffuse (50), Decay (50), Lo-Cut (15), Hi-Cut (10) |
| Reverb Large, Large 2 | то же, Decay (75), Lo-Cut (10). Large 2 легче по CPU |
| Reverb In Gate | как Large + Gate In 0–100: обрезать хвост, когда вход ниже порога |
| Reverb Out Gate | как Large + Gate Out 0–100: обрезать хвост, когда выход ниже порога |

**Динамика:**

| Эффект | Параметры |
|---|---|
| Compressor Opto, VCA, Vintage | Input −6…+18 дБ (0), Attack, Release (50), Threshold −50…0 дБ (0), Ratio 1–20 (1), Knee 1–100 (1), Output −6…+24 дБ (0); Dry/Wet (100). Отличаются характером |
| Bus Compressor | Attack, Release, Threshold, Ratio, Oldskool вкл/выкл, Output |
| Transient Shaper | Attack, Release (50), Output (50) |
| Mother Ducker Input + Mother Ducker | приглушение по сигналу другого трека. Input на треке-источнике шлёт сигнал в шину 1–8; Ducker на треке-получателе с Threshold, Ratio, Attack, Release |

Ducker требует **шин боковой цепи (sidechain)** в звуковом тракте. Это нужно учесть в архитектуре сразу (§7).

**EQ и фильтры:**

| Эффект | Параметры |
|---|---|
| **PEQ 4-Band** (параметрический EQ) | Low Freq 22–1000 Гц (220), Freq 1 82–3900 (820), Freq 2 220–10000 (2200), High Freq 560–19999 (5600), Q1–Q4 0–100 (5), Gain 1–4 ±18 дБ (0) |
| PEQ 2-Band, 2-Shelf | те же частоты; две полки (Low / High Gain) и две полосы (Gain 1/2, Q1/Q2) |
| LP / HP Filter | Frequency 10–19999 Гц (1500), Resonance 0–100 (0) |
| LP / HP Shelving Filter | + Gain ±18 дБ |
| LP / HP Filter Sweep | Low / High Frequency 0–100, Resonance (33), Rate 0–100 (10) — LFO |
| LP / HP Filter Sync | то же, Rate 8 тактов … 1/32 (1/4) |

🔧 Своему EQ добавим срезы Low Cut и High Cut и выбор полки или колокола на крайних полосах. Этим отличается AIR Para EQ, но это обычные приёмы, а не чужой код.

**Искажения:**

| Эффект | Параметры |
|---|---|
| Distortion Overdrive | Drive, Tone, Output (50) |
| Distortion Amp | Drive, Tone, Dynamics, Output (50) |
| Distortion Fuzz | Drive, Output, Low, Low-Mid, High-Mid, High (50) |
| Distortion Grimey | Drive, Grime, Center, Width, Resonance, Output (50) |
| Distortion Custom | Drive (50), +Soft 5–75, +Clip 5–50 (25), −Soft, −Clip, Low / Mid / High ±18 дБ, Output |
| Decimator, Resampler | понижение разрядности и частоты дискретизации (Decim, BitReduc) |
| Frequency Shifter | Frequency −1000…1000 (0), Asynchrony 0–1000, A / B Pan, A / B Gain (75) |

**Модуляция:**

| Эффект | Параметры |
|---|---|
| Chorus 2-Voice, 4-Voice | Delay (20), Amount (80), Width (80), Feedback (50), Rate (10) |
| Flanger | Rate (10), Feedback −100…100 (0), Delay (20), Width (80) |
| Flanger Sync | то же, Rate 8 тактов … 1/16T (1/4) |
| Phaser 1, 2 | Rate (10) |
| Phaser Sync | Rate 1 такт … 1/16T (1/4) |
| Tremolo, Tremolo Sync | Rate (10 или 1/4), Sine to Square 0–100 (0) |
| Autopan, Autopan Sync | Rate |
| Auto Wah | Resonance (75), Attack (30), Release (30), Center (50), Sensitivity (50) |

**Бюджет CPU.** Простые эффекты (EQ, фильтры, искажения, задержки, хорус) стоят доли процента ядра A55. Реверберации и компрессоры с детектором дороже. Сколько эффектов выдерживает поток звука, измерим строкой CPU при выходе. При нехватке ядра перенесём рендер треков на остальные ядра (§7 архитектуры).

---

## F. MIDI, проект, экспорт

### F.1 MIDI Learn

- **Две карты:**
  - глобальная — назначения на все проекты;
  - проектная — хранится в проекте.
- **Поля назначения:**
  - Target;
  - Type: Toggle Button, Momentary Button, Fixed Button, Note, Abs CC, Rel CC Offset, Rel CC 2's Complement;
  - канал, номер ноты или CC;
  - Flip — обратная полярность.
- **Назначение:** Learn → выбрать параметр → двинуть контроллер. Ещё есть очистка, удаление и «очистить всё».
- **Карты:**
  - Factory и User;
  - новая, дубль, удалить;
  - экспорт и импорт файла `.xmm`.
- **Проектные цели:**
  - микшер: громкость, панорама, Mute, посылы треков, возвратов, сабмиксов и мастера;
  - параметры пэда: тюн, фильтр, огибающие, слои, LFO, сила удара;
  - параметры трека;
  - параметры вставных эффектов.

✅ Цели берём из общей таблицы параметров. Learn включается кнопкой, а не мышью.

### F.2 MIDI Control Mode

Прошивка работает как MIDI-контроллер для внешних устройств по TRS и USB.

- **Пэд** (на банк или Set All на все банки):
  - канал, нота или CC;
  - Velocity вкл/выкл;
  - Aftertouch: Off, Channel или Poly;
  - подсветка: Never, MIDI Input или Local.
- **Кнопка:** канал, CC, Momentary или Toggle, подсветка.
- **Ручки** (у нас K1–K3, MAIN VOLUME, фейдер, энкодер): канал, CC, Absolute или Relative, Low / High Range.
- **Карта** хранится в проекте.

✅ По FAQ Akai штатная прошивка контроллером для MPC 3 не работает. У нас это просто MIDI-гаджет.

### F.3 MIDI Monitor

Списки входящих и исходящих сообщений, Clear. ✅

### F.4 MMC (MIDI Machine Control)

- **Отправка:**
  - Rec и Overdub → Record Strobe, затем Record Exit;
  - Stop → Stop;
  - Play → Deferred Play;
  - Play Start → Locate Zero и Deferred Play;
  - перемещение позиции → Locate.
- **Приём:** Deferred Play, Locate Zero + Play, Stop, Record Strobe, Pause (как Stop), Locate.
- **Включение:** отдельно для отправки и приёма.

✅

### F.5 Синхронизация и порты MIDI

- **Порты:**
  - вход: Global (всё на текущий трек), Control (на MIDI Learn), Track (виден трекам);
  - выход: Sync (часы), Track (виден трекам).
- **Sync Receive:** Off, MIDI Clock, MTC. Ableton Link ❌ — нет сети.
- **Sync Output:** Off, MIDI Clock, MTC.
  - MTC Frame Rate: 24, 25, 30 drop, 30 non-drop;
  - Start Time — чч:мм:сс:кадры.
- **Прочее:**
  - Filter "All Notes Off";
  - Program Change переключает программу, секвенцию или трек.

✅ SYNC OUT (импульсы) — из руководства MPC Sample, §2 архитектуры. Штатный MPC 3 запрещает запись звука, пока принимает MIDI Clock. 🔧 Мы можем это разрешить: часы внешние, а звук пишется в нашем потоке.

### F.6 Настройки секвенсора

| Настройка | Значения |
|---|---|
| Track Mute State Per Sequence | вкл/выкл |
| Instant MIDI Track Mute | выкл — звучащие ноты дозвучивают; вкл — глушить сразу |
| Instant Audio Track Mute | то же для звука |
| Rec Arm | Single / Multi |
| Solo | Single / Multi |
| Record Track Mute Events; Track и Pad Mute Events Write Automation; Mute / Solo Undoable; Mute Uses Track Colors | вкл/выкл |
| Record Pad Aftertouch Events | вкл/выкл (наши пэды его дают) |
| Place Events Recorded During Count-In at Start Point | как у MPC3000 |
| Truncate Duration при записи | To Sequence Length / To Sequence End / As Played |
| Play Truncate Duration | As Recorded / Truncate Length |
| Display Resolution | PPQN только для отображения; внутри 960 |

✅

### F.7 Настройки проекта по умолчанию, загрузка и сохранение

- **По умолчанию для нового проекта:**
  - темп, глобальный темп вкл/выкл;
  - длина и луп секвенции, размер такта;
  - Pad Slice: Pad или All;
  - фильтр Drum по умолчанию;
  - Monitor для звука и для MIDI;
  - фиксированный цвет трека;
  - фиксированная сила удара пэдов и её значение.
- **New Project:** пустой, заводской или свой Auto Load; шаблон (Template).
- **Автосохранение:** вкл/выкл и интервал.
- **Временная папка:** место для записи.

✅ Пишем на SD или в `/data`. Это решение принимается при реализации шага 7.

### F.8 Звук и экспорт: общие настройки

- **Disk Streaming:**
  - вкл/выкл;
  - у каждого сэмпла: в памяти или с диска.
  - По умолчанию сэмплы Drum и Keygroup держатся в памяти, потому что их быстро и часто перезапускают.
- **Разрядность:** записи; Bounce — 16 или 24 бита.
- **Export Audio:** трек, программа или мастер; хвост; учитывать громкость и панораму.
- **Warp:** алгоритм по умолчанию; Audio Track Auto Warp — записанное сразу подгоняется под темп.
- **BPM:** автоопределение и диапазон.

✅ (Elastique ❌, §A.4)

### F.9 Project Info, Project Notes, Undo

- **Project Info — треки и секвенции:** дубль (с событиями и без), удаление, сохранение трека (`.xtp`), переименование, порядок.
- **Project Info — сэмплы:**
  - прослушать, переименовать, сохранить (`.wav`), открыть в Sample Edit;
  - удалить; Purge Unused — убрать неиспользуемые; удалить все;
  - Memory или Stream.
- **Project Notes:** Title, Artist, Original Artist, Album, Year, Genre, Copyright и свободный текст.
- **Undo History:** пронумерованный список действий; переход на любое состояние назад или вперёд; Undo и Redo с SHIFT.

✅ Undo — через журнал команд с обратными действиями, не снимками проекта: так экономнее по памяти (§10 архитектуры, шаг 7).

### F.10 Экспорт

**Audio Mixdown** (секвенция, а в Song — вся песня):
- Start / End Bar, Audio Tail;
- источник: стереовыход или Separate Tracks — каждый трек отдельно; пэды должны идти в трек;
- Main Inserts — включать эффекты мастера; Export Returns — с возвратами или без;
- Save as Project Preview;
- форматы:
  - WAV и AIFF — 8, 16, 24 бита или 32 с плавающей точкой;
  - FLAC — 16 или 24;
  - MP3 и OGG — 128–320 кбит/с;
- частота: 44.1, 48, 88.2, 96 кГц.

🔧 WAV, AIFF и FLAC пишем сами. MP3 и OGG требуют сторонних кодеров (LAME под LGPL, libvorbis под BSD), решим отдельно. Частоты, отличные от 44.1, — через свой ресемплер. Рендер идёт в потоке файлов, быстрее реального времени, если хватает CPU.

**Ableton Live Set** (`.als`):
- MIDI-треки как звук или как MIDI;
- громкость и панорама;
- без эффектов;
- хвост, разрядность, частота.

🔧 Формат `.als` — сжатый XML, нужно разобрать.

**MPC Sample File.** Это файл, который MPC 3 экспортирует из Drum-трека для MPC Sample:
- сэмплы рендерятся с эффектами пэда и с громкостью и панорамой, если выбрано;
- длина хвоста задаётся;
- ноты переходят в секвенцию.

⚪ Формат ещё не разбирали. Нам нужно его **читать**: это главный путь переноса китов из MPC 3 на наше устройство.

**Expansions:**
- на автономных MPC экспаншены лежат в папке `Expansions` в корне носителя;
- внутри — киты, программы (`.xpm`) и сэмплы.

🔧 Просмотр и загрузка китов с SD. Магазин Akai и онлайн ❌.

### F.11 Форматы файлов ради совместимости с MPC 3

| Формат | Что это | Нужно |
|---|---|---|
| `.xpj` + папка `[ProjectData]` | проект (сжатый JSON с заголовком ACVS) и его сэмплы | читать и писать |
| `.xpm` | программа или кит; в MPC 3.9 — JSON | читать и писать |
| `.xtp` | трек | читать и писать |
| `.wav` | сэмплы, запись, экспорт | читать и писать (есть) |
| MPC Sample File | кит из MPC 3 для MPC Sample | читать ⚪ |
| `.xmm` | карта MIDI Learn | читать и писать |
| `.progression` | прогрессия аккордов для Pad Perform | читать и писать |
| `.aiff`, `.flac` | экспорт | писать |
| `.als` | экспорт в Ableton Live | писать 🔧 |

---

## Вне плана: нашлось в MPC 3, не разбираем

Решим позже, нужны ли:
- **Клипы:** Clip Launch, Clip Matrix, Navigate, Clip Editor, New clip program (решение 2026-10-23).
- **Слои 2–8 на пэде** и Layer Play: Cycle, Velocity, Random, Crossfade (решение 2026-10-23).
- **Drum-пэд:** Drum FX (8 слотов на пэд), артикуляции (рудименты, роллы), Randomize, Mute Targets, Edit Zones, Partial Pad Presets.
- **Синтез:** осцилляторы вместо сэмпла в слое, Global LFO, огибающая Aux, Ramp, Note Counter, Drift.
- **Keygroup:** Unison и Harmonizer (KG Stack), портаменто, Pitch Bend, Controller Mod.
- **Обработка:** Create Stems — платный модуль на нейросети.
- **Треки:** Explode Track, Merge Tracks, Merge Pads, Copy Pads, MIDI-фильтр трека (диапазон силы удара, фильтр CC), Track View.
- **Экспорт:** Export as MIDI, Export as Pattern (`.mpcpattern`).
- **Live Control Mode** — управление Ableton Live по сети: сети нет.
- **Эффекты:** все AIR (плагины Akai), Touch FX и XYFX, ретро-эмуляции MPC60 / MPC3000 / SP1200, вокальные (Doubler, Harmonizer, Tuner), AIR Expander, Vintage Filter.
