@echo off
where py >nul 2>nul
if errorlevel 1 (
 python "%~dp0tools\build.py" %*
) else (
 py -3 "%~dp0tools\build.py" %*
)
exit /b %errorlevel%
