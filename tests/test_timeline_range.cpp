// Odabir raspona na timelineu (LoomTimelineRange.h): klik i dalje pomice playhead, povlacenje
// oznaci raspon, drhtanje ruke ispod praga ne postane odabir, a klik bez povlacenja brise odabir.
// Kadar = x / 10 (100 px je kadar 10).
#include "TestHarness.h"

#include "../src/LoomTimelineRange.h"

int main(){
    TestReport report("timeline_range");
    auto frameAt = [](float x){ return double(std::round(x / 10.0f)); };

    Loom::TimelineRange range;
    //Klik: pritisak, pa pustanje na istom mjestu
    Loom::TimelineRangeInput in = Loom::updateTimelineRange(range, true, true, 200.0f, frameAt);
    Loom::updateTimelineRange(range, false, false, 200.0f, frameAt);
    report.check("klik pomakne playhead i ne napravi odabir", in.setPlayhead && in.playhead == 20.0 && !range.active,
                 fmt("playhead %.0f, odabir %d", in.playhead, range.active));

    //Drhtanje 3 px dok je gumb dolje: jos uvijek klik (scrub)
    Loom::updateTimelineRange(range, true, true, 200.0f, frameAt);
    in = Loom::updateTimelineRange(range, false, true, 203.0f, frameAt);
    Loom::updateTimelineRange(range, false, false, 203.0f, frameAt);
    report.check("drhtanje ispod praga ostaje scrub", in.setPlayhead && !range.active, "");

    //Povlacenje s 400 na 250: raspon 25-40, poredan
    Loom::updateTimelineRange(range, true, true, 400.0f, frameAt);
    Loom::updateTimelineRange(range, false, true, 330.0f, frameAt);
    in = Loom::updateTimelineRange(range, false, true, 250.0f, frameAt);
    Loom::updateTimelineRange(range, false, false, 250.0f, frameAt);
    report.check("povlacenje oznaci raspon (i unatrag)", range.active && range.first == 25.0 && range.last == 40.0 && !in.setPlayhead,
                 fmt("%.0f-%.0f", range.first, range.last));

    //Klik bez povlacenja brise odabir
    Loom::updateTimelineRange(range, true, true, 600.0f, frameAt);
    Loom::updateTimelineRange(range, false, false, 600.0f, frameAt);
    report.check("klik brise odabir", !range.active, "");
    return report.result();
}
