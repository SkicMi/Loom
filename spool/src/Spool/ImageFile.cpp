#include "ImageFile.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace Spool{

namespace{

//Four out, always. See the comment on Image about why this is not negotiable
constexpr int wantedChannels = 4;

std::string reason(){
    const char* why = stbi_failure_reason();
    return why ? std::string(why) : std::string("no reason given");
}

//stb hands back a malloc'd buffer; this copies it into the vector and frees it, so an
//exception anywhere above cannot leak it and the caller never has to think about it
Image adopt(unsigned char* decoded, int width, int height, int channels, const std::string& what){
    if(decoded == nullptr){
        throw std::runtime_error("Spool: could not decode " + what + " - " + reason());
    }

    if(width <= 0 || height <= 0){
        stbi_image_free(decoded);
        throw std::runtime_error("Spool: " + what + " decoded to an image with no size");
    }

    Image image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.sourceChannels = static_cast<uint32_t>(channels);

    const size_t bytes = size_t(image.width) * image.height * wantedChannels;
    image.pixels.assign(decoded, decoded + bytes);

    stbi_image_free(decoded);
    return image;
}

}

Image loadImage(const std::string& path){
    if(path.empty()){
        throw std::runtime_error("Spool: no path given to loadImage");
    }

    int width = 0, height = 0, channels = 0;
    unsigned char* decoded = stbi_load(path.c_str(), &width, &height, &channels, wantedChannels);

    return adopt(decoded, width, height, channels, path);
}

Image decodeImage(const void* data, size_t size){
    if(data == nullptr || size == 0){
        throw std::runtime_error("Spool: nothing to decode - the buffer is empty");
    }

    //stb counts in int. A buffer past that is a caller mistake worth naming rather than
    //an int that quietly wraps negative and decodes as a failure with no explanation
    if(size > static_cast<size_t>(std::numeric_limits<int>::max())){
        throw std::runtime_error("Spool: buffer is too large to decode in one piece");
    }

    int width = 0, height = 0, channels = 0;
    unsigned char* decoded = stbi_load_from_memory(static_cast<const stbi_uc*>(data),
        static_cast<int>(size), &width, &height, &channels, wantedChannels);

    return adopt(decoded, width, height, channels, "the buffer");
}

Image imageFromPixels(const void* pixels, uint32_t width, uint32_t height, ChannelOrder order){
    if(pixels == nullptr){
        throw std::runtime_error("Spool: imageFromPixels was given nothing to wrap");
    }
    if(width == 0 || height == 0){
        throw std::runtime_error("Spool: imageFromPixels was given an image with no size");
    }

    const size_t bytes = size_t(width) * height * 4;
    const uint8_t* source = static_cast<const uint8_t*>(pixels);

    Image image;
    image.width = width;
    image.height = height;
    image.sourceChannels = 4;
    image.pixels.assign(source, source + bytes);

    //Red and blue change places, alpha and green stay where they are
    if(order == ChannelOrder::BGRA){
        for(size_t i = 0; i + 3 < image.pixels.size(); i += 4){
            std::swap(image.pixels[i], image.pixels[i + 2]);
        }
    }

    return image;
}

void savePng(const std::string& path, const Image& image, const SaveConfig& config){
    if(!image.isValid()){
        throw std::runtime_error("Spool: refusing to write an empty image to " + path);
    }
    if(image.byteSize() != image.pixelCount() * 4){
        throw std::runtime_error("Spool: image says " + std::to_string(image.width) + "x" +
            std::to_string(image.height) + " but carries " + std::to_string(image.byteSize()) + " bytes");
    }

    //A missing directory is the most ordinary reason a sequence fails on its first frame,
    //and it is one this can simply fix
    const std::filesystem::path target(path);
    if(target.has_parent_path() && !target.parent_path().empty()){
        std::error_code code;
        std::filesystem::create_directories(target.parent_path(), code);
    }

    stbi_write_png_compression_level = std::clamp(config.pngCompression, 0, 9);

    const int stride = static_cast<int>(image.width) * 4;
    const int written = stbi_write_png(path.c_str(),
        static_cast<int>(image.width), static_cast<int>(image.height), 4,
        image.pixels.data(), stride);

    if(written == 0){
        throw std::runtime_error("Spool: could not write " + path + " - the encoder refused it, or the path is not writable");
    }
}

void saveImage(const std::string& path, const Image& image, const SaveConfig& config){
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    if(extension == ".png"){
        savePng(path, image, config);
        return;
    }

    throw std::runtime_error("Spool: no encoder for \"" + extension + "\" (" + path + ") - only .png so far");
}

}
