@echo off
REM =============================================
REM AquaBeacon MAIN board self-test loop
REM   build -> flash -> monitor -> analyze -> pass/fail
REM   (ASCII + CRLF for reliable cmd.exe parsing)
REM
REM   usage: scripts\run_test.bat [child|parent] [experiment_name] [--expect-buttons]
REM     child (default): env pico2w, port CM_PORT   -> reports\YYYYMMDD_HHMMSS_<name>\
REM     parent         : env parent, port PM_PORT
REM     --expect-buttons: wait for the first button press, then record; require UP/ENTER/DOWN
REM =============================================

REM ========== set to match your hardware ==========
set CM_PORT=COM10
set PM_PORT=COM5
set MONITOR_SEC=30
REM ================================================
set PYTHONIOENCODING=utf-8

set BOARD=child
set PIO_ENV=pico2w
set PORT=%CM_PORT%
set EXP_NAME=
set EXTRA=

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="child"  ( set BOARD=child&  set PIO_ENV=pico2w& set PORT=%CM_PORT%& shift & goto parse )
if /I "%~1"=="parent" ( set BOARD=parent& set PIO_ENV=parent& set PORT=%PM_PORT%& shift & goto parse )
if /I "%~1"=="--expect-buttons" ( set EXTRA=--expect-buttons& shift & goto parse )
set EXP_NAME=%~1
shift
goto parse
:parsed
if "%EXP_NAME%"=="" (
    if "%EXTRA%"=="" ( set EXP_NAME=%BOARD%_bench ) else ( set EXP_NAME=%BOARD%_buttons )
)

set TIMESTAMP=%DATE:~0,4%%DATE:~5,2%%DATE:~8,2%_%TIME:~0,2%%TIME:~3,2%%TIME:~6,2%
set TIMESTAMP=%TIMESTAMP: =0%
set REPORT_DIR=reports\%TIMESTAMP%_%EXP_NAME%

echo =============================================
echo  MAIN self-test   board=%BOARD% env=%PIO_ENV% port=%PORT%
echo  report: %REPORT_DIR%
echo =============================================

echo.
echo [1/4] Building (%PIO_ENV%)...
call pio run -e %PIO_ENV%
if errorlevel 1 (
    echo [ERROR] build failed
    exit /b 1
)

echo.
echo [2/4] Flashing (%PORT%)...
call pio run -e %PIO_ENV% -t upload --upload-port %PORT%
if errorlevel 1 (
    echo [ERROR] upload failed - check %PORT%
    exit /b 1
)

echo.
echo Waiting for Pico USB re-enumeration (5s)...
ping -n 6 127.0.0.1 > nul

echo.
echo [3/4] Monitoring (%MONITOR_SEC%s)...
set WAIT_OPT=
if not "%EXTRA%"=="" (
    echo   *** press UP / ENTER / DOWN, then rotate / tilt the board ***
    echo   *** recording starts at the first button press, waits up to 300s ***
    set WAIT_OPT=--wait-for-btn 300
)
python scripts\serial_monitor.py --port %PORT% --duration %MONITOR_SEC% --report-dir %REPORT_DIR% --set-rtc %WAIT_OPT%
if errorlevel 1 (
    echo [ERROR] serial monitor failed
    exit /b 1
)

echo.
echo [4/4] Analyzing...
REM analyze_log.py accepts the report directory (it reads the result CSV inside)
python scripts\analyze_log.py %REPORT_DIR% %EXTRA%
if errorlevel 1 (
    echo.
    echo [FAIL] see %REPORT_DIR%
    exit /b 1
) else (
    echo.
    echo [PASS] see %REPORT_DIR%
    exit /b 0
)
