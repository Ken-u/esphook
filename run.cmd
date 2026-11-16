@echo off
cd /d "%~dp0"
where uv >nul 2>nul
if not errorlevel 1 (
    uv run --python 3.11 --with-requirements tools/requirements-flash.txt python run.py %*
    exit /b
)
py -3 run.py %*
