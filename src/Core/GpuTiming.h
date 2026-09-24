#pragma once
#include <string>
#include <vector>

//One moment the CARD recorded within a frame.
//
//The time is the card's, not the CPU's. A stopwatch on the CPU measures when a command was
//submitted, and the card executes it when it gets there - so one step would get charged the
//time some other step spent. Here each timestamp sits in the command buffer between two
//commands, and the card writes it when it reaches it.
//
//A timestamp names what JUST FINISHED: the gap between it and the one before it is the time of
//the step that carries its name. The first timestamp in a frame is therefore just a start, and
//is always zero.
//
//Deliberately without a single Vulkan type, like ShadingRate: numbers and names, nothing more.
struct GpuTimestamp{
    std::string label;
    double milliseconds = 0.0;   //from the first timestamp in the same frame
};
