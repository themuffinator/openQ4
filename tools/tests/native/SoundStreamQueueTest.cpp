// SPDX-License-Identifier: GPL-3.0-or-later
// Production queue transitions, with counted OpenAL uploads and the real bounded
// decoder. The fixture compares every submitted PCM sample across loop seams.
#include "src/sound/SoundVorbisStream.h"
#include "src/sound/SoundSurround.h"
#include "src/sound/SoundQueue.h"
#include "tools/tests/native/SoundStreamJobStubs.h"
#include <climits>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#undef INT_MAX

static void Check(bool ok, const char* reason) { if (!ok) { std::fprintf(stderr, "%s\n", reason); std::exit(1); } }
template<class T> T Min(T a,T b) { return (std::min)(a,b); }
template<class T> T Max(T a,T b) { return (std::max)(a,b); }
struct idMath { static const int INT_MAX = 0x7fffffff; static int ClampInt(int a, int b, int c) { return c < a ? a : c > b ? b : c; } };
using ALuint = unsigned;
using byte = unsigned char;
using ALint = int;
using ALsizei = int;
constexpr int AL_NO_ERROR = 0;
enum { AL_INITIAL, AL_PLAYING, AL_STOPPED, AL_SOURCE_STATE, AL_BUFFERS_PROCESSED, AL_BUFFERS_QUEUED };
enum { AL_SAMPLE_OFFSET, AL_FORMAT_51CHN16 };
enum { AL_BUFFER = 100, AL_LOOPING, AL_SOURCE_TYPE, AL_STREAMING, AL_STATIC, AL_UNDETERMINED };
constexpr int AL_FALSE = 0;
#define OPENQ4_OPENAL_CENTER_ROUTING_SUPPORTED 1
static int sourceState = AL_PLAYING;
static int sourceQueued = 0;
// Detaching the buffer empties the queue; other source properties are not modelled.
void alSourcei(ALuint,int property,int value) { if (property == AL_BUFFER && value == 0) sourceQueued = 0; }
struct idWaveFile { static const int FORMAT_PCM = 1; };
constexpr int MAX_QUEUED_BUFFERS = 3;
struct Debug { bool GetBool() const { return false; } } s_debugHardware;
struct idLib { static void Printf(const char*,...) {} static void Warning(const char*,...) {} };
int alIsSource(ALuint source) { return source != 0; }
// As OpenAL Soft: a source that has left AL_INITIAL and is not playing reports
// every queued buffer as processed. Stopping keeps an idle source initial.
void alGetSourcei(ALuint,int property,int* value) {
	*value = property == AL_SOURCE_STATE ? sourceState : property == AL_BUFFERS_QUEUED ? sourceQueued :
		property == AL_BUFFERS_PROCESSED ? ( sourceState == AL_STOPPED ? sourceQueued : 0 ) :
		property == AL_SOURCE_TYPE ? ( sourceQueued ? AL_STREAMING : AL_UNDETERMINED ) : 0;
}
void alSourceUnqueueBuffers(ALuint,int count,ALuint*) { sourceQueued = Max(0, sourceQueued - count); }
void alSourcePlay(ALuint) { sourceState = AL_PLAYING; }
void alSourceStop(ALuint) { if (sourceState != AL_INITIAL) sourceState = AL_STOPPED; }
void alSourceRewind(ALuint) { sourceState = AL_INITIAL; }
void alDeleteBuffers(ALsizei, const ALuint*) {}
template<class T> using List = idList<T>;
// The production idSoundOggStream (@STREAM@ below) reads fixture files through
// these, with the deferring job system of SoundStreamJobStubs.h.
static int alive = 0;	// open stream files: a stream's decoder lives exactly as long
using uint64 = unsigned long long;
enum fsOrigin_t { FS_SEEK_SET };
struct idFile {
	std::vector<unsigned char> data;
	int cursor = 0;
	idFile() { ++alive; }
	~idFile() { --alive; }
	int Length() const { return static_cast<int>(data.size()); }
	int Tell() const { return cursor; }
	int Seek(long offset, fsOrigin_t) { if (offset < 0 || offset > Length()) return -1; cursor = static_cast<int>(offset); return 0; }
	int Read(void* target, int bytes) { bytes = Min(bytes, Length() - cursor); std::memcpy(target, data.data() + cursor, bytes); cursor += bytes; return bytes; }
};
struct VFS {
	idFile* OpenFileRead(const char* name) {
		std::ifstream file(name, std::ios::binary);
		if (!file) return nullptr;
		idFile* opened = new idFile;
		opened->data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return opened;
	}
	void CloseFile(idFile* file) { delete file; }
} vfs;
static VFS* fileSystem = &vfs;
static uint64 memoryUsed = 0;
static bool SoundMemoryAcquire(uint64 bytes) { memoryUsed += bytes; return true; }
static void SoundMemoryRelease(uint64 bytes) { memoryUsed -= bytes; }
idCVar s_streamMemoryMB("s_streamMemoryMB", "32", 0, "");

// @STREAM@

static std::vector<unsigned char> ReadFixture(const char* path) {
	std::ifstream file(path, std::ios::binary);
	return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
static int ReadFixtureAt(void* user, unsigned int at, void* target, int bytes) {
	auto& data = *static_cast<std::vector<unsigned char>*>(user);
	if (at > data.size()) return 0;
	bytes = Min(bytes, static_cast<int>(data.size() - at));
	std::memcpy(target, data.data() + at, bytes);
	return bytes;
}
struct idSoundSample_OpenAL {
	struct sampleBuffer_t { int numSamples = 0; const short* buffer = nullptr; int bufferSize = 0; };
	List<sampleBuffer_t> buffers;
	struct Format { struct Basic { int formatTag = 1, blockSize = 2; } basic; } format;
	ALuint openalBuffer = 0;
	int totalBufferSize = 0;
	int playLength = 0, playBegin = 0, channels = 0, rate = 0;
	bool streamed = true;
	idStr streamName;
	bool streamCountedLength = false;
	unsigned int streamPageStart = 0, streamPageEnd = 0, streamPageGranule = 0;
	std::vector<short> pcm;
	bool IsStreamed() const { return streamed; }
	bool EnsureCpuPayload() { return !streamed; }
	int NumChannels() const { return channels; }
	int SampleRate() const { return rate; }
	int GetOpenALBufferFormat() const { return channels; }
	const char* GetName() const { return streamName.c_str(); }
	void Load(const char* path) {
		streamName = path;
		std::vector<unsigned char> data = ReadFixture(path);
		SoundVorbisStream probe; Check(probe.Open(ReadFixtureAt, &data, static_cast<unsigned int>(data.size())), "fixture open");
		short* full = nullptr;
		playLength = stb_vorbis_decode_memory(data.data(), static_cast<int>(data.size()), &channels, &rate, &full);
		Check(playLength > 0, "reference PCM");
		pcm.assign(full, full + playLength * channels); std::free(full);
		// As LoadOggStream records a stream without length metadata.
		if (probe.Frames() == 0) {
			stb_vorbis_openq4_granule_page page{};
			Check(probe.FindLastGranulePage(page), "lengthless fixture has a positioned page");
			streamCountedLength = true;
			streamPageStart = page.page_start; streamPageEnd = page.page_end; streamPageGranule = page.granule;
		}
		totalBufferSize = playLength * channels * sizeof(short);
		format.basic.blockSize = channels * sizeof(short);
		buffers.push_back({playLength, pcm.data(), totalBufferSize});
	}
};
static std::map<ALuint,std::vector<short>> buffers;
static std::vector<short> queued;
static int uploadedMax = 0;
static bool failUpload = false;
void alBufferData(ALuint id, int, const void* data, int bytes, int) {
	uploadedMax = Max(uploadedMax, bytes);
	const short* samples = static_cast<const short*>(data);
	buffers[id].assign(samples, samples + bytes / sizeof(short));
}
void alSourceQueueBuffers(ALuint, int, const ALuint* id) { queued.insert(queued.end(), buffers[*id].begin(), buffers[*id].end()); ++sourceQueued; }
int CheckALErrors() { return failUpload ? 1 : 0; }
class idSoundVoice_OpenAL {
public:
	idSoundSample_OpenAL* leadinSample = nullptr;
	idSoundSample_OpenAL* loopingSample = nullptr;
	idSoundSample_OpenAL* nextQueuedSample = nullptr;
	idSoundSample_OpenAL* oggStreamSample = nullptr;
	idSoundOggStream* oggStream = nullptr;
	int nextQueuedOffset = 0, nextQueuedBuffer = 0;
	ALuint openalSource = 1;
	ALuint openalStreamingBuffer[3] = {1,2,3};
	bool paused = true;
	bool centerRouting = false;
	bool deepRoutedQueue = false;
	float centerChannel = 0.75f;
	bool openalStreamingBufferQueued[3] = {};
	bool queuedBufferPlaybackActive = false;
	~idSoundVoice_OpenAL() { ResetQueuedBufferState(); DeleteStreamingBuffers(); }
	int StreamingBufferIndex(ALuint id) { return static_cast<int>(id) - 1; }
	int SubmitBuffer(idSoundSample_OpenAL* sample, int buffer, int offset, ALuint streamBuffer);
	void ResetQueuedBufferState();
	void AdvanceQueuedBuffer();
	int StreamBlockFrames(idSoundSample_OpenAL* sample, int offset);
	bool PrepareStreamBlock(idSoundSample_OpenAL* sample, int offset);
	bool StreamBlockPending() const;
	bool SetNextQueuedSampleStart(idSoundSample_OpenAL* sample);
	static bool GetPlayableBufferRange(const idSoundSample_OpenAL*,int,int&,int&,int&,int&);
	bool QueueNextBuffer(ALuint buffer);
	int RoutedQueueFrames(int sampleRate);
	void DeleteStreamingBuffers();
	void FlushSourceBuffers();
	void ResetStreamingBufferQueueState();
	bool SourceHasPlayableBuffer() const;
	bool HasQueuedBufferState() const { return queuedBufferPlaybackActive || nextQueuedSample; }
	bool UsesStreamingBuffers() const { return true; }
	bool UsesBoundedQueue() const;
	bool Update();
	// Output changes are qualified by SoundOutputRecoveryTest; here they only gate Update.
	bool outputUnchanged = true, outputRecovery = false;
	bool CheckOutput() { return outputUnchanged; }
};
static bool outputConnected = true;
struct { struct { bool IsOutputConnected() const { return outputConnected; } } hardware; } soundSystemLocal;

// Production statics of AL_SoundVoice.cpp that the extracted bodies use.
static int soundDeepRoutedQueueVoices = 0;
static short soundRoutedQueueScratch[SOUND_QUEUE_MAX_FRAMES * 6];

// @PRODUCTION@

int main(int argc, char** argv) {
	Check(argc == 3, "provide matching mono leadin and loop fixtures");
	idSoundSample_OpenAL lead, loop;
	lead.Load(argv[1]); loop.Load(argv[2]);
	Check(lead.channels == loop.channels && lead.rate == loop.rate, "matching queue formats");
	// Pass 0 decodes each block inline (no accepting worker); pass 1 defers every block
	// to a job that runs only between "frames", as the workers would.
	for (int pass = 0; pass < 2; ++pass)
	for (int mode = 0; mode < 10; ++mode) {
		stubJobsSynchronous = pass == 0;
		// Mode 7 splits the leadin into two resident buffers; every other mode has one.
		lead.buffers.clear(); lead.buffers.push_back({lead.playLength,lead.pcm.data(),lead.totalBufferSize});
		lead.streamed = mode != 1 && mode != 5 && mode != 7; lead.openalBuffer = lead.streamed ? 0 : 7;
		loop.streamed = mode != 2 && mode != 6; loop.openalBuffer = loop.streamed ? 0 : 8;
		if (mode >= 8) { lead.streamed=loop.streamed=false; lead.openalBuffer=loop.openalBuffer=0; }
		buffers[7] = lead.pcm; buffers[8] = loop.pcm;
		idSoundVoice_OpenAL voice;
		voice.centerRouting = mode >= 4 && mode != 8;
		if (mode == 7) {
			const int half = lead.playLength / 2;
			lead.buffers.clear();
			lead.buffers.push_back({half,lead.pcm.data(),half * 2});
			lead.buffers.push_back({lead.playLength,lead.pcm.data()+half,(lead.playLength-half) * 2});
		}
		voice.leadinSample = &lead;
		voice.loopingSample = mode == 3 ? &lead : &loop;
		Check(voice.SetNextQueuedSampleStart(&lead), "initial queue start");
		const int offset = mode == 3 ? lead.playLength / 3 : 0;
		voice.nextQueuedOffset = offset;
		queued.clear();
		std::vector<short> expected(lead.pcm.begin() + offset * lead.channels, lead.pcm.end());
		const auto& repeat = mode == 3 ? lead.pcm : loop.pcm;
		for (int i = 0; i < 3; ++i) expected.insert(expected.end(), repeat.begin(), repeat.end());
		if (voice.centerRouting) {
			std::vector<short> surround(expected.size() * 6);
			SoundRouteMonoToCenter(expected.data(), static_cast<int>(expected.size()), voice.centerChannel, surround.data());
			expected.swap(surround);
		}
		int calls = 0, waits = 0;
		while (queued.size() < expected.size()) {
			Check(voice.nextQueuedSample != nullptr && ++calls < 10000, "loop progress");
			const size_t before = queued.size();
			if (voice.QueueNextBuffer((voice.centerRouting || voice.nextQueuedSample->openalBuffer==0) ? 1 + calls % 3 : 0)) continue;
			// Only a block still decoding may hold the queue back, and it queues nothing.
			Check(pass == 1 && voice.StreamBlockPending() && queued.size() == before && voice.nextQueuedSample, "a pending block keeps the voice");
			Check(StubRunPendingJobs() == 1, "one block decodes at a time per voice");
			++waits;
		}
		Check(std::equal(expected.begin(), expected.end(), queued.begin()), "leadin/loop sample continuity");
		Check(pass == 1 || waits == 0, "inline blocks never wait");
		Check(pass == 0 || waits > 0 || !lead.streamed || !loop.streamed || mode >= 8, "deferred streams wait for their blocks");
		voice.ResetQueuedBufferState();
		// A prefetched block may still be decoding: the stream waits for its worker.
		StubRunPendingJobs();
		idSoundOggStream::ReapReleased(false);
		Check(alive == 0 && idSoundOggStream::ReleasedCount() == 0 && memoryUsed == 0, "stop releases all decoder state");
	}
	stubJobsSynchronous = true;
	idSoundVoice_OpenAL voice;
	lead.streamed = true; lead.openalBuffer = 0;
	lead.buffers.clear(); lead.buffers.push_back({lead.playLength,lead.pcm.data(),lead.totalBufferSize});
	voice.leadinSample = &lead;
	voice.SetNextQueuedSampleStart(&lead);
	failUpload = true;
	Check(!voice.QueueNextBuffer(1) && alive == 0 && !voice.nextQueuedSample, "failed upload cancels decoder");
	failUpload = false;
	Check(voice.SetNextQueuedSampleStart(&lead) && voice.QueueNextBuffer(1), "recovered allocation");
	const auto oldQueueSize = queued.size();
	sourceState = AL_STOPPED; voice.paused = false;
	Check(!voice.Update() && queued.size() == oldQueueSize, "underrun requests logical-time recovery without replaying stale PCM");
	voice.ResetQueuedBufferState();
	Check(alive == 0, "underrun teardown releases decoder");
	{
		// A static source stopped by a lost endpoint recovers at the scene clock;
		// one that stopped with its output present finished. A changed output is
		// handed back before any queue work.
		idSoundVoice_OpenAL still;
		still.leadinSample = &lead; still.paused = false; sourceState = AL_STOPPED;
		outputConnected = false;
		Check(!still.Update() && still.outputRecovery, "a stop by a lost endpoint recovers at the clock");
		still.outputRecovery = false; outputConnected = true;
		Check(!still.Update() && !still.outputRecovery, "a stop with the output present is the end of the sound");
		sourceState = AL_PLAYING;
		Check(still.Update(), "a playing static voice carries on");
		still.outputUnchanged = false;
		Check(!still.Update(), "an output change is checked first");
	}
	{
		// A voice recycled from a sound that played and stopped. Its source stays
		// AL_STOPPED unless the flush starts it over, and then every buffer queued
		// on it counts as processed: the stream is unqueued as it is queued and
		// never plays once the voice pool has cycled.
		idSoundVoice_OpenAL recycled;
		lead.streamed = true; lead.openalBuffer = 0;
		lead.buffers.clear(); lead.buffers.push_back({lead.playLength,lead.pcm.data(),lead.totalBufferSize});
		sourceState = AL_PLAYING;
		recycled.FlushSourceBuffers();
		Check(sourceState == AL_INITIAL && sourceQueued == 0 && recycled.paused, "a flushed voice's source starts over");
		recycled.leadinSample = &lead;
		Check(recycled.SetNextQueuedSampleStart(&lead), "recycled queue start");
		for (ALuint buffer = 1; buffer <= 3; ++buffer) Check(recycled.QueueNextBuffer(buffer), "recycled queue block");
		Check(recycled.SourceHasPlayableBuffer(), "a stream queued on a recycled voice can play");
		recycled.ResetQueuedBufferState();
		Check(alive == 0, "recycled check releases decoder");
		// An output loss stops idle sources too; one already flushed stays initial.
		recycled.FlushSourceBuffers();
		alSourceStop(1);
		Check(sourceState == AL_INITIAL && !recycled.SourceHasPlayableBuffer(), "an idle source stopped by an outage stays ready");
		sourceState = AL_PLAYING;
	}
	Check(uploadedMax <= SOUND_QUEUE_DEEP_ROUTED_BUFFER_BYTES, "bounded queued PCM including centre routing");
	Check(soundDeepRoutedQueueVoices == 0, "finished voices return their deep routed slots");
	// The queue is refilled once per main-thread frame. Three buffers must ride
	// out a long frame (about one second) without exceeding the bytes per buffer
	// that the fixed and stream reservations count.
	for (int rate : {8000, 11025, 22050, 24000, 32000, 44100, 48000, 96000}) {
		for (int frameBytes : {2, 4, 12}) {
			const int frames = SoundQueueChunkFrames(rate, frameBytes);
			Check(frames >= 1 && frames * frameBytes <= SOUND_QUEUE_BUFFER_BYTES, "queue buffer within its reservation");
			Check(frames >= SOUND_QUEUE_MIN_FRAMES || frames * frameBytes == SOUND_QUEUE_BUFFER_BYTES, "queue never shallower than before");
			if (frameBytes == 2 && rate <= 48000) Check(3.0 * frames >= rate, "three mono buffers cover one second");
			if (frameBytes == 4 && rate <= 24000) Check(3.0 * frames >= rate, "three stereo buffers cover one second");
			if (frameBytes == 4 && rate <= 48000) Check(3.0 * frames >= 0.75 * rate, "three stereo buffers cover 750 ms");
			if (frameBytes == 12) Check(frames == 4096, "routed 5.1 outside the deep pool keeps one 4096-frame block");
			if (frameBytes == 12 && rate <= 48000) {
				const int deep = SoundQueueFrames(rate, 12, SOUND_QUEUE_DEEP_ROUTED_BUFFER_BYTES);
				Check(3.0 * deep >= rate && deep * 12 <= SOUND_QUEUE_DEEP_ROUTED_BUFFER_BYTES, "deep routed buffers cover one second");
			}
		}
	}
	for (int mode = 0; mode < 3; ++mode) {
		// 0: streamed Ogg, 1: CPU-resident PCM without an uploaded buffer, 2: centre-routed stream.
		idSoundVoice_OpenAL depth;
		lead.streamed = mode != 1; lead.openalBuffer = 0;
		lead.buffers.clear(); lead.buffers.push_back({lead.playLength,lead.pcm.data(),lead.totalBufferSize});
		depth.centerRouting = mode == 2;
		depth.leadinSample = &lead;
		Check(depth.SetNextQueuedSampleStart(&lead), "depth queue start");
		queued.clear(); uploadedMax = 0;
		for (ALuint buffer = 1; buffer <= 3; ++buffer) Check(depth.QueueNextBuffer(buffer), "depth queue block");
		const int outputChannels = mode == 2 ? 6 : lead.channels;
		const size_t queuedFrames = queued.size() / outputChannels;
		Check(queuedFrames >= static_cast<size_t>(Min(lead.playLength, lead.rate)), "three queued buffers hold one second");
		Check(uploadedMax <= (mode == 2 ? SOUND_QUEUE_DEEP_ROUTED_BUFFER_BYTES : SOUND_QUEUE_BUFFER_BYTES), "deeper queue keeps its per-buffer byte bound");
		Check(soundDeepRoutedQueueVoices == (mode == 2 ? 1 : 0) && depth.deepRoutedQueue == (mode == 2), "only routed voices hold a deep slot");
		std::vector<short> expected(lead.pcm.begin(), lead.pcm.begin() + Min(queuedFrames, lead.pcm.size()) * (mode == 2 ? 1 : lead.channels));
		if (mode == 2) {
			std::vector<short> surround(expected.size() * 6);
			SoundRouteMonoToCenter(expected.data(), static_cast<int>(expected.size()), depth.centerChannel, surround.data());
			expected.swap(surround);
		}
		Check(expected.size() == queued.size() && std::equal(expected.begin(), expected.end(), queued.begin()), "multi-read buffers keep sample continuity");
		depth.ResetQueuedBufferState();
		Check(alive == 0, "depth check releases decoder");
		depth.DeleteStreamingBuffers();
		Check(soundDeepRoutedQueueVoices == 0 && !depth.deepRoutedQueue, "deleting the buffers returns the deep slot");
	}
	{
		// The pool is bounded: routed voices beyond it keep 4096 frames until a slot frees.
		std::vector<idSoundVoice_OpenAL> routed(SOUND_QUEUE_DEEP_ROUTED_VOICES + 1);
		for (auto& voice : routed) voice.centerRouting = true;
		for (int i = 0; i < SOUND_QUEUE_DEEP_ROUTED_VOICES; ++i) Check(routed[i].RoutedQueueFrames(22050) == 7680, "deep routed slot");
		Check(routed.back().RoutedQueueFrames(22050) == 4096 && !routed.back().deepRoutedQueue, "pool exhausted keeps 4096 frames");
		Check(routed[0].RoutedQueueFrames(22050) == 7680 && soundDeepRoutedQueueVoices == SOUND_QUEUE_DEEP_ROUTED_VOICES, "slot reuse is not double counted");
		routed[0].DeleteStreamingBuffers();
		Check(routed.back().RoutedQueueFrames(22050) == 7680 && routed.back().deepRoutedQueue, "freed slot is reused");
	}
	Check(soundDeepRoutedQueueVoices == 0, "destroyed voices return every deep slot");
	{
		// A streamed voice starts without decoding on the caller: Update keeps it
		// while its first block decodes, then queues and plays it, one block ahead.
		stubJobsSynchronous = false;
		lead.streamed = true; lead.openalBuffer = 0;
		lead.buffers.clear(); lead.buffers.push_back({lead.playLength,lead.pcm.data(),lead.totalBufferSize});
		idSoundVoice_OpenAL start;
		start.leadinSample = &lead; start.loopingSample = &lead; start.paused = false;
		start.FlushSourceBuffers(); start.paused = false;
		Check(start.SetNextQueuedSampleStart(&lead), "start queue");
		start.nextQueuedOffset = lead.playLength / 2;
		queued.clear();
		Check(start.Update() && start.StreamBlockPending() && sourceQueued == 0 && sourceState == AL_INITIAL, "a pending start is not a finished voice");
		Check(start.Update() && start.StreamBlockPending() && stubPendingJobs.size() == 1, "polling a pending start submits nothing more");
		Check(StubRunPendingJobs() == 1, "first block decodes on the worker");
		Check(start.Update() && sourceQueued == 1 && sourceState == AL_PLAYING && start.StreamBlockPending(), "first block queued, played, next one decoding");
		Check(std::equal(queued.begin(), queued.end(), lead.pcm.begin() + (lead.playLength / 2) * lead.channels), "a pending start begins at its exact offset");
		StubRunPendingJobs();
		Check(start.Update() && sourceQueued == 2, "the queue fills a block per finished job");
		// Stopping while a block decodes neither waits nor leaks: the worker keeps the stream.
		Check(start.StreamBlockPending(), "a refill is pending at stop");
		const int jobsBefore = stubJobsRun;
		start.FlushSourceBuffers();
		Check(!start.oggStream && idSoundOggStream::ReleasedCount() == 1 && alive == 1, "a stopped voice hands its pending block to the release list");
		StubRunPendingJobs();
		Check(stubJobsRun == jobsBefore && stubJobsCancelled > 0, "the released block is cancelled before it runs");
		idSoundOggStream::ReapReleased(false);
		Check(alive == 0 && idSoundOggStream::ReleasedCount() == 0 && memoryUsed == 0, "the released stream is reaped");
		// A stream that cannot open fails the voice, as before.
		idSoundSample_OpenAL missing = lead; missing.streamName = "missing-fixture.ogg";
		idSoundVoice_OpenAL broken;
		broken.leadinSample = &missing; broken.paused = false; broken.FlushSourceBuffers(); broken.paused = false;
		Check(broken.SetNextQueuedSampleStart(&missing), "broken queue start");
		Check(!broken.Update() && !broken.nextQueuedSample && alive == 0, "a missing stream ends its voice");
		// A damaged header fails on the poll after its job, not on submission.
		idSoundSample_OpenAL wrong = lead; wrong.rate = lead.rate * 2;
		idSoundVoice_OpenAL mismatch;
		mismatch.leadinSample = &wrong; mismatch.paused = false; mismatch.FlushSourceBuffers(); mismatch.paused = false;
		Check(mismatch.SetNextQueuedSampleStart(&wrong) && mismatch.Update() && mismatch.StreamBlockPending(), "the header is checked off the caller");
		StubRunPendingJobs();
		Check(!mismatch.Update() && !mismatch.nextQueuedSample && alive == 0, "a header that disagrees with the load ends its voice");
		sourceState = AL_PLAYING;
		stubJobsSynchronous = true;
	}
	std::puts("PASS production queue: leadin/loop, mixed static/streamed, offset, three scratch buffers, one-second depth, recycled voices, cancellation and release, inline and worker-decoded blocks, pending starts and released refills");
}
