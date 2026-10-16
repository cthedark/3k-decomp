// C wrapper around ymfm's YM3812 (OPL2). See opl.h.
#include "opl.h"

#include <new>

#include "ymfm_opl.h"

namespace {
constexpr uint32_t kClock = 3579545;   // the AdLib card's crystal
}

struct opl {
    ymfm::ymfm_interface intf;   // the defaults are enough: no timers, no external memory
    ymfm::ym3812 chip;
    opl() : intf(), chip(intf) { chip.reset(); }
};

extern "C" {

opl_t *opl_create(void)
{
    return new (std::nothrow) opl();
}

void opl_free(opl_t *o)
{
    delete o;
}

void opl_reset(opl_t *o)
{
    o->chip.reset();
}

unsigned opl_native_rate(void)
{
    // clock / 72 = 49,715.9 -> 49,716
    return kClock / 72u + ((kClock % 72u) >= 36u ? 1u : 0u);
}

void opl_write(opl_t *o, uint8_t reg, uint8_t value)
{
    o->chip.write_address(reg);
    o->chip.write_data(value);
}

void opl_generate(opl_t *o, int16_t *out, size_t frames)
{
    ymfm::ym3812::output_data buf[256];
    while (frames > 0) {
        size_t n = frames < 256 ? frames : 256;
        o->chip.generate(buf, static_cast<uint32_t>(n));
        for (size_t i = 0; i < n; i++) {
            int32_t v = buf[i].data[0];
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[i] = static_cast<int16_t>(v);
        }
        out += n;
        frames -= n;
    }
}

}   // extern "C"
