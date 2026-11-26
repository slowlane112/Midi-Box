#include <stdio.h>
#include "smf.h"
int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        smf_t s; smf_err_t e = smf_load(&s, argv[a]);
        if (e) { printf("%s: error %d\n", argv[a], e); continue; }
        printf("title='%s' format=%u ntracks=%u ppq=%u tempos=%d total_ticks=%u total_ms=%llu parts=%d\n",
               s.title, s.format, s.ntracks, s.ppq, s.ntempos, s.total_ticks, (unsigned long long)(s.total_us/1000), s.nparts);
        for (int i = 0; i < s.ntempos; i++) printf("  tempo tick=%u us/qn=%u at=%llums\n", s.tempos[i].tick, s.tempos[i].us_per_qn, (unsigned long long)(s.tempos[i].us/1000));
        for (int i = 0; i < s.nparts; i++) printf("  part %d: trk %u ch %u prog %u notes %u name '%s'\n", i, s.parts[i].track, s.parts[i].channel+1, s.parts[i].program, s.parts[i].notes, s.parts[i].name);
        uint32_t t = smf_us_to_tick(&s, 3000000); printf("  3.0s -> tick %u -> %llums\n", t, (unsigned long long)(smf_tick_to_us(&s, t)/1000));
        smf_free(&s);
    }
}
