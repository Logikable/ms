@echo off
rem Double-click this to play.
rem
rem This deliberately does NOT resize the console. `mode con` sets the screen
rem BUFFER, and Windows Terminal leaves the window alone -- so asking for 40
rem lines inside a 30-line window left the game drawing ten rows above the top
rem of what could be seen. The game fits itself to whatever window it is given;
rem a wider one just shows more of the panels on the right.
title MapleStory

"%~dp0ms.exe" %*

rem Without this the window closes the instant the game ends, taking any
rem message it left with it.
echo.
pause
