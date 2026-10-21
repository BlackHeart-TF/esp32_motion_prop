@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Convert audio in a folder to 44.1 kHz, 16-bit, mono wavs in that same folder.
REM The second argument is the filename prefix: scream -> scream01.wav
REM   tools\convert_clips.bat raw\screams scream

if "%~2"=="" goto usage

set "FOLDER=%~1"
set "PREFIX=%~2"

where ffmpeg >nul 2>&1 || (
    echo ffmpeg not found
    exit /b 1
)
if not exist "%FOLDER%\" (
    echo not a directory: %FOLDER%
    exit /b 1
)

set N=0
for /f "delims=" %%F in ('dir /b /a-d /on "%FOLDER%"') do (
    set "EXT=%%~xF"
    if /I "!EXT!"==".wav"  call :add "%FOLDER%\%%F"
    if /I "!EXT!"==".mp3"  call :add "%FOLDER%\%%F"
    if /I "!EXT!"==".flac" call :add "%FOLDER%\%%F"
    if /I "!EXT!"==".ogg"  call :add "%FOLDER%\%%F"
    if /I "!EXT!"==".m4a"  call :add "%FOLDER%\%%F"
    if /I "!EXT!"==".aac"  call :add "%FOLDER%\%%F"
    if /I "!EXT!"==".aiff" call :add "%FOLDER%\%%F"
    if /I "!EXT!"==".aif"  call :add "%FOLDER%\%%F"
    if /I "!EXT!"==".wma"  call :add "%FOLDER%\%%F"
    if /I "!EXT!"==".opus" call :add "%FOLDER%\%%F"
)

if !N! EQU 0 (
    echo no audio files in %FOLDER%
    exit /b 1
)

for /L %%I in (1,1,!N!) do (
    set "IDX=00%%I"
    set "IDX=!IDX:~-2!"
    if %%I GEQ 100 set "IDX=%%I"
    set "NAME=!PREFIX!!IDX!.wav"
    echo !NAME!  ^<=  !FILE_%%I!
    ffmpeg -y -hide_banner -loglevel error -i "!FILE_%%I!" -ac 1 -ar 44100 -c:a pcm_s16le "%FOLDER%\!NAME!" || exit /b 1
)

if !N! GTR 64 echo warning: the firmware uses only 64 clips from each folder
echo wrote !N! files to %FOLDER%
exit /b 0

:add
set /A N+=1
set "FILE_!N!=%~1"
goto :eof

:usage
echo usage: convert_clips.bat FOLDER PREFIX
exit /b 1
