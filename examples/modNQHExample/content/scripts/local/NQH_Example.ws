//---------------------------------------------------
//-- Native Quest Hook example ----------------------
//---------------------------------------------------
// Listens to the random event "At the Mercy of Strangers" in Velen (mq1036_at_the_mercy_of_strangers.w2phase),
// and writes what happens to the script log under the channel NQHExample.
// Needs modNativeQuestHook installed. Without NativeQuestHook.asi it compiles and runs, and simply hears nothing.

class CNQHExample_Mercy extends CNQH_Listener
{
	public function Init() : CNQHExample_Mercy
	{
		watched.PushBack("4b435b43-30c2-40d8-9eae-08df8d1b7df9");   // Interaction dialog: fires Help or Nope
		watched.PushBack("4f6a4f44-3e1a-40f4-bd24-ebb2b16f1232");   // Pause "Outside mq1036_encounter_01_big_tr": the player left him
		return this;
	}

	public function OnBlockActivated(guid : string)
	{
		if (guid == "4b435b43-30c2-40d8-9eae-08df8d1b7df9")
		{
			LogChannel('NQHExample', "The deserter can be talked to");
		}
	}

	public function OnBlockFinished(guid : string, output : name)
	{
		if (guid == "4b435b43-30c2-40d8-9eae-08df8d1b7df9")
		{
			LogChannel('NQHExample', "The dialogue finished: " + NameToString(output));   // Help or Nope
		}
		else
		{
			LogChannel('NQHExample', "The player left him to it");
		}
	}

	public function OnBlockActive(guid : string)
	{
		LogChannel('NQHExample', "Already running after a load: " + guid);
	}

	public function OnBlockStopped(guid : string)
	{
		LogChannel('NQHExample', "Stopped without finishing: " + guid);
	}
}

@wrapMethod
function NQH_RegisterListeners()
{
	wrappedMethod();
	NQH_Register('NQHExample_Mercy', (new CNQHExample_Mercy in theGame).Init());
}
