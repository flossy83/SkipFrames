#include <avisynth.h>
#include <cstring>  // memset
#include <cmath>    // round
#include <vector>   // vector
#include <cstdint>  // ?

const AVS_Linkage* AVS_linkage = nullptr;  // required for modern Avisynth+ 

class SkipFrames : public GenericVideoFilter {

private:  // redundant 

    // user args
    int skipstrings;  // skip contiguous strings (0=disabled(def), 1=enabled(inf), 2=enabled(2f), 3=enabled(3f) etc)
    int resetonseek;  // reset skipahead on seek (0=disabled, 1=enabled(1s), 2=enabled(2s), 30=enabled(30s,def) etc)
    int seqthresh;    // number of sequential frames required before making decisions
    int ignorerange;  // number of frames to ignore at the beginning in relation to decision making
    int audioSyncMethod;  // audio sync method (1 = skipMap with fallback to skipahead, 2 = skipahead only)
    int debug;        // debug with PropShow(props="_SkipFrames*") (1=enabled, 0=disabled)

    // video  
    int skipahead;        // cumulative frameskip counter
    std::vector<int> skipMap;  // skipahead indexed by frame number
    int prevframe;        // previous frame number for detecting nonsequential access  
    int seqframes;        // number of sequential frames since last nonsequential access
    int prevseqframe;     // most recent sequential frame for detecting seeks
    int ignorecount;       // cumulative number of getframe()s within ignorerange
    int seekframes;       // number of frames to trigger a seek 
    int prevframeOnSeek;  // previous frame on last frame of seek event
    int currframeOnSeek;  // current frame on last frame of seek event

    // audio      
    int audioRelativeFrame;         // relative video frame for an audio block (vi.FramesFromAudioSamples(start))
    int audioRelativeFrameFound;    // nearest relative video frame found to contain a skipahead value
    int audioSkipaheadFrames;       // skipMap[audioRelativeFrameFound]
    double audioSamplesPerFrame;    // audio samples per video frame  
    int64_t audioSkipaheadSamples;  // cumulative audio skip counter in audio samples (skipahead * audioSamplesPerFrame)
    int64_t audioBlockStart;        // start of current audio block
    int64_t audioBlockNewStart;     // start of current audio block + audioSkipaheadSamples
    int64_t audioBlockLength;       // length of current audio block



public:

    // constructor
    SkipFrames(PClip _child, int _skipstrings, int _resetonseek, int _seqthresh, int _ignorerange,
        int _audioSyncMethod, int _debug, IScriptEnvironment* env)
        : GenericVideoFilter(_child), skipstrings(_skipstrings), resetonseek(_resetonseek), seqthresh(_seqthresh),
        ignorerange(_ignorerange), audioSyncMethod(_audioSyncMethod), debug(_debug),
        skipahead(0), prevframe(0), seekframes(-1), seqframes(0), prevseqframe(0), ignorecount(0), prevframeOnSeek(-1),
        currframeOnSeek(-1), audioRelativeFrame(-1), audioRelativeFrameFound(-1), audioSkipaheadFrames(0),
        audioSamplesPerFrame(0.0), audioSkipaheadSamples(0), audioBlockStart(0), audioBlockNewStart(0), audioBlockLength(0) {

        if (vi.num_frames <= 0) { env->ThrowError("SkipFrames: clip has no frames!"); }

        // validate user args
        if (skipstrings < 0) { env->ThrowError("SkipFrames: SkipStrings must be >= 0"); }
        if (resetonseek != 0 && resetonseek < 30) { env->ThrowError("SkipFrames: ResetOnSeek must be 0 or >= 30"); }
        if (seqthresh < 2) { env->ThrowError("SkipFrames: SeqThresh must be >=2"); }
        if (ignorerange < 0) { env->ThrowError("SkipFrames: IgnoreRange must be >= 0"); }
        if (audioSyncMethod != 1 && audioSyncMethod != 2) { env->ThrowError("SkipFrames: AudioSync must be 1 or 2"); }
        if (debug != 0 && debug != 1) { env->ThrowError("SkipFrames: Debug must be 0 or 1"); }

        // avoid having to calculate all this per frame / block
        double fps = static_cast<double>(vi.fps_numerator) / vi.fps_denominator;
        int roundedfps = static_cast<int>(std::round(fps));
        seekframes = roundedfps * resetonseek;
        audioSamplesPerFrame = static_cast<double>(vi.audio_samples_per_second) / fps;
        skipMap.resize(vi.num_frames, -1);   // initialise with <num_frames> elements all set to -1
    }

    // destructor
    ~SkipFrames() {

    }


    // Set default multithreading mode to MT_SERIALIZED (mutex).  
    // Note: even in MT_SERIALIZED mode, frame evaluation order is still not guaranteed sequential,
    // therefore multithreading should ideally not be used.  If it absolutely must, use RequestLinear()
    // or Preroll(24) before/after SkipFrames().  Without RequestLinear/Preroll, there is still some
    // logic in GetFrame to handle random nonsequential access, but it can still result in some frames
    // not being skipped when they should be.  The presentation order will always be sequential.
    int __stdcall SetCacheHints(int cachehints, int frame_range) override {
        switch (cachehints)
        {
        case CACHE_GET_MTMODE:
            return MT_SERIALIZED;
            // case CACHE_ACCESS_SEQ1:   // sequential access (big perf penalty, no improvement in testing)
                 //return 1;
        default:
            return 0;
        }
    }


    // frame props for debugging - set debug=1 and use PropShow(props="_SkipFrames*")
    void __stdcall SetFrameProps(PVideoFrame pvidframe, int currframe, int outframe, bool ignore, IScriptEnvironment* env) {

        AVSMap* rwProps = env->getFramePropsRW(pvidframe);
        env->propSetInt(rwProps, "_SkipFrames_01_Arg_SkipStrings", skipstrings, 0);
        env->propSetInt(rwProps, "_SkipFrames_02_Arg_ResetOnSeek", resetonseek, 0);
        env->propSetInt(rwProps, "_SkipFrames_03_Arg_SeqThresh", seqthresh, 0);
        env->propSetInt(rwProps, "_SkipFrames_04_Arg_IgnoreRange", ignorerange, 0);
        env->propSetInt(rwProps, "_SkipFrames_05_Arg_AudioSyncMethod", audioSyncMethod, 0);

        // env->propSetInt(rwProps, "_SkipFrames_06_Bool_Ignore", ignore ? 1 : 0, 0);
        env->propSetInt(rwProps, "_SkipFrames_06_Int_IgnoreCount", ignorecount, 0);
        env->propSetInt(rwProps, "_SkipFrames_07_Int_InFrame", currframe, 0);
        env->propSetInt(rwProps, "_SkipFrames_08_Int_SkipAhead", skipahead, 0);
        env->propSetInt(rwProps, "_SkipFrames_09_Int_OutFrame", outframe, 0);
        env->propSetInt(rwProps, "_SkipFrames_10_Int_PreviousFrame", prevframe, 0);

        env->propSetInt(rwProps, "_SkipFrames_11_Int_SequentialFrames", seqframes, 0);
        env->propSetInt(rwProps, "_SkipFrames_12_Int_PrevSeqFrame", prevseqframe, 0);
        env->propSetInt(rwProps, "_SkipFrames_13_Int_PrevFrameOnSeek", prevframeOnSeek, 0);
        env->propSetInt(rwProps, "_SkipFrames_14_Int_CurrFrameOnSeek", currframeOnSeek, 0);

        env->propSetInt(rwProps, "_SkipFrames_15_Int_AudioRelativeFrame", audioRelativeFrame, 0);
        env->propSetInt(rwProps, "_SkipFrames_16_Int_AudioRelativeFrameFound", audioRelativeFrameFound, 0);
        env->propSetInt(rwProps, "_SkipFrames_17_Int_AudioSkipaheadFrames", audioSkipaheadFrames, 0);

        env->propSetFloat(rwProps, "_SkipFrames_18_Dbl_AudioSamplesPerFrame", audioSamplesPerFrame, 0);
        env->propSetInt(rwProps, "_SkipFrames_19_Int_AudioSkipaheadSamples", audioSkipaheadSamples, 0);
        env->propSetInt(rwProps, "_SkipFrames_20_Int_AudioBlockStart", audioBlockStart, 0);
        env->propSetInt(rwProps, "_SkipFrames_21_Int_AudioBlockNewStart", audioBlockNewStart, 0);
        env->propSetInt(rwProps, "_SkipFrames_22_Int_AudioBlockLength", audioBlockLength, 0);

        return;
    }


    // video processing per frame
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {


        // ignore first few frames - random cache probes?
        if (n <= ignorerange - 1) {
            if (n >= 0 && n < (int)skipMap.size()) { skipMap[n] = skipahead; }
            int outframe = (n + skipahead >= vi.num_frames ? vi.num_frames - 1 : n + skipahead);
            PVideoFrame out = child->GetFrame(outframe, env);
            if (debug == 1) { ignorecount++; SetFrameProps(out, n, outframe, true, env); }
            return out;
        }


        // require <seqthresh> frames of sequential access before making any decisions
        if (n == prevframe + 1) {
            seqframes++;
            if (seqframes >= seqthresh + 1) { prevseqframe = n; }
        }
        else { seqframes = 0; }

        if (seqframes < seqthresh) {
            if (n >= 0 && n < (int)skipMap.size()) { skipMap[n] = skipahead; }
            int outframe = (n + skipahead >= vi.num_frames ? vi.num_frames - 1 : n + skipahead);
            PVideoFrame out = child->GetFrame(outframe, env);
            if (debug == 1) { SetFrameProps(out, n, outframe, false, env); }
            prevframe = n;
            return out;
        }


        // reset on seek  
        if (resetonseek > 0) {
            if (n > (prevseqframe + seekframes) || n < (prevseqframe - seekframes)) {
                if (debug == 1) { prevframeOnSeek = prevseqframe; currframeOnSeek = n; }
                skipahead = 0;
            }
        }


        // increment skipahead counter if SkipMe=1 on <currentframe+skipahead> 
        PVideoFrame checkFrame = child->GetFrame(n + skipahead, env);
        const AVSMap* props = env->getFramePropsRO(checkFrame);
        int error = 0;
        int64_t skipVal = env->propGetInt(props, "SkipMe", 0, &error);
        if (error == 0 && skipVal == 1) {
            skipahead++;

            // detect contiguous strings
            if (skipstrings > 0) {  // 0=disabled(def), 1=enabled(inf), 2=enabled(2f), 3=enabled(3f), etc.
                int prevSkipahead = skipahead;
                while (skipahead < (skipstrings == 1 ? vi.num_frames : prevSkipahead + skipstrings - 1)) {
                    PVideoFrame thisCheckFrame = child->GetFrame(n + skipahead, env);
                    const AVSMap* thisProps = env->getFramePropsRO(thisCheckFrame);
                    int thisError = 0;
                    int64_t thisSkipVal = env->propGetInt(thisProps, "SkipMe", 0, &thisError);
                    if (thisError == 0 && thisSkipVal == 1) {
                        skipahead++;
                        continue;
                    }
                    else { break; }
                }
            }
        }


        // set output frame
        if (n >= 0 && n < (int)skipMap.size()) { skipMap[n] = skipahead; }
        int outframe = (n + skipahead >= vi.num_frames ? vi.num_frames - 1 : n + skipahead);
        PVideoFrame out = child->GetFrame(outframe, env);
        if (debug == 1) { SetFrameProps(out, n, outframe, false, env); }

        prevframe = n;
        return out;
    }


    // audio processsing per block of samples from start to <start+count> 
    void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) override {

        // if no audio track or no samples to produce, return or make silent audio
        if (!vi.HasAudio() || count <= 0) {
            if (count > 0) std::memset(buf, 0, static_cast<size_t>(count) * vi.BytesPerAudioSample());
            return;
        }

        // calculate number of audio samples to skip ahead based on skipMap/skipahead
        if (audioSyncMethod == 1) {

            int relativeFrame = vi.FramesFromAudioSamples(start);
            if (debug == 1) { audioRelativeFrame = relativeFrame; }
            int searchRadius = 24;  // 23,22,21...0 for relativeFrame=23

            // search backwards through the skipMap until finding the nearest skipahead value for relativeFrame
            int foundSkipVal = -1;  int foundFrame = -1;
            for (int i = relativeFrame; (relativeFrame - i < searchRadius) && i >= 0 && i < vi.num_frames; --i)
            {
                foundSkipVal = skipMap[i];  foundFrame = i;
                if (foundSkipVal != -1) { break; }
                /*
                rf      i               breaking on i =
                0       0               -1
                1       1,0,            -1
                2       2,1,0           -1

                23      23,22,21...0    -1
                24      24,23,22...1    0
                25      25,24,23...2    1
                */
            }
            if (foundSkipVal != -1) {
                audioSkipaheadSamples = static_cast<int64_t>(foundSkipVal * audioSamplesPerFrame + 0.5);
                if (debug == 1) { audioRelativeFrameFound = foundFrame; audioSkipaheadFrames = foundSkipVal; }
            }
            else {  // fallback to audioSyncMethod=2
                audioSkipaheadSamples = static_cast<int64_t>(skipahead * audioSamplesPerFrame + 0.5);
                if (debug == 1) { audioRelativeFrameFound = -1;  audioSkipaheadFrames = skipahead; }
            }
        }
        else {
            audioSkipaheadSamples = static_cast<int64_t>(skipahead * audioSamplesPerFrame + 0.5);
            if (debug == 1) { audioRelativeFrameFound = -1;  audioSkipaheadFrames = skipahead; }
        }


        // calculate new audio block start position
        audioBlockNewStart = start + audioSkipaheadSamples;
        if (debug == 1) { audioBlockStart = start; audioBlockLength = count; }


        // return silent audio when repeating final padded frames
        if (audioBlockNewStart >= vi.num_audio_samples) {
            std::memset(buf, 0, static_cast<size_t>(count) * vi.BytesPerAudioSample());
            return;
        }

        // return silent audio for section of block which overlaps with final padded frames
        if (audioBlockNewStart + count > vi.num_audio_samples) {
            int64_t available = vi.num_audio_samples - audioBlockNewStart;
            // set buffer with start+available
            child->GetAudio(buf, audioBlockNewStart, available, env);
            // set remainder with silence
            std::memset(static_cast<char*>(buf) + available * vi.BytesPerAudioSample(), 0,
                static_cast<size_t>(count - available) * vi.BytesPerAudioSample());
            return;
        }

        // set buffer with new start position
        child->GetAudio(buf, audioBlockNewStart, count, env);
    }
};

// entry point 
AVSValue __cdecl Create_SkipFrames(AVSValue args, void* user_data, IScriptEnvironment* env) {
    return new SkipFrames(args[0].AsClip(),
        args[1].AsInt(0),        // skipstrings 
        args[2].AsInt(0),        // resetonseek
        args[3].AsInt(12),       // seqthresh
        args[4].AsInt(24),       // ignorerange
        args[5].AsInt(1),        // audiosyncmethod
        args[6].AsInt(0), env);  // debug
}

extern "C" __declspec(dllexport) const char* __stdcall
AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;
    env->AddFunction("SkipFrames",
        "c[SkipStrings]i[ResetOnSeek]i[SeqThresh]i[IgnoreRange]i[AudioSync]i[Debug]i", Create_SkipFrames, nullptr);
    return "SkipFrames v1.0 - dynamic frame/audio skipper";
}

