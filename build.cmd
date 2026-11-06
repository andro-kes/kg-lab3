@echo off
setlocal
cd /d "%~dp0"
if not defined LAB_TOOLS_ROOT (
    if exist "%~dp0.tools\" (
        set "LAB_TOOLS_ROOT=%~dp0.tools"
    ) else (
        set "LAB_TOOLS_ROOT=%~dp0..\lab1\.tools"
    )
)
set "VULKAN_SDK=%LAB_TOOLS_ROOT%\VulkanSDK\1.4.321.1"
set "PATH=%LAB_TOOLS_ROOT%\cmake-4.4.4-windows-x86_64\bin;%LAB_TOOLS_ROOT%\ninja;%LAB_TOOLS_ROOT%\llvm-mingw-20260922-ucrt-x86_64\bin;%VULKAN_SDK%\Bin;%PATH%"
for %%T in (
    "%LAB_TOOLS_ROOT%\cmake-4.4.4-windows-x86_64\bin\cmake.exe"
    "%LAB_TOOLS_ROOT%\ninja\ninja.exe"
    "%LAB_TOOLS_ROOT%\llvm-mingw-20260922-ucrt-x86_64\bin\clang.exe"
    "%LAB_TOOLS_ROOT%\llvm-mingw-20260922-ucrt-x86_64\bin\clang++.exe"
    "%VULKAN_SDK%\Bin\glslc.exe"
) do if not exist "%%~T" (
    echo Required tool is missing: %%~T
    echo See README.md for the required .tools layout.
    exit /b 1
)
set "LAB_OFFLINE=OFF"
if /i "%~1"=="--offline" set "LAB_OFFLINE=ON"
cmake --preset local -DFETCHCONTENT_FULLY_DISCONNECTED=%LAB_OFFLINE%
if errorlevel 1 exit /b 1
cmake --build --preset local
if errorlevel 1 exit /b 1
echo Build completed. Run run.cmd to open the application.
exit /b 0
