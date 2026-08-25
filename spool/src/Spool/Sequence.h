#pragma once
#include "ImageFile.h"
#include <cstdint>
#include <string>

namespace Spool{

//An image sequence on disk: frame_0000.png, frame_0001.png, and so on.
//
//This is what the rest of the export exists for. A renderer that can run without a window
//and is driven by a frame number rather than a clock produces the same frames every time;
//this is what makes them a thing you can come back to, resume, or compare against a
//previous run.
struct SequenceConfig{
    //Created if it is not there. A missing output folder is the most ordinary reason a
    //render that took an hour has nothing to show for it
    std::string directory = "sequence";

    std::string prefix = "frame_";
    std::string extension = ".png";

    //frame_0000 rather than frame_0: four digits sort correctly in every file browser and
    //every tool that globs them, which frame_10 next to frame_9 does not
    uint32_t digits = 4;

    //Where the numbering starts. Some pipelines count from one, some from a timecode
    uint32_t firstIndex = 0;

    SaveConfig save = {};
};

class SequenceWriter{
    public:
    explicit SequenceWriter(const SequenceConfig& config = {});

    //Writes the next frame and returns the path it went to. The index advances on success
    //only, so a throw does not leave a gap in the numbering
    std::string write(const Image& image);

    //Where a given frame number would go, without writing anything. Useful for reading a
    //sequence back, and for saying in a log where the frames are about to appear
    std::string pathFor(uint32_t index) const;

    //getters
    uint32_t frameCount() const {return written;}
    uint32_t nextIndex() const {return config.firstIndex + written;}
    const SequenceConfig& getConfig() const {return config;}

    private:
    SequenceConfig config;
    uint32_t written = 0;
};

}
