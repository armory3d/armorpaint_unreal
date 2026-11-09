@echo off
setlocal

rem Builds the plugin and packages it into out\armorpaint_unreal.zip
rem Usage: build.bat [path\to\UE_5.x]
rem Engine path can also be set via the UE_ROOT environment variable.
rem If neither is given, the newest engine in "C:\Program Files\Epic Games" is used.

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "OUT=%ROOT%\out"
set "STAGE=%OUT%\build\ArmorPaint"
set "ZIP=%OUT%\armorpaint_unreal.zip"

if not "%~1"=="" set "UE_ROOT=%~1"
if not defined UE_ROOT (
	for /d %%D in ("C:\Program Files\Epic Games\UE_*") do set "UE_ROOT=%%D"
)
if not defined UE_ROOT (
	echo Unreal Engine not found. Pass the engine path as an argument or set UE_ROOT.
	exit /b 1
)

set "UAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"
if not exist "%UAT%" (
	echo RunUAT.bat not found at "%UAT%"
	exit /b 1
)

echo Using engine: %UE_ROOT%

if exist "%OUT%\build" rmdir /s /q "%OUT%\build"
if exist "%ZIP%" del /q "%ZIP%"

rem BuildPlugin generates a template Config\FilterPlugin.ini when missing; remove it afterwards
set "HAD_CONFIG=0"
if exist "%ROOT%\Config" set "HAD_CONFIG=1"

call "%UAT%" BuildPlugin -Plugin="%ROOT%\ArmorPaint.uplugin" -Package="%STAGE%" -TargetPlatforms=Win64 -Rocket
set "UAT_RESULT=%ERRORLEVEL%"
if "%HAD_CONFIG%"=="0" if exist "%ROOT%\Config" rmdir /s /q "%ROOT%\Config"
if not "%UAT_RESULT%"=="0" (
	echo Plugin build failed.
	exit /b 1
)

rem Intermediate build files and debug symbols are not needed to load the plugin
if exist "%STAGE%\Intermediate" rmdir /s /q "%STAGE%\Intermediate"
del /s /q "%STAGE%\*.pdb" >nul 2>&1

tar -a -c -f "%ZIP%" -C "%OUT%\build" ArmorPaint
if errorlevel 1 (
	echo Failed to create zip.
	exit /b 1
)

rmdir /s /q "%OUT%\build"
echo Created %ZIP%
endlocal
