// Copyright (C) 2026 openQ4 contributors. GPL-3.0-or-later.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "src/sound/SoundDialogue.h"
template<class T> T Min(T a,T b){return std::min(a,b);}
template<class T> T Max(T a,T b){return std::max(a,b);}
struct idMath {static int FtoiFast(float v){return static_cast<int>(v);}};
struct idStr:std::string {
	using std::string::string;
	void StripFileExtension(){auto at=find_last_of('.');if(at!=npos)resize(at);}
	static int Icmpn(const char*a,const char*b,int n){return strncmp(a,b,n);}
	static int Icmp(const char*a,const char*b){return strcmp(a,b);}
};
template<class T> struct List:std::vector<T>{int Num()const{return int(this->size());}};
struct Vec {float x=0;Vec operator-(Vec v)const{return {x-v.x};}float Length()const{return std::abs(x);}};
struct Fade{float gain=1;float GetVolume(int)const{return gain;}};
enum {SSF_PRIVATE_SOUND=1,SSF_ANTI_PRIVATE_SOUND=2,SSF_IS_VO=4,SSF_VO_FOR_PLAYER=8,SSF_GLOBAL=16,SSF_NO_OCCLUSION=32,SSF_MUSIC=64};
constexpr int SOUND_MAX_CLASSES=4, DECL_LIPSYNC=0;
constexpr float SOUND_WORLD_VOLUME_EPSILON=.00001f;
struct Cvar{bool value=true;bool GetBool()const{return value;}}s_useOcclusion,ui_subtitleUntranscribed;
struct Parms{int soundShaderFlags=0,soundClass=0;float volume=1,minDistance=1,maxDistance=100;};
struct idSoundSample {const char*name="sound/vo/test.wav";int length=1000;bool fallback=false;const char*GetName()const{return name;}int LengthInMsec()const{return length;}bool IsDefault()const{return fallback;}};
struct Shader{const char*name="dialogue";int entries=1;int GetNumEntries()const{return entries;}const char*GetName()const{return name;}};
using idSoundShader=Shader;
struct idSoundChannel{Shader*soundShader=nullptr;idSoundSample*leadinSample=nullptr,*loopingSample=nullptr;int startTime=0,cursor=100,logicalChannel=1;void*hardwareVoice=nullptr;bool stopped=false,radio=false;Parms parms;Fade volumeFade;};
struct idSoundEmitterLocal {List<idSoundChannel*>channels;int emitterId=5,index=2;Vec origin;float spatializedDistance=1;int Index()const{return index;}};
struct rvDeclLipSync {const char*raw="text",*text="caption";bool implicit=false;const char*GetRawTranscribeText()const{return raw;}const char*GetSubtitleText()const{return text;}bool IsImplicit()const{return implicit;}};
struct DeclManager{std::map<std::string,rvDeclLipSync>decls;const rvDeclLipSync*FindType(int,const char*n,bool){auto i=decls.find(n);return i==decls.end()?nullptr:&i->second;}}manager;
auto*declManager=&manager;
struct soundSubtitle_t {const char*text,*shader;int emitterIndex,speakerId,startTime,timeMsec,durationMsec;bool radio,transcribed,authored;};
constexpr int SCHANNEL_ANY=0,SOUND_SUBTITLE_TAILS=4,SOUND_SUBTITLE_TAIL_MSEC=500,SOUND_SUBTITLE_MIN_DISPLAY_MSEC=1500;
struct soundSubtitleTail_t{idStr text;const idSoundShader*shader;int emitterIndex,logicalChannel,speakerId,startTime,durationMsec,until;bool radio,transcribed,authored;};
struct idSoundWorldLocal {List<idSoundEmitterLocal*>emitters;struct {int id=5;Vec pos;}listener;Fade soundClassFade[4];int now=100;int GetSoundTime(){return now;}int GetSubtitles(soundSubtitle_t*,int);
	soundSubtitleTail_t subtitleTails[SOUND_SUBTITLE_TAILS];int numSubtitleTails=0;void RememberSubtitleTail(idSoundEmitterLocal*,idSoundChannel*,int);void ForgetSubtitleTails(int,int=SCHANNEL_ANY);};
static bool SoundChannelHasCompleted(idSoundChannel*c,int){return c->stopped;}
static float SoundChannelPlaybackTimeMsec(idSoundChannel*c,int){return float(c->cursor);}
static bool SoundUsesMusicVolume(const Parms&p){return(p.soundShaderFlags&SSF_MUSIC)!=0||p.soundClass==3;}
static bool IsRadioChatterChannel(idSoundChannel*c){return c->radio;}
static void SoundWorldApplyDistanceFalloff(float&v,float d,float min,float max){v*=std::clamp((max-d)/(max-min),0.f,1.f);}
struct idCmdArgs{};
constexpr int SOUNDWORLD_GAME=1;
static std::string diagnostic;
struct idLib{static void Printf(const char*format,...){char out[2048];va_list args;va_start(args,format);vsnprintf(out,sizeof(out),format,args);va_end(args);diagnostic+=out;}};
struct SoundSystem{struct Hardware{std::vector<void*>voices;void PrintDistanceInfo(void*v){voices.push_back(v);}}hardware;
	idSoundWorldLocal*world=nullptr;idSoundWorldLocal*GetSoundWorldFromId(int){return world;}}soundSystemLocal;
#include "SoundSubtitlesUnderTest.inc"
int main(){
	// The selection cases below predate the untranscribed markers; they run without them.
	ui_subtitleUntranscribed.value=false;
	Shader shader;idSoundSample sample;idSoundChannel channel;
	channel.soundShader=&shader;channel.leadinSample=&sample;
	idSoundEmitterLocal emitter;emitter.channels.push_back(&channel);
	idSoundWorldLocal world;world.emitters.push_back(&emitter);
	soundSubtitle_t cues[4];manager.decls["dialogue"]={};
	assert(world.GetSubtitles(cues,4)==1&&cues[0].timeMsec==100&&cues[0].durationMsec==1000);
	// A finished line shows its last words until the mixer frees the channel.
	channel.stopped=true;assert(world.GetSubtitles(cues,4)==1);channel.stopped=false;
	emitter.channels.clear();assert(world.GetSubtitles(cues,4)==0);emitter.channels.push_back(&channel);
	channel.startTime=200;assert(world.GetSubtitles(cues,4)==0);channel.startTime=0;
	channel.cursor=1000;assert(world.GetSubtitles(cues,4)==1&&cues[0].timeMsec==999&&cues[0].transcribed&&!cues[0].authored);channel.cursor=100;
	sample.fallback=true;assert(world.GetSubtitles(cues,4)==0);sample.fallback=false;
	channel.parms.soundShaderFlags=SSF_MUSIC;assert(world.GetSubtitles(cues,4)==0);
	channel.parms.soundShaderFlags=SSF_PRIVATE_SOUND;world.listener.id=4;assert(world.GetSubtitles(cues,4)==0);
	channel.parms.soundShaderFlags=SSF_ANTI_PRIVATE_SOUND;world.listener.id=5;assert(world.GetSubtitles(cues,4)==0);
	channel.parms.soundShaderFlags=0;world.listener.id=4;emitter.spatializedDistance=101;assert(world.GetSubtitles(cues,4)==0);
	channel.parms.soundShaderFlags=SSF_GLOBAL;assert(world.GetSubtitles(cues,4)==1);
	channel.parms.volume=0;assert(world.GetSubtitles(cues,4)==0);channel.parms.volume=1;
	world.soundClassFade[0].gain=0;assert(world.GetSubtitles(cues,4)==0);world.soundClassFade[0].gain=1;
	manager.decls["dialogue"].raw="";assert(world.GetSubtitles(cues,4)==0);manager.decls["dialogue"]={};
	manager.decls["dialogue"].text="#str_missing";assert(world.GetSubtitles(cues,4)==0);manager.decls["dialogue"]={};
	shader.entries=2;assert(world.GetSubtitles(cues,4)==0);
	manager.decls["sound/vo/test"]={"literal","specific variant"};assert(world.GetSubtitles(cues,4)==1&&std::string(cues[0].text)=="specific variant");
	shader.entries=1;idSoundSample loop;loop.name="sound/vo/loop.wav";loop.length=400;channel.loopingSample=&loop;channel.cursor=1570;
	assert(world.GetSubtitles(cues,4)==0);manager.decls["sound/vo/loop"]={};assert(world.GetSubtitles(cues,4)==1&&cues[0].timeMsec==170);
	channel.cursor=100;channel.loopingSample=nullptr;
	idSoundChannel second=channel,third=channel;second.startTime=50;second.radio=true;third.startTime=80;
	emitter.channels.push_back(&second);emitter.channels.push_back(&third);
	assert(world.GetSubtitles(cues,4)==2&&cues[0].startTime==80&&cues[1].radio);
	emitter.channels.pop_back();assert(world.GetSubtitles(cues,4)==2&&cues[0].startTime==50&&cues[1].startTime==0);
	assert(world.GetSubtitles(nullptr,4)==0&&world.GetSubtitles(cues,0)==0);
	// Authored caption declarations, untranscribed markers and held tails.
	{
		ui_subtitleUntranscribed.value=false;manager.decls.clear();
		Shader spoken;spoken.name="vo_line";idSoundSample line;line.name="sound/vo/line.wav";line.length=2000;
		idSoundChannel voice;voice.soundShader=&spoken;voice.leadinSample=&line;voice.cursor=100;voice.logicalChannel=3;
		idSoundEmitterLocal speaker;speaker.index=7;speaker.channels.push_back(&voice);
		idSoundWorldLocal scene;scene.listener.id=5;scene.emitters.push_back(&speaker);soundSubtitle_t out[2];
		SoundTranscript status=SoundTranscript::Text;
		// No transcript: nothing, or a marker with no text while markers are asked for.
		assert(scene.GetSubtitles(out,2)==0);
		ui_subtitleUntranscribed.value=true;
		assert(scene.GetSubtitles(out,2)==1&&out[0].text==nullptr&&!out[0].transcribed&&!out[0].authored);
		// An authored sample-path declaration fills the gap and reports its source.
		manager.decls["subtitle/sound/vo/line"]={"raw","pack line"};
		assert(scene.GetSubtitles(out,2)==1&&std::string(out[0].text)=="pack line"&&out[0].transcribed&&out[0].authored);
		assert(std::string(SoundTranscriptText(&spoken,&line,true,&status))=="pack line"&&status==SoundTranscript::Authored);
		assert(std::string(SoundTranscriptName(SoundTranscript::Authored))=="authored");
		// It also overrides installed text; one that does not resolve falls back to it.
		manager.decls["vo_line"]={"raw","installed line"};
		assert(std::string(SoundTranscriptText(&spoken,&line,true,&status))=="pack line"&&status==SoundTranscript::Authored);
		manager.decls["subtitle/sound/vo/line"]={"raw","#str_missing"};
		assert(std::string(SoundTranscriptText(&spoken,&line,true,&status))=="installed line"&&status==SoundTranscript::Text);
		manager.decls["subtitle/sound/vo/line"]={"",""};
		assert(std::string(SoundTranscriptText(&spoken,&line,true,&status))=="installed line"&&status==SoundTranscript::Text);
		manager.decls.erase("subtitle/sound/vo/line");manager.decls.erase("vo_line");
		// A shader-named authored declaration maps a single-sample shader's leadin only.
		manager.decls["subtitle/vo_line"]={"raw","shader pack"};
		assert(std::string(SoundTranscriptText(&spoken,&line,true,&status))=="shader pack"&&status==SoundTranscript::Authored);
		assert(!SoundTranscriptText(&spoken,&line,false,&status));
		spoken.entries=2;assert(!SoundTranscriptText(&spoken,&line,true,&status)&&status==SoundTranscript::Undeclared);spoken.entries=1;
		// The implicit default the game makes up is not an authored declaration either.
		manager.decls["subtitle/vo_line"]={"","",true};assert(!SoundTranscriptText(&spoken,&line,true,&status));
		manager.decls["subtitle/vo_line"]={"raw","shader pack"};
		// Transcribed lines take the slots first; markers only fill what is left.
		Shader other;other.name="vo_other";idSoundSample otherLine;otherLine.name="sound/vo/other.wav";
		idSoundChannel bark=voice,third=voice;bark.soundShader=&other;bark.leadinSample=&otherLine;bark.startTime=90;
		third.startTime=60;speaker.channels.push_back(&bark);
		assert(scene.GetSubtitles(out,2)==2&&out[0].startTime==90&&!out[0].transcribed&&out[1].transcribed);
		// Two transcribed lines and a newer marker: the marker goes.
		speaker.channels.push_back(&third);
		assert(scene.GetSubtitles(out,2)==2&&out[0].transcribed&&out[1].transcribed&&out[0].startTime==60&&out[1].startTime==0);
		speaker.channels.pop_back();speaker.channels.pop_back();
		// A line that ends on its own holds its last words; a short one stays up 1.5 s in all.
		scene.now=2200;voice.cursor=2100;
		scene.RememberSubtitleTail(&speaker,&voice,scene.now);speaker.channels.clear();
		assert(scene.numSubtitleTails==1&&scene.subtitleTails[0].until==2700);
		assert(scene.GetSubtitles(out,2)==1&&std::string(out[0].text)=="shader pack"&&out[0].timeMsec==1999&&out[0].authored&&out[0].emitterIndex==7);
		scene.now=2699;assert(scene.GetSubtitles(out,2)==1);
		scene.now=2700;assert(scene.GetSubtitles(out,2)==0&&scene.numSubtitleTails==0);
		line.length=300;voice.cursor=400;scene.now=400;
		scene.RememberSubtitleTail(&speaker,&voice,scene.now);assert(scene.subtitleTails[0].until==1500);
		// The speaker's next line replaces it; a stop on its channel, or any stop, forgets it.
		idSoundChannel next=voice;next.startTime=450;next.cursor=10;line.length=2000;speaker.channels.push_back(&next);
		scene.now=460;assert(scene.GetSubtitles(out,2)==1&&out[0].startTime==450&&scene.numSubtitleTails==0);
		speaker.channels.clear();line.length=300;voice.cursor=400;
		scene.RememberSubtitleTail(&speaker,&voice,scene.now);scene.ForgetSubtitleTails(7,4);assert(scene.numSubtitleTails==1);
		scene.ForgetSubtitleTails(7,3);assert(scene.numSubtitleTails==0);
		scene.RememberSubtitleTail(&speaker,&voice,scene.now);scene.ForgetSubtitleTails(-1);assert(scene.numSubtitleTails==0);
		// Untranscribed lines hold a marker tail, shown only while markers are asked for.
		manager.decls.clear();scene.RememberSubtitleTail(&speaker,&voice,scene.now);
		assert(scene.GetSubtitles(out,2)==1&&!out[0].transcribed&&out[0].text==nullptr);
		ui_subtitleUntranscribed.value=false;assert(scene.GetSubtitles(out,2)==0);ui_subtitleUntranscribed.value=true;
		// A private line leaves no tail; four tails at most, the soonest to end replaced.
		scene.ForgetSubtitleTails(-1);voice.parms.soundShaderFlags=SSF_PRIVATE_SOUND;scene.listener.id=4;
		scene.RememberSubtitleTail(&speaker,&voice,scene.now);assert(scene.numSubtitleTails==0);
		voice.parms.soundShaderFlags=SSF_GLOBAL;
		idSoundEmitterLocal crowd[6];
		for(int i=0;i<6;++i){crowd[i].index=20+i;scene.RememberSubtitleTail(&crowd[i],&voice,scene.now+i);}
		assert(scene.numSubtitleTails==4);
		ui_subtitleUntranscribed.value=false;
	}
	// The status the s_logSpeech audit reports for each policy outcome.
	{
		manager.decls.clear();Shader single;idSoundSample line;line.name="sound/vo/line.wav";
		SoundTranscript status=SoundTranscript::Text;
		assert(!SoundTranscriptText(&single,&line,true,&status)&&status==SoundTranscript::Undeclared);
		manager.decls["dialogue"]={};
		assert(std::string(SoundTranscriptText(&single,&line,true,&status))=="caption"&&status==SoundTranscript::Text);
		assert(std::string(SoundTranscriptText(&single,&line,true,nullptr))=="caption");
		// a same-name transcript maps a single-sample shader's leadin only
		assert(!SoundTranscriptText(&single,&line,false,&status)&&status==SoundTranscript::Variant);
		single.entries=3;assert(!SoundTranscriptText(&single,&line,true,&status)&&status==SoundTranscript::Variant);
		manager.decls["sound/vo/line"]={"raw","variant text"};
		assert(std::string(SoundTranscriptText(&single,&line,true,&status))=="variant text"&&status==SoundTranscript::Text);
		single.entries=1;manager.decls.erase("sound/vo/line");
		manager.decls["dialogue"].raw="";assert(!SoundTranscriptText(&single,&line,true,&status)&&status==SoundTranscript::NoText);
		manager.decls["dialogue"]={"raw","#str_100"};assert(!SoundTranscriptText(&single,&line,true,&status)&&status==SoundTranscript::Unresolved);
		manager.decls["dialogue"]={"raw","<DEFAULTED>"};assert(!SoundTranscriptText(&single,&line,true,&status)&&status==SoundTranscript::Unresolved);
		manager.decls["dialogue"]={"raw",""};assert(!SoundTranscriptText(&single,&line,true,&status)&&status==SoundTranscript::Unresolved);
		manager.decls["dialogue"]={"raw",nullptr};assert(!SoundTranscriptText(&single,&line,true,&status)&&status==SoundTranscript::Unresolved);
		// The game's made-up default for an undeclared speech name is not a declaration:
		// it neither hides a sample-path transcript nor makes a variant.
		manager.decls["dialogue"]={"","",true};
		assert(!SoundTranscriptText(&single,&line,true,&status)&&status==SoundTranscript::Undeclared);
		manager.decls["sound/vo/line"]={"raw","sample text"};
		assert(std::string(SoundTranscriptText(&single,&line,true,&status))=="sample text"&&status==SoundTranscript::Text);
		manager.decls.erase("sound/vo/line");single.entries=3;
		assert(!SoundTranscriptText(&single,&line,true,&status)&&status==SoundTranscript::Undeclared);
		single.entries=1;
		assert(std::string(SoundTranscriptName(SoundTranscript::Variant))=="variant");
	}
	// The audit must classify a current channel, not every use of a sample.
	{
		idSoundSample shared;shared.name="sound/shared.wav";Shader spoken,effect;effect.name="effect";
		idSoundChannel speech,fx;speech.soundShader=&spoken;speech.leadinSample=&shared;speech.parms.soundShaderFlags=SSF_IS_VO;
		fx=speech;fx.soundShader=&effect;fx.parms.soundShaderFlags=0;fx.logicalChannel=2;
		speech.hardwareVoice=&speech;fx.hardwareVoice=&fx;
		idSoundEmitterLocal owner;owner.channels.push_back(&speech);owner.channels.push_back(&fx);
		idSoundWorldLocal testWorld;testWorld.emitters.push_back(&owner);
		soundSystemLocal.world=&testWorld;manager.decls.clear();manager.decls["dialogue"]={"raw","DO_NOT_PRINT_THIS_TRANSCRIPT"};
		auto report=[&](){diagnostic.clear();soundSystemLocal.hardware.voices.clear();SoundDialogueVoices_f(idCmdArgs{});};
		report();
		assert(diagnostic.find("shader=dialogue sample=sound/shared.wav dialogue=1 transcript=text")!=std::string::npos);
		assert(diagnostic.find("shader=effect sample=sound/shared.wav dialogue=0 transcript=undeclared")!=std::string::npos);
		assert(diagnostic.find("DO_NOT_PRINT_THIS_TRANSCRIPT")==std::string::npos);
		assert(soundSystemLocal.hardware.voices==std::vector<void*>({&speech,&fx}));
		// Music classification wins even when a channel has the speech flag.
		speech.parms.soundClass=3;report();assert(diagnostic.find("dialogue=1")==std::string::npos);speech.parms.soundClass=0;
		// A leadin/loop transition uses the sample playing now.
		idSoundSample loop;loop.name="sound/vo/loop.wav";fx.loopingSample=&loop;fx.cursor=1000;
		report();assert(diagnostic.find("shader=effect sample=sound/vo/loop.wav dialogue=1")!=std::string::npos);
		// A finished channel stays attributable until the mixer releases its
		// hardware voice; unallocated channels and absent worlds have no sources.
		speech.stopped=true;fx.startTime=200;report();assert(soundSystemLocal.hardware.voices.size()==2);
		speech.hardwareVoice=nullptr;fx.hardwareVoice=nullptr;report();assert(diagnostic=="SOUND_DIALOGUE_END channels=0\n");
		soundSystemLocal.world=nullptr;report();assert(diagnostic=="SOUND_DIALOGUE_END channels=0\n");
	}
	std::puts("Production subtitles and dialogue diagnostics: selection, routing, sample reuse, loops, privacy, transcript policy, authored captions, untranscribed markers and held tails passed");
}
