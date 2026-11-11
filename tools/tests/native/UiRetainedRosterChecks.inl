// Full connection rosters in the actual production documents and Runtime.
// These are semantic control tests; no OS input or game client is created.
static void CheckRosterCapacity(ScreenHost& host, const char* path, const char* tab,
    const char* prefix, bool interactive) {
    Runtime runtime(host);
    std::vector<Diagnostic> diagnostics;
    Check(runtime.Initialize() && runtime.LoadDocument(Read(path),"guis/menu/roster-test.rui",diagnostics),"load production roster");
    Viewport viewport; viewport.canvasHeight = 720; viewport.width = 1280; viewport.height = 720;
    double now = 1;
    std::string error;
    Runtime::EventEffects effects;
    const std::string p = prefix, bar = p+"-scroll", view = p+"-viewport";
    const auto frame = [&] { now += .2; runtime.Frame(viewport,now); };
    const auto key = [&](MenuInput input) {
        runtime.MenuAction(input,true,now+.01); runtime.MenuAction(input,false,now+.02); frame();
    };
    const auto box = [&](const std::string& id) {
        Bounds result; Check(runtime.GetBounds(id,result),"roster geometry exists"); return result;
    };
    StateValues values = {{"mp.players.a.shown",true},{"mp.players.a.count",32.0},
        {"mp.players.b.shown",false},{"mp.players.b.count",0.0},{"mp.players.s.count",0.0}};
    if (interactive) { values["mp.stat.client"] = 31.0; values["mp.stat.identity"] = std::string("8223"); }
    for (int row = 0; row < 32; ++row) {
        const std::string entry = "mp.players.a"+std::to_string(row);
        values[entry+".client"] = double(row);
        values[entry+".name"] = "Player "+std::to_string(row);
        values[entry+".identity"] = std::to_string(8192+row);
    }
    Check(runtime.SetState(values,error,now),"all 32 connection slots are declared");
    frame();
    Check(runtime.RunEvent("onActivate",now,effects,error),"activate roster card");
    frame();
    Check(runtime.RunEvent(std::string("tab_")+tab,now,effects,error),"open roster tab");
    frame(); frame();
    const auto scroll = runtime.GetWidgetState(bar);
    Check(scroll && scroll->scroll && scroll->scroll->geometry.usable && scroll->scroll->geometry.range > 0,
        "full roster overflows into a usable authored scrollbar");
    Check(Near(box(p+"-a-31").height,24,.1f),"full roster keeps a readable 24 dp row pitch");
    if (interactive) {
        Check(runtime.FocusedControl() == p+"-a-31","opening focuses the selected late row");
        const auto last = box(p+"-a-31"), area = box(view);
        Check(last.y >= area.y-.5f && last.y+last.height <= area.y+area.height+.5f,
            "focus reveals the selected row beyond sixteen");
        key(MenuInput::Accept);
        const auto actions = runtime.TakeActions();
        Check(actions.size() == 1 && runtime.RunEvent(actions[0].event,now,effects,error) && effects.actions.size() == 1 &&
            std::get<std::string>(effects.actions[0].arguments.at("command")) == "mpSelectPlayer" &&
            std::get<double>(runtime.GetState().at("card.client")) == 31,"late row selects its actual client");
        Check(std::get<double>(effects.actions[0].arguments.at("client")) == 31 &&
            std::get<std::string>(effects.actions[0].arguments.at("identity")) == "8223","player event freezes the connection identity in its operands");
        key(MenuInput::Accept);
        const auto stale = runtime.TakeActions();
        Check(stale.size() == 1 && runtime.CanDispatchControlAction(stale[0],now),"queued selection belongs to its current player");
        Check(runtime.SetState({{"mp.players.a31.score",std::string("100")}},error,now),"score changes without reconnect"); frame();
        Check(runtime.CanDispatchControlAction(stale[0],now),"score refresh retains queued player selection");
        Check(runtime.SetState({{"mp.players.a31.identity",std::string("16415")}},error,now),"same slot receives a new connection"); frame();
        Check(!runtime.CanDispatchControlAction(stale[0],now),"queued row cannot select a replacement occupant");
        Check(runtime.SetState({{"mp.players.a31.identity",std::string("8223")}},error,now),"original identity returns"); frame();
        Check(!runtime.CanDispatchControlAction(stale[0],now),"old work cannot revive when identity changes back");
        if (std::string(path).find("mp_escape") != std::string::npos) for (const auto* button : {"players-mute","players-friend"}) {
            Check(runtime.SetState({{"mp.mute.available",true},{"mp.friend.available",true},
                {"mp.mute.shown",true},{"mp.friend.shown",true}},error,now),"player permits marks"); frame();
            Check(runtime.FocusControl(button,now),"focus player mark"); key(MenuInput::Accept);
            const auto mark = runtime.TakeActions();
            Check(mark.size() == 1 && runtime.CanDispatchControlAction(mark[0],now),"current player mark is dispatchable");
            Check(runtime.SetState({{"mp.stat.identity",std::string("16415")}},error,now),"selected subject replaced"); frame();
            Check(!runtime.CanDispatchControlAction(mark[0],now),"queued mark cannot follow another selected connection");
            Check(runtime.SetState({{"mp.stat.identity",std::string("8223")}},error,now),"restore selected identity"); frame();
        }
    } else {
        Check(runtime.FocusedControl() == bar,"read-only roster gives navigation to its scrollbar");
    }
    Check(runtime.FocusControl(bar,now),"focus roster scrollbar");
    const auto headings = box(p+"-headings");
    key(MenuInput::End);
    const auto atEnd = runtime.GetWidgetState(bar);
    Check(atEnd && atEnd->scroll && Near(float(atEnd->scroll->geometry.offset),float(atEnd->scroll->geometry.range),.5f),
        "End reaches the final connection");
    const auto last = box(p+"-a-31"), area = box(view);
    Check(last.y >= area.y-.5f && last.y+last.height <= area.y+area.height+.5f,"final row is fully visible at scroll end");
    Check(Near(box(p+"-headings").y,headings.y,.01f),"column headings stay fixed while rows scroll");
    key(MenuInput::Home);
    Check(Near(float(runtime.GetWidgetState(bar)->scroll->geometry.offset),0,.1f),"Home returns to the first team");
    key(MenuInput::PageDown);
    Check(runtime.GetWidgetState(bar)->scroll->geometry.offset > 0,"Page Down moves through the roster");
    // A score refresh changes neither the selection nor the current scroll.
    const double offset = runtime.GetWidgetState(bar)->scroll->geometry.offset;
    Check(runtime.SetState({{"mp.players.a31.score",std::string("99")}},error,now),"live score refresh");
    frame();
    Check(Near(float(runtime.GetWidgetState(bar)->scroll->geometry.offset),float(offset),.1f),"score refresh preserves scroll");
    // Late spectators remain reachable after both team bands; shrinking the
    // list clamps its offset and makes the unused bar unavailable.
    Check(runtime.SetState({{"mp.players.a.count",10.0},{"mp.players.b.shown",true},{"mp.players.b.count",10.0},
        {"mp.players.s.count",12.0},{"mp.players.s11.name",std::string("Last spectator")}},error,now),"full mixed-team roster");
    frame(); key(MenuInput::End);
    const auto spectator = box(p+"-s-11");
    Check(spectator.y >= area.y-.5f && spectator.y+spectator.height <= area.y+area.height+.5f,
        "last spectator is reachable below both teams");
    Check(runtime.SetState({{"mp.players.a.count",1.0},{"mp.players.b.shown",false},{"mp.players.b.count",0.0},
        {"mp.players.s.count",0.0}},error,now),"most players disconnect");
    frame(); frame();
    const auto reduced = runtime.GetWidgetState(bar);
    Check(reduced && reduced->scroll && !reduced->scroll->geometry.usable && Near(float(reduced->scroll->geometry.offset),0,.1f),
        "short roster clears its scroll offset and disables unused scrollbar");
    Check(!runtime.CanActivateControl(bar,now),"fitting roster scrollbar cannot consume navigation");
    if (std::string(path).find("mp_escape") != std::string::npos) {
        StateValues kick = {{"mp.vote.kick.row_shown",true},{"mp.vote.kick.row_allowed",true},
            {"mp.vote.kick_count",32.0},{"mp.vote.kick",-1.0}};
        for (int row = 0; row < 31; ++row) kick["mp.vote.kick"+std::to_string(row)] = "Target "+std::to_string(row+1);
        Check(runtime.SetState(kick,error,now),"kick chooser declares every other connection");
        Check(runtime.RunEvent("tab_vote",now,effects,error),"open vote tab"); frame();
        Check(runtime.FocusControl("vote-row-kick",now),"focus kick chooser");
        key(MenuInput::Accept); key(MenuInput::End); key(MenuInput::Accept);
        const auto actions = runtime.TakeActions();
        ActionInvocation invocation;
        Check(actions.size() == 1 && actions[0].proposal &&
            runtime.ResolveAction(actions[0].action,invocation,error,&*actions[0].proposal) &&
            std::get<std::string>(invocation.arguments.at("command")) == "mpVoteKick" &&
            std::get<double>(invocation.arguments.at("value")) == 30,
            "last of 31 other connections can be drafted for a kick vote");
    }
    Check(host.errors == 0,"roster rendering has no diagnostics");
}
