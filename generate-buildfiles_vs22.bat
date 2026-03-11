@echo off
echo Generating Visual Studio 2022 project files for rf-rtx...
tools\premake5 %* vs2022
echo.
echo Done. Open build\rf-rtx.sln in Visual Studio 2022.
pause
