# openQ4 (Legacy Notes)
Quake 4 SDK integrated with the open-source engine base

![alt text](https://i.ibb.co/V3Mw0Rv/unknown-1.png)
![alt text](https://i.ibb.co/TTHTTkQ/image.png)

INSTALLATION:
	Sound: This project uses OpenAL audio code that doesn't support OGG sound files. You need to convert them. Go to q4base/sound and open convert.bat. Fix up the paths, and then run convert.bat. This will convert all the ogg files to wav files(they need to be extracted first, don't forget zpak_english.pk4!). 

This project is a code integration of the Quake 4 SDK against an open-source engine base. 
This means this includes new engine side features that were in the Quake 4 engine,
and some adjustments to the Quake 4 SDK code.

The game is at visual partity with the original game, and is basically entirely playable(with a known issue see below) with the exception of the particle system which was never open sourced(BSE or Basic Set of Effects).

Progression Blocker:
There is a known bug were some doors don't open and have to be noclipped past. 

Also the main menu works visually, but when you hit one button on the main menu it goes black, to start a game simply type devmap game/airdefense1 in the console. 
