# Video audio import fixtures

These synthetic files contain no third-party media. The tone fixtures have one black 16-by-16 video frame and two 0.25-second mono audio streams: 440 Hz first, then 880 Hz. The silent-video fixture contains only video.

They were generated with FFmpeg 9.0.2. For example, run this from the repository root:

```sh
ffmpeg -f lavfi -i color=black:s=16x16:r=4:d=0.25 \
  -f lavfi -i sine=frequency=440:sample_rate=48000:duration=0.25 \
  -f lavfi -i sine=frequency=880:sample_rate=48000:duration=0.25 \
  -map 0:v:0 -map 1:a:0 -map 2:a:0 -c:v mpeg4 -c:a aac \
  -shortest tests/fixtures/tone.mp4
```

For `tone.mkv`, use MPEG-4 video and PCM signed 16-bit audio. For `tone.webm`, use `libvpx-vp9` and `libopus`. For `silent-video.mp4`, use the same color source with MPEG-4 video and `-an`.
