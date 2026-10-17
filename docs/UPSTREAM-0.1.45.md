# 上游 v0.1.45 研究笔记（2026-10-17，**未升级**）

> **状态**：仅研究，**没有合并**。本机仍在 **0.1.44.1**（分支 `local/instrument`，带插桩）。
> **来源**：GitHub release notes（`Niko1221/Strata` v0.1.45，发布 2026-10-17 06:53，commit `477ac13c`）
> ＋ 本地 `git log --oneline v0.1.44.1..v0.1.45` 的实测统计。

## 0. 一句话

**264 个提交 / 267 个文件 / +30785 −1289**。主题是 **Qwen3.6-35B-A3B 支持读图 + 多请求并发（最高 +40%）+ 单请求提速**。

**关键前提**：`Qwen3.8-Flash-Next` 系列（本机模型）的输出与 **0.1.44.1 逐字节一致** —— 上游在 CUDA **sm_60 / sm_86**、HIP gfx1201、Intel A750、Mac M4 上做过一致性测试。
⚠ 但**没有 sm_70（V100）**。

## 1. 对本机直接相关的 6 条

| # | 变更 | 对我们的影响 |
|---|---|---|
| 1 | **默认 prompt chunk 变大**（4096 / 2048 / 1024，按显存；`--prefill 512` 恢复旧行为） | 上游把"更大 chunk 更快"做成了默认 —— **这正是本机手工调出的 `--prefill 4096`**。升级后要核对双卡下的新默认值，可能与我们设的重合或冲突 |
| 2 | **新增 `--prefill-pipe`**（#1441，opt-in）：**按层切分的流水线**分配 chunk，b/a 由 `--calibrate` 定 | 本机手工调的正是这块（`--prefill 4096` + `--pipeline-windows 2`）。升级后**有官方版可对比** |
| 3 | **层切分 auto 改进**（#2644）：两卡机器若首选切法出现"很小的一段"，先试各卡份额再走三层规则 | **本机 K=27 可能会变** ⇒ §10 的切分结论要在新版上重做 |
| 4 | **修复 `attention.compress_ratios` 全 0**（#2396）：0.1.44 会拒绝 OrcaRouter 的 Qwen3.8-Flash-Next-Uncensored GGUF（`compress_ratios[3] = 0`） | **本机 `strata-orca-iq4_xs.json` 那个模型在 0.1.44 上加载不了** —— 这是升级最实际的理由 |
| 5 | **图像 token 默认值变了**：默认最多 **300** 图像 token；`--vision-tokens 1024` 提高细节 | ⚠ 本机配置写的是 `max_tokens: 1024 / min_tokens: 1024` —— **同名不同义，升级要核对** |
| 6 | **24 GB 以下 NVIDIA 卡默认改用 CPU 编码器**（#2406）；GPU 编码器占约 1.4 GB VRAM | 与本机手动配的 `"gpu": false` **方向一致** ✓ |

**顺带值得一试的 opt-in**：`STRATA_PCIE_BALANCE=1` —— 双 A4000 上 +5.6%（单卡略慢）。本机是**异构双卡**，可能有收益。

## 2. 对本机没用 / 明确别碰

- **`--parallel N`（并发 1–8）+ `--batch-mtp`**：多用户/多 agent 场景。**代价写得很明确**：显存放不下全部专家时，**单请求慢 4–11%**（RTX 3060 实测 −4%，A4000/P100 实测 −7~−11%）。本机 79% 专家常驻、单人使用 ⇒ **不要开**。
- 一大串多用户服务器开关（`STRATA_SLOT_COPY_DIRECT/BATCHSYNC`、`STRATA_CKPT_POOL/THREADS`、`STRATA_ADMIT_*`、`STRATA_BATCH_SELF_COMMIT`、`STRATA_QSA_SLOTS`、`STRATA_HANDOFF_PIPE`、`-DSTRATA_MMQ_TAB=ON`…）
- AMD / Intel / Mac 专项（`STRATA_HIP_WMMA`、Arc kernel、Metal、M 系列小内存修复）
- ⚠ **明确不要用**：`STRATA_SIDE_MIXER=2`、`STRATA_SH_STREAM=1`（gfx1201 上掉到 **0.41× / 0.44×**）、`STRATA_QSA_WARP=1`（**Tesla P100 崩溃**）、`STRATA_SLOT_CKPT_MOVE=1`（实验性）

## 3. 三个风险（都与本机配置直接相关）

1. **Volta/Pascal 优化被推迟到 0.1.46**。Release notes 原话：合入后会让 RTX 3060 在长 prompt 之后**慢约 4%**，正在定位。
   ⇒ **0.1.45 不会让 V100 变快**，而 V100 正是本机流水线的瓶颈段（21 层 × 0.66 ms > 27 层 × 0.48 ms）。
2. **一致性验证不含 sm_70**：测的是 sm_60 / sm_86 / HIP / Arc / Mac。**V100 的性能与答案在 0.1.45 上都没有上游验证。**
3. **Windows 硬件整体未测**（release notes 只说"Windows zip 包含引擎且能启动"）。本机组合 = **Windows + 3090+V100 异构层切分**，完全在测试矩阵之外。

## 4. 升级的冲突成本（本地实测）

| 插桩文件 | 上游 264 个提交里的改动 | 冲突风险 |
|---|---|---|
| `include/strata/core/progress.hpp`（插桩主体 167 行） | **0 个 commit** | ✅ **零冲突** |
| `src/program/generate.cpp`（插桩 43 行） | **54 个提交，+1981/−162** | ⚠ **高风险，需手工解** |
| `src/core/verify.cpp`（13 行） | 27 个提交，+733/−162 | ⚠ 中高 |
| `src/core/mtp.cpp`（6 行） | 4 个提交，+45/−8 | 低 |
| `src/core/session.cpp`（4 行） | 1 个提交，+2/−1 | 很低 |
| `tools/strata_bench.py`（编码修复 +2/−1） | 1 个提交，+11/−2 | 低（要合并我们的修复） |

**好消息**：插桩 167 行主体在 `progress.hpp`，**上游一次都没动**。冲突几乎全在 `generate.cpp`。

## 5. 建议

**短期（不升）**：现有 0.1.44.1 + 本机调优已验证能跑（`--prefill 4096` / `--pipeline-windows 2` / `--spec-min-p 0.7`），且 v0.1.45 的提速点（更大 chunk）**我们已经在做了**。

**要升的充分理由**：① orca（Uncensored GGUF）能加载了；② `--prefill-pipe` + 层切分 auto 改进可能给出更好的 K；③ 更省 VRAM 的 CPU 视觉编码器默认（我们已手动做到）。

**升级流程**（细节见 `docs/TRACE-AND-BUILD.md` §7）：

```
备份 engine\ 与全部 strata-*.json（cfg-20261017-pre-*）
→ git fetch origin → git checkout main → git merge --no-edit origin/main
→ git checkout local/instrument → git rebase main（解 generate.cpp）
→ build_v100_cuda12.cmd → 拷到 engine\ → 起服务冒烟
→ 重跑 BENCH，与以下基线对比：
    4K 1345.5 / 16K 2222.0 / 短问答 93.7 / agent 99.7（无插桩、prefill 4096 + pw2 + spec-min-p 0.7）
→ 特别核对三处：--prefill 的新默认、--vision-tokens 的语义、layer split 的 K 是否还是 27
```

**或者等 0.1.46**（Volta 优化回来 + 新特性更成熟），代价是继续留在 0.1.44.1。
