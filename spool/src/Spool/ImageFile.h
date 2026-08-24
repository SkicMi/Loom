#pragma once
#include <cstdint>
#include <string>
#include <vector>

//Spool is where things come from and where they go to. Loom knows how to draw; Spool knows
//how to read a file and, later, how to write one.
//
//It deliberately knows nothing about Vulkan and nothing about Loom. The dependency runs
//app -> Spool and app -> Loom, never between the two, which is what keeps "how it is drawn"
//and "where it came from" separable. That is also why an image comes back as plain bytes
//rather than as anything Loom defines.
namespace Spool{

//A decoded image, in the one layout a GPU actually wants: eight bits per channel, four
//channels, tightly packed, first row first.
//
//Four channels always, even when the file held three. Vulkan does not require drivers to
//support sampled images with three eight bit channels and most do not, so a three channel
//upload fails at image creation - far away from the decode that could have prevented it.
struct Image{
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;

    //How many channels the file itself held, before the conversion above. Kept because it
    //is the only way left to tell an image that is opaque from one that never had an alpha
    //channel to begin with - the pixels look identical either way
    uint32_t sourceChannels = 0;

    bool isValid() const {return width > 0 && height > 0 && !pixels.empty();}
    size_t pixelCount() const {return size_t(width) * height;}
    size_t byteSize() const {return pixels.size();}
};

//From a file. Whatever the decoder understands - png, jpg, bmp, tga, gif and more.
//
//Throws, with the path and the reason, rather than returning an empty image: a texture that
//silently loaded as nothing shows up as a black surface three layers away, and the path is
//the one piece of information that would have made it obvious
Image loadImage(const std::string& path);

//From bytes already in memory: an asset pack, a network reply, a test with the file inlined
Image decodeImage(const void* data, size_t size);

}
