# Community benchmark on NVIDIA GeForce RTX 3090 + Tesla V100-PCIE-32GB

Measured on 2026-10-17 with `tools/strata_bench.py` (suite 1, 3 runs per test, medians with ranges). Speeds are the engine's own timings; first-token times are measured by the client over HTTP. Speed only: no correctness check was run.

## Hardware and software

- GPU and VRAM: NVIDIA GeForce RTX 3090 + Tesla V100-PCIE-32GB, 56 GiB VRAM
- CPU: AMD Ryzen 9 7940HX with Radeon Graphics (16 cores, 32 threads); installed RAM: 94.7 GiB; storage: not measured
- OS: Windows 11 (10.0.26200); GPU driver: 581.80; CUDA: 13.0
- Strata: project 0.1.44.1, commit 19502dd7; engine 0.1.44.1
- Background workloads and power limits: not recorded

## Model and configuration

- Model and quantization: qwen3.8-flash-next-iq3_s-2gpu
- Context: 262144 tokens; parallel slots: 1
- Settings: the install's own configuration, unchanged; thinking off for the suite; temperature 0

```text
strata.exe --pack iq3_s --native Qwen3.8-Flash-Next-GSQ-RCO-IQ3_S-00001-of-00002.gguf --ple-gguf Qwen3.8-Flash-Next-GSQ-RCO-IQ3_S-00002-of-00002.gguf --expert-profile expert-profile.bin --expert-cache auto --prefill 2048 --spec 4 --spec-min-p 0.5 --mtp rt --max-context 262144 --kv int8 --kv-resident 32768 --control-vector-scaled Huihui-Qwen3.8-Flash-Next-refusal-direction-r.gguf:1.0 --control-vector-layer-range 1 47 --cvec-mode project --cvec-dir per-layer --vram-reserve-mib 2000
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
| short chat | 46 (43-51) | 0 | 100 (100-100) | 9 | not measured | 58.9 (54.7-79.3) | 0.40 (0.34-0.48) |
| 4K prompt, fresh | 4019 (4019-4019) | 0 (0-0) | 24 cap | 3 | 1232.9 (1230.7-1286.3) | not measured | 3.29 (3.18-3.30) |
| 16K prompt, fresh | 16058 (16058-16058) | 0 (0-0) | 24 cap | 3 | 1658.5 (1631.8-1666.6) | not measured | 9.72 (9.68-9.88) |
| agent session, follow-up turns | 14541 (14532-14576) at the last turn | see below | up to 64 | 3 | see below | 62.9 (49.3-77.2) | 2.99 (2.05-3.20) |

Ranges are the lowest and highest of the runs; `Runs` counts requests that returned a token.

### Agent session (conversation cache)

Cache hit over the follow-up turns: 68.6 % (68.4-68.7); earlier-turn tokens read again per session: 73 (4-144) (earlier turns that the cache did not hold; the 13729 (13728-13729) new tokens of the files are read once); first turn (nothing cached) first token: 1.50 s (1.47-1.50).

| Turn (run 1) | Prompt tokens | Reused | Read | Prompt tok/s | First token (s) | Decode tok/s |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 609 | 0 | 609 | 428.8 | 1.47 | 48.6 |
| 1 | 3749 | 628 | 3121 | 1006.4 | 3.156 | 62.9 |
| 2 | 5282 | 3812 | 1470 | 717.9 | 2.093 | 70.0 |
| 3 | 8726 | 5345 | 3381 | 1085.2 | 3.166 | 53.9 |
| 4 | 11613 | 8751 | 2862 | 974.0 | 2.991 | 68.9 |
| 5 | 14532 | 11633 | 2899 | 984.7 | 2.983 | 49.3 |

Per-request data: `results.json`. Engine log lines from the start: `engine-log-excerpt.txt` (when this run started the server).

## Correctness and limitations

Not measured: this is a speed suite. The conversation uses the model's own replies, so token counts differ a little between runs. Machine details come from the server, `nvidia-smi`/`rocm-smi` and the OS; background load and power limits are not recorded. Total time of the suite: 123 s.
