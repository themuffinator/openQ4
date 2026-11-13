// The buy card (ui-visual-design.md 14.18): the Escape card's construction
// and size over the softened match; four tabs over the game's catalog; plates
// that collapse where the server's rules do not sell their item, lock where
// it cannot be bought now, check where it is owned and turn their price red
// where it is out of reach; the hot plate's detail beside the list; buying
// asks the session with the item's catalog index, an unavailable plate only
// shakes; MODS says why it is empty under Quake 4's rules; Back returns.
static void CheckBuy(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	const bool valid = document.Load(source,diagnostics);
	for (const auto& diagnostic : diagnostics) std::fprintf(stderr,"mp_buy %s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
	Check(valid && document.Model().id == "openq4.mp_buy" && document.Model().canvasHeight == 720,"the buy card validates");
	CheckSessionActions(document);
	int buys = 0;
	for (const auto& [id,action] : document.Model().actions) {
		if (std::get<std::string>(action.arguments.at("command").literal) != "mpBuy") continue;
		Check(id == "buy"+std::to_string(static_cast<int>(std::get<double>(action.arguments.at("item").literal))),
			"each plate buys its own catalog item");
		++buys;
	}
	Check(buys == 20,"one purchase per catalog item");
	const auto& root = document.Model().root;
	Check(root.children.size() == 3 && root.children[0].id == "scene-softfocus" && root.children[1].id == "scrim" &&
		root.children[2].id == "chrome","the softened view, its scrim stand-in, then the card");
	Runtime runtime(host);
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/menu/mp_buy.rui",diagnostics),"the buy card loads into a runtime");
	Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
	std::string error;
	Runtime::EventEffects effects;
	const auto bounds = [&](const std::string& id) { Bounds b; Check(runtime.GetBounds(id,b),"a buy card node is laid out"); return b; };
	const auto number = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented value"); return static_cast<float>(value->data[0]);
	};
	const auto text = [&](const std::string& node, const char* property) {
		const auto value = runtime.PresentedValue(node,property); Check(value.has_value(),"a presented keyword"); return value->text;
	};
	const auto bought = [&](int item) {
		return effects.actions.size() == 1 && std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpBuy" &&
			std::get<double>(effects.actions[0].arguments.at("item")) == item;
	};
	// The Awakening's rules in a team game with 1250 credits: the shotgun
	// owned, the hyperblaster for sale, the Dark Matter Gun out of reach, the
	// Napalm Gun not sold; the bounce mod waits for its weapon.
	StateValues page = {{"mp.buy.title",std::string("PURCHASE EQUIPMENT")},{"mp.buy.rules",std::string("THE AWAKENING")},
		{"mp.buy.credits",std::string("Credits: 1250")},{"mp.buy.weapons_count",8.0},{"mp.buy.mods_count",4.0},
		{"mp.buy.gear_count",3.0},{"mp.buy.team_count",4.0}};
	const char* const names[20] = {"Shotgun","Hyperblaster","Grenade Launcher","Nailgun","Rocket Launcher","Railgun","Lightning Gun",
		"Dark Matter Gun","Napalm Gun","Bounce Shot Mod","Firepower Mod","Burst Fire Mod","Chain Lightning Mod","Light Armor","Heavy Armor",
		"Ammo Refill","Team Ammo Regen","Team Health Regen","Team Doubler","FC Armor Regen"};
	for (int item = 0; item < 20; ++item) {
		const std::string key = "mp.buy"+std::to_string(item);
		page[key+".shown"] = item != 8;
		page[key+".available"] = item == 1 || item == 13 || item == 15;
		page[key+".owned"] = item == 0;
		page[key+".dear"] = item == 7 || item == 16;
		page[key+".label"] = std::string(names[item]);
		page[key+".price"] = std::string(item == 7 ? "5000" : "700");
		page[key+".detail"] = std::string(item >= 9 ? "What the item does." : "");
		page[key+".reason"] = std::string(item == 0 ? "Owned" : item == 7 ? "Not enough credits" : item == 9 ? "Carry the weapon first" : "");
	}
	Check(runtime.SetState(page,error,1),"publish the buy card");
	host.softFocus = true;
	Check(runtime.RunEvent("open",1.01,effects,error),"the buy card opens");
	runtime.Frame(viewport,1.4);
	const auto card = bounds("card");
	Check(Near(card.x+card.width/2,640) && Near(card.y+card.height/2,360),"the buy card is centered");
	Check(card.width >= 648-.5f && card.width <= 928+.5f && Near(card.height,477,1),"the buy card takes the Escape card's size");
	Check(text("header-title","text") == "PURCHASE EQUIPMENT" && text("header-credits","text") == "Credits: 1250" &&
		text("header-rules","text") == "THE AWAKENING","the header names the card, the rules and the credits");
	Check(runtime.FocusedControl() == "buy-0","the WEAPONS page focuses its first plate");
	Check(text("page-weapons","display") == "block" && text("page-mods","display") == "none","the card opens on WEAPONS");
	const auto first = bounds("buy-0"), second = bounds("buy-1"), dmg = bounds("buy-7");
	Check(Near(second.y-first.y,36,.5f) && Near(dmg.y-first.y,7*36,.5f),"plates stand on the 36 dp pitch");
	Check(text("buy-8","display") == "none","an item the rules do not sell takes no plate");
	Check(text("buy-0-check","display") == "block" && text("buy-0-lock","display") == "none","an owned item shows its check");
	Check(text("buy-7-lock","display") == "block" && text("buy-7-price-dear","display") == "block" && text("buy-7-price","display") == "none",
		"an item out of reach locks and turns its price red");
	Check(text("buy-1-lock","display") == "none" && text("buy-1-price","display") == "block","an item for sale shows its price");
	Check(Near(number("buy-1-row","opacity"),1,.001f) && Near(number("buy-7-row","opacity"),.6f,.001f),"an unavailable plate dims");
	// Focus feedback starts on the frame that notices the focus.
	runtime.Frame(viewport,1.5);
	Check(number("buy-0-detail","opacity") > .99f && number("buy-1-detail","opacity") < .01f,"the focused plate shows its detail");
	const auto detail = bounds("buy-0-detail"), list = bounds("buy-0");
	Check(detail.x >= list.x+list.width && Near(detail.y,bounds("page-weapons").y,.5f),"beside the list, at the page's top");
	Check(text("buy-0-detail-owned","display") == "block" && text("buy-0-detail-owned","text") == "Owned","the detail says it is owned");
	Check(runtime.FocusControl("buy-7",1.5),"focus the Dark Matter Gun");
	runtime.Frame(viewport,1.8);
	Check(number("buy-7-detail","opacity") > .99f && number("buy-0-detail","opacity") < .01f,"the detail follows the focus");
	Check(Near(bounds("buy-7-detail").y,bounds("page-weapons").y,.5f),"a lower plate's detail keeps the page's top");
	Check(text("buy-7-detail-reason","display") == "block" && text("buy-7-detail-reason","text") == "Not enough credits",
		"the detail says why it cannot be bought");
	effects = {};
	Check(runtime.RunEvent("buy_7",1.9,effects,error) && effects.actions.empty(),"an unavailable plate buys nothing");
	effects = {};
	Check(runtime.RunEvent("buy_1",2,effects,error) && bought(1),"an available plate buys its item");
	// The pages: MODS, ARMOR & AMMO and TEAM each focus their first plate.
	Check(runtime.RunEvent("tab_mods",2.1,effects,error),"switch to MODS");
	runtime.Frame(viewport,2.4);
	Check(text("page-mods","display") == "block" && runtime.FocusedControl() == "buy-9","MODS focuses its first mod");
	Check(text("page-mods-empty","display") == "none" && text("buy-9-lock","display") == "block","a mod waits for its weapon");
	Check(text("buy-9-detail-text","text") == "What the item does." && text("buy-9-detail-reason","text") == "Carry the weapon first",
		"its detail says what it does and why it waits");
	Check(runtime.RunEvent("onTabNext",2.5,effects,error),"the next tab");
	runtime.Frame(viewport,2.8);
	Check(text("page-gear","display") == "block" && runtime.FocusedControl() == "buy-13","ARMOR & AMMO follows");
	Check(runtime.RunEvent("onTabNext",2.9,effects,error),"the next tab");
	runtime.Frame(viewport,3.2);
	Check(text("page-team","display") == "block" && runtime.FocusedControl() == "buy-16","TEAM follows");
	// Quake 4's rules: the Dark Matter Gun goes, the Napalm Gun comes and
	// MODS says why it is empty.
	StateValues quake4 = {{"mp.buy.rules",std::string("QUAKE 4")},{"mp.buy.mods_count",0.0},{"mp.buy7.shown",false},{"mp.buy8.shown",true}};
	for (int item = 9; item < 13; ++item) quake4["mp.buy"+std::to_string(item)+".shown"] = false;
	Check(runtime.SetState(quake4,error,3.3),"publish Quake 4's rules");
	Check(runtime.RunEvent("tab_weapons",3.4,effects,error),"back to WEAPONS");
	runtime.Frame(viewport,3.7);
	Check(text("buy-7","display") == "none" && Near(bounds("buy-8").y-bounds("buy-0").y,7*36,.5f),
		"the Napalm Gun closes the gap the Dark Matter Gun left");
	Check(runtime.RunEvent("tab_mods",3.8,effects,error),"to MODS");
	runtime.Frame(viewport,4.1);
	Check(text("page-mods-empty","display") == "block","an empty MODS says why");
	effects = {};
	Check(runtime.RunEvent("onBack",4.2,effects,error) && effects.actions.size() == 1 &&
		std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpClose","Back closes the card");
	Check(host.errors == 0,"no retained buy card diagnostics");
}
