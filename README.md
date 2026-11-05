Made with the Witcher III Modding Tool, developed and released by CD PROJEKT RED for use with The Witcher III: Wild Hunt. © 2014 and © CD PROJEKT RED. All rights reserved. Check out the Witcher III Modding Tool here: [http://redkit.cdprojektred.com/]

# Native Quest Hook

Quest block events for The Witcher 3 Remastered, in WitcherScript: hear when a quest block starts, finishes and moves on, without changing quest files.

The game's engine still has the quest listener that REDkit's quest debugger uses. `NativeQuestHook.asi` attaches a listener to it and sends script the blocks mods ask for.

**Work in progress.** It only runs on the current Steam build (exe timestamp 0x6ADFBD7A), and switches itself off on any other build. `NQH_Test.ws` is a test listener and doesn't ship.

## Using it from a mod

Your mod needs no .asi of its own, only this mod's scripts installed.

```witcherscript
class CMyListener extends CNQH_Listener
{
	public function Init() : CMyListener
	{
		watched.PushBack("4b435b43-30c2-40d8-9eae-08df8d1b7df9");   // a block's GUID, from W3 Mod Workbench
		return this;
	}

	public function OnBlockFinished(guid : string, output : name)
	{
		// the block finished and fired output: the quest moved on
	}
}

NQH_Register('MyMod', (new CMyListener in theGame).Init());
```

Events, all on `CNQH_Listener`:

| Event | When |
|---|---|
| `OnBlockActivated(guid)` | A watched block started |
| `OnBlockFinished(guid, output)` | It finished and fired `output` (None for unnamed outputs, such as pause blocks) |
| `OnBlockActive(guid)` | It was already running: after a load, a world change, or when you registered |
| `OnBlockStopped(guid)` | It stopped without finishing: a load, leaving its world, or its quest stopping |

Other functions:

- `NQH_IsBlockActive(guid)`: whether a watched block is running now.
- `NQH_IsNativeActive()`: whether the .asi is installed and working.
- `NQH_ApiVersion()`: the version of this script API.

Without the .asi, mods still compile and run, and simply hear nothing.

## Debugging

To log every quest block, not only watched ones, put this in `bin\x64_dx12\NativeQuestHook.ini`:

```ini
[Debug]
LogEveryBlock=1
```

The log is `bin\x64_dx12\NativeQuestHook.log`.

## Building

1. Build with CMake and Visual Studio 2022 (x64). Each build installs into the game folder set by `W3_GAME_DIR`.
2. Compile the scripts with `sh tools/compile.sh`, which uses REDkit's wcc_lite.

## Licence

MIT, for the code: see `LICENSE`.
