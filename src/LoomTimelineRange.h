#pragma once
//=============================================================================================
// ODABIR RASPONA NA TIMELINEU: animator oznaci "od ovdje do ovdje ne valja", a radnje za taj dio
// (ispravi pozu, generiraj ponovo) pojave se iznad odabira.
//
// ZASTO. Radnje nad dijelom klipa bile su razbacane: raspon za "dio takea iznova" birao se gumbima
// Start here / End here u kartici Review lijevo, a pose blend u koracima u Inspectoru desno. Animator
// misli obrnuto - prvo KAD (odabir na timelineu), pa STO (radnja uz odabir).
//
// GESTA. Klik bez pomaka i dalje pomice playhead, kao prije. Tek kad se mis pomakne vise od
// dragThreshold piksela dok je gumb dolje, klik postaje odabir raspona. Ravnalo (gornja traka
// timelinea) ostaje cisti scrub i ne odabire nikad - tako je scrub uvijek dostupan.
//=============================================================================================
#include <algorithm>
#include <cmath>

namespace Loom{

struct TimelineRange{
    bool active = false;        //odabir postoji
    double first = 0.0, last = 0.0;   //kadrovi scene, first <= last
    //Stanje geste
    bool pressed = false;
    bool dragging = false;
    float pressX = 0.0f;
    double anchor = 0.0;

    void clear(){ active = false; pressed = false; dragging = false; }
};

//Rezultat jednog kadra geste: pomakni playhead (klik) i/ili promijenjen odabir
struct TimelineRangeInput{
    bool setPlayhead = false;
    double playhead = 0.0;
};

//held: gumb je dolje i gesta je pocela u traci; pressed: pritisak u ovom kadru. frameAt pretvara
//x u kadar scene (vec zaokruzen i ogranicen na raspon)
template<class FrameAt>
TimelineRangeInput updateTimelineRange(TimelineRange& range, bool pressed, bool held, float mouseX,
                                       FrameAt frameAt, float dragThreshold = 4.0f){
    TimelineRangeInput out;
    if(pressed){
        range.pressed = true;
        range.dragging = false;
        range.pressX = mouseX;
        range.anchor = frameAt(mouseX);
        out.setPlayhead = true;
        out.playhead = range.anchor;
        return out;
    }
    if(!range.pressed) return out;
    if(held){
        if(!range.dragging && std::fabs(mouseX - range.pressX) > dragThreshold) range.dragging = true;
        if(range.dragging){
            const double here = frameAt(mouseX);
            range.first = std::min(range.anchor, here);
            range.last = std::max(range.anchor, here);
            range.active = range.last > range.first;
        }else{
            out.setPlayhead = true;
            out.playhead = frameAt(mouseX);
        }
        return out;
    }
    //Pusteno: klik bez povlacenja brise stari odabir (kao u svakom montaznom programu)
    if(!range.dragging) range.active = false;
    range.pressed = false;
    range.dragging = false;
    return out;
}

}
