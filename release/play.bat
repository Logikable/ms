@echo off
rem Double-click this to play. It only sets the window up and starts the game.
rem
rem The console opens at 80x25 by default, which is narrower than the game
rem draws; without this the panels wrap into each other.
mode con: cols=120 lines=40
title MapleStory

"%~dp0ms.exe" %*

rem Without this the window closes the instant the game ends, taking any
rem message it left with it.
echo.
pause
