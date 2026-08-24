# Video fixtures

`mecaps_demo.webm` is generated media containing five seconds of a GStreamer
`videotestsrc` SMPTE RP 219 test pattern with an advancing running-time overlay
and a synchronized `audiotestsrc` sine wave. It does not contain third-party
footage or audio.

Regenerate it with GStreamer base and good plugins:

```sh
gst-launch-1.0 -q webmmux name=mux \
    ! filesink location=mecaps_demo.webm \
    videotestsrc num-buffers=150 pattern=smpte-rp-219 \
    ! video/x-raw,width=320,height=180,framerate=30/1 \
    ! timeoverlay valignment=bottom halignment=center font-desc="Sans 20" \
    ! vp8enc deadline=1 target-bitrate=200000 ! queue ! mux.video_0 \
    audiotestsrc num-buffers=150 samplesperbuffer=1470 wave=sine \
    ! audio/x-raw,rate=44100 ! vorbisenc ! queue ! mux.audio_0
```
