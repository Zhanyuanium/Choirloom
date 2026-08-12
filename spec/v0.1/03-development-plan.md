---
title: 开发计划与 Harness Roadmap
version: 0.1
status: Frozen Baseline
language: zh-CN
updated: 2026-08-12
---

# 开发计划与 Harness Roadmap v0.1

## 1. 开发方法

本项目采用 **schema-first + harness-first + vertical-slice** 方法，而不是“所有模块先做 30%”。

核心原则：

- 先证明数据模型和确定性转换正确，再依赖 OMR；
- OMR 与 UI 可以并行，但不得互相定义私有真值；
- 每个阶段都有可执行输入、可检查输出和明确退出条件；
- 所有模型迭代必须通过 Curated Real Gold 与 Synthetic Gold 回归；
- 人工修订成本是模型优化的一等指标。

## 2. 工作流分层

### 2.1 产品工作流

```text
Import
 → Recognition Draft
 → Correction / Review
 → ScoreIR + GeometryGraph
 → Responsive Reader / Jianpu
 → PerformanceIR
 → Synth / SVS
 → Export / Share
```

### 2.2 模型工作流

```text
Corpus / Synthetic generator
 → training
 → evaluation
 → ONNX export
 → ModelPackage
 → Windows local benchmark
 → application rollout
```

## 3. M0 — Architecture & Corpus Foundation

### 目标

冻结并实现最小可运行规范骨架，建立后续所有工作的公共语言。

### 交付物

- monorepo；
- CI；
- `/spec` 文档；
- ScoreIR v0.1 schema；
- GeometryGraph v0.1 schema；
- JianpuIR v0.1 schema 草案；
- PerformanceIR v0.1 schema 草案；
- ModelPackage v0.1；
- Project SQLite v0.1 migration framework；
- stable ID / revision primitives；
- CLI/harness skeleton；
- Synthetic Gold generator skeleton；
- Curated Real Gold 数据管理规范；
- WinUI shell；
- C++ Core ↔ Windows host interop 骨架。

### Harness

必须能：

- 读取一个最小 ScoreIR fixture；
- schema validate；
- round-trip serialize；
- 创建/迁移项目 DB；
- 比较两个 ScoreIR revision；
- 在 CI 中运行 golden fixtures。

### 退出条件

- schema 已有版本号与 migration policy；
- 核心对象 ID 保存/读取稳定；
- 项目 crash-safe transaction 基础验证通过；
- headless harness 与 WinUI host 都能加载同一 fixture；
- 测试目录和 corpus licensing metadata 可用。

## 4. M1 — Score Core & Jianpu Deterministic Stack

### 目标

在完全不依赖 OMR 的情况下证明：给出正确结构化乐谱，本项目能够正确完成音乐语义、Jianpu、基本导出。

### 交付物

- MusicXML import → ScoreIR；
- ScoreIR validator；
- PerformerRole / divisi；
- lyrics/melisma；
- repeat/form representation；
- Source/Effective Score + PitchTransform；
- JianpuCompiler；
- JianpuIR；
- JianpuLayoutEngine 初版；
- platform-neutral JianpuScene；
- Windows Jianpu renderer；
- Jianpu Only view；
- MusicXML export baseline；
- Jianpu vector export baseline。

### 测试重点

- rational time；
- pitch spelling/sounding pitch；
- tenor octave；
- accidentals；
- la-based minor / tonic-as-1；
- mid-score key changes；
- tuplets；
- grace/ornament semantic preservation；
- S1/S2/A1/A2；
- lyrics alignment；
- repeat graph；
- ScoreIR → JianpuIR exact fixtures。

### 退出条件

- 结构化 gold corpus 的 Jianpu 转换可由自动测试验证；
- `ScoreIR pitch/time → JianpuIR` 不依赖截图人工判断；
- MusicXML round-trip 不丢失 v1 目标语义中的关键字段；
- Sparks 不作为运行时依赖。

## 5. M2 — GeometryGraph & Responsive Reader

### 目标

证明“原稿视觉 + 结构化音乐语义”可以形成设备无关响应式阅读，而不是 PDF 页面查看器。

### 前置输入

使用人工/ground-truth GeometryGraph，不等待成熟 OMR。

### 交付物

- Source View；
- Responsive Score View；
- hybrid facsimile renderer；
- Canonical staff-space geometry；
- ScoreIR ↔ GeometryGraph 双向链接；
- VisualAnchor / RhythmicAnchor；
- responsive measure packing；
- Continuously scrolling；
- horizontal Paged Flow / half viewport；
- dual-panel wide layout；
- Content Scale / Inspection Zoom；
- Full/Section/Performer focus；
- Highlight/Dim/Solo；
- Inline Jianpu；
- Parallel Staff + Jianpu；
- Source/Responsive 双向定位；
- fallback source crop；
- forced/preferred/avoid break；
- optional semantic line-breaking prototype。

### 关键测试

- resize invariants；
- RhythmicAnchor strict alignment；
- barline strict alignment；
- user selection/loop identity after reflow；
- source provenance round-trip；
- local unsupported region fallback；
- touch and mouse navigation。

### 退出条件

- Inline Jianpu 无逻辑 X 偏差；
- 同一项目在多种 viewport 宽度下稳定 reflow；
- resize 不改变 ScoreIR identity；
- Source/Responsive 双向定位可靠；
- 普通滚动/缩放达到 60 fps 级体验目标的初步基线。

## 6. M3 — OMR Baseline & Correction Loop

### 目标

形成第一个完整的：

```text
PDF → Draft ScoreIR/GeometryGraph → Human Correction → Reviewed
```

### 交付物

- page normalization；
- layout/system/staff/measure detector；
- symbol/instance geometry baseline；
- semantic decoder baseline；
- structure solver；
- hard/soft validator；
- provenance/confidence；
- Unknown/Opaque；
- Correction Mode；
- insert/delete/relink；
- Review Queue；
- Draft/Reviewed/Verified；
- region-level rerun；
- model revision / human override conflict；
- ChoralSemanticProfile；
- AccompanimentPlaybackProfile baseline。

### 校订器优先级

第一版不要追求 MuseScore 式自由创作；必须优先让下列错误可快速修：

1. pitch；
2. duration/onset；
3. measure/barline；
4. voice/PerformerRole；
5. lyrics/melisma；
6. repeat/form；
7. tie/slur/spanner；
8. geometry association；
9. missing/false-positive symbol。

### 退出条件

- 真实扫描谱可以从导入走到 Reviewed；
- HumanVerified 重跑后不被覆盖；
- Hard Error 为 0 才能进入 Reviewed；
- 钢琴不依赖完整 voice topology 就可生成伴奏事件；
- harness 能输出字段级 OMR 指标与 corrections/page。

## 7. M4 — Professional OMR Loop

### 目标

将“可工作 baseline”提升为实际排练可接受的低人工成本 OMR。

### 模型路线

建议并行研究：

- score-specific vision backbone；
- layout detector；
- symbol/dense geometry head；
- staff/system semantic decoder；
- grammar-constrained decoding；
- compact specialized models；
- Rescue VLM 仅用于冲突/低置信度区域；
- teacher/student distillation；
- Windows ONNX inference profiling。

### Synthetic Gold

合成器应覆盖：

- SATB / SSA / TTBB；
- SATB + piano；
- S1/S2 等 divisi；
- 同 staff 多 voice；
- 中文/英文多 verse；
- IPA layer；
- melisma；
- key/time/tempo changes；
- tuplets / grace；
- ornaments；
- dynamics / hairpins；
- repeats / endings / D.S./Coda；
- breath / fermata；
- diverse fonts / spacing / renderers。

扫描退化：

- skew；
- blur；
- low contrast；
- noise；
- JPEG；
- resolution loss；
- page warp；
- partial shadow；
- crop / margin variation。

### Active-learning loop

人工 correction 可导出为本地训练样本：

```text
source crop
model prediction
human correction
geometry
model version
```

默认不上传。

### 核心指标

- Critical corrections/page；
- Major corrections/page；
- fields F1；
- exact-measure；
- exact-system；
- review time proxy；
- NPU/GPU/CPU memory/throughput。

### 退出条件

不预先拍脑袋写死某个 99.x%。发布门槛在 Curated Real Gold 足够后冻结，并必须包含**人工修订成本**而非只有 token accuracy。

## 8. M5 — Rehearsal Audio

### 目标

建立确定性、可严格同步的排练播放器。

### 交付物

- PerformanceIR compiler；
- repeat/ending/D.S./Coda expansion；
- performance instance identity；
- tempo realization；
- fermata/rit profile；
- simple synth；
- piano accompaniment bus；
- per-Role track/mixer；
- role solo/mute/volume；
- speed / transpose；
- A-B loop / measure loop；
- Count-in；
- Metronome；
- Performance Mode；
- Rehearsal Range Mode；
- cursor/sample → event → RhythmicAnchor sync；
- WAV/FLAC export baseline。

### 退出条件

- 反复第二遍能正确回到同一谱面但保持 Pass identity；
- seek/loop/reflow 后同步不漂移；
- audio thread 不受 reader layout 卡顿影响；
- 钢琴伴奏能整体播放且无需完整钢琴 voice 解析。

## 9. M6 — Natural Singing

### 目标

实现可靠、自然、适合排练的 SVS；不追求专业真人歌唱表现力。

### 交付物

- PronunciationProfile；
- Mandarin/English baseline；
- custom dialect/IPA mapping；
- ModelPhonemeAdapter；
- la / ah / movable-do solfege / original lyrics；
- per-Role segment planner；
- lyric punctuation / phrase-aware segmentation；
- overlap/context cache；
- SVS backend interface；
- waveform alignment map；
- high-quality time-stretch；
- per-Role cache invalidation；
- mixer integration。

### 特别验收

- 每个 Role 可独立按自身歌词断句切分；
- 不因 SATB 其他声部断句不同而强行共用边界；
- 不支持 phoneme 必须报告；
- missing dialect mapping 不静默普通话化；
- 改一个局部 correction 只失效相关音频 segment；
- score sync 优先于“听起来更像真人”。

## 10. M7 — v1 Hardening & Release

### 目标

把技术栈打磨成可长期日常使用的 Windows 开源产品。

### 交付物

- Project migration hardening；
- model manager UX；
- official signatures/checksums；
- community/unverified model flow；
- source relocation；
- project archive；
- PDF export（Jianpu Only / Parallel）；
- MusicXML import/export hardening；
- WAV/FLAC；
- AAC-LC/M4A v1 后段；
- natural singing mixer export；
- localization zh-CN/en；
- accessibility；
- keyboard/touch polish；
- crash recovery；
- privacy review；
- installer/package/signing；
- public schema docs；
- user/developer docs。

### 退出条件

- v1 Scope Contract 全部达标；
- migration fixtures 覆盖历史 schema；
- Curated Real Gold 发布门通过；
- offline end-to-end test 通过；
- no-telemetry/privacy test 通过；
- long-project reopen/cancel/resume/recovery 通过；
- export round-trip/golden test 通过。

## 11. 建议并行工作流

### Track A — Core / Schema

M0 → M1 → M2 全程主导核心不变量。

### Track B — Recognition / ML

M0 建 corpus → M3 baseline → M4 professional loop。

### Track C — Audio

M1 中建立基本 time semantics 后可提前开始 M5；M6 依赖 Pronunciation 与 PerformanceIR 稳定。

### Track D — Windows UX

WinUI shell 从 M0 开始；M2 完成主 reader；M3 加 Correction；M5 加 Transport；M7 hardening。

### Track E — Harness / QA

从 M0 开始常驻，不是最后阶段测试工作。

## 12. Harness 体系

### 12.1 Unit

- rational time；
- pitch spelling；
- repeat graph；
- Jianpu conversion；
- Pronunciation precedence；
- schema migrations。

### 12.2 Golden semantic

- ScoreIR JSON/schema；
- GeometryGraph；
- JianpuIR；
- PerformanceIR。

### 12.3 Visual

- JianpuScene rendering；
- responsive layout snapshots；
- source/inline overlay alignment；
- export PDF。

### 12.4 Corpus benchmark

- Synthetic Gold；
- Curated Real Gold；
- Adversarial Edge。

### 12.5 End-to-end

```text
PDF
 → recognition
 → correction fixtures
 → ScoreIR/GeometryGraph
 → responsive/jianpu
 → playback
 → export
```

## 13. Definition of Done（功能级）

一个功能不得仅因为“UI 看起来能用”就完成。至少满足：

1. 数据归属层明确；
2. schema/API 有测试；
3. error/unknown path 明确；
4. undo/revision/migration 影响已考虑；
5. offline path 可用；
6. harness 有最小 fixture；
7. 不破坏核心不变量；
8. 若是 AI 功能，有 model/version/provenance；
9. 若是布局功能，有 resize/reflow test；
10. 若是音频功能，有 timing/sync test。

## 14. 高风险项与缓解

### RISK-1 复杂复调 OMR

**风险**：voice/时值/关系恢复错误。  
**缓解**：specialized models + grammar solver + validator + Correction Mode + synthetic corpus。

### RISK-2 响应式原貌重建

**风险**：跨行 spanner/共享 glyph 难以重建。  
**缓解**：hybrid facsimile；必要线条重绘；unsupported region source crop fallback。

### RISK-3 Jianpu 专业排版复杂度

**风险**：依赖第三方语法会形成能力上限。  
**缓解**：自有 JianpuIR/LayoutEngine，Sparks 仅作参考。

### RISK-4 SVS 分段接缝

**风险**：逐块生成不自然。  
**缓解**：每 Role 自己的歌词/乐句边界、上下文重叠、cache invalidation、time-stretch。

### RISK-5 模型许可证/数据许可证

**风险**：开源代码被权重或数据 NC 条款限制。  
**缓解**：代码 Apache-2.0；模型/数据独立清单；默认官方模型优先可商业使用和再分发；NC 仅实验/用户自行安装。

### RISK-6 过早插件化

**风险**：稳定 ABI、安全和兼容负担。  
**缓解**：先开放 schema/接口，不在 v1 承诺通用 native plugin system。

## 15. Release Gate 摘要

| Gate | 核心问题 |
|---|---|
| M0 | 我们是否拥有稳定公共语言和 harness？ |
| M1 | 正确数字乐谱能否确定性得到正确 Jianpu？ |
| M2 | 原稿是否真正脱离 PDF 分页实现响应式阅读与严格对齐？ |
| M3 | 真实 PDF 是否能进入可用 Correction Loop？ |
| M4 | 人工校订成本是否下降到日常可接受范围？ |
| M5 | 排练播放是否正确、可控、严格同步？ |
| M6 | 自然模唱是否可靠、清楚、可用于练习？ |
| M7 | 是否达到长期维护、离线、迁移、导出、隐私和发布要求？ |

