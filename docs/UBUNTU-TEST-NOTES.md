# Ubuntu 测试注意事项（本机专用）

> 配套 `docs/TRACE-AND-BUILD.md`（Windows 版）。**本文所有路径都是占位符**，到机器上再替换：
> `<STRATA_DIR>` = 仓库根，`<MODEL_ROOT>` = 模型数据根（那个含 `models/ packs/ mtp/ cvec/ iq4_xs/` 的目录），
> `<CONFIG>` = 要用的 `strata-*.json`，`<PORT>`、`<TRACE_FILE>` 同理。

---

## 0. 前提：掉电问题已结案（2026-10-17）

> **根因 = 电源（PSU），仅更换电源即修复。** 由另一台 Ubuntu 机器上的一段**瞬态负载脚本**稳定复现过同样的硬复位。
> 完整结论见 `docs/STATE-2026-10-17.md` §5。
> ⇒ **本文不再是"查掉电"的工具书**，而是"**在 Ubuntu 上把 Strata 跑起来并做对比测试**"的注意事项。

### 0.1 当年的排除清单（保留：它说明了为什么结论只能是硬件）

规律：**每一次硬复位都是 Event 41 + `BugcheckCode = 0`**，无蓝屏、无 WHEA、无 crash dump、无电源键 ⇒ **CPU 根本没走到 Windows 的崩溃处理程序**。

| 变量 | 状态 | 依据 |
|---|---|---|
| OS / 驱动 / venv / 盘 | **已排除** | 另一块盘上的全新 Win10 22H2 **同样复现** |
| 安全 / 虚拟化驱动（AMD Crash Defender、火绒、MuMu、夜神） | **已排除** | 关掉全套前后都在崩 |
| 稳态功耗 / 过热 / GPU 满载 | **已排除** ⚠ | 掉电瞬间 GPU 32–168 W、51–56 °C；有一次是**空闲**时死的；自写 AVX-512（2346 GFLOP/s）与 GPU DMA/GEMM/graph 压力全部存活 |
| 加载期 PCIe 猛灌 | **已排除** | 21–27 GB/s 预填、46.84 GiB 权重读取全部存活 |
| **引擎二进制（release vs 本地自编）** | **已排除** ⚠ | release 崩过（0.1.41、0.1.42 release，多次）；**本地自编 + 插桩版也崩**：10/14 00:15（本地 0.1.42 + 插桩，`trace-…-0015-crash-0142.log` 里带 `HEAD engine 26936`）、10/14 17:55（本地 **0.1.44.1** + 插桩，`engine/BUILD.json` 的 `source == "local"`） |
| **是否在跑 Strata** | **已排除** | 10/16 21:46 那次硬复位期间，**没有任何 Strata 进程/文件活动** |

> ✅ 事后看：这张表里唯一"没被排除、也没敢下结论"的那条 —— **供电瞬态** —— 就是真因。
> ⚠ 但「稳态功耗 / 过热 / GPU 满载」那一行要**正确读**：它只排除了"稳态"和"**单向**压力"，**完全没有排除瞬态 `dI/dt`**。
> 当时把压力测试的"存活"当成反证是**误用**。Ubuntu 上再遇到类似症状时别再犯这个错。
> ⚠ 另外，早期有一版结论是「release 崩、本地构建不崩 ⇒ 变量在编译产物」—— **该结论早已被推翻**（本地版同样崩）。**"本地构建不会崩"这个前提不要再用了。**

### 0.2 所以这份文档现在还有什么用

1. **路径与环境的核对清单**（§1–§3、§8）：从 Windows 搬到 Linux，哪些不能照搬、哪些字段必须一一改过。
2. **编译**（§4）：**要在这台机器上用 V100，Linux 侧也必须是 CUDA 12.x** —— CUDA 13 已放弃 Volta（`docs/NVIDIA_V100.md`）。这条与平台无关，是 V100 的硬约束。
3. **速度对比的前提**（§6）：PCIe 链路宽度能让 tok/s 差 **4 倍**（本机实测 26.8 / 13.4 / 6.5 GB/s 三档），**跨平台对比前两边都必须记录**。
4. **取证 playbook**（§7）：万一日后出现**新的**硬故障，Linux 有 Windows 完全没有的通道（`Xid` / `NVRM`、MCE / EDAC、persistent journal、时间戳不需要折算）。**备着，不等于预期它会发生。**
5. 无 WDDM，显存 / 电源行为更干净；`nvidia-smi -lgc` / `-pl` 在 Linux 上的生效机制与 Windows 不同（值得重验）。

> 📌 历史备注：当年观测到两个"触发窗口"（**活动结束后约 40 s 的 GPU 空闲降档**、**请求瞬间**）。既然真因是电源，它们不是软件缺陷，但**仍是电源瞬态最容易咬人的时刻** —— 拿旧电源复现时，这两个窗口最好都覆盖到。

---

## 1. 不能照搬的东西

| Windows | Linux | 备注 |
|---|---|---|
| `.venv\Scripts\python.exe` | `.venv/bin/python` | **venv 不能跨系统复制**，必须重建 |
| `engine\strata.exe` | `engine/strata`（ELF） | 名字与格式都不同 |
| `START-HERE.bat` / `BENCH.bat` / `*.cmd` | `setup.sh` / `bench.sh` / `update.sh` | 仓库里已有 Linux 版 |
| JSON 里 `"a\\b\\c"` | `"a/b/c"` | JSON 转义的双反斜杠 → 单正斜杠 |
| `lib_dirs`（指向 CUDA wheel 的 `bin\x86_64`） | 一般不需要 | 若要，指向系统 CUDA 的 **`lib64`**，不是 `bin`、不是 `x86_64` 子目录 |
| `check_env.exe <pid>` | `tr '\0' '\n' < /proc/<pid>/environ \| grep '^STRATA_'` | 确认插桩开关真进了引擎进程 |
| 旧机器配置（指向不存在的盘符） | 直接删掉/忽略 | 有历史配置指向别的机器布局，别以为"改一处就够" |

---

## 2. 配置里必须改的字段

同一个 `strata-*.json` 里，**路径散落在多个层级**，逐个核对：

| 位置 | 字段 | 坑 |
|---|---|---|
| 顶层 | `exe` | 指向引擎二进制 |
| 顶层 | `cwd` | 启动工作目录 |
| 顶层 | `tokenizer` | **不在 `args` 里，最容易漏** |
| 顶层 | `log` | 引擎日志落盘位置 |
| 顶层 | `lib_dirs` | 数组，可能有多项 |
| `args[]` | `--pack` | 专家包目录 |
| `args[]` | `--native` | 原始 GGUF 的**第 1 个分片** |
| `args[]` | `--ple-gguf` | 通常指第 2 个分片 |
| `args[]` | `--mtp` | MTP 草稿模型目录 |
| `args[]` | `--control-vector-scaled` | ⚠ 值形如 `…gguf:1.0`，**`:1.0` 是缩放系数不是路径**，批量替换路径时别把它切掉 |
| `vision{}` | `exe` / `mmproj` / `model` | 视觉路线才用到 |

改完**不要靠肉眼看**，跑一遍路径存在性校验（从 `<STRATA_DIR>` 执行，无硬编码路径）：

```bash
python3 - <<'PY'
import json, pathlib, re
ROOT = pathlib.Path(".")                       # <STRATA_DIR>
DRIVE = re.compile(r"^(?:[A-Za-z]:)?[\\/]")    # 绝对路径（含 POSIX 与 Windows 形式）
bad = 0
for f in sorted(ROOT.glob("strata-*.json")):
    raw = f.read_text(encoding="utf-8-sig")
    try:
        cfg = json.loads(raw)
    except Exception as e:
        print(f"--- {f.name}\n    JSON PARSE ERROR: {e}"); bad += 1; continue
    vals = [(f"args[{i}]", a) for i, a in enumerate(cfg.get("args") or [])
            if isinstance(a, str) and DRIVE.match(a)]
    for k in ("exe", "cwd", "tokenizer", "log"):
        v = cfg.get(k)
        if isinstance(v, str) and DRIVE.match(v):
            vals.append((k, v))
    vals += [("lib_dirs", d) for d in (cfg.get("lib_dirs") or []) if isinstance(d, str)]
    vals += [(f"vision.{k}", v) for k, v in (cfg.get("vision") or {}).items()
             if isinstance(v, str) and DRIVE.match(v)]
    miss = []
    for where, v in vals:
        s = v[:-4] if v.endswith(":1.0") else v     # --control-vector-scaled FILE:scale
        if not pathlib.Path(s).exists():
            miss.append((where, v))
    bad += len(miss)
    print(f"--- {f.name}: {len(vals)} 个路径值, {len(miss)} 缺失")
    for where, v in miss:
        print(f"    MISSING [{where}] {v}")
print("\nTOTAL MISSING:", bad)
PY
```

**必须全绿再起服务**，含 `log`/`cwd` 这类不存在的目录也要建好（有的实现不会替你创建父目录）。

---

## 3. 模型数据的两条硬约束

### 3.1 分片名不能改

`packs/*/native_experts.txt` 里存的是**文件内绝对偏移 + 可选的分片名**，不是完整路径。表头原文：

```
# strata native experts v3: layer gu_type d_type offset blob_bytes ... 
#   absolute offsets in <name>.gguf, or in the named shard beside it
```

⇒ 只要 `--native` 指到分片 1，其余分片是靠**名字在同一个目录里**找的。**改名、单独挪动任何一个分片都会废掉这套包。**整体搬迁（连同目录一起）没问题。

### 3.2 大小写必须保持原样

`models/IQ3_S`、`packs/iq3_s`、`iq4_xs` 是**混用大小写**的。NTFS 不分大小写、ext4 **分**。跨系统搬运后要核对，别顺手"统一一下大小写"——配置里写的是哪个就得是哪个。

### 3.3 搬运方式

- 用 `rsync -aH --info=progress2`（`-a` 含 `-t`，保留 mtime；`-H` 保硬链接）。不要用不保留时间戳的方式。
- 搬完校验（不写盘）：`rsync -naH --delete <src>/ <dst>/` 应**无输出**。
- **不要把模型放在 exfat / NTFS 分区上跑**：权限、大小写、`O_DIRECT` 行为都不同。
- 数据盘用 ext4 / xfs 即可，跑之前 `df -h` 确认余量（这套数据约 160 GiB）。

---

## 4. 编译（自编的理由是插桩，不是「本地版不崩」）

> ⚠ 先纠正一个前提：**自编不是因为「本地构建不会崩」**——该结论已于 10/14 被两次本地构建硬复位推翻（见 §0.1）。
> 自编的唯一理由是：**只有本地构建才带插桩**。官方 release 里没有 `STRATA_TRACE_FILE`，出事时等于没有黑匣子，而"发一个请求就断电"这种情况下没有黑匣子就永远只能猜。

```bash
./setup.sh --build --yes --no-start
```

- **找 nvcc 的顺序**（`setup.py:find_nvcc`）：`STRATA_NVCC` → `CUDA_PATH` / `CUDA_HOME` → `/usr/local/cuda*` → `/opt/cuda*`。
  - `STRATA_VCVARS` 是 **Windows 专用**，Linux 上不要设。
  - `STRATA_NVCC=<nvcc 绝对路径>` 是唯一被采纳的手工指定项。
- 依赖：`build-essential`（g++）、`git`、CUDA Toolkit；NVIDIA 驱动 **580+**（CUDA 13.0）。
- 3090 = **sm_86**。只编一个 arch 能省大量时间；默认可能覆盖 80/86/89/120。
- 插桩是 **5 个文件**：`include/strata/core/progress.hpp`、`src/program/generate.cpp`、`src/core/verify.cpp`、`src/core/mtp.cpp`、`src/core/session.cpp`。
  用 `git diff --stat` 确认仍是 5 个；补丁备份在 `our-instrumentation.patch`（合并上游前先刷新）。
- **产物校验**：
  ```bash
  cat engine/BUILD.json              # 必须 "source": "local"，且有 "version"
  strings engine/strata | grep STRATA_TRACE_FILE   # 有输出 = 插桩在里面
  ```
  若 `source` 变成 `release`、或体积突然涨到 ~3 倍，说明被官方引擎覆盖了，换回备份。
- `progress.hpp` 本身**已经是跨平台的**（`#if defined(_WIN32) _commit(...) #else ::fsync(::fileno(fp))`），Linux 上不用改。

---

## 5. 插桩与 trace（与 Windows 完全一致）

环境变量照抄 `docs/TRACE-AND-BUILD.md` §2：

| 变量 | 作用 | 默认 |
|---|---|---|
| `STRATA_TRACE_FILE=<path>` | **总开关**。设了才记录；追加打开，每行 `fflush` + `fsync` | 不设 |
| `STRATA_TRACE=1` | 把 `NOTE` 行同时镜像到 stderr（进 server 日志）；硬复位可能丢尾部，**以文件为准** | 不设 |
| `STRATA_TRACE_HEARTBEAT_S` | 心跳间隔秒；`0` = 关 | 5 |
| `STRATA_TRACE_MAX_MB` | 文件上限，超了改名 `<path>.1` 重开（新文件首行 `HEAD rotate`）；`0` = 不轮转 | 64 |
| `STRATA_WATCHDOG_S` | （上游的）请求心跳停多久就结束引擎并让 server 重启它 | 60 |

行格式：`<进程内毫秒> <KIND> <what 可含空格> <a> <b> <c> …`，**取数字要从行尾往回数**。
`HEAD` 运行头 / `STAGE` 阶段 / `NOTE` 危险操作**之前**一行 / `EV` verify 握手字 / `HEART` 心跳。

**掉电后只看一件事：文件的最后一行。** 它落在哪个 `NOTE` 上，机器就死在那个操作上。

### ⚠ Linux 专属的三个坑

1. **trace 文件绝对不能放在 tmpfs 上**（`/tmp`、`/dev/shm`、`/run`）。
   掉电重启后 tmpfs 内容**全部丢失**，你会失去唯一的证据。放 `/var/tmp`、家目录或数据盘。
   先验证：`df -T <TRACE_FILE 所在目录>` 必须显示 ext4/xfs，**不能是 tmpfs**。
2. **每行 `fsync`** ⇒ 文件所在盘的写延迟会体现在解码速度上。别放机械盘、别放网络盘、别放正在被其他东西狂写的盘。
3. **轮转自测要自己编一次**：`trace_rotate_test.cpp` 是独立小工具，只依赖 `include/`，不需要 CUDA。
   ```bash
   g++ -std=c++20 -O2 -I <STRATA_DIR>/include -o trace_rotate_test trace_rotate_test.cpp -pthread
   ```
   （`-pthread` 是因为 `progress.hpp` 用了 `std::thread` / `std::mutex`。）
   配合 `STRATA_TRACE_MAX_MB=0.02` 跑，验证轮转与 `HEAD rotate` 行。

取证工具 `read_trace.py` 是纯 Python，Linux 上直接可用：`python3 read_trace.py <TRACE_FILE>`。

---

## 6. 内存 / 显存预算，以及"能不能直接比速度"

本机是 93.7 GB RAM + 24 GB RTX 3090，贴边跑。参数含义与 Windows 相同，但有几处**平台差异会让速度不可直接比**：

- **专家缓存槽位由"启动时的空闲显存"决定。** Windows 有 WDDM 常驻开销 + 桌面/浏览器各占一份；Linux 没有 WDDM（无头时几乎全空）。
  ⇒ **同一张卡两边槽位数不同，测出来的 tok/s 不能直接对比。**
  要对比就得：控制两边桌面负载一致，并**记录启动时的空闲显存**；用 `--vram-reserve-mib` 去补偿那份差异。
- **Linux 独有开关**：
  - `STRATA_DROP_CACHE_ON_EXIT=1`：退出时把模型文件的 page cache 交还系统（该特性在 Windows 上不生效）。
  - `STRATA_IO_THREADS`：**默认 Linux 16 / Windows 4**（见 `src/platform/direct_file.cpp`），读盘并发行为不同。
- ⚠ **内存映射 / arena pin —— 跨平台时这里会变，值得记一笔**（**不再是"查 H3"，H3 已排除**；但它直接影响内存占用与速度）：
  Windows 上 `cudaHostRegister` **整段 pin ~50 GB 失败**，退回「分 44 片各 pin 42 GiB」的降级路径；同时 `SeLockMemoryPrivilege` 为 Disabled、页面文件只有 6 GB。
  Linux 上决定走哪条路的是：`ulimit -l`（memlock；官方容器用 `--ulimit memlock=-1`）、`vm.overcommit_memory` / `vm.overcommit_ratio`、`vm.max_map_count`（大量 mmap）。
  做法：先放开 `ulimit -l`，然后**在引擎日志里确认它走的是「整段 pin」还是「分片 pin 降级路径」**（照抄 Windows 那行日志的判据）。
  **两条路径的内存占用与速度不同 —— 记下走的是哪条，别把两种路径下的跑分拿来对比。**
- ⚠⚠ **PCIe 链路宽度必须在每次运行前记录 —— 这是最容易毁掉跨 OS 对比的变量。**
  本机实测：链路 **x16 时引擎自测 host→device 26.8 GB/s（`pcie_frac 0.55`）**，**x4 时只有 6.6 GB/s（引擎自适应降到 `pcie_frac 0.18`）** —— **差 4 倍**。
  引擎每次启动都会把这行打进自己日志，是免费的链路体检档案：
  ```bash
  grep 'PCIe probe' <STRATA_DIR>/strata-*.log | tail -5
  nvidia-smi --query-gpu=pcie.link.gen.current,pcie.link.width.current,pcie.link.width.max --format=csv
  ```
  ⇒ **两台/两次跑的 `PCIe probe` 值不一致时，tok/s 的差异就不是 OS 的功劳**，整组对比作废。
- **不要用 swap 换容量**：专家驻留被换出去就掉到 SSD 速度，速度结论会废。
- OOM 检查：`dmesg -T | grep -i -E 'oom|killed process'`。

---

## 7. 掉电证据在 Linux 上怎么取（替代 Event 41）

**先开 persistent journal，否则重启后什么都没有：**

```bash
sudo mkdir -p /var/log/journal
sudo systemd-tmpfiles --create --prefix /var/log/journal
sudo systemctl restart systemd-journald
journalctl --disk-usage
```

| 命令 | 判读 |
|---|---|
| `journalctl --list-boots` | 确认上一次 boot 的编号（下一条用 `-b -1`） |
| `journalctl -b -1 -k \| tail -100` | 断电前最后的内核消息 |
| `journalctl -b -1 -k -p warning` | 只看警告以上 |
| `journalctl -b -1 \| grep -iE 'panic\|oops\|BUG:\|Machine Check\|hardware error\|critical temp'` | **全空 = 与 Windows 的 `BugcheckCode=0` 同义：瞬断** |
| `journalctl -b -1 -k \| grep -iE 'NVRM\|Xid'` | ⚠ **Xid 错误是 NVIDIA 驱动的实锤线索**，Windows 上没有对等物 |
| `nvidia-smi -q -d POWER,CLOCK,TEMPERATURE,PERFORMANCE` | 功耗上限、当前功耗、温度、降频原因 |
| `cat /sys/class/hwmon/*/temp*_input` | 各传感器温度（毫摄氏度） |
| `ras-mc-ctl --errors` / `mcelog --client` | 机器检查异常（需装 rasdaemon / mcelog） |
| `journalctl -b -1 -k \| grep -iE 'EDAC\|Machine check\|mce:'` | 内存 CE/UE 计数（H1 的直接判据）。⚠ **无 ECC 内存不会报 UE，全空 ≠ 内存没问题** |
| `edac-util -v` | EDAC 计数器汇总（装了 edac-utils 才有） |
| `lspci -vv -s <GPU 的 BDF>` | ⚠ **PCIe 链路宽度的权威读法**：`LnkCap:` 给能力（`Width x16`），`LnkSta:` 给实际（`Width x4 (downgraded)`）——**Linux 会直接标出 `downgraded` 和降级原因**，Windows 只有 `Max/Current` 两个数字，看不到原因。同时看 `DevSta:` 有没有 Link Retrain / Correctable Error 计数 |

> ⚠ **两平台的时间戳语义不同，别搞混**：Windows 的 Event 41 记的是**下一次开机的时间**，不是掉电时刻（本机实测固定偏移 **+23 s**，所有"掉电时刻"都要 −23 s 折算）。
> Linux 的 journal 记的是**真实的最后一条日志时间**，不需要折算 —— 这本身就是换平台的一个好处。

**Linux 上比 Windows 好做的一次性对照实验（强烈建议）：**

```bash
sudo nvidia-smi -pl 300     # 3090 默认 420W；降到 300W 后重跑同一套 benchmark
```

若限功耗后不再复位 ⇒ 基本锁定**瞬态功耗**（电源余量 / 供电线 / 转接头）。
（Windows 上试过 `nvidia-smi -lgc` 锁频，**救不了**；`-pl` 是另一回事，值得单独验。重启会清除该设置。）

---

## 8. 起服务与 benchmark

```bash
.venv/bin/python -m serve.server --engine strata --config <CONFIG> --port <PORT>
```

- 默认只监听 `127.0.0.1`；**对外暴露必须同时给 `--api-key`**。
- **配置里的 `port` 字段要和 `--port` 一致**。`tools/strata_bench.py` 靠 `/health` 返回 `service == "strata"` 判断"已有服务可复用"；端口对不上它会以为没服务，**于是另起一个模型来抢显存**。
- `bench.sh` 不带参数时按 **mtime 取最新的** `strata-*.json`。多配置机器上这会选错模型。**永远显式传 `--config`。**
  （Windows 上踩过：最新的是 `iq3_xxs`（8080），而实际在跑的是 `iq3_s`（8184）。）
- 跨系统对比：
  ```bash
  ./bench.sh --config <CONFIG> --compare <older results folder>
  ```
  两边必须 **同一天 / 同一 commit / 同样 `--runs` / 同样桌面负载**；报告里核对 `machine.engine_version` 与 `quant` 是否真的一致。
- 已知坑（已修，勿回退）：`tools/strata_bench.py` 读 `CMakeLists.txt` 时缺 `encoding="utf-8"`，在 gbk 默认编码的中文 Windows 上会直接抛 `UnicodeDecodeError` 而整个 benchmark 起不来。**该行已补**；Linux 默认 UTF-8 本来不会触发。若从上游重拉后被还原，记得再补。

---

## 9. 跑之前的一页清单

**准备**

1. [ ] venv 是在 Linux 上**重建**的（不是从 Windows 拷来的）
2. [ ] 引擎是**自编 + 插桩**：`BUILD.json` 的 `source == "local"`，`strings engine/strata | grep STRATA_TRACE_FILE` 有输出
3. [ ] `git diff --stat` 是 5 个插桩文件
4. [ ] 所有 `strata-*.json` 路径字段改完，§2 的校验脚本 **TOTAL MISSING: 0**
5. [ ] 模型分片名未改、大小写未改
6. [ ] trace 文件所在目录 `df -T` **不是 tmpfs**
7. [ ] journald 已设为 persistent，`journalctl --list-boots` 可用
8. [ ] `ulimit -l` 已放开，**并记下引擎实际走的是「整段 pin」还是「分片 pin 降级」**（影响内存占用与速度，见 §6）
9. [ ] 记录启动基线：`nvidia-smi` 空闲显存 / 温度 / 功耗上限、`free -g`、当前 boot 号
10. [ ] ⚠ **记录 PCIe 链路宽度与引擎自测带宽**（`pcie.link.width.current` + 日志里的 `PCIe probe`）：**档位和上次不一致就不要拿去对比**（§6）
11. [ ] 引擎是**双架构（sm_70+sm_86）还是单架构**、`lib_dirs` 是否指向匹配的 CUDA 版本（§4、§5）

**跑**

12. [ ] 请求期与空闲期都跑（**空闲期是电源瞬态最容易咬人的时刻**，见 §0 的历史备注），每次记录当时的 `nvidia-smi` 状态
13. [ ] 长 prompt（≥ 32K）跑一次 —— 加载之后最重的单次负载

**万一出现新的硬复位（注意：已知的那个是电源问题，已结案）**

14. [ ] `read_trace.py <TRACE_FILE>` 读最后一行 → 死在哪一步、在请求期还是空闲期
15. [ ] `journalctl -b -1 -k | tail -100`、`grep -iE 'NVRM|Xid'`（Linux 独有通道）
16. [ ] `ras-mc-ctl --errors` / `edac-util -v`（内存 CE/UE 计数）
17. [ ] 与 Windows 侧对照：同一次负载、同一处死点吗？
18. [ ] 降功耗（`nvidia-smi -pl`）复现一次 —— 对**供电瞬态**类问题是有效判据

---

## 10. 判据（两个平台通用，**备用**）

**本机的掉电已结案 = 电源（PSU），仅更换电源即修复**（见 §0）。以下判据留着备用 —— 万一日后出现**新的**硬复位时按它判：

1. **trace 文件最后一行是完整的业务行，且系统侧没有任何 oops / panic / MCE / Xid（Linux）或 Bugcheck / WHEA（Windows） ⇒ 平台被从 OS 外部复位（供电瞬断最常见），与软件无关。**
2. 反过来，**只要 Linux 上出现 Xid / Machine Check / EDAC UE / oops**，就说明另有硬件 / 驱动侧的问题 —— 这些信息在 Windows 上根本看不到（`BugcheckCode = 0` 时它是哑的）。
3. 记下**死点落在「请求期」还是「活动结束后的空闲降档」**：对电源类问题是"什么时候耗电最猛"，对软件类问题是"哪条代码路径"，两件事。
4. **已经不用再试的方向**（全部已被证伪，见 §0.1）：换 OS、换盘、换引擎二进制（release ↔ 本地自编）、降**稳态**功耗与温度、减轻加载期 PCIe 负载、`nvidia-smi -lgc` 锁频。
