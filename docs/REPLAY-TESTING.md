# 真实流量回放测试法（2026-10-19 建立）

> **为什么有这份文档**：BENCH 的合成 prompt 与 agent 的真实负载**不是一回事**，用它调参会被误导（§1）。
> 本文记录录制、回放、精简子集、指标口径、噪声基线，以及已经踩过的坑。**换会话/换人接手时先读这一份。**

## 0. 一句话流程

```
录制（一次）：  agent ──► record_proxy.py:8185 ──► Strata:8184 ──► agent-traffic-<日期>.jsonl
测试（每次）：  配置A → 重启引擎 → replay.py --suite 56:6,0:4 → 配置B → 重启 → 同一 suite → compare_replay.py
```

## 1. 为什么不能用 BENCH 代表真实负载

录到的 62 条真实 agent 请求（2026-10-19 18:44–18:59）：

| 形状 | BENCH 的合成负载 | **真实 agent 流量** |
|---|---|---|
| prompt tokens | 4K / 16K | **196K–207K**（99.9% 前缀复用） |
| messages 条数 | 1 | **97–224** |
| tools | 0 | **112** |
| 输出长度 | 24–100 | 543（中位，最长 4817） |
| 草稿接受率 | ~50% | **79.3%** |

⇒ 在 BENCH 上调出来的 `--prefill 4096` / `--pipeline-windows 2` 对**短 prompt 交互**有效，
但真实流量几乎全部命中前缀缓存（每条只读几百个新 token），**prefill 路径几乎不参与**。
反过来，真实流量里热的是**解码**和 **KV 流式读取**，BENCH 压不到。

## 2. 录制（`record_proxy.py`）

```
python record_proxy.py --port 8185 --upstream http://127.0.0.1:8184 --out agent-traffic-<日期>.jsonl
```

- OpenAI/Anthropic 兼容的**直通中继**：`agent → 8185 → 8184`，SSE **逐块转发不缓冲**（不改变首 token 延迟）
- 每请求一行 JSONL：完整 `request`（messages / tools / 采样参数）+ `usage` + `timings`（含 `draft_n`/`draft_n_accepted`）+ `t_first_s`/`t_total_s`
- 逐行 `flush`，掉电也留得下
- 让 agent 指向它：pi 的 `models.json` 里 `baseUrl` 改成 `http://127.0.0.1:8185/v1`（测完改回 8184）
- `--no-body` 只记形状不记内容（隐私考虑）；默认记内容，**文件只在本机**
- 只录 `POST /v1/chat/completions` 与 `/v1/messages`；GET 只转发

## 3. 回放（`replay.py`）

```
python replay.py --list                          # 看录了些什么
python replay.py                                 # 全量 62 条（约 12 分钟）
python replay.py --suite 56:6,0:4                # 精简子集（约 4 分钟，见 §4）
python replay.py --start 50 --limit 5            # 单块
```

- **按原顺序发送**：录制下来的 bodies 是一条**不断增长**的对话；顺序重放才能复现 checkpoint 行为（每条只读增量）
- body **原样发送**（同样的 messages / tools / 采样 / `stream` 标志）
- 每条之后读 `/metrics` 的 `requests[0]`（刚完成那条）：`hit_rate` / `pcie_share` / `decode_tok_s` / `prompt_tokens` / `reused`
- 结果写 `replay-<arm>.jsonl`，逐行 flush

### 3.1 单档工装 `replay_arm.cmd`

```
replay_arm.cmd <config.json> <arm-name>
```

它做：**停服务（powershell -File）→ taskkill 兜底 → 起配置 → 轮询等 `loaded=true` → 打印引擎 pid/启动时间 → 跑 replay.py**。

> ⚠ **踩过的坑**：`.ps1` 必须用 `powershell -File` 调用。写成 `call stop_strata.ps1` 会**静默失败**，
> 于是整档测的是**上一个引擎**（2026-10-19 发生过一次）。工装里现在加了 `taskkill` 兜底和引擎 pid 打印，跑之前先看那行 pid 是不是新的。

## 4. 精简子集（控制时间）

全量 62 条 = **708 s ≈ 11.8 分钟/档**。逐条耗时分布很不均：`#0` 一条就 68 s（135K token 冷读），
`#50/#60/#61` 分别 29/26/30 s（长输出）。

实测的候选块（**温暖顺序跑**时的耗时）：

| 块 | 范围 | 耗时 | prompt 规模 |
|---|---|---|---|
| A 冷启动 + 短上下文 | `#0–5` | 80 s | 135K → 137K |
| B 中段 | `#40–45` | 54 s | 183K → 187K |
| C **尾段（真实工作点）** | `#56–61` | 78 s | 204K → 207K |

⚠ **跳块要付冷读**：块首那条的 prompt 必须从头读一遍（135K ≈ 62 s；204K ≈ 95 s）。
所以单独跑某块的真实成本 = 冷读 + 块内剩余条目。

**推荐精简集：`--suite 56:6,0:4`**（约 **4 分钟/档**，覆盖冷启动 + 200K 尾段）：

```
block0@56 (#56–61, 200K 尾段)   冷读 ~95 s + 5 条增量 ≈ 170 s
block1@0  (#0–3, 冷启动)        冷读 ~62 s + 3 条增量 ≈  76 s
```

再快的办法（未采用，会破坏保真度）：截断 `max_tokens`、或只跑单块。

## 5. 指标口径（三段占比）

`/metrics` 里 `requests[0]` 的字段：

```
VRAM 命中 = hit_rate
PCIe 搬运 = pcie_share
CPU 池    = 1 - hit_rate - pcie_share
```

> 依据：`serve/web/index.html` 的列标题写明 **VRAM hit rate 不含经 PCIe 取来的专家**，那部分单独显示为 `+N% PCIe`；
> `server.py` 的 `hit_rate = hits / lookups`、`pcie_share = offloaded / routed`，所以相减即为 CPU 池份额。

**2026-10-19 的真实基线（全量 62 条）**：

| | base | rl300 | 差 |
|---|---:|---:|---:|
| decode tok/s（中位） | **108.8** | 107.5 | −1.2% |
| 首 token（中位） | 2.89 s | 2.93 s | +1.4% |
| VRAM 命中 | 96.80% | 96.65% | −0.15pp |
| PCIe | 0.20% | 0.20% | 0 |
| **CPU 池** | **3.00%** | **3.15%** | +0.15pp |
| 墙钟 | 708 s | 698 s | −1.4% |

## 6. 噪声基线（同一份流量）

`rl300` 那一档改的是**理论上无效果**的参数（`--vram-reserve-later-mib 300`，见 §8），
所以两档之差就是**噪声**：

- **decode tok/s：±1.2%**
- **首 token：±1.4%**
- 三段占比：±0.15 个百分点

⇒ **比 BENCH 稳得多**（BENCH 的 A/A 是 ±3.3% chat 解码、±9.6% agent 解码、串行跑还有 −13% 漂移）。
判据：**decode 差 >2% 才算信号**，占比差 >0.3pp 才算信号。

## 7. ⚠ 两个最容易混错的量

**① "KV streaming: N MiB read from RAM" 是读取流量，不是内存占用**

```
base  : KV streaming: 96.47% of 244,862,232 block reads hit VRAM, 34,847 MiB read from RAM
```

- `34,847 MiB` = **708 秒内累计从 RAM 读出的字节数**（均摊 ~49 MB/s），是**流量**
- **驻留占用**是另一回事：非驻留 KV cell 数 × 每 cell 字节（int8 = 1056 B，k8v4 = 816 B）× KV 层数
- 两者不能互换，也不能拿流量当"用了多少内存"来推算显存/内存压力（2026-10-19 我说错过一次）

**② 专家缓存槽位数 ≠ 覆盖率，而且每次启动都会变**

```
base  : expert cache 8642 slots (15.69 GiB)      ← 3090
rl300 : expert cache 8541 slots (15.50 GiB)
```

`expert cache auto` 按**启动那一刻的空闲显存**算（`18.55 GiB free, 1300 MiB reserved`），
桌面/浏览器占用波动就带来 **±1.2% 的槽位差异** —— 这是 hit rate 的固定噪声源，比较时要知道。

## 8. 用它已经否掉的结论

| 改动 | 结果 | 判定 |
|---|---|---|
| `--vram-reserve-later-mib 300`（给 V100 多 1 GB） | decode −1.2%（噪声内）、V100 缓存 **10752 → 10752 一个没涨**、CPU 池 +0.15pp | ❌ **无收益**。V100 那 21 层本来就是 10752/10752 = 100%，没有第二个专家可放；多出的显存被 prompt ring 吃掉，而真实流量 99.9% 前缀复用 ⇒ ring 不干活 |

## 9. 下一步该测什么（按真实流量的结论排序）

| 优先 | 项 | 为什么 |
|---|---|---|
| 1 | **`--kv k8v4`** | KV 每 cell 1056 → **816 B（−23%）**。真实场景是 200K 上下文，KV 驻留是稀缺资源，省下的字节可直接换成更多驻留或更多专家槽位 |
| 2 | `--kv-resident` **往上调** | 现在 96.5% 的 KV 读命中显存；把剩余 3.5% 也拉进显存能减少 RAM 读（代价是专家槽位） |
| ❌ | ~~`--kv-resident 20480`（往下调）~~ | 撤回：那是按 BENCH 的 4K/16K 想的，200K 场景下调低会让 RAM 读更多 |
| 3 | `--pcie-frac` 方向 | 真实流量里 PCIe 只承担 **0.20%**，CPU 池 3.0%；`STRATA_PCIE_MIN1=1` 是否能把一部分搬过去，仍未验 |

## 10. 加一个新 arm 的步骤

1. 从 `strata-iq3_s-2gpu.json` 复制一份，**只改一项**，并改 `model_name` 和 `log`（否则日志会混在一起）
2. 校验：`nargs`、无重复标志、路径存在（`--vision` 与 vision 块别丢）
3. `replay_arm.cmd <新配置> <arm名>`（先确认它打印的引擎 pid 是新的）
4. `compare_replay.py replay-base.jsonl replay-<arm>.jsonl`
5. 判据见 §6；三项都要看：decode、三段占比、墙钟

## 11. 相关文件

| 文件 | 用途 |
|---|---|
| `D:\strata-blackbox\record_proxy.py` | 录制中继（8185 → 8184） |
| `D:\strata-blackbox\agent-traffic-20261019.jsonl` | 录制下来的 62 条真实请求（56.5 MB） |
| `D:\strata-blackbox\replay.py` | 回放器（`--suite` 分块） |
| `D:\strata-blackbox\replay_arm.cmd` | 单档工装（停→起→等→回放） |
| `D:\strata-blackbox\compare_replay.py` | 两档对比（含三段占比） |
| `replay-base.jsonl` / `replay-rl300.jsonl` | 已跑的两档原始数据 |
