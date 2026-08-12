---
title: 合唱乐谱应用规范集
version: 0.1
status: Frozen Baseline
language: zh-CN
updated: 2026-08-12
---

# 合唱乐谱应用规范集 v0.1

本目录记录截至 2026-08-12 已与产品发起人逐项确认并冻结的产品、系统与开发规范。本文档集用于直接进入仓库 `/spec`，作为后续实现、评审、测试、模型训练和版本迁移的共同基线。

> **重要原则**：本规范中的“已冻结”表示当前实现基线。未来允许通过明确的 ADR / schema migration / 版本升级修改，但不得在实现中静默改变含义。

## 文档目录

1. [`01-product-requirements.md`](01-product-requirements.md)  
   产品需求、v1 范围、功能需求、非功能需求、验收标准和明确非目标。
2. [`02-system-architecture.md`](02-system-architecture.md)  
   ScoreIR、GeometryGraph、PerformanceIR、JianpuIR、OMR、响应式渲染、项目存储、模型管理、进程和模块边界。
3. [`03-development-plan.md`](03-development-plan.md)  
   M0–M7 开发路线、Harness、测试门、模型研发路线、风险和交付物。
4. [`04-decision-register.md`](04-decision-register.md)  
   已冻结 D/M/G/O/P/J/R/A/V 决策的追踪记录和主要理由。

## 术语约定

- **CWMN**：Conventional Western Music Notation，本文特指现代印刷的常规西方五线谱记谱。
- **Jianpu（简谱）**：项目与 API 统一使用 `Jianpu`；英文文档首次写作 **Jianpu (Numbered Musical Notation)**；`NMN` 仅作为通行别名，不作为核心代码命名。
- **ScoreIR**：忠实保存“原谱写了什么”的音乐语义真值。
- **GeometryGraph**：原稿视觉对象、几何、mask、来源坐标及其与 ScoreIR 的关联。
- **PerformanceIR**：由 ScoreIR 与练习设置派生的实际播放/演唱时间线。
- **JianpuIR**：由 ScoreIR 派生的简谱语义，不受具体文本语法或渲染器绑定。
- **PerformerRole**：实际演唱角色，如 S、A、T、B、S1、S2、A1、A2、Solo、Choir I 等；不是固定四声部枚举。
- **RhythmicAnchor**：事件在音乐时间轴上的严格水平锚点；行内 Jianpu 的对齐真值。
- **VisualAnchor**：原稿实际 glyph/notehead 的视觉位置；不一定等于 RhythmicAnchor。

## 规范关键词

- **必须（MUST）**：v1 基线或架构不变量，除非通过正式决策变更。
- **应（SHOULD）**：默认实现方向，允许有充分理由的例外。
- **可（MAY）**：扩展能力或非阻塞功能。

## 产品一句话定义

一个面向声乐成员与临时指挥的 Windows 开源应用：从现代印刷合唱五线谱扫描/矢量 PDF 中恢复可校订的音乐语义与几何，提供响应式原貌阅读、专业 Jianpu 转换、声部聚焦、同步伴奏与自然模唱，并以纯本地、可验证、可扩展的核心架构长期维护。
