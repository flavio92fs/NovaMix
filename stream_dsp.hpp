#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace streams {
constexpr unsigned outputRate = 48000;
struct Frame { float left = 0, right = 0; };

// Capture and rendering run on one thread. A small elastic buffer absorbs
// packet jitter and the difference between the device clocks.
class SourceBuffer {
    std::vector<Frame> frames;
    size_t head = 0, used = 0, target;
    double phase = 0, correction = 0;
    bool playing = false;
    const double ratio;

    void consume(size_t count) {
        head = (head + count) % frames.size();
        used -= count;
    }

public:
    uint64_t dropped = 0, underruns = 0;

    SourceBuffer(unsigned rate, unsigned targetMs)
        : frames(rate / 5), target(rate * targetMs / 1000),
          ratio(double(rate) / outputRate) {}

    void push(Frame frame) {
        if (used == frames.size()) {
            consume(1);
            ++dropped;
        }
        frames[(head + used) % frames.size()] = frame;
        ++used;
    }

    void discontinuity() {
        dropped += used;
        head = used = 0;
        phase = correction = 0;
        playing = false;
    }

    void beginBlock() {
        if (used > target * 3) {
            dropped += used - target * 2;
            consume(used - target * 2);
        }
        if (!playing) {
            playing = used >= target;
            correction = 0;
        } else {
            double error = double(used) / double(target) - 1.25;
            double desired = std::clamp(error * 0.008, -0.004, 0.004);
            correction += (desired - correction) * 0.12;
        }
    }

    Frame next() {
        if (!playing) return {};
        double advance = phase + ratio * (1 + correction);
        size_t count = size_t(advance);
        if (used < 2 || used < count) {
            ++underruns;
            discontinuity();
            return {};
        }
        const auto& a = frames[head];
        const auto& b = frames[(head + 1) % frames.size()];
        float weight = float(phase);
        Frame sample{a.left + (b.left - a.left) * weight,
                     a.right + (b.right - a.right) * weight};
        consume(count);
        phase = advance - double(count);
        return sample;
    }
};

class StereoMix {
    float pc = 0, chat = 0, limiter = 1;

    static float approach(float value, float target) {
        constexpr float step = 1.0f / (outputRate * .005f);
        return value + std::clamp(target - value, -step, step);
    }

public:
    Frame next(Frame a, Frame b, float pcTarget, float chatTarget) {
        pc = approach(pc, pcTarget);
        chat = approach(chat, chatTarget);
        Frame result{a.left * pc + b.left * chat, a.right * pc + b.right * chat};
        const float peak = std::max(std::abs(result.left), std::abs(result.right));
        const float required = peak > 1 ? 1 / peak : 1;
        limiter = std::min(required, limiter + (1 - limiter) * .0005f);
        return {result.left * limiter, result.right * limiter};
    }
};
}
