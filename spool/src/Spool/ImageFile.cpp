#include "ImageFile.h"
#include <stb_image.h>
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

}
