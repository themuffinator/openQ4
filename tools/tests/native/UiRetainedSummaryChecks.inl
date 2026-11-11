// The match summary card (ui-visual-design.md 14.14): the Escape card's
// construction and size; the result in the winners' band; the standings with
// places, tints, your marker and the best weapons; the chat with its tags and
// the field that asks the session to type; the typing prompts; the STATISTICS
// page's match awards and the SCOREBOARD page's columns; Back returns.
static void CheckSummary(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	const bool valid = document.Load(source,diagnostics);
	for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"mp_summary %s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
	Check(valid && document.Model().id == "openq4.mp_summary" && document.Model().canvasHeight == 720,"the summary card validates");
	CheckSessionActions(document);
	const auto& root = document.Model().root;
	Check(root.children.size() == 3 && root.children[0].id == "scene-softfocus" && root.children[1].id == "scrim" &&
		root.children[2].id == "chrome","the softened view, its scrim stand-in, then the card");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/mp_summary.rui",diagnostics),"the summary card loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error;
	Runtime::EventEffects effects;
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a summary node is laid out"); return b; };
	const auto number = [&](const char* node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented value"); return static_cast<float>(value->data[0]);
	};
	const auto text = [&](const char* node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented keyword"); return value->text;
	};
	const auto asked = [&](const char* command) {
		return effects.actions.size() == 1 && std::get<std::string>(effects.actions[0].arguments.at("command")) == command;
	};
	// A team match the Marines won 12 to 7: your team first, you on its
	// second row with the railgun your best weapon, then the Strogg; a team
	// line, a server line and your own line in the chat.
	Check(runtime.SetState({{"mp.title",std::string("THE FRAGGING YARD")},{"mp.mode",std::string("Team DM")},{"mp.clock",std::string("0:09")},
		{"mp.score",std::string("MARINES 12 - 7 STROGG")},
		{"mp.summary.result",std::string("MARINE TEAM WINS")},{"mp.summary.result_color",0.0},{"mp.summary.result_score",std::string("12 - 7")},
		{"mp.summary.teams",true},{"mp.summary.channels",true},
		{"mp.summary.p0.shown",true},{"mp.summary.p0.band",true},{"mp.summary.p0.title",std::string("MARINES")},{"mp.summary.p0.color",0.0},
		{"mp.summary.p0.score",std::string("12")},{"mp.summary.p0.count",3.0},
		{"mp.summary.p0r0.name",std::string("Anderson")},{"mp.summary.p0r0.score",std::string("6")},{"mp.summary.p0r0.w0",4.0},
		{"mp.summary.p0r1.name",std::string("Kane")},{"mp.summary.p0r1.clan",std::string("RHINO")},{"mp.summary.p0r1.score",std::string("4")},
		{"mp.summary.p0r1.you",true},{"mp.summary.p0r1.w0",6.0},{"mp.summary.p0r1.w1",0.0},
		{"mp.summary.p0r2.name",std::string("Voss")},{"mp.summary.p0r2.score",std::string("2")},
		{"mp.summary.p1.shown",true},{"mp.summary.p1.band",true},{"mp.summary.p1.title",std::string("STROGG")},{"mp.summary.p1.color",1.0},
		{"mp.summary.p1.score",std::string("7")},{"mp.summary.p1.count",2.0},
		{"mp.summary.p1r0.name",std::string("Gladiator")},{"mp.summary.p1r1.name",std::string("Makron")},
		{"mp.summary.chat0.live",true},{"mp.summary.chat0.kind",1.0},{"mp.summary.chat0.team",1.0},{"mp.summary.chat0.name",std::string("Makron")},
		{"mp.summary.chat0.text",std::string("Regroup at the flag")},
		{"mp.summary.chat1.live",true},{"mp.summary.chat1.kind",2.0},{"mp.summary.chat1.text",std::string("Kane connected")},
		{"mp.summary.chat2.live",true},{"mp.summary.chat2.kind",0.0},{"mp.summary.chat2.you",true},{"mp.summary.chat2.name",std::string("Kane")},
		{"mp.summary.chat2.text",std::string("Good game everyone")}},error,1),"publish a team summary");
	host.softFocus = true;
	Check(runtime.RunEvent("open",2,effects,error),"the summary card opens");
	runtime.Frame(viewport,2.3);
	const auto card = bounds("card");
	Check(Near(card.x+card.width/2,640) && Near(card.y+card.height/2,360),"the summary card is centered");
	Check(card.width >= 648-.5f && card.width <= 928+.5f && Near(card.height,477,1),"the summary card takes the Escape card's size");
	Check(runtime.FocusedControl() == "summary-chat-field","the chat field has focus as the card opens");
	Check(text("summary-result-band-0","display") == "block" && text("summary-result-band-3","display") == "none","the Marines' band holds their win");
	Check(text("summary-result-title","text") == "MARINE TEAM WINS" && text("summary-result-score","text") == "12 - 7","the result and the final score");
	const auto result = bounds("summary-result"), standings = bounds("summary-standings"), chat = bounds("summary-chat");
	Check(standings.y >= result.y+result.height && chat.y >= result.y+result.height && standings.x+standings.width <= chat.x,
		"the standings and the chat stand side by side under the result");
	Check(text("summary-p0-band","display") == "block" && text("summary-p1-band","display") == "block" &&
		text("summary-p0-band-0","display") == "block" && text("summary-p1-band-1","display") == "block","each team's panel opens with its band");
	Check(text("summary-p0r2","display") == "block" && text("summary-p0r3","display") == "none" && text("summary-p1r1","display") == "block" &&
		text("summary-p1r2","display") == "none","each panel shows its players");
	Check(bounds("summary-p1-band").y >= bounds("summary-p0r2").y+bounds("summary-p0r2").height,"the other team follows the winners");
	Check(text("summary-p0r1-marker","display") == "block" && text("summary-p0r0-marker","display") == "none" &&
		Near(number("summary-p0r1-band","opacity"),.29f,.001f) && Near(number("summary-p0r0-band","opacity"),.08f,.001f),
		"your row takes the marker and the brighter band");
	Check(text("summary-p0r0-place","display") == "none","team panels show no places");
	Check(text("summary-p0r1-w0","display") == "block" && text("summary-p0r1-w1","display") == "block" && text("summary-p0r1-w2","display") == "none",
		"a row shows the weapons it has");
	Check(text("summary-p0r1-w0","image").find("item_railgun") != std::string::npos &&
		text("summary-p0r0-w0","image").find("item_nailgun") != std::string::npos,"each weapon its own icon");
	const auto rail = runtime.PresentedValue("summary-p0r1-w0","image-color");
	Check(rail && Near(static_cast<float>(rail->data[0]),0,.01f) && Near(static_cast<float>(rail->data[1]),1,.01f),"in its color code");
	// The chat: a team line's tag, a server line's symbol without a name, your
	// own line's marker, the newest at the bottom over the field.
	Check(text("summary-chat0","display") == "flex" && text("summary-chat3","display") == "none","the chat shows its messages");
	Check(text("summary-chat0-tag","display") == "block" && text("summary-chat2-tag","display") == "none","a team line opens with its tag");
	Check(text("summary-chat1-info","display") == "block" && text("summary-chat1-name","display") == "none","a server line carries the symbol");
	Check(text("summary-chat2-marker","display") == "block" && text("summary-chat0-marker","display") == "none","your own line takes the marker");
	const auto oldest = bounds("summary-chat0"), newest = bounds("summary-chat2"), field = bounds("summary-chat-field");
	Check(oldest.y < newest.y && newest.y+newest.height <= field.y,"oldest first, the newest just above the field");
	Check(text("summary-chat-field-hint","display") == "block" && text("summary-chat-field-caret","display") == "none",
		"the empty field at rest invites a message");
	Check(runtime.RunEvent("summary_chat",2.4,effects,error) && asked("mpSummaryChat"),"choosing the field asks the session to type");
	// Typing, as the session publishes it: the entry around the caret, the
	// channel, the prompts for sending and cancelling.
	Check(runtime.SetState({{"mp.summary.typing",true},{"mp.summary.entry_before",std::string("Rematch on")},
		{"mp.summary.entry_after",std::string(" Phrantic?")},{"mp.summary.entry_left",108.0},{"mp.summary.entry_team",true}},error,2.5),
		"publish the typing");
	runtime.Frame(viewport,2.6);
	Check(text("summary-chat-field-hint","display") == "none" && text("summary-chat-field-caret","display") == "block","the caret blinks while typing");
	Check(text("summary-chat-field-team","display") == "block" && text("summary-chat-field-all","display") == "none","the channel tag follows");
	Check(text("prompts-typing","display") == "flex" && text("prompts","display") == "none" && text("prompt-channel","display") == "flex",
		"the prompts send, switch the channel and cancel");
	Check(text("summary-chat-field-count","display") == "none","the count waits for 80% of the limit");
	Check(runtime.SetState({{"mp.summary.entry_left",12.0}},error,2.7),"near the limit");
	runtime.Frame(viewport,2.8);
	Check(text("summary-chat-field-count","display") == "block" && text("summary-chat-field-count","text") == "12","the characters left show");
	Check(runtime.SetState({{"mp.summary.typing",false},{"mp.summary.entry_before",std::string("")},{"mp.summary.entry_after",std::string("")}},
		error,2.9),"typing stops");
	runtime.Frame(viewport,3);
	Check(text("prompts","display") == "flex" && text("prompts-typing","display") == "none","the card's own prompts return");
	// Deathmatch: places, the stock's first three tints, no bands.
	Check(runtime.SetState({{"mp.summary.teams",false},{"mp.summary.result",std::string("Anderson wins!")},{"mp.summary.result_color",3.0},
		{"mp.summary.result_detail",std::string("2ND OF 4")},{"mp.summary.p0.band",false},{"mp.summary.p0.count",4.0},{"mp.summary.p1.shown",false},
		{"mp.summary.p0r0.place",std::string("1.")},{"mp.summary.p0r0.tint",1.0},{"mp.summary.p0r1.place",std::string("2.")},
		{"mp.summary.p0r1.tint",2.0},{"mp.summary.p0r2.place",std::string("3.")},{"mp.summary.p0r2.tint",3.0},
		{"mp.summary.p0r3.name",std::string("Morris")},{"mp.summary.p0r3.place",std::string("4.")}},error,3.1),"publish deathmatch");
	runtime.Frame(viewport,3.2);
	Check(text("summary-result-band-3","display") == "block" && text("summary-result-detail","text") == "2ND OF 4","the leader wins in olive, your place beside");
	Check(text("summary-p0-band","display") == "none" && text("summary-p1-band","display") == "none","deathmatch has no bands");
	Check(text("summary-p0r0-place","display") == "block" && text("summary-p0r3-place","text") == "4.","rows show their places");
	Check(text("summary-p0r0-tint1","display") == "block" && text("summary-p0r1-tint2","display") == "block" &&
		text("summary-p0r2-tint3","display") == "block" && text("summary-p0r3-tint1","display") == "none" &&
		Near(number("summary-p0r1-band","opacity"),0,.001f),"the first three places keep their tints in place of the band");
	Check(text("summary-p0r1-marker","display") == "block","your marker stays on a tinted row");
	// STATISTICS: the Players page's lists and statistics, the match awards
	// where its Mute and Friend stand.
	Check(runtime.SetState({{"mp.players.a.shown",true},{"mp.players.a.title",std::string("PLAYERS")},{"mp.players.a.color",3.0},
		{"mp.players.a.count",2.0},{"mp.players.a0.name",std::string("Anderson")},{"mp.players.a0.client",0.0},
		{"mp.players.a1.name",std::string("Kane")},{"mp.players.a1.client",1.0},{"mp.players.a1.local",true},{"mp.stat.client",1.0},
		{"mp.stat.name",std::string("Kane")},{"mp.stat.match_award0",std::string("Rail Master")}},error,3.3),"publish the statistics");
	Check(runtime.RunEvent("tab_statistics",3.4,effects,error),"to STATISTICS");
	runtime.Frame(viewport,3.7);
	Check(text("page-statistics","display") == "block" && runtime.FocusedControl() == "statistics-a-1","the page focuses your statistics' row");
	Check(text("stat-match-award-0","display") == "block" && text("stat-match-award-1","display") == "none","the match awards show");
	Bounds unused;
	Check(!runtime.GetBounds("players-mute",unused) && !runtime.GetBounds("players-friend",unused),"no Mute or Friend on the summary");
	Check(runtime.RunEvent("statistics_a0",3.8,effects,error) && asked("mpSelectPlayer") &&
		std::get<double>(runtime.GetState().at("card.client")) == 0,"a row asks for its player's statistics");
	// SCOREBOARD: the scoreboard's columns, KILLS by the scoreboard's rule, a
	// bot's tag in place of its clan.
	Check(runtime.SetState({{"mp.players.a0.clan",std::string("RHINO")},{"mp.players.a1.bot",true},{"mp.players.a0.ping",std::string("90")},{"mp.players.a0.ping_ms",90.0},
		{"mp.players.kills_shown",false}},error,3.9),"publish the scoreboard columns");
	Check(runtime.RunEvent("tab_scoreboard",4,effects,error),"to SCOREBOARD");
	runtime.Frame(viewport,4.3);
	Check(text("page-scoreboard","display") == "block" && runtime.FocusedControl() == "tab-scoreboard","nothing to choose; the tab keeps focus");
	Check(text("board-a-0","display") == "block" && text("board-a-2","display") == "none","the board lists its players");
	Check(text("board-a-1-bot","display") == "block" && text("board-a-1-clan","display") == "none" &&
		text("board-a-0-clan","display") == "block","a bot's tag replaces its clan");
	Check(text("board-a-0-kills","display") == "none" && text("board-head-kills","display") == "none","KILLS waits for the flag modes");
	Check(text("board-a-0-bars3","display") == "block" && text("board-a-1-meter","display") == "none","ping bars for players, none for bots");
	Check(text("board-a-1-marker","display") == "block","your row's marker");
	Check(runtime.SetState({{"mp.players.kills_shown",true}},error,4.4),"a flag mode");
	runtime.Frame(viewport,4.5);
	Check(text("board-a-0-kills","display") == "block" && text("board-head-kills","display") == "block","KILLS shows in the flag modes");
	// Back returns to the game, as the stock summary's Esc does.
	Check(runtime.RunEvent("onBack",4.6,effects,error) && asked("mpClose"),"Back returns to the game");
	host.softFocus = false;
}
