@echo off
REM Build all board environments in parallel and report results

setlocal EnableDelayedExpansion

set ENVS=m5stickcplus_11 m5stickcplus_2 m5_cardputer m5_cardputer_adv t_lora_pager t_display_16mb diy_smoochie
set LOGDIR=%TEMP%\pio_build_%RANDOM%
mkdir "%LOGDIR%" 2>nul

set JOBS=%1
if "%JOBS%"=="" set JOBS=4

echo Building environments with %JOBS% parallel jobs...
echo.

REM Launch all builds in parallel using start /B
set COUNT=0
set PIDS=
for %%e in (%ENVS%) do (
  echo   Started: %%e
  start "" /B cmd /c "pio run -e %%e > "%LOGDIR%\%%e.log" 2>&1 && echo 0 > "%LOGDIR%\%%e.exit" || echo 1 > "%LOGDIR%\%%e.exit""
)

REM Wait for all builds to complete by checking exit files
:waitloop
set DONE=0
set TOTAL=0
for %%e in (%ENVS%) do (
  set /a TOTAL+=1
  if exist "%LOGDIR%\%%e.exit" set /a DONE+=1
)
if !DONE! lss !TOTAL! (
  timeout /t 2 /nobreak >nul
  goto waitloop
)

echo.

REM Collect results
set PASSED=
set FAILED=
for %%e in (%ENVS%) do (
  set /p CODE=<"%LOGDIR%\%%e.exit"
  if "!CODE!"=="0" (
    set "PASSED=!PASSED! %%e"
  ) else (
    set "FAILED=!FAILED! %%e"
  )
)

echo ======================================
echo RESULTS:
echo   PASSED:!PASSED!
if defined FAILED (
  echo   FAILED:!FAILED!
  echo.
  echo Build logs in: %LOGDIR%
  exit /b 1
) else (
  echo   All builds passed!
  rmdir /s /q "%LOGDIR%" 2>nul
)