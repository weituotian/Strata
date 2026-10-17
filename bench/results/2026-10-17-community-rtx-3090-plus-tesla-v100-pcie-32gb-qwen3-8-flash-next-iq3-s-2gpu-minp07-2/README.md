# Community benchmark on NVIDIA GeForce RTX 3090 + Tesla V100-PCIE-32GB

Measured on 2026-10-17 with `tools/strata_bench.py` (suite 1, 3 runs per test, medians with ranges). Speeds are the engine's own timings; first-token times are measured by the client over HTTP. Speed only: no correctness check was run.

## Hardware and software

- GPU and VRAM: NVIDIA GeForce RTX 3090 + Tesla V100-PCIE-32GB, 56 GiB VRAM
- CPU: AMD Ryzen 9 7940HX with Radeon Graphics (16 cores, 32 threads); installed RAM: 94.7 GiB; storage: not measured
- OS: Windows 11 (10.0.26200); GPU driver: 581.80; CUDA: 13.0
- Strata: project 0.1.44.1, commit 19502dd7; engine 0.1.44.1
- Background workloads and power limits: not recorded

## Model and configuration

- Model and quantization: qwen3.8-flash-next-iq3_s-2gpu-minp07
- Context: 262144 tokens; parallel slots: 1
- Settings: the install's own configuration, unchanged; thinking off for the suite; temperature 0

```text
strata.exe --pack iq3_s --native Qwen3.8-Flash-Next-GSQ-RCO-IQ3_S-00001-of-00002.gguf --ple-gguf Qwen3.8-Flash-Next-GSQ-RCO-IQ3_S-00002-of-00002.gguf --expert-profile expert-profile.bin --expert-cache auto --prefill 4096 --spec 4 --mtp rt --max-context 262144 --kv int8 --kv-resident 32768 --control-vector-scaled Huihui-Qwen3.8-Flash-Next-refusal-direction-r.gguf:1.0 --control-vector-layer-range 1 47 --cvec-mode project --cvec-dir per-layer --vram-reserve-mib 1500 --pipeline-windows 2 --vision --spec-min-p 0.7
```

## Method

Run by `python tools/strata_bench.py` against the HTTP API of a Strata server (already running); model loading is not timed. One warm-up chat and a 1K prompt are discarded first. Every prompt starts with a unique tag, so the conversation cache cannot help the fresh-prompt tests.

- short chat: the three questions of `tools/ab_engine.py`, up to 100 new tokens, 3 runs
- 4K / 16K prompts: generated word lists sized by a calibration request, 24 new tokens, 3 fresh prompts each
- agent session: system prompt and four tools, then 5 turns that each add about 1500 words of a project file and continue from the model's own earlier answers (the way agent clients resend a conversation), 64 new tokens per turn, 3 sessions; the cache numbers come from the answer's usage (`cached_tokens`) and timings (`prompt_n`)
- concurrency (when run): N requests sent at the same moment, total tokens over the wall time of the burst, latency over all requests

## Results

| Configuration | Actual prompt tokens | Reused tokens | Generated tokens | Runs | Prompt tok/s median and range | Decode tok/s median and range | TTFT seconds median and range |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| short chat | 46 (43-51) | 0 | 100 (100-100) | 9 | not measured | 100.1 (89.1-145.4) | 0.21 (0.19-0.24) |
| 4K prompt, fresh | 4019 (4019-4019) | 0 (0-0) | 24 cap | 3 | 1342.6 (1323.3-1344.2) | not measured | 3.04 (3.01-3.07) |
| 16K prompt, fresh | 16058 (16058-16058) | 0 (0-0) | 24 cap | 3 | 2196.8 (2157.0-2217.0) | not measured | 7.35 (7.27-7.47) |
| agent session, follow-up turns | 14585 (14531-14594) at the last turn | see below | up to 64 | 3 | see below | 102.6 (67.3-129.2) | 2.74 (2.03-3.04) |

Ranges are the lowest and highest of the runs; `Runs` counts requests that returned a token.

### Agent session (conversation cache)

Cache hit over the follow-up turns: 68.5 % (68.4-68.7); earlier-turn tokens read again per session: 144 (74-145) (earlier turns that the cache did not hold; the 13729 (13729-13730) new tokens of the files are read once); first turn (nothing cached) first token: 1.46 s (1.44-1.48).

| Turn (run 1) | Prompt tokens | Reused | Read | Prompt tok/s | First token (s) | Decode tok/s |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 609 | 0 | 609 | 427.7 | 1.443 | 74.9 |
| 1 | 3752 | 631 | 3121 | 1108.5 | 2.841 | 90.3 |
| 2 | 5285 | 3745 | 1540 | 740.7 | 2.104 | 104.5 |
| 3 | 8729 | 5278 | 3451 | 1149.8 | 3.042 | 98.5 |
| 4 | 11617 | 8755 | 2862 | 1042.6 | 2.793 | 95.1 |
| 5 | 14531 | 11632 | 2899 | 1068.4 | 2.743 | 83.2 |

Per-request data: `results.json`. Engine log lines from the start: `engine-log-excerpt.txt` (when this run started the server).

## Correctness and limitations

Not measured: this is a speed suite. The conversation uses the model's own replies, so token counts differ a little between runs. Machine details come from the server, `nvidia-smi`/`rocm-smi` and the OS; background load and power limits are not recorded. Total time of the suite: 100 s.
