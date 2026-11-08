@echo off
setlocal EnableExtensions

:: Special Nights launcher: update, build and play in one go.
::
::   play.bat                       menu
::   play.bat play                  git pull + build + start the game
::   play.bat race [track] [char]   git pull + build + straight into a race
::   play.bat run                   start the game (no pull, no build)
::   play.bat runrace [track] [char]  straight into a race (no pull, no build)
::
:: Track/character are asked for when not given; Enter reuses the last ones.
:: Extra game options (e.g. --fullscreen) can go in play-options.txt, one line.

cd /d "%~dp0"

set "GAME=build-msvc-x86\Release\ctr_native.exe"
set "LAST_FILE=.play-last.cmd"
set "EXTRA="
if exist "play-options.txt" set /p EXTRA=<"play-options.txt"

set "LAST_TRACK=crash cove"
set "LAST_CHARACTER="
if exist "%LAST_FILE%" call "%LAST_FILE%"

set "MODE=%~1"
set "TRACK=%~2"
set "CHARACTER=%~3"
if /i "%MODE%"=="play" goto do_play
if /i "%MODE%"=="race" goto do_race
if /i "%MODE%"=="run" goto do_run
if /i "%MODE%"=="runrace" goto do_runrace
if not "%MODE%"=="" (
    echo Unknown option: %MODE%
    echo Use: play.bat [play^|race^|run^|runrace] [track] [character]
    exit /b 1
)

:menu
echo.
echo  ==== SPESSU NIGHT ====
echo   1  Update + build + play
echo   2  Update + build + race     (pick track and character)
echo   3  Play                      (no update, no build)
echo   4  Race                      (no update, no build)
echo   Q  Quit
echo.
set "CHOICE="
set /p "CHOICE=Choose: "
if "%CHOICE%"=="1" goto do_play
if "%CHOICE%"=="2" goto do_race
if "%CHOICE%"=="3" goto do_run
if "%CHOICE%"=="4" goto do_runrace
if /i "%CHOICE%"=="q" exit /b 0
goto menu

:do_play
call :update_and_build || goto failed
goto start_game

:do_race
call :update_and_build || goto failed
goto race_setup

:do_run
goto start_game

:do_runrace
goto race_setup

:: ---------------------------------------------------------------------------

:race_setup
if not exist "%GAME%" (
    echo The game is not built yet: choose an option that builds first.
    goto failed
)
if not "%TRACK%"=="" goto have_track
:ask_track
set "TRACK="
set /p "TRACK=Track [%LAST_TRACK%] (? lists them): "
if "%TRACK%"=="" set "TRACK=%LAST_TRACK%"
if "%TRACK%"=="?" (
    "%GAME%" --track ?
    goto ask_track
)
:have_track
if not "%CHARACTER%"=="" goto have_character
set "CHARACTER="
if "%LAST_CHARACTER%"=="" (
    set /p "CHARACTER=Character [crash] (? lists them): "
) else (
    set /p "CHARACTER=Character [%LAST_CHARACTER%] (? lists them): "
)
if "%CHARACTER%"=="" set "CHARACTER=%LAST_CHARACTER%"
if "%CHARACTER%"=="?" (
    "%GAME%" --track "%TRACK%" --character ?
    set "CHARACTER="
    goto have_track
)
:have_character
(
    echo set "LAST_TRACK=%TRACK%"
    echo set "LAST_CHARACTER=%CHARACTER%"
) >"%LAST_FILE%"

echo.
if "%CHARACTER%"=="" (
    echo Racing on %TRACK%
    "%GAME%" %EXTRA% --track "%TRACK%"
) else (
    echo Racing on %TRACK% as %CHARACTER%
    "%GAME%" %EXTRA% --track "%TRACK%" --character "%CHARACTER%"
)
goto game_exited

:start_game
if not exist "%GAME%" (
    echo The game is not built yet: choose an option that builds first.
    goto failed
)
"%GAME%" %EXTRA%
goto game_exited

:game_exited
if errorlevel 1 (
    echo.
    echo The game exited with an error. The log is "Crash Team Racing.log".
    pause
)
exit /b 0

:failed
echo.
pause
exit /b 1

:: ---------------------------------------------------------------------------

:update_and_build
echo.
echo === git pull ===
git pull --ff-only
if not errorlevel 1 goto build
:: Outside a ( ) block, so %GO_ON% is read after set /p fills it.
echo.
echo git pull failed: local changes, or no network.
set "GO_ON="
set /p "GO_ON=Build what you have anyway? [y/N] "
if /i not "%GO_ON%"=="y" exit /b 1
:build
echo.
echo === build ===
call build-msvc.bat
if errorlevel 1 (
    echo.
    echo The build failed: see the messages above.
    exit /b 1
)
exit /b 0
