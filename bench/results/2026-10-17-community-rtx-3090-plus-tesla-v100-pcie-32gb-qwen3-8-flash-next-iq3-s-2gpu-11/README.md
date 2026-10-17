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
strata.exe --pack iq3_s --native Qwen3.8-Flash-Next-GSQ-RCO-IQ3_S-00001-of-00002.gguf --ple-gguf Qwen3.8-Flash-Next-GSQ-RCO-IQ3_S-00002-of-00002.gguf --expert-profile expert-profile.bin --expert-cache auto --prefill 4096 --spec 4 --mtp rt --max-context 262144 --kv int8 --kv-resident 32768 --control-vector-scaled Huihui-Qwen3.8-Flash-Next-refusal-direction-r.gguf:1.0 --control-vector-layer-range 1 47 --cvec-mode project --cvec-dir per-layer --vram-reserve-mib 1500 --pipeline-windows 2 --vision --spec-min-p 0.5
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
| short chat | 46 (43-51) | 0 | 100 (100-100) | 9 | not measured | 96.9 (85.1-130.6) | 0.21 (0.19-0.24) |
| 4K prompt, fresh | 4019 (4019-4019) | 0 (0-0) | 24 cap | 3 | 1341.7 (1325.7-1343.6) | not measured | 3.03 (3.01-3.07) |
| 16K prompt, fresh | 16058 (16058-16058) | 0 (0-0) | 24 cap | 3 | 2208.9 (2188.9-2210.9) | not measured | 7.30 (7.29-7.38) |
| agent session, follow-up turns | 14552 (14526-14603) at the last turn | see below | up to 64 | 3 | see below | 97.8 (71.3-112.1) | 2.73 (2.04-2.98) |

Ranges are the lowest and highest of the runs; `Runs` counts requests that returned a token.

### Agent session (conversation cache)

Cache hit over the follow-up turns: 68.8 % (68.5-68.8); earlier-turn tokens read again per session: 3 (3-75) (earlier turns that the cache did not hold; the 13729 (13728-13730) new tokens of the files are read once); first turn (nothing cached) first token: 1.47 s (1.44-1.47).

| Turn (run 1) | Prompt tokens | Reused | Read | Prompt tok/s | First token (s) | Decode tok/s |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 609 | 0 | 609 | 429.1 | 1.438 | 88.0 |
| 1 | 3755 | 633 | 3122 | 1111.0 | 2.835 | 88.0 |
| 2 | 5288 | 3819 | 1469 | 721.8 | 2.059 | 94.5 |
| 3 | 8732 | 5351 | 3381 | 1146.5 | 2.975 | 89.5 |
| 4 | 11619 | 8757 | 2862 | 1049.9 | 2.755 | 105.5 |
| 5 | 14552 | 11653 | 2899 | 1061.5 | 2.759 | 111.0 |

Per-request data: `results.json`. Engine log lines from the start: `engine-log-excerpt.txt` (when this run started the server).

## Correctness and limitations

Not measured: this is a speed suite. The conversation uses the model's own replies, so token counts differ a little between runs. Machine details come from the server, `nvidia-smi`/`rocm-smi` and the OS; background load and power limits are not recorded. Total time of the suite: 100 s.
