---
title: 决策记录（Decision Register）
version: 0.1
status: Frozen Baseline
language: zh-CN
updated: 2026-08-12
---

# 决策记录 v0.1

本文记录需求确认阶段冻结的 D / M / G / O / P / J / R / A / V 决策。目的不是重复所有实现细节，而是确保未来修改时知道“为什么这样设计”。

> 变更规则：若未来改变本文任一 Frozen 决策，应新增 ADR，说明旧决策、变更原因、迁移影响和兼容策略；不得只改代码。

---

# D — 产品与总体设计

| ID | 决策 | 主要理由 |
|---|---|---|
| D1 | 响应式模式采用 hybrid facsimile：尽量保留原 glyph，换行后必须变化的 staff/barline/slur/hairpin 等允许重绘；Source View 永远保留 100% 原稿。 | 纯像素裁切无法同时支持自由 reflow 与跨行长记号。 |
| D2 | 普通情况下只在 measure boundary 重新换行。 | 保持音乐阅读的稳定最小重排单元。 |
| D3 | Staff 与 Jianpu 使用共享音乐坐标；barline 和 rhythmic time 必须严格一致。 | “严格对齐”必须成为可测试不变量，而不是视觉近似。 |
| D4 | 同 staff 多 voice 提供 Highlight / Dim / Solo；共享 glyph 在 Solo 中保留，不伪造像素。 | 共享 notehead 等物理上没有唯一可分割解。 |
| D5 | 钢琴伴奏进入 ScoreIR/播放，但 v1 不生成 Jianpu；后续进一步简化为不恢复复杂 logical voice。 | 目标用户是声乐成员/临时指挥，钢琴主要承担伴奏功能。 |
| D6 | Correction-first editor，不做 MuseScore 级完整作曲软件。 | 控制 v1 范围，把工程投入放在 OMR 校订效率。 |
| D7 | 原扫描上的手写批注/盖章等进入 Source Annotation Layer，默认不解释为音乐语义。 | 降低手写内容误识别为乐谱符号的风险。 |
| D8 | 用户变调采用非破坏性 PitchTransform；默认原谱模式，显式“变调练习模式”可让 Jianpu/音频跟随新调，原貌五线谱不改变。 | 保持 Source Score 真值和练习表现分离。 |
| D9 | ScoreIR 保存实际 pitch/tonality；Jianpu 的 1–7 由可配置 profile 派生。 | 简谱习惯不能污染 OMR 真值。 |
| D10 | 歌词原文、syllable mapping、语言和发音/IPA 分层保存。 | 显示文本与 SVS 发音需求不同。 |
| D11 | 默认项目不嵌入源 PDF；可选打包归档；外部源丢失可重新定位验证。 | 减小日常项目体积，同时保留归档能力。 |
| D12 | 官方默认模型优先使用可商业使用/再分发许可；NC 模型只作为实验/用户自行安装选项。 | 避免开源项目被模型权重许可证锁死。 |

---

# M — ScoreIR 音乐语义

| ID | 决策 | 主要理由 |
|---|---|---|
| M1 | 音乐时间使用精确有理数。 | tuplets/复调不能依赖浮点误差。 |
| M2 | Part / Staff / Voice / PerformerRole 四概念分离。 | 合唱谱的视觉 staff、复调 voice、演唱角色不是同一概念。 |
| M3 | Voice 可临时开始/结束；PerformerRole 可任意扩展到 S1/S2/A1/A2…；齐唱事件可属于多个 Role。 | 支持复杂 divisi 与 unison。 |
| M4 | 不为钢琴恢复复杂 cross-staff / logical voice；只需可整体正确播放。 | 不是钢琴教学/制谱工具，降低无价值复杂度。 |
| M5 | Pitch 保存拼写、八度、实际 sounding pitch；显示 accidental 单独保存。 | F# 与 Gb、实际音高与纸面升降号不能混为一体。 |
| M6 | KeySignature 与 Tonality 分离。 | 调号不等于自动确定 tonic/mode，且直接影响 Jianpu NumberingBasis。 |
| M7 | Tie / Slur / Hairpin / Ottava 等 typed spanner 分离。 | 视觉相似不代表音乐语义相同。 |
| M8 | Grace/ornament/tremolo 保存原记谱语义，实际展开交由 PerformanceIR。 | 演奏 realization 并非源谱确定事实。 |
| M9 | Tuplet、beam、chord、duration 分离。 | 时间、视觉分组和同时发声是不同关系。 |
| M10 | Lyrics 是独立 underlay layer；支持中文方言 PronunciationProfile（如客家话）覆盖默认普通话。 | 多语言、多 verse、方言发音和 melisma 需要独立模型。 |
| M11 | Repeat/ending/D.S./Coda 在 ScoreIR 不展开，由 PerformanceIR 编译路线。 | 保留原谱结构并支持第二遍实例身份。 |
| M12 | 明确 tempo 与 rit./a tempo 等语义指令分离。 | 原谱通常没有精确 rit 曲线。 |
| M13 | Dynamic/articulation 原语义与后端 velocity/length 映射分离。 | 不绑定具体合成器。 |
| M14 | 任意文本必须原样保存，能理解再附 semantic tag。 | “不认识”不能等于丢失。 |
| M15 | 不确定性可细到字段级，保存 provenance/confidence；人工可 verified。 | 支持可解释校订和模型比较。 |
| M16 | cue-size/parenthesized/cautionary/editorial 等 notation flag 保留。 | 原稿意义和视觉校订需要。 |
| M17 | 自由节拍 v1 可保存/人工编辑/显示，不以自动 OMR 为发布门槛。 | 数据模型成本低，但自动识别研究范围过大。 |
| M18 | 非标准扩展记号无法规范化时保留 Opaque/Unsupported。 | 正确性优先，禁止 AI 装懂。 |

---

# G — GeometryGraph / 对齐

| ID | 决策 | 主要理由 |
|---|---|---|
| G1 | 同时保存 Source Coordinate 与 Canonical staff-space coordinate。 | 既能追溯原图，又能跨 DPI/透视统一布局。 |
| G2 | Geometry 支持 bbox/polygon/mask/curve 等，而非只有矩形框。 | 乐谱有大量长曲线、重叠符号。 |
| G3 | 普通 measure 不从中间断行；极端 measure 可 overflow。 | 保持阅读语义稳定。 |
| G4 | 原 PDF page/system break 只作 provenance，不支配响应式布局。 | 目标是电子响应式阅读。 |
| G5 | 不允许非等比扭曲原 glyph。 | 保持原貌，避免 notehead 变形。 |
| G6 | 新 ResponsiveSystem 可补充派生 clef/key/time/role/brace 等 Display Decoration。 | 原 system 中间断开后仍需可读上下文。 |
| G7 | 跨新行 spanner 按语义重绘；必要时避免/退回 source crop。 | 原始长线无法简单裁切复用。 |
| G8 | Inline Jianpu 统一按 RhythmicAnchor 对齐，VisualAnchor 仅描述原视觉位置。 | 同 onset notehead 可能视觉错位。 |
| G9 | 改 pitch 不移动原图；只有 geometry/link 错误才修几何关联。 | 音乐语义和原稿像素必须分层。 |
| G10 | Geometry Correction 是正式但高级的校订能力。 | 自动检测边界也会出错，但普通用户不应被复杂工具干扰。 |

---

# O — OMR / Correction

| ID | 决策 | 主要理由 |
|---|---|---|
| O1 | OMR 是可缓存多阶段 pipeline。 | 支持局部重跑、模型替换和故障恢复。 |
| O2 | 识别以 System/Staff group 上下文为主，细粒度 Geometry Head 定位符号。 | 音乐符号需要上下文，不能只看孤立 crop。 |
| O3 | 专用模型 + compact decoder + constraint solver 为主；VLM 只作 rescue。 | 本地部署、可控性与正确性优先。 |
| O4 | Validator 区分 Hard Constraint 与 Soft Constraint。 | 罕见音乐不能被“常见性”规则误改。 |
| O5 | 每个关键识别事实保存来源、模型版本和 confidence。 | 可解释、可回归、可迁移。 |
| O6 | HumanVerified 永远高于模型。 | 保护用户校订成果。 |
| O7 | 新模型重识别通过 geometry/measure/time/neighbors 做 correspondence migration；不确定时进入 conflict queue。 | 长期模型升级不能丢人工修订。 |
| O8 | 默认不显示满屏置信度；校订模式按风险呈现。 | 保证阅读体验并提高人工复核效率。 |
| O9 | 校订器直接操作谱面与属性，不要求用户理解 MusicXML 内部机制。 | 目标用户不是格式专家。 |
| O10 | 必须能新增漏检对象和删除 false positive。 | 只允许修改模型已有结果无法完成真正校订。 |
| O11 | correction 默认只留本地，训练样本由用户主动导出。 | 版权与隐私。 |
| O12 | Unknown/Opaque 是正式状态。 | 不强迫模型输出错误标签。 |
| O13 | 分离 ChoralSemanticProfile 与 AccompanimentPlaybackProfile。 | 钢琴和合唱语义需求不同。 |
| O14 | 支持 document/page/system/staff/measure/region 级重识别。 | 长谱局部修复必须高效。 |
| O15* | 项目状态为 Draft → Reviewed → Verified；Draft 可导出，有 Hard Error 时警告。 | 专业工具让用户掌控，不因低风险未确认强行阻塞。 |
| O16* | Measure/Staff/System/Page 可批量标记已人工检查。 | 避免逐音符点击确认。 |

`O15/O16` 为确认阶段后续补充的 OMR 产品规则，编号在本文中补录以便追踪。

---

# P — PerformanceIR / Playback / SVS

| ID | 决策 | 主要理由 |
|---|---|---|
| P1 | PerformanceIR 是派生物，可丢弃重建。 | 不污染原谱。 |
| P2 | 反复每次经过拥有独立 performance instance，如 M18@Pass2。 | 谱面位置相同但演奏上下文不同。 |
| P3 | MusicalTime 与 PlaybackTime 分离。 | 乐谱精确时值与实际秒数不同。 |
| P4 | 用户练习速度用 TempoTransform/PlaybackProfile，不改源 tempo。 | 保持 Source Fact。 |
| P5 | rit./fermata 由 realization policy 生成实际时间。 | 原谱通常未规定精确曲线。 |
| P6 | 播放变调跟随 Effective Score。 | 与 D8 一致。 |
| P7 | Audio/Singing backend 可替换。 | 不把核心绑定具体合成器或 SVS。 |
| P8 | 每 PerformerRole 独立逻辑 track；Role 与歌声音色解耦。 | 支持 solo/mute、S1/S2 和未来不同 voice model。 |
| P9 | Piano 为独立 accompaniment bus。 | 符合排练工作流。 |
| P10 | la/ah/首调唱名/原歌词属于 VocalizationProfile。 | 同一旋律可多种模唱文本。 |
| P11 | PronunciationProfile → 抽象 IPA/phoneme → ModelPhonemeAdapter。 | 项目发音数据不绑定某个 SVS phoneme 表。 |
| P12 | SVS 必须尽量 score-conditioned。 | 排练同步和音乐正确性优先。 |
| P13 | 简单合成器是 timing 参考真值；SVS 漂移用 AlignmentMap 映射。 | 不靠 ASR 猜播放位置。 |
| P14 | SVS 按每个 PerformerRole 自己的歌词标点/断句/乐句/休止优先切段，并用上下文降低拼接痕迹。 | 各声部歌词断句可能不同，强行统一切块会降低质量。 |
| P15 | 音频 cache 是可重建 cache，key 包含 score/model/profile/revision。 | 项目事实与大体积派生数据分离。 |
| P16 | backend 不支持演奏细节时显式 fallback。 | 禁止静默编造。 |
| P17 | 所有播放方式共享 Transport。 | UI/同步逻辑一致。 |
| P18 | 高亮依据 PerformanceIR → ScoreIR → GeometryGraph。 | 精确同步。 |
| P19 | 自然模唱默认当前 Role 100%、钢琴中等、其他人声静音。 | 更符合声部练习器。 |
| P20 | Count-in 与 Metronome 纳入 v1。 | 排练价值高、实现成本相对低。 |
| P21 | 同时提供 Performance Mode 与 Rehearsal Range Mode。 | 正式反复结构与局部线性排练需求不同。 |
| P22 | 音频导出纳入 v1 后段；简单合成与 mixer 支持 WAV/FLAC/AAC-LC（推荐 M4A）。 | 便于分享与实际排练。 |
| P23 | 自然模唱目标是正确、清楚、同步、自然；明确不追求专业真人级歌唱表现力。 | 聚焦可实现且对排练真正有价值的目标。 |

---

# J — Jianpu

| ID | 决策 | 主要理由 |
|---|---|---|
| J1 | JianpuIR 是 ScoreIR 的派生表示。 | 不成为原谱真值。 |
| J2 | NumberingBasis 显式配置；默认 la-based minor，相对大调体系；tonic-as-1 可选。 | 符合目标用户常见简谱习惯，同时保留可配置性。 |
| J3 | 中途转调建立新的 tonal segment。 | 不能只在曲首定义 1=。 |
| J4 | Jianpu accidental 与实际 pitch / 原 staff accidental 分离。 | 三者语义不同。 |
| J5 | 八度点从 sounding pitch 生成。 | Tenor 等记谱不能靠图像高度猜真实音区。 |
| J6 | JianpuIR 保存 exact duration，线/点只是渲染。 | 语义与视觉分离。 |
| J7 | Beam/grouping 是重要提示但不覆盖 duration。 | 保留节奏组织又不混淆时间。 |
| J8 | Rest 与 multi-measure rest 是结构对象。 | 不粗暴展开大量 0。 |
| J9 | Tie/Slur 在 JianpuIR 仍不同。 | 与 ScoreIR 语义一致。 |
| J10 | 原生支持多 voice 与 PerformerRole lane；完整谱默认 compact，个人练习默认 expanded。 | 同时满足总谱和分部阅读。 |
| J11 | Standalone/Parallel 显示歌词；Inline 默认复用 staff 歌词，可选 IPA/发音辅助层。 | 避免重复占用纵向空间。 |
| J12 | 不使用 Sparks 作为 renderer；自有 Jianpu 语义、语法扩展和渲染器。Sparks 只是前期工作/参考。 | 避免削足适履和能力上限。 |
| J13 | 长跨度记号按语义跨响应式行重绘。 | 支持 reflow。 |
| J14 | Inline/Parallel 与 staff 使用同一 Responsive LayoutPlan。 | 真正严格对齐。 |
| J15 | Jianpu Only 可独立重排。 | 没有 staff 对齐时应优化自身阅读。 |
| J16 | 弱起/不完整小节保留真实时值。 | 不伪造补满。 |
| J17 | 变调练习从 Effective Score 生成。 | 与 D8 一致。 |
| J18 | NumberingBasis/tonality 不确定属于高优先级 Review。 | 一个调性错误会系统性污染整段 Jianpu。 |
| J19 | Sparks AST/文本/ID 不得成为永久项目模型；JianpuScene platform-neutral。 | 长期开放与跨平台。 |
| J20 | Jianpu 转换必须做语义级自动测试。 | 不把截图当唯一正确性判断。 |
| J21* | 内部/API 统一使用 `Jianpu`；英文首次 `Jianpu (Numbered Musical Notation)`；NMN 仅为别名。 | Jianpu 更准确地指向本项目实现的中国简谱体系，避免 NMN 过宽/歧义。 |

---

# R — Reader / Interaction

| ID | 决策 | 主要理由 |
|---|---|---|
| R1 | Responsive Score View 与 Source View 分离。 | 日常 reflow 与原稿核对职责不同。 |
| R2 | Responsive layout 没有永久 page，只有 ScoreFlow/Viewport。 | 适配任意设备尺寸。 |
| R3 | 保留横向 Paged Flow，“半页”重新定义为半 viewport。 | 延续用户阅读习惯而不依赖 PDF。 |
| R4 | 宽屏“双页”定义为双响应式面板。 | 真正 PDF 双页仅 Source View。 |
| R5 | Content Scale 与 Inspection Zoom 分离。 | 放大乐谱应 reflow，而不是只裁切画面。 |
| R6 | pinch 过程中临时 zoom，结束后转换成 ContentScale/reflow。 | 防止手势过程中不断重排跳动。 |
| R7 | Role filter 分 Full Score / Section / Performer Focus。 | 面向总谱、分部与个人练习。 |
| R8 | “指挥视图”等是 ViewProfile，不创建新数据模型。 | 避免产品 preset 污染核心。 |
| R9 | Inline Jianpu 是正式 StaffLane 参与 layout，不是浮动 overlay。 | collision/layout 必须统一求解。 |
| R10 | 默认换行只看空间/密度；语义优化换行为可选高级/实验功能。 | 声乐成员已习惯普通换行，默认应可预测、稳定。 |
| R11 | 用户可手工 Forced/Preferred/Avoid Break。 | 给高级排练布局控制权。 |
| R12 | 播放跟随采用 Comfort Zone/预滚动。 | 避免光标总在屏幕边缘和持续微滚动。 |
| R13 | 用户手动滚开时自动跟随暂停。 | 不抢夺用户视图控制权。 |
| R14 | 普通点击选择，播放动作避免与触屏滚动误触冲突。 | 触控安全。 |
| R15 | Correction Mode 必须显式进入。 | 防止阅读时误改乐谱。 |
| R16 | Correction 所见即所得，Structure Inspector 只作高级工具。 | 目标用户不应操作内部树。 |
| R17 | Source/Responsive 双向定位。 | 校订、核对和 provenance 的关键能力。 |
| R18 | 导航按音乐语义，而非只有页码。 | measure/rehearsal mark/lyrics 更稳定。 |
| R19 | 保留原稿页码，但不创造不稳定的“响应式电子页码”。 | resize 后页码不应改变位置身份。 |
| R20 | 键鼠和触屏都是一级输入，核心能力不可仅靠 hover。 | WinUI 目标设备包含触控。 |
| R21 | 多小节 selection 基于 MusicalRange，不基于像素矩形。 | reflow 后仍保持选区。 |
| R22 | 预留 UserAnnotationLayer；完整批注 UI 非 v1 核心。 | 后续排练批注有价值，但不扩大当前范围。 |
| R23 | reflow 不可靠的复杂区域允许局部 source crop fallback。 | 正确性优先于强制响应式化。 |
| R24 | 只布局 viewport 附近，measure layout 可缓存。 | 长谱触控性能。 |

---

# A — 工程 / 存储 / 模型 / 平台

| ID | 决策 | 主要理由 |
|---|---|---|
| A1 | 默认项目文件使用单文件 SQLite。 | 事务、增量编辑、crash recovery、历史记录更适合。 |
| A2 | 另提供含 DB + sources 的 archive。 | 日常轻量与归档自包含兼得。 |
| A3 | schema 显式版本化与 migration。 | 长期维护。 |
| A4 | 使用持久稳定对象 ID；重 OMR 是新 revision + correspondence。 | 人工修正和引用不能随排序变化。 |
| A5 | 当前 snapshot + correction/revision history，不做纯 Event Sourcing。 | 兼顾读取效率和审计/undo。 |
| A6 | render/SVS/ONNX cache 与项目事实分离。 | 可删可重建，避免 DB 膨胀。 |
| A7 | Source reference 保存 path/URI + cryptographic hash + fingerprint。 | 支持重新定位和校验。 |
| A8 | ModelPackage 是纯数据，不执行仓库代码。 | 供应链安全。 |
| A9 | Model Manifest 自描述任务、版本、opset、I/O、license、hardware、hash 等。 | 可复现和兼容管理。 |
| A10 | 官方模型签名/checksum，第三方标记未验证。 | 镜像/手动安装供应链保护。 |
| A11 | ModelResolver 抽象 Local/Mirror/HF/Future。 | 不把 HF 写死进 OMR 核心。 |
| A12 | 已安装模型后完全离线；远端不能成为静默 fallback。 | 隐私与可预测性。 |
| A13 | Job Engine 使用 versioned DAG + checkpoint/cache key。 | 局部失效、取消、恢复、模型迭代。 |
| A14 | 重 OMR/推理运行于独立 worker process。 | 崩溃隔离、GPU/NPU 生命周期、取消。 |
| A15 | 从第一天抽象 Local/Remote executor，核心契约不使用绝对本地路径。 | 未来服务器执行不返工。 |
| A16 | v1 不稳定通用 native plugin ABI。 | 避免过早承担安全/兼容债务。 |
| A17 | 必须有 headless CLI/harness。 | 训练服务器、CI、benchmark 与 GUI 共享核心。 |
| A18 | ScoreIR/JianpuIR/ModelPackage 等公开 schema/spec。 | 开源核心应可被其他应用复用。 |
| A19 | Portable C++20 Core + C ABI；C# / WinUI 3 host；Python 仅训练/研究。 | Windows 体验与未来平台开放性平衡。 |
| A20 | 初期 monorepo。 | schema/model/app/harness 需同步演进。 |
| A21 | Standalone/Parallel Jianpu 需要打印级矢量 PDF 导出。 | 独立简谱与对照谱本身有分享/印刷价值。 |
| A22 | MusicXML import/export 是核心互操作能力。 | 校订后的 OMR 结果应可进入外部音乐生态。 |
| A23 | 公开项目相关规范，但 SQLite 私有索引/cache 表不承诺公共 API。 | 开放性与内部演进空间兼顾。 |
| A24 | 允许第三方符合 ModelPackage 的 ONNX 模型。 | 开源生态与模型创新。 |
| A25 | 模型更新独立于应用；默认提示不自动下载大模型；可 pin；不自动重 OMR。 | reproducibility 和用户控制。 |

---

# V — v1 Scope / 验收 / 发布原则

| ID | 决策 | 主要理由 |
|---|---|---|
| V1 | Windows 11 25H2+ / WinUI 3，触控与键鼠一级支持。 | 明确首发平台。 |
| V2 | PDF 为主要输入，MusicXML 为结构化入口。 | 符合现有谱源。 |
| V3 | OMR 覆盖页面→语义→validation 的完整链路。 | 产品核心。 |
| V4 | 支持任意 PerformerRole divisi。 | 复杂合唱谱现实需求。 |
| V5 | 中文/英文歌词、IPA、自定义发音规则。 | 自然模唱与多语合唱。 |
| V6 | 钢琴到可整体播放精度。 | 声乐用户价值最大化。 |
| V7 | 完整 Correction Mode。 | 专业级可靠性的前提。 |
| V8 | Draft/Reviewed/Verified。 | 明确质量语义。 |
| V9 | 响应式原貌阅读 + Source View。 | 摆脱 PDF 分页同时保留核对。 |
| V10 | Full/Section/Performer focus。 | 个人与指挥场景。 |
| V11 | 自有 Jianpu stack。 | 长期能力上限自主。 |
| V12 | Jianpu Only/Parallel/Inline。 | 核心产品需求。 |
| V13 | Inline 使用 RhythmicAnchor。 | 严格对齐。 |
| V14 | 简单合成、Role mixer、钢琴、变速/变调/循环/节拍。 | 排练工具完整性。 |
| V15 | SVS 支持 la/ah/唱名/歌词。 | 自然模唱主要场景。 |
| V16 | HF/镜像/本地模型管理。 | 全球/大陆网络环境与离线需求。 |
| V17 | 纯本地默认。 | 隐私和可靠性。 |
| V18 | 项目 revision/provenance/migration。 | 长期维护。 |
| V19 | MusicXML 与 Jianpu PDF 导出。 | 互操作和分享。 |
| V20 | WAV/FLAC 属于 v1 主体。 | 无损/标准音频导出。 |
| V21 | AAC-LC/M4A 与自然模唱 mixer 导出在 v1 后段。 | 分享便利。 |
| V22 | Headless harness 必须与 GUI 同核心。 | 研究/CI/训练。 |
| V23 | 公开核心 schema。 | 平台型开源设计。 |
| V24 | UserAnnotation schema 预留，完整 UI 后置。 | 不阻塞 v1。 |
| V25 | 语义优化换行是可选实验功能。 | 默认可预测布局优先。 |

## 其他发布决策

- 代码许可证：**Apache-2.0**；模型和数据许可证独立披露。
- v1 UI：**简体中文 + 英文**，从第一天资源化。
- 默认：**0 telemetry**；未来 crash reporting 必须 opt-in 且默认不包含谱面/歌词/DB。
- 更新渠道：保持 MSIX/常规 Windows 包可行；GitHub Releases / Store 可在接近发布时决定，不绑定核心架构。

---

# 关键 ADR 候选

未来进入实现仓库后，建议优先将下列决策升级为独立 ADR：

1. ADR-001 — ScoreIR / GeometryGraph / PerformanceIR / JianpuIR 四层分离；
2. ADR-002 — SQLite project store + archive；
3. ADR-003 — HumanVerified override policy；
4. ADR-004 — RhythmicAnchor 作为 Inline Jianpu 对齐真值；
5. ADR-005 — Piano AccompanimentPlaybackProfile；
6. ADR-006 — Jianpu 自有 IR/Layout/Renderer，Sparks 仅参考；
7. ADR-007 — Hybrid facsimile responsive rendering；
8. ADR-008 — ModelPackage 禁止任意执行代码；
9. ADR-009 — Local-first / explicit remote execution；
10. ADR-010 — PerformanceIR repeat instance / realization model；
11. ADR-011 — PronunciationProfile 与 ModelPhonemeAdapter 分离；
12. ADR-012 — 默认几何换行，语义优化可选。

