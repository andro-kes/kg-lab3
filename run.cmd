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
set "VK_LAYER_PATH=%VULKAN_SDK%\Bin"
set "PATH=%LAB_TOOLS_ROOT%\llvm-mingw-20260922-ucrt-x86_64\bin;%VULKAN_SDK%\Bin;%PATH%"
if not exist "%~dp0build\lab3.exe" (
    echo Application is not built. Run build.cmd first.
    exit /b 1
)
"%~dp0build\lab3.exe"
exit /b %errorlevel%
