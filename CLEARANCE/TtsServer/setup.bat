@echo off
REM Quick setup helper: installs Python deps and reminds you to grab voice models.

echo Installing Python dependencies...
python -m pip install -r requirements.txt
if errorlevel 1 (
    echo Pip install failed - make sure Python 3.10+ is on PATH.
    exit /b 1
)

echo.
echo Dependencies installed.
echo.
echo Next step: download voice models into .\voices\
echo See README.md for the Hugging Face links.
echo.
