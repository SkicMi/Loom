#include "Sequence.h"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace Spool{

SequenceWriter::SequenceWriter(const SequenceConfig& config) : config(config){
    if(config.digits == 0 || config.digits > 12){
        throw std::runtime_error("Spool: a frame number needs between 1 and 12 digits");
    }
    if(config.extension.empty() || config.extension.front() != '.'){
        throw std::runtime_error("Spool: sequence extension has to start with a dot, got \"" + config.extension + "\"");
    }
}

std::string SequenceWriter::pathFor(uint32_t index) const{
    std::ostringstream name;
    name << config.prefix << std::setfill('0') << std::setw(static_cast<int>(config.digits)) << index << config.extension;

    if(config.directory.empty()){
        return name.str();
    }

    return (std::filesystem::path(config.directory) / name.str()).string();
}

std::string SequenceWriter::write(const Image& image){
    const std::string path = pathFor(nextIndex());

    //saveImage makes the directory and throws with the path if anything goes wrong, so the
    //counter below is only reached by a frame that really is on disk
    saveImage(path, image, config.save);

    ++written;
    return path;
}

}
