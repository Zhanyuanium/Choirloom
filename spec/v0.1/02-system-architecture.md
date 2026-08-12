---
title: 系统设计概要（System Architecture Specification）
version: 0.1
status: Frozen Baseline
language: zh-CN
updated: 2026-08-12
---

# 系统设计概要 v0.1

## 1. 架构目标

系统必须同时满足：

- 对扫描合唱谱进行高质量、可人工校订的 OMR；
- 将“原谱音乐事实”“原稿视觉几何”“实际播放表现”“Jianpu 派生结果”严格分层；
- 支撑响应式原貌阅读，而不是把 PDF 页当最终 UI；
- 保证 Staff/Jianpu 在共享时间轴上严格对齐；
- 支撑本地 CPU/GPU/NPU 模型执行与未来远端 executor；
- 允许模型、渲染器、音频 backend 演进，而不破坏长期项目格式；
- 让 headless harness、训练服务器和 Windows GUI 共享同一核心规范。

## 2. 总体架构

```text
                         Windows Application
┌──────────────────────────────────────────────────────────┐
│ WinUI 3 / C#                                             │
│ Reader · Correction · Player · Model Manager · Settings  │
└───────────────────────┬──────────────────────────────────┘
                        │ thin interop / facade
                        ▼
┌──────────────────────────────────────────────────────────┐
│ Portable ScoreCore (C++20)                               │
│                                                          │
│ ScoreIR        GeometryGraph       PerformanceIR         │
│ JianpuIR       Validation          Project Domain        │
│ JianpuCompiler Layout Engines      Import/Export         │
└─────────────┬───────────────┬────────────────────────────┘
              │               │
              │               └──────────────┐
              ▼                              ▼
┌──────────────────────────┐      ┌────────────────────────┐
│ Recognition / Job Layer  │      │ Audio Layer            │
│ DAG · cache · revisions  │      │ Synth · SVS · Mixer    │
└─────────────┬────────────┘      └────────────────────────┘
              │ IPC
              ▼
┌──────────────────────────────────────────────────────────┐
│ Recognition Worker                                      │
│ Windows ML / ONNX · OCR · PDF/Image processing          │
└──────────────────────────────────────────────────────────┘
```

训练与研究工具独立存在：

```text
Python tooling
 ├─ dataset generation
 ├─ augmentation
 ├─ training / distillation / fine-tuning
 ├─ benchmark
 └─ ONNX export / model packaging
```

Python 不作为最终 Windows 应用运行时强制依赖。

## 3. 核心架构原则

### 3.1 Source Fact 与派生结果分离

ScoreIR 忠实描述原谱；用户练习设置不得破坏性修改原谱事实。

```text
Source Score
  + PitchTransform
  + TempoTransform / PlaybackProfile
  + PronunciationProfile
  + VocalizationProfile
          ↓
     Derived Views
```

例如用户把 C 大调整体升大二度：

- ScoreIR 中原来的 C4 仍是 C4；
- `PitchTransform = +M2`；
- Effective Score 得到 D4；
- Jianpu/Audio/SVS 可跟随 Effective Score；
- 原貌五线谱保持原稿。

### 3.2 语义、几何、表现、简谱四层分离

```text
ScoreIR         = what the score says
GeometryGraph   = where/how the source drew it
PerformanceIR   = how this playback instance unfolds
JianpuIR        = how ScoreIR maps into Jianpu semantics
```

任何一层不得成为其他层的隐藏私有真值。

### 3.3 不知道就保留，不装懂

识别层必须允许 Unknown/Opaque/Unsupported。响应式渲染失败的局部区域可退回 source crop。任何无法可靠解释的符号不得被强制映射为“最像的标签”。

### 3.4 Human Override 不可静默覆盖

人工确认/编辑优先级高于模型。重识别只生成建议与 conflict，不得静默覆盖 HumanVerified 数据。

## 4. ScoreIR

### 4.1 职责

ScoreIR 是平台无关、版本化的音乐语义模型。它不是 MusicXML AST，也不是 Jianpu AST。

### 4.2 核心层级

建议概念层级：

```text
Score
 ├─ Metadata
 ├─ Parts
 │   ├─ ChoralPart
 │   └─ AccompanimentPart
 ├─ Staves
 ├─ Voices
 ├─ PerformerRoles
 ├─ Measures
 ├─ Events
 ├─ Spanners
 ├─ LyricsLayers
 ├─ Key/Tempo/Time/Clef context
 └─ Repeat/Form structure
```

### 4.3 Part / Staff / Voice / PerformerRole 必须分离

- `Part`：逻辑乐谱部分；
- `Staff`：纸面五线谱；
- `Voice`：独立复调时间线；
- `PerformerRole`：真实演唱角色。

`PerformerRole` 是可扩展实体，不是固定枚举。可表达：S、S1、S2、A1、Choir II Bass、Solo 等。

同一齐唱事件可：

```text
performedBy = {S1, S2}
```

分声时再产生独立事件。

### 4.4 钢琴伴奏降低语义目标

ChoralPart 追求完整 voice/role/lyrics 语义。

AccompanimentPart 只要求足够播放：

- pitch；
- onset；
- duration；
- simultaneity/chord；
- tie；
- sustain/pedal；
- 主要 dynamic / tempo。

不要求恢复钢琴 logical voice、cross-staff voice continuity、指法或钢琴 Jianpu。

### 4.5 时间模型

Notation musical time 使用精确有理数，不能以浮点秒数作为原谱真值。

事件必须可表达：

- measure identity；
- offset within measure；
- exact duration；
- chord/same-onset relationship；
- tuplet ratio；
- beam group；
- grace relation。

### 4.6 Pitch 模型

至少区分：

- notated/written pitch spelling；
- accidental glyph/display status；
- sounding pitch；
- Effective/Transformed pitch（派生，不写回源事实）。

F# 与 Gb 不得仅保存为同一 MIDI 数字。

### 4.7 KeySignature 与 Tonality 分离

`KeySignature` 是谱面客观事实；`Tonality` 可为明确标注、算法推断或人工指定，并带 source/confidence。

Jianpu 的 NumberingBasis 依赖 Tonality/Profile，不得简单把 key signature 直接当 tonic。

### 4.8 Spanner

Tie、Slur、Hairpin、Ottava、Trill extension、Lyric extender 等必须是有类型的关系对象。Tie 影响持续时间；Slur 不得与 Tie 合并。

### 4.9 Grace / Ornament / Tremolo

ScoreIR 保存记谱语义，不在源层强制展开成实际演奏音列。展开/realization 属于 PerformanceIR。

### 4.10 Lyrics

歌词采用独立 underlay/layer：

```text
LyricsLayer
 ├─ language / variety
 ├─ appliesTo PerformerRole(s)
 └─ Syllable
      ├─ originalText
      ├─ event alignment
      ├─ hyphen/elision/melisma
      └─ optional pronunciation / IPA
```

歌词不强制“一条 Voice = 一条 Lyrics”。

### 4.11 Repeat/Form

ScoreIR 保存原始 repeat、ending、D.C./D.S./Segno/Coda/Fine，不破坏性展开。

### 4.12 Source fact 与 realization

`rit.`、fermata、dynamic、articulation 等源谱只保存其原始语义；具体 BPM 曲线、时长比例、velocity/length mapping 属于 PerformanceIR 或 backend policy。

## 5. GeometryGraph

### 5.1 职责

GeometryGraph 保存原稿视觉对象及其与 ScoreIR 的关联，服务于：

- OMR 校订；
- Source/Responsive 双向定位；
- 原貌响应式渲染；
- Role isolation；
- Jianpu 严格对齐；
- provenance；
- 人工几何修正。

### 5.2 双坐标系

必须保存：

1. Source Coordinate：原 PDF/扫描页坐标；
2. Canonical Coordinate：去倾斜/透视/弯曲后，以 staff-space 为主要单位的标准几何。

### 5.3 几何表示

根据对象类型支持：

- bbox；
- polygon；
- instance mask；
- centerline/control points；
- text baseline；
- source crop / fingerprint。

### 5.4 两类 Anchor

`VisualAnchor`：原稿 glyph 实际视觉位置。  
`RhythmicAnchor`：音乐时间列的严格位置。

Inline Jianpu 必须以 `RhythmicAnchor` 对齐。视觉错位排版不得传递到 Jianpu 时间轴。

### 5.5 几何编辑

修改 ScoreIR pitch 不意味着移动原图像素。只有“检测对象/关联选错”时才修改 Geometry link/anchor。高级 Geometry Correction 必须显式进入校订工具。

## 6. JianpuIR

### 6.1 命名

代码/API 统一使用 `Jianpu`。英文文档首次使用 `Jianpu (Numbered Musical Notation)`；`NMN` 仅作为别名。

### 6.2 管线

```text
ScoreIR
  + JianpuProfile
  + optional PitchTransform
        ↓
JianpuCompiler
        ↓
JianpuIR
  ├─ serializer / textual syntax
  └─ JianpuLayoutEngine
          ↓
       JianpuScene
          ↓
   platform renderer / export
```

Sparks 只作为语法设计、兼容与测试参考。

### 6.3 JianpuIR 语义

必须表达：

- scale degree；
- Jianpu accidental；
- octave dots/register；
- exact duration；
- rests；
- chord/polyphony/divisi；
- lyrics references；
- articulation / ornament；
- tie/slur/spanners；
- dynamics；
- PerformerRole；
- RhythmicAnchor relationship。

### 6.4 NumberingBasis

默认 `LaBasedRelativeMajor`，例如 A minor 主音为 6；`TonicAsOne` 作为可选 JianpuProfile。

### 6.5 Sounding pitch

Jianpu 八度由 sounding pitch 推导。Tenor 等特殊谱号不得根据 notehead 图像高度直接判断实际八度。

### 6.6 Layout

- Inline/Parallel 与 staff 共享同一 Responsive LayoutPlan；
- Jianpu Only 可独立 reflow；
- Full score 可 compact 多 voice；
- Performer practice 可按 Role 展开 lane；
- JianpuLayoutEngine 不依赖 WinUI。

## 7. Responsive Score DOM / Layout

### 7.1 Source View 与 Responsive View

Source View：忠实显示原 PDF/扫描页。  
Responsive View：以 ScoreFlow/ResponsiveSystem 组织，不保留永久电子页身份。

### 7.2 Hybrid facsimile rendering

响应式模式尽可能保留原稿 glyph/文字/符号外观；因为换行拓扑必然变化的 staff line、barline、slur、hairpin、ottava、歌词延音线等允许按 ScoreIR/GeometryGraph 重绘。

困难区域可作为 `ResponsiveUnsupportedRegion` 退回原始 system crop。

### 7.3 换行

默认只依据：

- viewport width；
- content scale；
- measure width/density；
- 基本排版约束。

语义优化断行（lyrics/spanner/phrase penalty）为可选高级策略，不作为默认。

普通情况下只在 measure boundary 换行。自由节拍/极端超长 measure 可局部 overflow/横向滚动。

### 7.4 不允许非等比扭曲 glyph

可整体等比缩放、调整 gutter、重绘线条；不得把原 notehead 横向拉伸/压缩来“塞满行”。

### 7.5 新行上下文

当新 ResponsiveSystem 从原 system 中间开始时，renderer 可生成 Display Decoration：当前 clef、key signature、必要时 time signature、role name、brace 等。它们必须标记为派生显示元素，不伪装成原稿像素。

### 7.6 Viewport navigation

- Continuous；
- horizontal Paged Flow；
- 半 viewport 推进；
- 宽屏双面板；
- Content Scale 与 Inspection Zoom 分离。

## 8. PerformanceIR

### 8.1 派生关系

```text
ScoreIR
 + PlaybackProfile
 + PitchTransform
 + PronunciationProfile
 + VocalizationProfile
         ↓
    PerformanceIR
```

PerformanceIR 可缓存、可丢弃重建，不是用户永久编辑的源音乐事实。

### 8.2 Performance instance

同一 ScoreIR measure 在反复中可生成：

```text
M18@Pass1
M18@Pass2
```

两者映射回同一 ScoreIR/Geometry 位置，但拥有不同实际播放实例身份。

### 8.3 两套时间

- MusicalTime：精确有理数；
- PlaybackTime：秒/sample，经过 tempo realization、fermata、变速等编译。

### 8.4 Tempo / Expression realization

rit./accel./fermata 不在 ScoreIR 伪造精确曲线。PlaybackProfile 可提供默认 realization，用户可覆盖，所有结果仅存在于派生层。

### 8.5 Track

- 每个 PerformerRole 逻辑独立 track；
- piano/accompaniment 独立 bus；
- Role 与实际 SVS singer/voice profile 解耦。

## 9. Pronunciation 与 SVS

### 9.1 Pronunciation pipeline

```text
LyricsText
  ↓
PronunciationProfile
  ↓
Abstract IPA / phoneme representation
  ↓
ModelPhonemeAdapter
  ↓
SVS backend
```

项目保存语言学目标发音，不把特定模型 phoneme ID 写回歌词真值。

### 9.2 PronunciationProfile

支持项目级 dialect/variety，例如客家话。映射应允许：

- syllable override；
- phrase mapping；
- word mapping；
- character mapping；
- context rules。

缺失映射默认进入 review，不静默回退普通话。

### 9.3 SVS segmentation

每个 PerformerRole 独立寻找切分边界，优先：

- 标点/歌词断句；
- 明确乐句；
- 较长休止；
- breath/pause cues。

不同 Role 的 segment 不要求对齐。普通 barline、PDF system break 不是天然边界。

应允许上下文重叠、中心有效区或其他模型特定连续机制减少拼接痕迹。

### 9.4 Timing

SVS 必须尽量 score-conditioned，接受 note pitch/timing/phoneme alignment。若 waveform 有 timing drift，保存 `SVSAlignmentMap`，确保 sample → PerformanceIR → ScoreIR → RhythmicAnchor 可追踪。

## 10. OMR Pipeline

### 10.1 阶段

```text
InputAsset
  ↓
Page normalization
  ↓
Layout analysis
  ↓
System / Staff / Measure geometry
  ↓
Symbol & instance geometry recognition
  ↓
Semantic decoding
  ↓
Structure solving
  ↓
Music validation
  ↓
ScoreIR + GeometryGraph + provenance
```

### 10.2 System-first

主要识别单元为 system/staff group，保留足够上下文；同时使用细粒度 geometry head 定位 symbol/mask。

### 10.3 AI 层次

默认：

1. 专用 Layout Model；
2. Music Vision Model；
3. compact semantic decoder；
4. constraint solver/validator；
5. 低置信度/冲突时 Rescue VLM。

Rescue VLM 没有 validator 豁免权。

### 10.4 Hard / Soft validation

Hard Constraint：结构不成立，产生 Error。  
Soft Constraint：罕见但可能合法，产生 Warning。

“音域异常”“和声罕见”不得自动改谱。

### 10.5 Canonical OMR Representation

模型不应直接以 MusicXML 作为唯一训练/解码目标。建议存在紧凑、可规范化、可 grammar-constrained 的 Canonical OMR 表示，并通过 geometry pointer 关联真实检测对象，再编译为 ScoreIR。

### 10.6 Recognition profiles

- `ChoralSemanticProfile`：完整合唱语义；
- `AccompanimentPlaybackProfile`：降低钢琴语义目标，以正确播放为中心。

## 11. Validator 与 Confidence

每个可疑事实可以保存字段级：

```text
value
source/model/version
confidence
validation status
human status
revision
```

ReviewPriority 建议：

```text
Uncertainty × MusicalImpact × PropagationRisk
```

这是一种排序思想，不要求固定为简单乘法公式；实现可通过可解释权重/规则逐步演化。

## 12. Project Storage

### 12.1 默认项目文件

默认使用单文件 SQLite 数据库作为 durable project repository，扩展名随最终产品名确定。

必须包含或可恢复：

- schema version；
- current ScoreIR/GeometryGraph state；
- source references；
- correction/revision history；
- recognition revisions；
- provenance；
- view/layout preferences；
- PronunciationProfile；
- project settings。

### 12.2 不是纯 Event Sourcing

读取使用当前 snapshot；Correction/Revision Log 用于 undo/redo、审计、模型迁移和解释。必要时建立 checkpoint。

### 12.3 Source reference

保存：

- path/URI；
- cryptographic hash；
- size/page fingerprint 等。

源文件丢失可重新定位。

### 12.4 打包归档

另提供 archive，包含 project DB + source files。日常项目默认不嵌入 PDF。

### 12.5 Cache

PDF tile、render cache、SVS audio、ONNX compiled cache 等默认放项目事实之外，可删除重建。

## 13. ModelPackage 与供应链边界

### 13.1 ModelPackage 内容

允许声明式资源：

```text
manifest.json
model.onnx
tokenizer / vocab
grammar / FSA
labels
normalization config
LICENSE
checksums / signature
```

不得从包中执行任意 Python、PowerShell、DLL、EXE 或脚本。

### 13.2 Manifest

至少包含：

- model ID/version；
- task/profile；
- API compatibility；
- ONNX/opset；
- input/output specification；
- license；
- hash/signature；
- recommended hardware/EP；
- approximate memory requirements；
- capability declaration。

### 13.3 Resolver

```text
LocalDirectoryResolver
ConfiguredMirrorResolver
HuggingFaceResolver
FutureResolver
```

OMR engine 不依赖具体下载服务。

### 13.4 Trust

UI 区分 Official / Community / Unverified。官方包应签名并校验 hash。

## 14. Job Engine

### 14.1 DAG

重任务以 versioned DAG 表达，每阶段 cache key 至少考虑：

- input fingerprint；
- algorithm version；
- model version；
- relevant configuration。

更改 JianpuProfile 不得导致 OMR 重跑；更换 staff detector 应使其依赖下游失效。

### 14.2 Job 能力

必须支持：

- cancel；
- resume；
- stage retry；
- region-level rerun；
- checkpoint；
- revision comparison。

## 15. 进程边界

重 OMR/AI worker 与 WinUI host 分离，通过 IPC 交互。目的：

- UI 不被模型崩溃拖死；
- 更好管理 GPU/NPU context 和内存；
- 可靠取消；
- 后续多 worker / remote executor 扩展。

实时 audio engine 可采用独立低延迟设计，不强制与 Recognition Worker 相同边界。

## 16. Inference abstraction

建议接口概念：

```text
IInferenceBackend
IRecognitionExecutor
IModelResolver
IAudioRenderer
ISingingRenderer
IScoreImporter
IScoreExporter
```

v1 不稳定通用 native plugin ABI；先保证内部接口与公开 schema 开放。第三方 native 插件机制后置。

Windows 首发执行基座：Windows ML / ONNX 生态，按运行时能力选择 CPU/GPU/NPU。核心算法不得硬编码某一代 Intel 型号。

## 17. 远端执行预留

接口不能以 `D:\path\score.pdf` 作为核心契约，应使用 `InputAsset / ContentIdentity / JobRequest`。Local Executor 可读取本机；未来 Remote Executor 自行负责传输。

远端执行永远需要用户显式选择。

## 18. MusicXML

MusicXML 是标准互操作格式，不是内部唯一真值。

- import → normalized ScoreIR；
- export ← ScoreIR；
- Layout/Geometry 不依赖 MusicXML 能完全复现原 PDF；
- 钢琴 synthetic voice assignment 在 export metadata/说明中标记为非语义恢复。

## 19. 公共格式

应公开并版本化：

- ScoreIR schema/spec；
- GeometryGraph schema/spec；
- JianpuIR schema/spec；
- ModelPackage spec；
- Canonical OMR representation（达到稳定后）；
- 项目 archive manifest。

project SQLite 的内部索引/cache 表不承诺成为公共 API。第三方应通过 schema/core library 访问。

## 20. Monorepo 建议

```text
/apps
  /windows

/core
  /score
  /geometry
  /performance
  /jianpu
  /layout
  /validation

/io
  /musicxml
  /pdf
  /project

/recognition
  /contracts
  /pipeline
  /windows-ml

/audio
  /performance-compiler
  /synth
  /svs

/platform
  /windows

/tools
  /cli
  /harness
  /dataset
  /benchmark

/models
  /schemas

/spec
/tests
  /unit
  /golden
  /visual
  /corpus
```

## 21. 核心实现技术

- Portable Core：C++20；
- 对外长期边界：稳定 C ABI / schema；
- Windows host：C# + WinUI 3；
- Windows-specific facade 可使用 WinRT；
- Training/research：Python；
- Production model：优先 ONNX；
- Project durable store：SQLite；
- Rendering：platform-neutral scene + Windows renderer；
- 音频编码：WAV/FLAC，v1 后段 AAC-LC/M4A。

## 22. 关键不变量清单

实现评审时必须检查：

1. ScoreIR 不因用户练习变调/变速而被改写；
2. MusicXML/Sparks/WinUI/某个模型都不能成为核心数据模型；
3. Inline Jianpu 永远基于 RhythmicAnchor；
4. Resize/Reflow 不改变 ScoreIR identity；
5. HumanVerified 不被模型静默覆盖；
6. Piano 不被迫恢复不需要的 logical voice；
7. ModelPackage 不执行任意下载代码；
8. 已安装模型后全流程可离线；
9. Remote backend 不可静默上传；
10. Unsupported/Opaque 是合法状态；
11. Cache 可删，项目事实仍完整；
12. Source View 与 Responsive View 通过 GeometryGraph 可逆定位。
