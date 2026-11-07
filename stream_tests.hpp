#pragma once

int streamSelfTest() {
    using namespace streams;
    std::ofstream report(fs::path(exeDir()) / L"test-streams.txt");
    int failures = 0;
    auto expect = [&](bool ok, const char* label) {
        report << (ok ? "PASS " : "FAIL ") << label << '\n';
        failures += !ok;
    };
    auto closeEnough = [](float a, float b) { return std::abs(a - b) < .0005f; };

    WAVEFORMATEX pcm{WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0};
    BYTE pcmSamples[]{0x00, 0x40, 0x00, 0xc0};
    auto stereo = InputFormat::read(pcm).frame(pcmSamples);
    expect(closeEnough(stereo.left, .5f) && closeEnough(stereo.right, -.5f), "stereo PCM16 conversion");
    WAVEFORMATEX mono{WAVE_FORMAT_PCM, 1, 48000, 144000, 3, 24, 0};
    BYTE monoSamples[]{0, 0, 0xc0};
    auto dualMono = InputFormat::read(mono).frame(monoSamples);
    expect(closeEnough(dualMono.left, -.5f) && closeEnough(dualMono.right, -.5f), "packed PCM24 mono conversion");

    SourceBuffer source(44100, 30);
    unsigned counter = 0;
    bool finite = true;
    // Two clocks with a 200 ppm offset and packet sizes rounded to whole frames.
    for (unsigned block = 0; block < 6000; ++block) {
        unsigned end = unsigned((block + 1) * 441.0882);
        for (; counter < end; ++counter) source.push({.25f, -.25f});
        source.beginBlock();
        for (unsigned i = 0; i < 480; ++i) {
            Frame frame = source.next();
            if (block > 4) finite &= closeEnough(frame.left, .25f) && closeEnough(frame.right, -.25f);
        }
    }
    expect(finite && source.underruns == 0 && source.dropped == 0,
           "sustained 44.1-to-48 kHz with clock mismatch");
    source.discontinuity();
    source.beginBlock();
    expect(source.next().left == 0, "capture gap cannot replay stale samples");
    for (unsigned i = 0; i < 50000; ++i) source.push({.2f, .2f});
    source.beginBlock();
    expect(source.dropped > 0 && closeEnough(source.next().left, .2f), "bounded latency after capture burst");

    StereoMix mixer;
    shared.enabled = true;
    shared.gentleMix = false;
    applyMixInput({7, 0x45, 0, 100});
    auto levels = snapshot().levels;
    Frame frame;
    for (int i = 0; i < 300; ++i)
        frame = mixer.next({.4f, 0}, {0, .6f}, mix::gain(1, levels.pc, 100), mix::gain(1, levels.chat, 100));
    expect(closeEnough(frame.left, 0) && closeEnough(frame.right, .6f), "wheel report silences PC samples, preserves Chat");
    applyMixInput({7, 0x45, 100, 0});
    levels = snapshot().levels;
    for (int i = 0; i < 300; ++i)
        frame = mixer.next({.4f, 0}, {0, .6f}, mix::gain(1, levels.pc, 50), mix::gain(1, levels.chat, 50));
    expect(closeEnough(frame.left, .2f) && closeEnough(frame.right, 0), "opposite wheel extreme and software master");
    for (int i = 0; i < 300; ++i) frame = mixer.next({.9f, .3f}, {.9f, .3f}, 1, 1);
    expect(closeEnough(frame.left, 1) && closeEnough(frame.right, 1.f / 3), "linked peak limiter preserves stereo balance");
    return failures ? 1 : 0;
}
