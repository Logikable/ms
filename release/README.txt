MapleStory -- an idle RPG for the terminal
==========================================

This is a trial. A character stops earning EXP at level 30, which is as far
as this release goes.


Playing it
----------

Windows: double-click play.bat.

  You can run ms.exe directly instead, but the console it opens is 80x25 by
  default and the game draws wider than that. play.bat sets the window up
  first.

Linux: run ./play.sh from a terminal, or open it from a file manager and
choose "Run in Terminal".

Either way, give the window at least 100 columns and 35 rows. There is
nothing to install and nothing beside the program that it needs: the whole
game, data included, is the one file.


Playing
-------

Tab moves between panels. Arrow keys move within one. Enter chooses, and
Escape backs out.

The game plays itself -- your character fights whatever lives on the map
you have picked, and keeps fighting while you watch. What you decide is
where to fight, what to wear, and what to spend points on.

Very little is on screen at first. The rest arrives as you level: somewhere
to put your points at 2, the equipment panel at 3, your bag at 4, an
advancement and your skills at 10, a shop at 20.


Your progress
-------------

The game saves itself. It writes to ms.save, in this folder, every thirty
seconds and again when you leave -- whether you quit from the menu, press
Ctrl+C, or close the window. Starting the game picks that save back up, so
there is nothing to load and nothing to remember to do.

There is one save. To start over, close the game and delete ms.save.

If that file is ever damaged, the game will say so and refuse to start
rather than quietly write over it. Moving it out of this folder begins a
new character.


Known limits
------------

Scrolling appears at level 30, and star force and item recovery sit above
the level cap, so this release does not reach them.
