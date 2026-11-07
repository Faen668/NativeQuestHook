//---------------------------------------------------
//-- Native Quest Hook ------------------------------
//---------------------------------------------------
// Quest block events from NativeQuestHook.asi, for any mod to listen to in script.
//
// Extend CNQH_Listener, put the GUIDs of the quest blocks you want in watched (W3 Mod Workbench shows them),
// and register it once with NQH_Register. GUIDs can be in the standard form (4b435b43-30c2-40d8-9eae-08df8d1b7df9),
// REDkit's (4B435B43-40D830C2-DF08AE9E-F97D1B8D), in braces or without dashes, in any case. Only watched blocks are sent to script, and each listener hears only its own.
// Without the .asi nothing is sent: a mod using this still runs, it just hears nothing. NQH_IsNativeActive says which.

class CNQH_Listener
{
	public var watched : array<string>;

	// A watched block started.
	public function OnBlockActivated(guid : string) {}

	// A watched block finished and fired an output: this is what moves a quest on.
	// None for blocks whose output has no name, such as pause blocks.
	public function OnBlockFinished(guid : string, output : name) {}

	// A watched block was already running: after a load or a world change, or when you registered.
	public function OnBlockActive(guid : string) {}

	// A watched block stopped without finishing: a load, leaving its world, or its quest stopping.
	public function OnBlockStopped(guid : string) {}
}

//---------------------------------------------------

class CNQH_Registry
{
	private var ids			: array<name>;
	private var listeners	: array<CNQH_Listener>;
	private var version		: int;
	private var sent		: array<string>;   // the list as the .asi last had it: events name a block by its place in it
	private var sentVersion	: int;
	private var active		: array<string>;
	public var lastHello	: float;
	public var announced	: bool;   // NQH_RegisterListeners has been called this session

	// Registering again under the same id replaces the old listener
	public function Register(id : name, listener : CNQH_Listener)
	{
		var Idx : int = ids.FindFirst(id);

		if (Idx >= 0)
		{
			listeners[Idx] = listener;
		}
		else
		{
			ids.PushBack(id);
			listeners.PushBack(listener);
		}
		version += 1;
	}

	public function Changed()
	{
		version += 1;
	}

	public function WatchList() : string
	{
		var text	: string;
		var guid	: string;
		var Idx		: int;
		var i		: int;

		sent.Clear();
		for (Idx = 0; Idx < listeners.Size(); Idx += 1)
		{
			for (i = 0; i < listeners[Idx].watched.Size(); i += 1)
			{
				if (sent.Size() > 0)
				{
					text += ",";
				}
				guid = NQH_NormaliseGuid(listeners[Idx].watched[i]);
				sent.PushBack(guid);
				text += guid;
			}
		}
		sentVersion = version;
		return IntToString(version) + "|" + text;
	}

	public function Dispatch(listVersion : int, index : int, kind : int, output : name)
	{
		var guid	: string;
		var Idx		: int;
		var i		: int;

		if (listVersion != sentVersion || index < 0 || index >= sent.Size())
		{
			return;
		}
		guid = sent[index];

		if (kind == 1 || kind == 3)
		{
			if (!active.Contains(guid))
			{
				active.PushBack(guid);
			}
		}
		else
		{
			active.Remove(guid);
		}

		for (Idx = 0; Idx < listeners.Size(); Idx += 1)
		{
			for (i = 0; i < listeners[Idx].watched.Size(); i += 1)
			{
				if (NQH_NormaliseGuid(listeners[Idx].watched[i]) != guid)
				{
					continue;
				}
				switch (kind)
				{
				case 1: listeners[Idx].OnBlockActivated(listeners[Idx].watched[i]); break;
				case 2: listeners[Idx].OnBlockFinished(listeners[Idx].watched[i], output); break;
				case 3: listeners[Idx].OnBlockActive(listeners[Idx].watched[i]); break;
				case 4: listeners[Idx].OnBlockStopped(listeners[Idx].watched[i]); break;
				}
			}
		}
	}

	public function IsActive(guid : string) : bool
	{
		return active.Contains(NQH_NormaliseGuid(guid));
	}
}

//---------------------------------------------------
// For mods
//---------------------------------------------------

@addField(CR4Game)
var nqhRegistry : CNQH_Registry;

function NQH_ApiVersion() : int
{
	return 1;
}

function NQH_Get() : CNQH_Registry
{
	var game : CR4Game = theGame;

	if (!game.nqhRegistry)
	{
		game.nqhRegistry = new CNQH_Registry in game;
	}
	return game.nqhRegistry;
}

function NQH_Register(id : name, listener : CNQH_Listener)
{
	NQH_Get().Register(id, listener);
}

// Wrap this to register your listeners: it's called once a session, when the .asi first says hello.
//
//   @wrapMethod
//   function NQH_RegisterListeners()
//   {
//       wrappedMethod();
//       NQH_Register('MyMod', (new CMyListener in theGame).Init());
//   }
//
// You can also call NQH_Register yourself whenever you like: registering again under the same id replaces the listener.
function NQH_RegisterListeners()
{
}

// After changing a registered listener's watched list
function NQH_Changed()
{
	NQH_Get().Changed();
}

// Whether a watched block is running now, as far as the events have said
function NQH_IsBlockActive(guid : string) : bool
{
	return NQH_Get().IsActive(guid);
}

// Whether the .asi is there and sending: it says hello every second or so
function NQH_IsNativeActive() : bool
{
	var hello : float = NQH_Get().lastHello;

	return hello > 0 && theGame.GetEngineTimeAsSeconds() - hello < 5.0;
}

// A block's GUID in the standard lower-case form the .asi uses, from any of the forms watched takes:
// the standard 8-4-4-4-12 form, REDkit's 8-8-8-8 (the same 16 bytes as four little-endian uint32s),
// either in braces, or 32 hex digits without dashes (read as the standard form). Anything else comes back just lower-cased.
function NQH_NormaliseGuid(guid : string) : string
{
	var g	: string;
	var b	: string;
	var c	: string;
	var d	: string;

	g = StrLower(guid);
	g = StrReplaceAll(g, "{", "");
	g = StrReplaceAll(g, "}", "");
	g = StrReplaceAll(g, " ", "");

	if (StrLen(g) == 35 && StrMid(g, 8, 1) == "-" && StrMid(g, 17, 1) == "-" && StrMid(g, 26, 1) == "-")
	{
		// REDkit: groups 2-4 are bytes 4-7, 8-11 and 12-15 as little-endian uint32s
		b = StrMid(g, 9, 8);
		c = NQH_ReverseBytes(StrMid(g, 18, 8));
		d = NQH_ReverseBytes(StrMid(g, 27, 8));
		return StrLeft(g, 8) + "-" + StrRight(b, 4) + "-" + StrLeft(b, 4) + "-" + StrLeft(c, 4) + "-" + StrRight(c, 4) + d;
	}
	if (StrLen(g) == 32 && StrFindFirst(g, "-") < 0)
	{
		return StrLeft(g, 8) + "-" + StrMid(g, 8, 4) + "-" + StrMid(g, 12, 4) + "-" + StrMid(g, 16, 4) + "-" + StrRight(g, 12);
	}
	return g;
}

// Eight hex digits with their four bytes in the other order
function NQH_ReverseBytes(hex : string) : string
{
	return StrMid(hex, 6, 2) + StrMid(hex, 4, 2) + StrMid(hex, 2, 2) + StrMid(hex, 0, 2);
}

//---------------------------------------------------
// Called by NativeQuestHook.asi
//---------------------------------------------------

function NQH_Hello() : int
{
	var registry : CNQH_Registry = NQH_Get();

	registry.lastHello = theGame.GetEngineTimeAsSeconds();
	if (!registry.announced)
	{
		registry.announced = true;
		NQH_RegisterListeners();
	}
	return NQH_ApiVersion();
}

function NQH_WatchList() : string
{
	return NQH_Get().WatchList();
}

// kind: 1 activated, 2 finished, 3 already active, 4 stopped
function NQH_Event(version : int, index : int, kind : int, output : name)
{
	NQH_Get().Dispatch(version, index, kind, output);
}
