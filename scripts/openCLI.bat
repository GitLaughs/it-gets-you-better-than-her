@echo off

REM Navigate to src directory
cd /d "E:\it-gets-you-better-than-her\src"

REM Check if GitHub CLI is installed
where gh >nul 2>nul
if %errorlevel% neq 0 (
    echo GitHub CLI is not installed. Please install GitHub CLI first.
    pause
    exit /b 1
)

REM Display current directory and GitHub CLI version
echo Current directory: %cd%
echo GitHub CLI version:
gh --version
echo.
echo Common commands:
echo - Login to GitHub: gh auth login
echo - View repository: gh repo view
echo - Create issue: gh issue create
echo - View PRs: gh pr list
echo.

REM Start GitHub CLI interactive terminal
echo Starting GitHub CLI terminal...
echo Press any key to exit...
gh

pause