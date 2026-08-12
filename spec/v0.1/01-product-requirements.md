---
title: 产品需求与软件需求规范（PRD / SRS）
version: 0.1
status: Frozen Baseline
language: zh-CN
updated: 2026-08-12
---

# 产品需求与软件需求规范（PRD / SRS）v0.1

## 1. 目的

本文定义 v1 产品范围、目标用户、核心功能、非功能要求、验收方式及明确非目标。实现细节见 `02-system-architecture.md`，阶段计划见 `03-development-plan.md`。

## 2. 产品目标

### 2.1 目标用户

主要用户：

- 合唱团声乐成员；
- 分部负责人；
- 临时指挥或需要快速读总谱的排练组织者。

应用不是钢琴教学软件，也不是通用作曲/刻谱软件。

### 2.2 核心价值

用户导入现代印刷 CWMN 合唱谱 PDF 后，应能够：

1. 自动获得高质量、可解释、可人工修正的 OMR 草稿；
2. 在不受原 PDF 分页限制的响应式视图中舒适阅读原貌五线谱；
3. 按声部生成和阅读专业 Jianpu；
4. 在五线谱与 Jianpu 同屏时保持严格音乐时间对齐；
5. 快速聚焦 S/A/T/B 或 S1/S2/A1/A2 等任意 PerformerRole；
6. 使用钢琴/简单合成器/自然模唱进行同步排练；
7. 全流程在设备端离线执行，并保留未来远端执行扩展能力；
8. 将人工校订结果长期保存，并可用于模型回归与可选训练样本生成。

### 2.3 “专业级”的定义

“专业级”不等于“任意复杂扫描件零人工 100% 自动正确”。本项目的专业性由以下共同构成：

- 高自动化 OMR；
- 明确的不确定性与 provenance；
- 高效率 Correction Mode；
- 严格音乐约束与可验证转换；
- 用户人工确认优先于模型；
- 稳定的响应式布局与对齐；
- 可复现、可迁移、可回归测试的项目和模型版本管理。

## 3. 支持范围

### 3.1 v1 支持的输入

必须支持：

- 扫描 PDF（主要场景）；
- 软件导出的矢量 PDF；
- MusicXML 作为结构化导入/互操作入口。

若同时存在可靠结构化源，应用应优先建议导入结构化源，并将 PDF 作为原稿与几何来源。

### 3.2 v1 乐谱范围

必须支持现代印刷 CWMN 合唱谱，包括但不限于：

- SATB、SSA、TTBB；
- S1/S2/A1/A2/T1/T2/B1/B2 等任意 divisi；
- 同一 staff 中临时高低 voice；
- 中文、英文歌词；
- 其他语言以 IPA / 外部 PronunciationProfile 作为主要发音表示；
- 常见调号、拍号、变调、速度变化；
- 常见反复、ending、D.C./D.S./Segno/Coda/Fine；
- grace note、trill、tremolo、ornament、articulation、dynamic、hairpin、fermata、slur/tie 等常规记号。

自由节拍/无小节线区域：v1 数据模型和人工校订支持，但自动 OMR 不作为发布门槛。

无法规范化的特殊现代记号必须保留为 Opaque/Unsupported，不得丢弃或强行猜测。

## 4. 功能需求

### 4.1 导入与项目创建

**FR-INGEST-001** 应用必须从扫描或矢量 PDF 创建项目。  
**FR-INGEST-002** 应用必须支持 MusicXML 导入。  
**FR-INGEST-003** 原 PDF 默认作为外部源引用，不默认嵌入项目文件。  
**FR-INGEST-004** 用户必须可以选择“打包/归档项目”，将源 PDF/图像一并嵌入归档。  
**FR-INGEST-005** 源文件失效时，项目必须仍可打开结构化数据，并提供重新定位与 hash/fingerprint 验证。

### 4.2 OMR 与校订

**FR-OMR-001** OMR 必须为可检查、可缓存、可单阶段重跑的多阶段流水线。  
**FR-OMR-002** 必须支持 Page/System/Staff/Measure/Selected Region 级别重识别。  
**FR-OMR-003** 合唱部分采用高语义精度识别：pitch、duration、onset、voice、PerformerRole、lyrics、spanners、ornaments、dynamics、repeat 等。  
**FR-OMR-004** 钢琴伴奏采用降低语义目标的 Playback Profile：只需恢复可整体播放的 pitch/onset/duration/chord/tie/pedal/主要 dynamics，不要求恢复复杂 logical voice 或 cross-staff continuity。  
**FR-OMR-005** Correction Mode 必须支持修改、插入、删除、重新关联、重做几何关联；不能只修改模型已检测对象。  
**FR-OMR-006** 用户人工确认/修正必须拥有最高优先级；新模型重跑不得静默覆盖。  
**FR-OMR-007** 每个关键字段应保存 provenance/confidence；人工确认可标记 `HumanVerified`。  
**FR-OMR-008** 模型必须允许 `UnknownSymbol` / `OpaqueNotation` / `UnsupportedRelation` 作为合法结果。  
**FR-OMR-009** 默认阅读界面不显示置信度热力图；校订模式提供 Next Issue、区域提示和字段级 Inspector。  
**FR-OMR-010** ReviewPriority 应综合不确定性、音乐影响和传播风险，而不是只按 confidence 排序。

### 4.3 质量状态

项目必须支持：

- **Draft**：刚完成识别，可含 Error/Warning/Opaque；允许阅读、试听和导出，但明确提示未校订；
- **Reviewed**：所有 Hard Error 已解决；适合日常排练；
- **Verified**：系统要求人工复核的高风险内容均已确认，关键结构校验通过；适合作为回归真值或最终分享版本。

Draft 必须允许导出；存在 Hard Error 时必须明显警告，但默认不强制水印。

用户必须可按 Measure / Staff / System / Page 批量标记“已人工检查”。

### 4.4 响应式阅读

应用必须提供两种内容视图：

1. **Responsive Score View**：日常默认；不服从原 PDF page/system break；
2. **Source View**：原 PDF/扫描件；用于核对、校订、手写标注查看和 provenance 追踪。

**FR-READ-001** Responsive View 必须基于 ScoreFlow/ResponsiveSystem，而不是永久电子页。  
**FR-READ-002** 必须提供连续滚动与横向 Paged Flow。  
**FR-READ-003** 横向翻阅默认推进约半个当前 viewport，不依赖原 PDF 页宽。  
**FR-READ-004** 宽屏可提供双面板阅读；真正 PDF 双页只属于 Source View。  
**FR-READ-005** Content Scale 必须触发重新排版；Inspection Zoom 仅做视觉放大。  
**FR-READ-006** 触控 pinch 结束后应转换成 Content Scale 并 reflow，尽量保持用户关注位置。  
**FR-READ-007** 默认断行只基于几何/密度与基本排版约束；语义感知断行为可选高级功能。  
**FR-READ-008** 用户可设置 PreferredBreak / ForcedBreak / AvoidBreak，并可恢复自动布局。  
**FR-READ-009** 播放跟随必须使用舒适区/预滚动；用户主动滚开后自动跟随应暂停，并提供“回到播放位置”。  
**FR-READ-010** Source View 与 Responsive View 必须可按 ScoreIR/GeometryGraph 双向定位。  
**FR-READ-011** 导航应支持 measure、rehearsal mark、歌词搜索、书签、OMR issue、源 PDF page 等音乐语义位置。

### 4.5 PerformerRole 聚焦

必须支持：

- Full Score；
- Section Focus；
- Performer Focus；
- Highlight / Dim Others / Solo；
- 可选钢琴显示/隐藏；
- ViewProfile 预设，例如“完整总谱”“我的声部”“分部排练”“简谱练习”“指挥视图”。

`PerformerRole` 不得限制为 SATB 四项固定枚举，应支持 S1/S2、A1/A2、Choir I/II、Solo 等扩展。

### 4.6 Jianpu

项目和 API 统一称为 **Jianpu**。

必须采用自有：

- `JianpuCompiler`；
- `JianpuIR`；
- `JianpuLayoutEngine`；
- platform-neutral Jianpu scene/rendering abstraction。

Sparks 只作为语法、兼容和测试参考，不是运行时依赖或功能上限。

必须支持三种呈现：

1. **Jianpu Only**；
2. **Parallel Staff + Jianpu**；
3. **Inline Jianpu**。

**FR-JIANPU-001** Inline 模式允许只为选定 PerformerRole 生成。  
**FR-JIANPU-002** Inline Jianpu 的事件 X 位置必须严格使用 `RhythmicAnchor`；barline 使用共享 BarlineAnchor。  
**FR-JIANPU-003** 五线谱与 Jianpu 同屏时必须共同服从同一 Responsive LayoutPlan。  
**FR-JIANPU-004** Jianpu Only 可独立重排，不受原谱换行约束。  
**FR-JIANPU-005** 完整 Jianpu 默认可按原 staff/section compact 呈现多 voice；个人排练可按 PerformerRole 展开 lane。  
**FR-JIANPU-006** 默认小调采用 la-based relative-major 习惯；tonic-as-1 作为可选 JianpuProfile。  
**FR-JIANPU-007** Jianpu 八度必须由 sounding pitch 生成，不允许根据五线谱垂直图形位置直接猜测。  
**FR-JIANPU-008** 独立与对照 Jianpu 必须支持矢量 PDF 导出；Inline v1 不要求打印导出。

### 4.7 变调

用户主动变调必须使用非破坏性的 `PitchTransform`：

- Source Score / 原稿语义不变；
- Effective Score 派生当前练习音高；
- 原貌五线谱仍显示原谱；
- 变调练习模式下 Jianpu、播放、首调唱名、SVS 跟随 Effective Score；
- UI 必须显著显示当前变调状态；
- 原始音高和当前有效音高均可查询。

### 4.8 歌词与发音

歌词必须保存：

- original text；
- verse/layer；
- syllable mapping；
- melisma/elision/hyphenation；
- language/variety；
- 可选 pronunciation/IPA。

必须支持项目级 `PronunciationProfile`。对于中文方言（例如客家话）应支持导入自定义发音映射，覆盖默认普通话规则。

发音优先级：

1. 人工逐 syllable 覆盖；
2. 项目短语/词组映射；
3. 项目字词映射；
4. 选定语言变体 G2P/发音库；
5. 默认语言规则；
6. 未解析。

缺失方言映射默认应“标记待确认”，不应静默回退普通话；用户可显式启用回退。

### 4.9 播放与排练

必须提供统一 Transport：

- 播放/暂停/seek；
- Role solo/mute/volume；
- 钢琴伴奏独立 mixer bus；
- 全局速度百分比与 BPM；
- 变调；
- 小节循环 / A-B Loop；
- Count-in；
- Metronome；
- 自动跟随；
- 从任意 RhythmicAnchor 开始；
- 严格遵循反复的 Performance Mode；
- 线性区间排练的 Rehearsal Range Mode。

简单合成器必须是同步参考真值：严格依赖 PerformanceIR 调度。

### 4.10 自然模唱（SVS）

自然模唱目标：**可靠、自然、适合排练**，不追求专业真人歌唱表现力。

必须支持：

- la；
- ah；
- 首调唱名；
- 原歌词 / IPA。

每个 PerformerRole 应能作为独立逻辑 track 生成/混音；Role 与 Singer Voice/模型音色不得绑定。

SVS 分段优先依据**各 PerformerRole 自己的歌词断句/标点/乐句边界与较长休止**，不同声部允许不同切分点。原 PDF system break 和普通 barline 不是天然音频切点。

应支持上下文重叠/安全拼接，以降低分段生成接缝。

用户即时变速应优先对已生成 waveform 做高质量 time-stretch；高质量重新生成作为可选质量优化。

SVS backend 不支持某些演奏细节时必须明确降级，不得静默编造。

### 4.11 音频导出

v1 主体必须支持 WAV、FLAC。  
v1 后段必须支持 AAC-LC，默认推荐 `.m4a` 容器，便于分享。  
已生成的自然模唱和当前 mixer 输出也应允许导出。

### 4.12 MusicXML 与互操作

必须支持 MusicXML import/export。  
钢琴导出若需要 technical voice assignment，可由 exporter 生成 synthetic/non-semantic voice，不得宣称恢复了原钢琴 logical voice。

### 4.13 模型管理

必须支持：

- Hugging Face；
- 可配置镜像 endpoint；
- 手工拷贝/本地目录；
- 第三方符合 ModelPackage 规范的 ONNX 模型；
- 模型版本 pinning；
- 官方/社区/未验证状态区分。

模型更新独立于应用更新。新模型不得自动触发旧项目重新 OMR。

### 4.14 离线与未来远端

已安装所需模型后，正常导入、识别、阅读、校订、播放和导出必须可完全断网运行。

未来远端 backend 必须为用户显式选择；本地失败或硬件不支持时只提示，不得静默上传源谱。

## 5. 非功能需求

### 5.1 平台

- Windows 11 25H2+；
- WinUI 3；
- 目标硬件优先 Intel Core Ultra 200/300 系列及同级现代 CPU/GPU/NPU；
- 不考虑老旧软硬件兼容作为 v1 约束；
- 核心库保持平台开放，但 v1 正式客户端只做 Windows。

### 5.2 性能与交互

- 后台 OMR 不得阻塞 UI；
- 阅读滚动/缩放/reflow 以 60 fps 级体验为基本目标；
- 音频不得因布局卡顿而中断；
- 长任务必须可取消、恢复或按 stage 重跑；
- 长谱必须增量布局，仅维护 viewport 附近区域；
- 模型吞吐量发布门槛在基准模型建立后通过 harness 数据制定，不在 v0.1 拍脑袋写死。

### 5.3 隐私

- 默认 0 telemetry；
- crash reporting 若未来加入必须显式 opt-in；
- crash package 默认不得包含源谱图像、歌词或项目 DB；
- 用户 correction 默认只保存在本机；
- 训练样本上传/导出必须由用户主动发起。

### 5.4 安全

官方 ModelPackage 只允许声明式资源，不允许从模型仓库下载后执行 Python/PowerShell/EXE/DLL 等任意代码。第三方模型也受相同限制。

### 5.5 可维护性

- 核心采用平台无关 schema 和稳定对象 ID；
- 所有关键模型/算法结果带版本和 provenance；
- schema migration 必须显式、可回归；
- project cache 可删除重建；
- headless CLI/harness 与 GUI 使用同一核心 pipeline。

### 5.6 国际化

v1 正式提供简体中文与英文资源；从第一天采用可本地化资源，不在代码中散落硬编码中文 UI 字符串。

### 5.7 开源

代码建议 Apache-2.0。模型权重和训练数据允许按来源采用独立许可证，并必须在 Model Manifest / 数据清单中清晰披露。

## 6. 验收标准

### 6.1 Reviewed 的硬门槛

`Unresolved Hard Errors = 0`。

Hard Error 包括但不限于：

- 无法解释的小节时值结构；
- 非法 tie；
- 无合法 onset 的事件；
- 关键 measure/repeat graph 结构损坏。

罕见但合法的音乐现象只能成为 Warning，不得因“不像常规和声”自动纠正。

### 6.2 OMR Benchmark

Harness 必须分别报告至少：

- Pitch；
- Duration；
- Onset；
- Voice；
- PerformerRole；
- Lyrics / lyric alignment；
- Accidentals；
- Articulations；
- Ornaments；
- Dynamics；
- Barlines；
- Repeat structure。

指标至少包括 Precision / Recall / F1 / Exact-measure / Exact-system。

### 6.3 人工成本指标

必须记录：

- Human Corrections Per Page；
- Human Corrections Per 100 Measures；
- Critical / Major / Minor correction 分类。

长期优化重点：Critical correction 接近零，Major correction 持续下降。

### 6.4 Jianpu 对齐

Inline Jianpu 与五线谱必须共享同一 `RhythmicAnchor` 和 BarlineAnchor。逻辑对齐差值为 0；最终设备像素只允许正常 pixel rounding。

明显横向错位属于 layout bug，而不是允许的 OMR 误差。

### 6.5 Responsive invariants

窗口 resize/reflow 后：

- measure 顺序不变；
- ScoreIR event identity 不变；
- RhythmicAnchor relation 不变；
- loop/selection/bookmark 不丢；
- 播放仍指向同一 ScoreIR event；
- Jianpu 与 staff 共同 reflow；
- Source provenance 仍可逆定位。

## 7. 测试语料

必须维护三类 corpus：

1. **Synthetic Gold**：结构化真值 → 多排版 → 扫描退化；
2. **Curated Real Gold**：真实扫描合唱谱人工完整校订；
3. **Adversarial / Edge Corpus**：极密 divisi、复杂歌词、反复、模糊扫描、特殊记号等。

Curated Real Gold 不得只保存 MusicXML；必须覆盖 ScoreIR、GeometryGraph、PerformerRole mapping、Lyrics alignment、Expected JianpuIR 等项目特有真值。

## 8. v1 明确非目标

v1 不承诺：

- 手写谱、古谱、图形谱、实验记谱完整自动理解；
- 任意交响/管弦总谱通用 OMR；
- MuseScore 级完整作曲/专业刻谱工作流；
- 钢琴复杂 logical voice/cross-staff 语义恢复；
- 钢琴 Jianpu；
- Inline Jianpu 打印级导出；
- 保持原 PDF 分页作为响应式阅读布局；
- 所有特殊区域都强制 reflow；失败时可安全退回原貌 crop；
- 专业真人歌唱家级 SVS；
- Android/iOS/macOS/鸿蒙正式客户端；
- v1 稳定第三方 native plugin ABI；
- 云服务作为必需依赖。

## 9. v1 范围合同

v1.0 必须至少包含：

- Windows 11 25H2+ / WinUI 3；
- PDF/MusicXML 导入；
- OMR + Correction Mode；
- 响应式阅读 + Source View；
- PerformerRole 聚焦；
- 自有 Jianpu 栈与三种呈现；
- PerformanceIR / 简单合成 / 排练控制；
- 自然模唱基础能力；
- 本地模型管理；
- 项目存储、迁移、provenance；
- MusicXML / PDF / WAV / FLAC 导出；
- v1 后段 AAC 与自然模唱 mixer 导出；
- Headless CLI/harness；
- 公开 ScoreIR / GeometryGraph / JianpuIR / ModelPackage 规范。

