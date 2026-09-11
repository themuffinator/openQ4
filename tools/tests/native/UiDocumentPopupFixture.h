// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Source-bound first-party fixture helpers; no production callbacks replaced.
namespace PopupFixture {
static Json::Value Typed(const char* type, Json::Value value, const char* unit = nullptr) {
	Json::Value result; result["type"]=type; result["value"]=std::move(value); if (unit) result["unit"]=unit; return result;
}
static Json::Value Array(std::initializer_list<double> values) { Json::Value result(Json::arrayValue); for (auto value:values) result.append(value); return result; }
static Json::Value Ref(const char* state) { Json::Value result; result["state"]=state; return result; }
static std::string Text(const Json::Value& value) { Json::StreamWriterBuilder writer; writer["indentation"]=""; writer["precision"]=17; return Json::writeString(writer,value); }
static Json::Value Parse(const std::string& source) {
	Json::CharReaderBuilder builder; std::unique_ptr<Json::CharReader> reader(builder.newCharReader()); Json::Value result; std::string error;
	Check(reader->parse(source.data(),source.data()+source.size(),&result,&error),"snapshot JSON parses"); return result;
}
static Json::Value Box(const std::string& id, double x, double y, double width, double height, const char* type = "group") {
	Json::Value result; result["id"]=id; result["type"]=type; auto& p=result["properties"];
	p["position"]=Typed("keyword","absolute"); p["display"]=Typed("keyword","block"); p["opacity"]=Typed("number",1);
	p["left"]=Typed("length",x,"dp"); p["top"]=Typed("length",y,"dp"); p["width"]=Typed("length",width,"dp"); p["height"]=Typed("length",height,"dp");
	if (std::string(type)=="text") p["text"]=Typed("text","#str_label");
	else result["children"]=Json::Value(Json::arrayValue);
	return result;
}
static void Colour(Json::Value& node, std::initializer_list<double> colour) { node["properties"]["background-color"]=Typed("color",Array(colour)); }
static void AttachControl(Json::Value& source, Json::Value& node, const char* role, const char* state, const char* type) {
	const auto id=node["id"].asString(); auto& control=node["control"];
	control["role"]=role; control["label"]="#str_label"; control["value"]=Ref(state); control["action"]=id+"-edit";
	for (const auto* name:{"default","hover","focus","pressed","disabled"}) control["states"][name]=id+"-feedback";
	auto& action=source["actions"][id+"-edit"]; action["input"]=type; action["operation"]="test.edit"; action["arguments"]["value"]["input"]="value";
	Json::Value timeline, track; timeline["id"]=id+"-feedback"; timeline["durationMs"]=1;
	track["node"]=id; track["property"]="opacity";
	for (unsigned at:{0u,1u}) { Json::Value key; key["atMs"]=at; key["value"]=Typed("number",1); track["keys"].append(key); }
	timeline["tracks"].append(track); source["timelines"].append(timeline);
}
static void PropertyTimeline(Json::Value& source,const char* id,const char* node,const char* property,Json::Value from,Json::Value to,unsigned duration) {
	Json::Value timeline,track,first,last;timeline["id"]=id;timeline["durationMs"]=duration;track["node"]=node;track["property"]=property;
	first["atMs"]=0;first["value"]=std::move(from);last["atMs"]=duration;last["value"]=std::move(to);
	track["keys"].append(first);track["keys"].append(last);timeline["tracks"].append(track);source["timelines"].append(timeline);
}
static void TransformTimeline(Json::Value& source,const char* id,const char* node,Json::Value from,Json::Value to,unsigned duration) {
	PropertyTimeline(source,id,node,"transform",Typed("transform",from,"dp"),Typed("transform",to,"dp"),duration);
}
static Json::Value Slider(Json::Value& source, const char* id, const char* state, bool vertical, double x, double y) {
	const std::string prefix=id; auto node=Box(id,x,y,vertical?60:220,vertical?220:50); AttachControl(source,node,"slider",state,"number");
	auto& control=node["control"]; control["minimum"]=0; control["maximum"]=2; control["step"]=.1; control["decimals"]=2; control["orientation"]=vertical?"vertical":"horizontal";
	for (const auto* part:{"track","fill","thumb","value"}) control["parts"][part]=prefix+"-"+part;
	auto track=Box(prefix+"-track",10,10,vertical?20:200,vertical?200:20); Colour(track,{.2,.2,.2,1});
	auto fill=Box(prefix+"-fill",0,0,vertical?20:0,vertical?0:20); Colour(fill,{.8,.1,.1,1});
	if (vertical) { fill["properties"]["top"]=Typed("keyword","auto"); fill["properties"]["bottom"]=Typed("length",0,"dp"); }
	auto thumb=Box(prefix+"-thumb",0,0,20,20); Colour(thumb,{.3,.4,.9,1});
	track["children"].append(fill); track["children"].append(thumb); node["children"].append(track);
	node["children"].append(Box(prefix+"-value",vertical?30:10,vertical?10:32,vertical?30:180,18,"text")); return node;
}
static std::string Source() {
	Json::Value source; source["format"]="openq4-ui"; source["version"]=1; source["id"]="value-runtime";
	for (const auto& [name,value]:std::map<std::string,Json::Value>{{"flag",false},{"mixed",false},{"level",1.05},{"verticalLevel",1.0},{"choice",0},{"thirdEnabled",false}}) {
		source["state"][name]["type"]=value.isBool()?"boolean":"number"; source["state"][name]["initial"]=value;
	}
	auto root=Box("root",0,0,640,400); root["properties"]["font-family"]=Typed("font","root-font"); root["properties"]["font-size"]=Typed("length",12,"dp");
	auto toggle=Box("toggle",20,20,140,40); AttachControl(source,toggle,"toggle","flag","boolean");
	toggle["control"]["mixed"]=Ref("mixed"); toggle["control"]["parts"]["checked"]="checked"; toggle["control"]["parts"]["mixed"]="mixed-mark";
	auto checked=Box("checked",5,5,20,20); Colour(checked,{0,1,0,1}); toggle["children"].append(checked);
	auto mixed=Box("mixed-mark",5,12,20,5); Colour(mixed,{1,1,0,1}); toggle["children"].append(mixed); root["children"].append(toggle);
	auto slider=Slider(source,"slider","level",false,40,80); slider["properties"]["transform"]=Typed("transform",Array({0,0,1,1,0}),"dp");
	slider["control"]["navigation"]["next"]="vertical"; root["children"].append(slider);
	root["children"].append(Slider(source,"vertical","verticalLevel",true,330,60));
	// Deliberately clipped canonical parent near the lower/right viewport edge.
	auto parent=Box("clipped-parent",480,340,180,40); parent["properties"]["overflow"]=Typed("keyword","hidden");
	parent["properties"]["pointer-events"]=Typed("keyword","auto");
	parent["properties"]["transform"]=Typed("transform",Array({0,0,1,1,0}),"dp");
	parent["properties"]["font-family"]=Typed("font","choice-font"); parent["properties"]["font-size"]=Typed("length",18,"dp");
	auto choice=Box("choice",0,0,180,30); AttachControl(source,choice,"choice","choice","number"); auto& c=choice["control"];
	c["visibleRows"]=2; c["parts"]["popup"]="popup"; c["parts"]["viewport"]="viewport"; c["parts"]["content"]="content"; c["parts"]["value"]="choice-value";
	auto popup=Box("popup",0,0,180,80), viewport=Box("viewport",0,0,180,80), content=Box("content",0,0,180,240);
	Colour(popup,{.08,.12,.16,1}); viewport["properties"]["overflow"]=Typed("keyword","hidden");
	for (unsigned i=0;i<6;++i) {
		const auto id="option-"+std::to_string(i); auto row=Box(id,0,i*40,180,40); Json::Value option;
		option["id"]=id; option["node"]=id; option["label"]="#str_options"; option["labelIndex"]=i; option["value"]=i;
		if (i==2) option["enabled"]=Ref("thirdEnabled");
		for (const auto* part:{"selected","highlight","label"}) {
			const auto partId=id+"-"+part; option["parts"][part]=partId;
			auto node=Box(partId,part==std::string("label")?12:0,0,part==std::string("label")?168:6,40,part==std::string("label")?"text":"group");
			if (part==std::string("selected")) Colour(node,{0,.7,.2,1});
			if (part==std::string("highlight")) Colour(node,{.9,.5,0,1}); row["children"].append(node);
		}
		c["options"].append(option); content["children"].append(row);
	}
	viewport["children"].append(content); popup["children"].append(viewport); choice["children"].append(popup);
	choice["children"].append(Box("choice-value",0,0,180,30,"text")); parent["children"].append(choice); root["children"].append(parent);
	source["root"]=root;
	TransformTimeline(source,"rotate-slider","slider",Array({30,20,1,1,90}),Array({30,20,1,1,90}),1);
	TransformTimeline(source,"move-choice-parent","clipped-parent",Array({0,0,1,1,0}),Array({-200,-200,1,1,0}),100);
	TransformTimeline(source,"reset-choice-parent","clipped-parent",Array({0,0,1,1,0}),Array({0,0,1,1,0}),1);
	PropertyTimeline(source,"fade-parent","clipped-parent","opacity",Typed("number",.5),Typed("number",.5),1);
	PropertyTimeline(source,"fade-popup","popup","opacity",Typed("number",.5),Typed("number",.5),1);
	PropertyTimeline(source,"unfade-parent","clipped-parent","opacity",Typed("number",1),Typed("number",1),1);
	PropertyTimeline(source,"unfade-popup","popup","opacity",Typed("number",1),Typed("number",1),1);
	source["aliases"]["parent::noevents"]["node"]="clipped-parent";
	source["aliases"]["parent::noevents"]["property"]="noevents";
	return Text(source);
}
static Json::Value* BoxFrameNode(Json::Value& node,const char* id) {
    if(node["id"].asString()==id)return &node;
    if(!node.isMember("children"))return nullptr;
    for(auto& child:node["children"])if(auto* found=BoxFrameNode(child,id))return found;
    return nullptr;
}
static Json::Value BoundedChoice(bool bar=true) {
    auto source=Parse(Source());
    auto& parent=*BoxFrameNode(source["root"],"clipped-parent");
    for(const auto& [name,value]:std::map<std::string,double>{{"left",80},{"top",50},{"width",480},{"height",300}})
        parent["properties"][name]=Typed("length",value,"dp");
    auto& choice=*BoxFrameNode(source["root"],"choice");
    choice["control"]["placementBounds"]="clipped-parent";
    choice["properties"]["left"]=Typed("length",40,"dp");choice["properties"]["top"]=Typed("length",240,"dp");
    choice["properties"]["width"]=Typed("length",360,"dp");choice["properties"]["height"]=Typed("length",40,"dp");
    auto& popup=*BoxFrameNode(source["root"],"popup");
    popup["properties"]["display"]=Typed("keyword","none");popup["properties"]["padding"]=Typed("length",8,"dp");
    popup["properties"]["border-width"]=Typed("length",1,"dp");popup["properties"]["box-sizing"]=Typed("keyword","border-box");
    if(bar) {
        auto& scroll=choice["control"]["scrollbar"];scroll["track"]="choice-track";scroll["thumb"]="choice-thumb";
        scroll["minimumThumb"]=36;scroll["lineStep"]=36;
        auto track=Box("choice-track",0,0,36,80),thumb=Box("choice-thumb",10,0,16,36);
        Colour(track,{.11,.22,.33,1});Colour(thumb,{.9,.8,.7,1});track["children"].append(thumb);popup["children"].append(track);
    }
    for(const char* id:{"clipped-parent","choice"}) {
        source["aliases"][std::string(id)+"::rect"]["node"]=id;
        source["aliases"][std::string(id)+"::rect"]["property"]="rect";
    }
    return source;
}
}
