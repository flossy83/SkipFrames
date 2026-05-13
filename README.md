### SkipFrames
---

#### About

SkipFrames is a simple Avisynth plugin for skipping video and audio dynamically in realtime without needing
multiple passes.

Why not just use Trim or DeleteFrame inside a ScriptClip?  The problem is that ScriptClip doesn't touch audio -
it always outputs the same audio as the input clip, so the audio will dynamically go out of sync.

This plugin just remaps input frames to output frames and its corresponding audio samples along with it.

#### Usage

Inside your ScriptClip, set a frame property called "SkipMe" to 1 if you want the frame and its audio to be skipped.
Then call SkipFrames() to skip them.

#### Example

```
ScriptClip(last, "SetFramesToSkip(last, current_frame)", after_frame=true, local=false)

SkipFrames()

# RequestLinear() or Preroll(24)   # you may need to use one of these if multithreading
# PropShow(props="_SkipFrames*")   # for debugging - requires v3.7.4+

function SetFramesToSkip(clip c, int current_frame){

	# flag every 24th frame to be skipped
	if (Fmod(float(current_frame+1), 24.0) == 0){
		return c.PropSet("SkipMe", 1) 
	}
	else { return c }
}
```

#### Advanced Settings

```
SkipFrames(

\ SkipStrings=0,    /*  Allow skipping of contiguous strings of frames.

                    0 = disabled (default). Only skips individual frames.  Minimises CPU overhead
                        as it only looks ahead once per frame for the next "SkipMe" frame. 

                    1 = enabled.  Skips strings of any length.  Can cause CPU spikes on long strings
                        as it keeps looking ahead until the end of the string.  eg. encountering a
                        block of 100 "SkipMe" frames can suddenly trigger 100 GetFrame()s.

                    >=2 = enabled, but limited to strings of specified length. eg. a value of
                        2 means strings of max length 2 will be skipped. Try to minimise this
                        value for your use case to prevent CPU spikes. */


\ ResetOnSeek=0,    /*  Reset to original time code when seeking.  Only needed for realtime use.

                    0 = disabled (default).  Seeking during playback will be relative to the current
                        output frame, which is a function of the number of frames skipped so far.
                        If that number has grown large, it means eg. seeking to frame 0 will seek
                        to 0+skipcount and you won't be able to seek to frame 0 anymore. 

                    >=30 = enabled, and limits the seek window to this many seconds. eg. a value of
                        30 means you must seek more than 30 seconds in either direction to reset
                        the skipcount and get a seek to the original time code. */


\ SeqThresh=12,     /*  Number of sequential frames needed before decision making can start/continue
                        (default=12).

                        This setting relates to the problem of what to do when Avisynth requests
                        frames in nonsequential order, which can occur if multithreading is enabled
                        and RequestLinear() or Preroll() is not used before/after SkipFrames().

                        If a nonsequential frame is encountered, SkipFrames waits until <SeqThresh>
                        sequential frames have occurred before detecting any more "SkipMe" frames. */


\ IgnoreRange=24,   /*  Number of frames to ignore at the beginning of the clip in relation to
                        decision making (default=24).

                        When testing with multithreading, it was observed that the prefetcher would
                        sporadically make requests to frame number <PrefetchFrames> throughout
                        the duration of the clip, thus frequently triggering nonsequential access
                        detection and the associated <SeqThresh> delay.  This setting was able to
                        avoid the issue on the author's system by ignoring those frames. If you need
                        to skip frames within the ignore range, pad the start with a BlankClip(). */

\ AudioSync=1,      /*  Method of synchronising audio to video.

                        1 = sync to video frame number corresponding to audio block (default)
                        2 = sync directly to GetFrame() (not recommended)  */

\ Debug=0           /*  Write debug info to frame properties
                        
                        0 = disabled (default)
                        1 = enabled (use PropShow(props="_SkipFrames*") to display)

                        Values to check:
                        OutFrame = InFrame + SkipAhead
                        SkipMes occur on PreviousFrame when SkipAhead increments
                        SequentialFrames resets to 0 on nonsequential access or seeking
                        AudioSkipaheadSamples = AudioSamplesPerFrame * AudioSkipaheadFrames  */

\ )
```

#### Testing

For testing you can use [this clock video](https://www.ixbt.com/multimedia/video-methodology/other/watch-4k-24p.mp4)

<img width="854" height="480" alt="skipframes avs_snapshot_00 11 540" src="https://github.com/user-attachments/assets/2ea4c90d-8cd6-4fd3-99bb-c6c6cead9259" />
</br></br>

With the following script, a frame skip and audio click should occur when the hand points at 12 o'clock.

```
LWLibavVideoSource("clock.mp4", cache=false)
AudioDub(last, Tone(60.0, 440, 48000, 2, "sine", 1.0))
BicubicResize(854,480)

ScriptClip(last, 
\ """
	if (Fmod(float(current_frame), 24.0) == 0){ 
		return last.PropSet("SkipMe", 1) }
	else { return last } 
	
\ """, after_frame=true, local=false)

SkipFrames(debug=1)   # try adding .Prefetch(4,8) or .Prefetch(8,16)
RequestLinear()       # try without this line or Preroll(24) instead
PropShow(props="_SkipFrames*")
```

