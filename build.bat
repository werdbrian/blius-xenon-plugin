@echo off
REM Xenon Plugin SDK Build Script
REM Requires: LLVM/Clang with WASM target (clang --target=wasm32)
REM
REM Usage:
REM   build.bat <file.cpp>              Build a single plugin
REM   build.bat --library <file.cpp>    Build a library plugin (exports all non-lifecycle functions)
REM   build.bat                         Build all plugins in examples/

setlocal
set CLANG=clang
set IS_LIBRARY=0
set SCRIPT_DIR=%~dp0
if "%SCRIPT_DIR:~-1%"=="\" set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%
set ORIG_DIR=%CD%

pushd "%SCRIPT_DIR%" >nul

REM Check if clang is available
where %CLANG% >nul 2>&1
if ERRORLEVEL 1 (
    echo Error: Clang not found in PATH. Please install LLVM/Clang with WASM target.
    popd >nul
    exit /b 1
)

REM Check for --library flag
if "%~1"=="--library" (
    set IS_LIBRARY=1
    shift
)

if "%~1"=="" goto BuildAll

REM Create output directory
if not exist "output" mkdir output

if "%IS_LIBRARY%"=="1" (
    call :BuildLibrary "%~1"
) else (
    call :BuildOne "%~1"
)
set EXIT_CODE=%ERRORLEVEL%
goto End

:BuildAll
set FAIL_COUNT=0
set BUILD_COUNT=0

REM Create output directory
if not exist "output" mkdir output

echo Building all plugins in examples and plugins...
for %%f in (examples\*.cpp examples\**\*.cpp plugins\*.cpp plugins\**\*.cpp) do (
    findstr /m /c:"XENON_LIBRARY_INFO" "%%f" >nul
    if not errorlevel 1 (
        call :BuildLibrary "%%f"
    ) else (
        call :BuildOne "%%f"
    )
    if ERRORLEVEL 1 (
        set /a FAIL_COUNT+=1
    ) else (
        set /a BUILD_COUNT+=1
    )
)

echo.
if not "%FAIL_COUNT%"=="0" goto BuildFail
echo All plugins built successfully: %BUILD_COUNT% plugin(s)
set EXIT_CODE=0
goto End

:BuildFail
echo Build completed with errors: %FAIL_COUNT% failed, %BUILD_COUNT% succeeded
set EXIT_CODE=1
goto End

:BuildOne
set FILE=%~1
if "%FILE%"=="" exit /b 1
if not exist "%FILE%" set FILE=%ORIG_DIR%\%~1
if not exist "%FILE%" goto BuildMissingFile
set OUTPUT=output\%~n1.wasm

echo Building plugin: %FILE%

REM -mno-bulk-memory + -mno-bulk-memory-opt: disable WASM bulk-memory ops
REM (memory.fill / memory.copy). LLVM has two separate feature flags: the
REM proposal-level feature (-bulk-memory) and the codegen opt-in for memset/
REM memcpy lowering (-bulk-memory-opt) — both must be off, or LLVM still emits
REM memory.fill for zero-initializing larger structs (e.g. `PluginEntity{}`).
REM WAMR's fast interpreter does not implement bulk-memory and traps on the
REM opcode, which makes the plugin appear to hang silently. Disabling forces
REM clang back to explicit i32.store loops.
%CLANG% --target=wasm32 ^
    -O2 ^
    -std=c++17 ^
    -nostdlib ^
    -fno-exceptions ^
    -fno-rtti ^
    -mno-bulk-memory -mno-bulk-memory-opt ^
    -Wl,--no-entry ^
    -Wl,--export=on_load ^
    -Wl,--export=on_unload ^
    -Wl,--export=on_frame ^
    -Wl,--export=on_render ^
    -Wl,--export=on_menu ^
    -Wl,--export=on_hero_changed ^
    -Wl,--export=on_get_info ^
    -Wl,--export=on_perform_action ^
    -Wl,--allow-undefined ^
    -I./include ^
    "%FILE%" ^
    -o "%OUTPUT%"

if ERRORLEVEL 1 goto BuildOneFail
echo   OK: %OUTPUT%
exit /b 0

:BuildLibrary
set FILE=%~1
if "%FILE%"=="" exit /b 1
if not exist "%FILE%" set FILE=%ORIG_DIR%\%~1
if not exist "%FILE%" goto BuildMissingFile
set OUTPUT=output\%~n1.wasm

echo Building library: %FILE%

REM Library mode: export all symbols (not just lifecycle functions)
REM This exports every non-hidden function so the host can discover and bridge them
REM -mno-bulk-memory: see comment in :BuildOne above.
%CLANG% --target=wasm32 ^
    -O2 ^
    -std=c++17 ^
    -nostdlib ^
    -fno-exceptions ^
    -fno-rtti ^
    -mno-bulk-memory -mno-bulk-memory-opt ^
    -Wl,--no-entry ^
    -Wl,--export-all ^
    -Wl,--allow-undefined ^
    -I./include ^
    "%FILE%" ^
    -o "%OUTPUT%"

if ERRORLEVEL 1 goto BuildOneFail
echo   OK: %OUTPUT% (library)
exit /b 0

:BuildMissingFile
echo   FAILED: %~1 (file not found)
exit /b 1

:BuildOneFail
echo   FAILED: %FILE%
exit /b 1

:End
if not defined EXIT_CODE set EXIT_CODE=%ERRORLEVEL%
popd >nul
exit /b %EXIT_CODE%
