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

//How four bytes of a pixel are ordered in memory. Loom's swapchain and render targets are
//eB8G8R8A8Srgb, which is the format nearly every Vulkan surface offers - so the bytes that
//come back from a readback are BGRA, and a PNG written straight out of them has red and
//blue swapped. That is a pixel layout question, not a Loom question, which is why it is
//answered here
enum class ChannelOrder{
    RGBA,
    BGRA
};

//Wraps bytes that are already decoded - a readback, a generated pattern - as an Image,
//putting them in RGBA order on the way in. Four bytes per pixel, tightly packed
Image imageFromPixels(const void* pixels, uint32_t width, uint32_t height,
                      ChannelOrder order = ChannelOrder::RGBA);

//From a file. Whatever the decoder understands - png, jpg, bmp, tga, gif and more.
//
//Throws, with the path and the reason, rather than returning an empty image: a texture that
//silently loaded as nothing shows up as a black surface three layers away, and the path is
//the one piece of information that would have made it obvious
Image loadImage(const std::string& path);

//From bytes already in memory: an asset pack, a network reply, a test with the file inlined
Image decodeImage(const void* data, size_t size);


//How hard to work at making the file small. PNG is lossless either way - this only trades
//encode time against bytes on disk, which is the trade a sequence export cares about most:
//a preview pass wants frames written faster than they are rendered, a final pass does not
struct SaveConfig{
    //0 to 9. stb's own default is 8, which is slow enough to become the bottleneck when a
    //whole sequence is being written
    int pngCompression = 6;
};

//Writes a PNG. Creates the directories above the file if they are not there yet.
//Throws with the path rather than returning false: a frame that silently failed to write
//leaves a hole in a sequence that nobody notices until the sequence is played
void savePng(const std::string& path, const Image& image, const SaveConfig& config = {});

//The same, choosing the encoder from the file's extension. Only .png for now - the point of
//the seam is that adding another one does not change a single caller
void saveImage(const std::string& path, const Image& image, const SaveConfig& config = {});

}
