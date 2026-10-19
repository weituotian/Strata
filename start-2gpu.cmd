@echo off
title Strata - RTX 3090 + V100 (layer split)
cd /d "%~dp0"

rem Daily launcher for the 3090+V100 layer split. No instrumentation: STRATA_TRACE* is
rem cleared, so the trace code costs one pointer test per call (not the per-line fsync).
rem Do NOT use START-HERE.bat for daily use: it re-runs setup and can rewrite the configs.

echo.
echo   Stopping any previous instance ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0stop-strata.ps1"
set STRATA_TRACE=
set STRATA_TRACE_FILE=
set STRATA_TRACE_MAX_MB=
rem arena-skip: the V100's 21 layers (27-47) hold ALL their experts in VRAM, so the CPU never reads that part of the
rem expert arena. The engine loads it as a second block and hands the whole block back (21.7 GiB of commit), which is
rem what keeps this PC off the commit limit. Set to 0 to load one 46.8 GiB block again (no speed change either way;
rem decode measured identical, and a released blob would fail loudly rather than read zeros).
set STRATA_ARENA_RELEASE_BLOCK=1

echo.
echo   ============================================================
echo    Model : qwen3.8-flash-next-iq3_s    (IQ3_S, 256K context)
echo    GPUs  : RTX 3090 layers 0-26  +  Tesla V100 layers 27-47
echo    Tuning: --prefill 4096  --pipeline-windows 2  --spec-min-p 0.7
echo    Vision: on (CPU encoder, first image ~30 s, then cached)
echo    URL   : http://127.0.0.1:8184/
echo.
echo    Loading takes 1-3 minutes; the PC may freeze meanwhile.
echo    Close this window (or Ctrl+C) to stop the model.
echo   ============================================================
echo.

".venv\Scripts\python.exe" -m serve.server --engine strata --config strata-iq3_s-2gpu.json --port 8184 --open

echo.
echo   The model has stopped.
pause
