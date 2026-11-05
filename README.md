Made with the Witcher III Modding Tool, developed and released by CD PROJEKT RED for use with The Witcher III: Wild Hunt. © 2014 and © CD PROJEKT RED. All rights reserved. Check out the Witcher III Modding Tool here: [http://redkit.cdprojektred.com/]

# Native Quest Hook

**Quest events for The Witcher 3 Remastered, in WitcherScript.** Find out when a quest block starts, finishes and moves on, without changing a single quest file.

Mods that follow quest progress have always had to edit `.w2phase` files to add script or fact blocks. Those edits are what makes quest mods conflict with each other. Native Quest Hook does away with that. Your mod names the blocks it cares about by their GUID, and it hears about them as the game runs them.

```witcherscript
public function OnBlockFinished(guid : string, output : name)
{
	// "Interaction dialog" in At the Mercy of Strangers finished, and the player chose Help
}
```

## How it works

The game's engine still has the quest listener REDkit's quest debugger uses. Every quest thread calls it whenever a block activates or deactivates. Nothing listens in a normal game.

- **It listens:** `NativeQuestHook.asi` attaches a listener of its own. There's no detour on quest code: it runs off the quest system's own tick.
- **Cheap in the engine:** the engine's callbacks only copy what happened. Everything else waits until the quest tick has finished.
- **Filtered:** only the blocks mods have asked for are passed on to script, through a plain typed call. About 10,000 block events can fire around a single load, so that matters.
- **Moved on, with the output:** when a block finishes, the hook reads the output it fired, and where that output leads, from the block's own links.
- **Loads and world changes:** it tells real progress apart from blocks the game is only restoring after a load, a fast travel or a world change.

## Installing

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader): its `dinput8.dll` goes in `bin\x64_dx12`. Mods such as Progress on the Path NG already ship it.
2. Copy `NativeQuestHook.asi` into `bin\x64_dx12`.
3. Copy `modNativeQuestHook` into `mods`.

It needs no settings. Its log is `bin\x64_dx12\NativeQuestHook.log`.

## Using it in your mod

Your mod needs no .asi or C++ of its own: only `modNativeQuestHook` installed beside it. If a player doesn't have `NativeQuestHook.asi`, your mod still compiles and runs, and its listener simply hears nothing.

**1. Find the GUIDs.** Every quest block has a GUID that stays the same across patches and mods:

- Brothers In Arms keeps 130,541 of the vanilla game's 130,545.
- Progress on the Path keeps every one of Brothers In Arms'.

Decode the phase with [W3 Mod Workbench](https://github.com/Faen668/W3ModWorkbench) (`--to-json`) and look for the block's `guid`. Or set `LogEveryBlock=1` (see Debugging) and play the quest.

**2. Write a listener.**

```witcherscript
class CMyMod_Listener extends CNQH_Listener
{
	public function Init() : CMyMod_Listener
	{
		watched.PushBack("4b435b43-30c2-40d8-9eae-08df8d1b7df9");   // Interaction dialog: Help or Nope
		watched.PushBack("4f6a4f44-3e1a-40f4-bd24-ebb2b16f1232");   // Pause: the player left the area
		return this;
	}

	public function OnBlockFinished(guid : string, output : name)
	{
		if (guid == "4b435b43-30c2-40d8-9eae-08df8d1b7df9" && output == 'Help')
		{
			// the player helped him
		}
	}
}
```

**3. Register it.**

```witcherscript
@wrapMethod
function NQH_RegisterListeners()
{
	wrappedMethod();
	NQH_Register('MyMod', (new CMyMod_Listener in theGame).Init());
}
```

`NQH_RegisterListeners` is called once a session. You can also call `NQH_Register` yourself at any time; registering again under the same id replaces your listener.

A complete example is in [`examples/modNQHExample`](examples/modNQHExample).

### Events

| Event | When |
|---|---|
| `OnBlockActivated(guid)` | A watched block started |
| `OnBlockFinished(guid, output)` | It finished and fired `output`, which is what moves a quest on. `None` for blocks whose output has no name, such as pause blocks. |
| `OnBlockActive(guid)` | It was already running when you registered, or the game restored it after a load or a world change |
| `OnBlockStopped(guid)` | It stopped without finishing: a load, leaving its world, or the quest moving past it |

### Functions

| Function | |
|---|---|
| `NQH_Register(id, listener)` | Registers a listener, or replaces the one with that id |
| `NQH_Changed()` | Call after changing a registered listener's `watched` |
| `NQH_IsBlockActive(guid)` | Whether a watched block is running now |
| `NQH_IsNativeActive()` | Whether `NativeQuestHook.asi` is installed and working |
| `NQH_ApiVersion()` | The script API's version, 1 for now |

### Good to know

- **`OnBlockFinished` comes one tick before the next blocks run.** That makes it early enough to stand in for a script block you'd otherwise add to a quest.
- **One output can lead to several blocks, and blocks can join and loop.** Watch the block whose finishing you care about, not the one after it.
- **A GUID is unique across the game and its DLC.** One shared external phase can still run in several places at once, though. Mods can also copy blocks and create duplicates.

## Debugging

To log every quest block the game runs, not only watched ones, create `bin\x64_dx12\NativeQuestHook.ini` with:

```ini
[Debug]
LogEveryBlock=1
```

Each line shows:
- the block's class, name and GUID;
- the phase it's in;
- the output it fired;
- the GUIDs of the blocks that output leads to.

The log can be read while the game is running.

## Game versions

It knows the current Steam build of The Witcher 3 (exe timestamp `0x6ADFBD7A`), and checks the code at every address it uses.

On any other build it searches the exe for everything it needs. If any of it can't be found exactly, it switches itself off and logs why. It never guesses. Your listeners then hear nothing, and `NQH_IsNativeActive` returns false.

## Building

- **The .asi:** CMake and Visual Studio 2022 (x64). Set `W3_GAME_DIR` to install into your game folder after each build.
- **The scripts:** `sh tools/compile.sh` compiles them with REDkit's `wcc_lite` into `precompiled.rsblob`.

The research behind it, from which functions to call to the layouts of quest blocks and threads, is summed up in the source's comments.

## Licence

The code is [MIT](LICENSE), © 2026 Faen90. It's free to use, change and build on.

The Witcher 3 and REDkit are CD PROJEKT RED's. This is an unofficial mod, and isn't endorsed by them.
