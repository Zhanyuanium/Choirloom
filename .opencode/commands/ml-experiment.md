---
description: Run a scoped ML experiment (corpus/training/benchmark/ONNX/ModelPackage) with mandatory safety and confirmation gates.
agent: ml-researcher
---

Run the ML experiment described in: $ARGUMENTS

Required steps (strict — do not skip or reorder):

1. Ground the experiment in `spec/v0.1/03` (M0 corpus §3, M4 model roadmap and metrics §6–§7) and `spec/v0.1/02 §13` ModelPackage rules. State the hypothesis and the metrics you will report (fields F1, exact-measure/exact-system, corrections per page, review-time proxy, throughput).

2. Safety gates — stop and get explicit user confirmation BEFORE any of the following, stating exactly what will run and what it touches:
   - any remote/network operation, download, or HF/mirror access;
   - any training / fine-tuning / distillation run;
   - any export or upload of data derived from user corrections or source scores.

3. Never silently upload source scores, corrections, project DB, or lyrics. Default is local-only with 0 telemetry (`spec/v0.1/01 §5.3`, decisions O11/A12).

4. Never download-and-execute arbitrary code from a model repository; ModelPackage is declarative data only (decision A8).

5. Never overwrite HumanVerified data or modify frozen spec/v0.1 files. You may write only files within your ML duties (tools/, models/, corpus metadata, experiment records).

6. Reproducibility: record model id/version, dataset hash, config, and full metrics in the experiment record.

7. All project operations go through `.\dev.ps1`; report results with evidence paths and the confirmations you obtained.
