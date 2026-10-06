@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PikPlayer - generar instalador

echo ==============================================
echo   PikPlayer - generando PikPlayer-Setup.exe
echo ==============================================
echo.

rem --- 1. Compilador de C++ (si falta, se instala solo con winget) ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSTRIED="
:findvs
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if defined VSPATH goto :havevs
if defined VSTRIED goto :novs
set "VSTRIED=1"
where winget >nul 2>&1
if errorlevel 1 goto :novs
echo [1/9] Instalando las herramientas de compilacion de Visual Studio...
echo       Es automatico, pero tarda varios minutos y descarga unos GB. Acepta el aviso de administrador.
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-package-agreements --accept-source-agreements --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
goto :findvs
:havevs
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :novs
echo [1/9] Compilador MSVC x64 listo.

rem --- 2. libmpv ---
set "MPVCACHE=%LOCALAPPDATA%\PikPlayer-build\mpv"
if /i "%~1"=="/updatempv" goto :dlmpv
call :scanmpv "libs\mpv"
if not errorlevel 1 goto :havempv
call :scanmpv "%MPVCACHE%"
if errorlevel 1 goto :dlmpv
echo [2/9] libmpv encontrada en la cache, copiando...
call :copycache
call :scanmpv "libs\mpv"
if errorlevel 1 goto :dlmpv
goto :havempv
:dlmpv
echo [2/9] Descargando libmpv ^(solo una vez; se guarda en la cache^)...
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\get-libmpv.ps1" -Cache "%MPVCACHE%"
if errorlevel 1 goto :nompv
call :copycache
call :scanmpv "libs\mpv"
if errorlevel 1 goto :nompv
:havempv
echo [2/9] libmpv lista: %MPVDLL%

rem --- 3. Import lib + resource embed data ---
if not exist build mkdir build
if not exist dist mkdir dist
if not exist "libs\mpv\mpv.def" call :makedef
lib /nologo /machine:x64 /def:"libs\mpv\mpv.def" /name:%MPVDLL% /out:"build\mpv.lib" >nul
if errorlevel 1 goto :fail
> "build\dllname.h" echo #define MPV_DLL_NAME L"%MPVDLL%"
> "build\dll.rcinc" echo MPVDLL RCDATA "%CD:\=\\%\\libs\\mpv\\%MPVDLL%"
echo [3/9] Preparado.

rem Flags comunes: optimizado, sin símbolos de depuración, endurecido
set "CFLAGS=/nologo /std:c++17 /O2 /GL /MT /EHsc /utf-8 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /DNDEBUG /GS /Gy"
set "LFLAGS=/LTCG /OPT:REF /OPT:ICF /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /INCREMENTAL:NO /DEBUG:NONE"

rem --- 4. PikPlayer.exe ---
pushd src
rc /nologo /I ..\build /fo ..\build\app.res app.rc
set "RCERR=%errorlevel%"
popd
if not "%RCERR%"=="0" goto :fail
cl %CFLAGS% /I"libs\mpv\include" /Ibuild src\main.cpp src\usm.cpp /Fobuild\ /Fe"build\PikPlayer.exe" /link /SUBSYSTEM:WINDOWS %LFLAGS% /DELAYLOAD:%MPVDLL% build\app.res build\mpv.lib delayimp.lib
if errorlevel 1 goto :fail
echo [4/9] PikPlayer.exe compilado.

rem --- 5. PikPlayerThumbnail.dll ---
cl %CFLAGS% /LD src\thumbnail.cpp /Fobuild\thumbnail.obj /Fe"build\PikPlayerThumbnail.dll" /link /DEF:src\thumbnail.def /DLL %LFLAGS%
if errorlevel 1 goto :fail
echo [5/9] PikPlayerThumbnail.dll compilada.

rem --- 6. Uninstall.exe ---
cl %CFLAGS% src\uninstall.cpp /Fobuild\uninstall.obj /Fe"build\Uninstall.exe" /link /SUBSYSTEM:WINDOWS %LFLAGS%
if errorlevel 1 goto :fail
echo [6/9] Uninstall.exe compilado.

rem --- 7. Empaquetar + cifrar TODO en un unico payload.pkpk ---
rem    Semilla = PayloadSeed() en setup.cpp = 0xA4C4E210
cl /nologo /O2 /MT /EHsc tools\pack-payload.cpp /Fe"build\pack-payload.exe" /link /SUBSYSTEM:CONSOLE >nul
if errorlevel 1 goto :fail
build\pack-payload.exe A4C4E210 "build\payload.pkpk" "PikPlayer.exe" "build\PikPlayer.exe" "PikPlayerThumbnail.dll" "build\PikPlayerThumbnail.dll" "Uninstall.exe" "build\Uninstall.exe"
if errorlevel 1 goto :fail
echo [7/9] Paquete cifrado listo (payload.pkpk).

rem --- 8. Instalador autonomo (todo dentro de un solo .exe) ---
pushd src
rc /nologo /fo ..\build\setup.res setup.rc
set "RCERR=%errorlevel%"
popd
if not "%RCERR%"=="0" goto :fail
cl %CFLAGS% src\setup.cpp /Fobuild\setup.obj /Fe"dist\PikPlayer-Setup.exe" /link /SUBSYSTEM:WINDOWS %LFLAGS% build\setup.res
if errorlevel 1 goto :fail
echo [8/9] Instalador creado.

rem --- 9. Limpieza de artefactos intermedios sensibles ---
del /q "build\payload.pkpk" "build\pack-payload.exe" 2>nul
echo [9/9] Terminado.
echo.
echo ============================================================
echo   Instalador listo:  %CD%\dist\PikPlayer-Setup.exe
echo   Un solo .exe portable con todo cifrado dentro.
echo ============================================================
echo.
choice /c SN /m "Quieres ejecutar el instalador ahora"
if errorlevel 2 goto :end
start "" "dist\PikPlayer-Setup.exe"
:end
exit /b 0

:scanmpv
set "MPVDLL="
if not exist "%~1\include\mpv\client.h" exit /b 1
for %%F in ("%~1\*mpv*.dll") do set "MPVDLL=%%~nxF"
if not defined MPVDLL exit /b 1
exit /b 0

:copycache
if not exist "libs\mpv" mkdir "libs\mpv"
if exist "libs\mpv\mpv.def" del /q "libs\mpv\mpv.def"
xcopy "%MPVCACHE%\*" "libs\mpv\" /e /i /y /q >nul
exit /b 0

:makedef
> "libs\mpv\mpv.def" echo EXPORTS
for /f "tokens=4" %%A in ('dumpbin /exports "libs\mpv\%MPVDLL%" ^| findstr /r "mpv_"') do >> "libs\mpv\mpv.def" echo %%A
exit /b 0

:novs
echo.
echo ERROR: no se pudo instalar/encontrar el compilador de C++ de Visual Studio.
echo Instala "Build Tools para Visual Studio" ^(carga de trabajo "Desarrollo para el escritorio con C++"^) desde
echo https://visualstudio.microsoft.com/visual-cpp-build-tools/ y vuelve a ejecutar Compilar.bat.
goto :fail2

:nompv
echo.
echo ERROR: no se pudo obtener libmpv.
echo Comprueba la conexion a Internet o extrae manualmente el paquete mpv-dev en libs\mpv
echo ^(ver libs\mpv\PON-AQUI-LIBMPV.txt^).
goto :fail2

:fail
echo.
echo ERROR de compilacion. Revisa los mensajes de arriba.
:fail2
echo.
pause
exit /b 1
