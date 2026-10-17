# 我们的日志插桩与本地编译（本机专用，勿与上游混淆）

> 一句话：**引擎源码 = 上游最新 + 我们的"掉电留痕"插桩；引擎二进制自己编**（本机两套工具链，见 §5）。
> 自编的理由**只有一条：只有本地构建才带插桩** —— 官方 release 里没有 `STRATA_TRACE_FILE`，出事时等于没有黑匣子。
>
> ⚠ **2026-10-17 更正（重要）**：本文早期版本写过"官方 release 在请求处硬复位、而**本地构建迄今没崩过**" ——
> **这条已被推翻**：本地自编 + 插桩版**同样硬复位**（10/14 00:15、10/14 17:55、10/15 02:38）。
> **掉电的真正原因已确认是电源（PSU），仅更换电源即修复** —— 完整结论见 `docs/STATE-2026-10-17.md` §5。
> ⇒ **不要再把"release vs 本地构建"当成一个变量**（那是当时的误判）；但**继续用本地插桩版**（为了黑匣子）。

---

## 1. 插桩改了什么（5 个文件）

| 文件 | 改动 |
|---|---|
| `include/strata/core/progress.hpp` | 日志设施本体：运行头 `HEAD`、阶段 `STAGE`、操作 `NOTE`、verify 握手 `EV`、心跳 `HEART`；每行 `fflush` + `fsync`/`_commit`；**大小上限轮转** |
| `src/program/generate.cpp` | 请求/加载期的阶段与操作打点 |
| `src/core/verify.cpp` | verify 窗口前后的打点 + `EV` 行（握手字） |
| `src/core/mtp.cpp` | MTP prefill draft 的捕获入口打点 |
| `src/core/session.cpp` | token 图捕获的打点（逐层） |

查看当前改动：`git diff --stat`（应为 5 个文件）。
补丁备份：`D:\strata-blackbox\our-instrumentation.patch`（`git diff` 导出，合并上游前先刷新一次）。

## 2. 环境变量开关（插桩完全是环境变量驱动）

| 变量 | 作用 | 默认 |
|---|---|---|
| `STRATA_TRACE_FILE=<路径>` | **总开关**。设了才记录：文件以追加方式打开，每行写盘并 `fsync`。**不设 = 完全不记录、零开销** | 不设 |
| `STRATA_TRACE=1` | 把 `NOTE` 行同时镜像到 stderr（进 server 的 `strata-iq3_s.log`）。硬复位可能丢尾部，**以文件为准** | 不设 |
| `STRATA_TRACE_HEARTBEAT_S=<秒>` | 心跳间隔；`0` = 关 | `5` |
| `STRATA_TRACE_MAX_MB=<n>` | 文件上限（可用小数）。超过就把当前文件改名 `<路径>.1` 并重开，新文件首行是 `HEAD rotate`；`0` = 不轮转 | `64` |
| `STRATA_WATCHDOG_S=<秒>` | （上游的，非我们加的）请求心跳停多久就结束引擎并让 server 重启它 | `60` |

## 3. 日志行格式

```
<steady_clock 毫秒> <KIND> <what 可含空格> <a> <b> <c> [d] [e] [f]
```

- **取数字要从行尾往回数**，因为 `what` 里有空格。
- `HEAD`：运行头（角色、pid、心跳周期、时刻）。换文件/轮转后也会再写一条。
- `STAGE`：阶段切换（`progress_at`）。`a`=detail（如层号/槽位数），`b`=chunk。
- `NOTE`：危险操作**之前**一行（`progress_note`）——最后一行落在哪，就说明死在哪个操作上。
- `EV`：verify 窗口的握手字（a=step，b=layer，c=aux，d=GPU ring seq，e=flag A，f=flag B）。
- `HEART`：`a`=beats，`b`=ticks，`c`=当前阶段已持续毫秒。

**掉电后只看一件事：文件的最后一行。**

## 4. 掉电之后怎么取证据

```
双击 D:\strata-blackbox\read_trace.cmd          # 直接翻译最后一行
python D:\strata-blackbox\read_trace.py <trace 文件>
```

`.1` 是轮转掉的上一代（一般不必要看：最后落盘的永远是主文件）。

## 5. 自己编译

**本机有两套 CUDA 工具链**，按要跑哪张卡选：

| 工具链 | 版本 | 覆盖架构 | 用途 |
|---|---|---|---|
| `D:\env\cuda13` | 13.4.92 | sm ≥ 75 | 只跑 3090 时够用（**CUDA 13 已放弃 Volta**） |
| `D:\env\cuda12` | **12.9.1** | 含 **sm_70** | **要跑 V100 就必须用它**（由 `cuda12_fetch.py` 装；见 `docs/NVIDIA_V100.md`） |

宿主编译器：MSVC（VS2022 Community，`vcvars64.bat`）。

### 5.1 推荐路径：双架构（3090 + V100 共用一个二进制）

```cmd
D:\strata-blackbox\build_v100_cuda12.cmd
```
它在 `build-cuda12\` 里用 CUDA 12.9.1 编
`-DCMAKE_CUDA_ARCHITECTURES="70;86" -DSTRATA_EXPERIMENTAL_SM60=ON -DSTRATA_BUILD_TESTS=OFF`。
编完**手工把 `build-cuda12\strata.exe` 拷到 `engine\`**（脚本只打印路径，不自动安装）。

### 5.2 只用 3090 时（CUDA 13 路线）

```cmd
set STRATA_NVCC=D:\env\cuda13\bin\nvcc.exe
set STRATA_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
set STRATA_BUILD_TESTS=OFF
.venv\Scripts\python.exe setup.py --build --yes --no-start
```
一键脚本：`D:\strata-blackbox\build_latest.cmd`（备份当前引擎到 `engine-before-latest\`，日志写 `build-latest.log`）。
⚠ **这条路只产出 sm_86** —— 拿它的产物覆盖 `engine\` 会让 V100 跑不了。

### 5.3 通用注意

- **必须先停服务**：引擎进程占着 `engine\strata.exe`，不停会报 `used by another process`。
- 增量编译很快；动了 `progress.hpp` 会重编所有包含它的 TU（约 2–4 分钟）。
- **产物校验（三道，缺一不可）**：
  1. `engine\BUILD.json`：`"source": "local"`、有 `"version"`。双架构时是 `"archs": [70, 86]` + `"cuda_dirs": ["D:\\env\\cuda12\\bin"]`；只跑 3090 时是 `[86]` + `D:\env\cuda13`。
  2. 二进制里应有字符串 `STRATA_TRACE_FILE`（**插桩在里面**）。
  3. 架构覆盖：`D:\env\cuda12\bin\cuobjdump.exe -lelf engine\strata.exe` 应看到**成对**的 `*.sm_70.cubin` 与 `*.sm_86.cubin`（双架构各 62 个；体积 **127.9 MB**，单架构只有 57.7 MB）。
     （若 `source` 变成 `release`、或体积 ~157 MB，说明被官方引擎覆盖了，用备份换回。）
- **回滚**：备份在 `D:\strata-blackbox\engine-cuda13-sm86\`（旧的 CUDA13/单 sm_86 版）与 `engine-before-latest\`。
- ⚠ **`build_incr.cmd` 编的是旧的 CUDA13 / 单 sm_86 `build\`** —— 编完再拷回 `engine\` 会把双架构引擎换掉。
- **配置里的 `lib_dirs` 必须与引擎的 CUDA 版本一致**：CUDA 12 的引擎要指向 `D:\env\cuda12\bin`，
  否则起服务时找不到 `cudart64_12.dll` / `cublas64_12.dll`。目前 `strata-iq3_s.json` 与 `strata-iq3_s-v100.json` 已改好，
  **`-nocap` / `-nocvec` / `iq3_xxs` / `orca-iq4_xs` 四个还没改**。

## 6. 起 / 停服务

```
起：双击 D:\strata-blackbox\start_trace_run.cmd      # 默认档 + 掉电留痕；会先删旧的 trace 文件
停：杀 serve.server 的 python 进程 + strata.exe
```

## 7. 更新上游源码同时保留插桩

```powershell
cd d:\projects\ai\Strata
git diff > D:\strata-blackbox\our-instrumentation.patch     # 1) 先备份插桩
git stash push -m "our instrumentation"                     # 2) 收起来
git -c http.proxy=http://127.0.0.1:10800 fetch --all --tags # 3) 代理已配在 git config，直接 fetch 也行
git merge --no-edit origin/main                             # 4) 合并
git stash pop                                               # 5) 贴回（有冲突就手工合）
```

实测记录（2026-10-13）：合并了 105 个提交到 `0.1.43.1` 那条线，插桩**零冲突**贴回。
贴回后务必再跑一次编译（见 §5），并检查 `git diff --stat` 仍是 5 个文件。

## 8. 注意事项

1. **用本地自编版，理由只有一个：它带插桩。**
   ~~"官方 release 会在请求处硬复位、本地构建不会"~~ —— **该结论已于 2026-10-17 被推翻**（本地版同样硬复位；真因是电源）。见 `docs/STATE-2026-10-17.md` §5。
2. 一次请求约 **2,500 行 / ~300 KB** 日志（上游自己也打了逐层进度），所以设了上限轮转；默认 64 MB。
3. 每行都 `fsync` 是"宁可慢也要落盘"的取舍；不设 `STRATA_TRACE_FILE` 时零开销（一次指针判空）。
4. `setup.py --yes` 有副作用：会生成 `run-iq3_xxs.bat` / `strata-iq3_xxs.json`（默认端口 8080），
   也可能重写模型配置。你的 `strata-iq3_s*.json` 备份在 `D:\strata-blackbox\cfg-backup\`。
5. 改了 `progress.hpp` 后**必须重编再起服务**，否则跑的还是旧二进制。
6. `nvidia-smi -lgc` 锁频：本机实测**救不了**（禁掉 P8 降档后请求照样硬复位），且重启会清除。
   事后看这是**预期的**：真因是电源瞬态，而锁频只是压平频率/电压曲线，**不动瞬态电流**。同理，`avx512_stress`
   与 GPU DMA/GEMM 压力的"存活"也不构成反证 —— 它们只覆盖**单向**压力，没覆盖 **CPU+GPU+PCIe 同时突变**。
7. 自测工具（不需要引擎、不需要 GPU）：
   - `D:\strata-blackbox\build_rot_test.cmd` → `trace_rotate_test.exe`：验证日志轮转（配合 `STRATA_TRACE_MAX_MB=0.02`）。
   - `D:\strata-blackbox\check_env.exe <pid>`：直读引擎进程的 `STRATA_*` 环境变量，确认插桩开关真的生效。
