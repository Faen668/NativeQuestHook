//---------------------------------------------------
//-- Native Quest Hook: test listener ---------------
//---------------------------------------------------
// For testing only, not for release. Watches the vanilla blocks Progress on the Path's own PotP_CompleteEvent blocks
// hang off in mq1036_at_the_mercy_of_strangers.w2phase, and logs what it hears next to PotP's own facts.

class CNQH_MercyTest extends CNQH_Listener
{
	public function Init() : CNQH_MercyTest
	{
		watched.Clear();
		watched.PushBack("4b435b43-30c2-40d8-9eae-08df8d1b7df9");   // Mercy 1: interaction dialog, Help or Nope
		watched.PushBack("4f6a4f44-3e1a-40f4-bd24-ebb2b16f1232");   // Mercy 1: pause "Outside mq1036_encounter_01_big_tr"
		watched.PushBack("3dc10e27-dec1-4e47-87b0-8ab808dfb1ed");   // Mercy 2: reward "mq1036_bandits_killed"
		watched.PushBack("4161d330-fa3b-46b6-9c32-7f9c3e29975a");   // Mercy 2: reward "mq1036_bandits_reward"
		return this;
	}

	private function What(guid : string) : string
	{
		switch (guid)
		{
		case "4b435b43-30c2-40d8-9eae-08df8d1b7df9": return "Mercy 1, dialogue with the deserter";
		case "4f6a4f44-3e1a-40f4-bd24-ebb2b16f1232": return "Mercy 1, player left the area";
		case "3dc10e27-dec1-4e47-87b0-8ab808dfb1ed": return "Mercy 2, reward for killing the bandits";
		default: return "Mercy 2, reward from the bandits";
		}
	}

	private function Report(what : string)
	{
		LogChannel('NativeQuestHook', what + " | PotP's facts: event 1 = " + FactsQuerySum("PotP_Event_AtTheMercyOfStrangers_1_NG")
			+ ", event 2 = " + FactsQuerySum("PotP_Event_AtTheMercyOfStrangers_2_NG"));
	}

	public function OnBlockActivated(guid : string) { Report("activated: " + What(guid)); }
	public function OnBlockFinished(guid : string, output : name) { Report("finished: " + What(guid) + ", output " + NameToString(output)); }
	public function OnBlockActive(guid : string) { Report("already active: " + What(guid)); }
	public function OnBlockStopped(guid : string) { Report("stopped: " + What(guid)); }
}

// Called with every hello: registering again under the same id just replaces it, so once a session is enough
function NQH_RegisterTests()
{
	if (NQH_Get().HasTest())
	{
		return;
	}
	NQH_Register('NQH_MercyTest', (new CNQH_MercyTest in theGame).Init());
	NQH_Get().MarkTest();
}

@addField(CNQH_Registry)
var testRegistered : bool;

@addMethod(CNQH_Registry)
function HasTest() : bool
{
	return testRegistered;
}

@addMethod(CNQH_Registry)
function MarkTest()
{
	testRegistered = true;
}
