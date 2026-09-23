#include "WeaverMotion.h"

#include <glm/gtx/quaternion.hpp>

#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>

namespace Engine::WeaverMotion{
namespace{

struct TokenStream{
    explicit TokenStream(const std::string& input) : text(input){}

    bool next(std::string& out){
        skipSpace();
        if(at >= text.size()) return false;
        const char c = text[at];
        if(c == '{' || c == '}'){
            out.assign(1, c);
            ++at;
            return true;
        }
        const size_t start = at;
        while(at < text.size() && !std::isspace(static_cast<unsigned char>(text[at])) &&
              text[at] != '{' && text[at] != '}') ++at;
        out = text.substr(start, at - start);
        return true;
    }

    bool expect(const char* wanted, std::string& error){
        std::string got;
        if(!next(got)){ error = std::string("ocekivao ") + wanted + ", dosao kraj datoteke"; return false; }
        if(got != wanted){ error = std::string("ocekivao ") + wanted + ", dobio " + got; return false; }
        return true;
    }

    bool number(double& value, std::string& error){
        std::string token;
        if(!next(token)){ error = "ocekivao broj, dosao kraj datoteke"; return false; }
        const char* begin = token.data();
        const char* end = token.data() + token.size();
        const std::from_chars_result parsed = std::from_chars(begin, end, value);
        if(parsed.ec != std::errc() || parsed.ptr != end){ error = "neispravan broj: " + token; return false; }
        return true;
    }

    void skipJointBlock(){
        std::string token;
        int depth = 0;
        while(next(token)){
            if(token == "{") ++depth;
            else if(token == "}"){
                if(depth == 0) return;
                --depth;
                if(depth == 0) return;
            }
        }
    }

    void skipSpace(){
        while(at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
    }

    const std::string& text;
    size_t at = 0;
};

enum class Channel{
    Xposition, Yposition, Zposition,
    Xrotation, Yrotation, Zrotation
};

struct JointChannels{
    size_t joint = 0;
    std::vector<Channel> channels;
};

Channel channelFrom(const std::string& token, bool& ok){
    ok = true;
    if(token == "Xposition") return Channel::Xposition;
    if(token == "Yposition") return Channel::Yposition;
    if(token == "Zposition") return Channel::Zposition;
    if(token == "Xrotation") return Channel::Xrotation;
    if(token == "Yrotation") return Channel::Yrotation;
    if(token == "Zrotation") return Channel::Zrotation;
    ok = false;
    return Channel::Xposition;
}

glm::quat rotation(Channel channel, double degrees){
    const float radians = glm::radians(float(degrees));
    switch(channel){
        case Channel::Xrotation: return glm::angleAxis(radians, glm::vec3(1.0f, 0.0f, 0.0f));
        case Channel::Yrotation: return glm::angleAxis(radians, glm::vec3(0.0f, 1.0f, 0.0f));
        case Channel::Zrotation: return glm::angleAxis(radians, glm::vec3(0.0f, 0.0f, 1.0f));
        default: return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
}

bool parseJoint(TokenStream& in, const std::string& name, int parent, Clip& clip,
                std::vector<JointChannels>& channelMap, BvhImportOptions options,
                std::string& error){
    if(!in.expect("{", error)) return false;

    const size_t jointIndex = clip.joints.size();
    Joint joint;
    joint.name = name;
    joint.parent = parent;
    clip.joints.push_back(joint);

    std::string token;
    while(in.next(token)){
        if(token == "}") return true;
        if(token == "OFFSET"){
            double x = 0.0, y = 0.0, z = 0.0;
            if(!in.number(x, error) || !in.number(y, error) || !in.number(z, error)) return false;
            clip.joints[jointIndex].offset = glm::vec3(float(x), float(y), float(z)) * options.lengthScale;
        }else if(token == "CHANNELS"){
            double countNumber = 0.0;
            if(!in.number(countNumber, error)) return false;
            const int count = int(countNumber);
            if(count < 0 || std::fabs(countNumber - double(count)) > 1e-9){
                error = "CHANNELS mora biti cijeli nenegativan broj";
                return false;
            }
            JointChannels channels;
            channels.joint = jointIndex;
            channels.channels.reserve(size_t(count));
            for(int i = 0; i < count; ++i){
                std::string channelToken;
                if(!in.next(channelToken)){ error = "nedostaje ime BVH kanala"; return false; }
                bool known = false;
                const Channel channel = channelFrom(channelToken, known);
                if(!known){ error = "nepoznat BVH kanal: " + channelToken; return false; }
                channels.channels.push_back(channel);
            }
            channelMap.push_back(std::move(channels));
        }else if(token == "JOINT"){
            std::string childName;
            if(!in.next(childName)){ error = "JOINT bez imena"; return false; }
            if(!parseJoint(in, childName, int(jointIndex), clip, channelMap, options, error)) return false;
        }else if(token == "End"){
            if(!in.expect("Site", error)) return false;
            in.skipJointBlock();
        }else{
            error = "neocekivan token u hijerarhiji: " + token;
            return false;
        }
    }

    error = "BVH hijerarhija nije zatvorena";
    return false;
}

bool parseHierarchy(TokenStream& in, Clip& clip, std::vector<JointChannels>& channelMap,
                    BvhImportOptions options, std::string& error){
    if(!in.expect("HIERARCHY", error)) return false;
    if(!in.expect("ROOT", error)) return false;
    std::string rootName;
    if(!in.next(rootName)){ error = "ROOT bez imena"; return false; }
    return parseJoint(in, rootName, -1, clip, channelMap, options, error);
}

bool parseMotion(TokenStream& in, Clip& clip, const std::vector<JointChannels>& channelMap,
                 BvhImportOptions options, std::string& error){
    if(!in.expect("MOTION", error)) return false;
    if(!in.expect("Frames:", error)) return false;
    double frameCountNumber = 0.0;
    if(!in.number(frameCountNumber, error)) return false;
    const int frameCount = int(frameCountNumber);
    if(frameCount <= 0 || std::fabs(frameCountNumber - double(frameCount)) > 1e-9){
        error = "Frames mora biti pozitivan cijeli broj";
        return false;
    }
    if(!in.expect("Frame", error) || !in.expect("Time:", error)) return false;
    double frameTime = 0.0;
    if(!in.number(frameTime, error)) return false;
    if(frameTime <= 0.0){ error = "Frame Time mora biti pozitivan"; return false; }
    clip.framesPerSecond = 1.0 / frameTime;

    clip.frames.clear();
    clip.frames.reserve(size_t(frameCount));
    for(int frame = 0; frame < frameCount; ++frame){
        Pose pose;
        pose.translations.assign(clip.joints.size(), glm::vec3(0.0f));
        pose.rotations.assign(clip.joints.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

        for(const JointChannels& jointChannels : channelMap){
            glm::vec3 translation(0.0f);
            glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
            for(Channel channel : jointChannels.channels){
                double value = 0.0;
                if(!in.number(value, error)) return false;
                switch(channel){
                    case Channel::Xposition: translation.x = float(value) * options.lengthScale; break;
                    case Channel::Yposition: translation.y = float(value) * options.lengthScale; break;
                    case Channel::Zposition: translation.z = float(value) * options.lengthScale; break;
                    case Channel::Xrotation:
                    case Channel::Yrotation:
                    case Channel::Zrotation:
                        orientation = glm::normalize(orientation * rotation(channel, value));
                        break;
                }
            }
            pose.translations[jointChannels.joint] = translation;
            pose.rotations[jointChannels.joint] = orientation;
        }

        clip.frames.push_back(std::move(pose));
    }
    return true;
}

}

bool parseKimodoBvh(const std::string& text, Clip& clip, std::string& error,
                    BvhImportOptions options){
    Clip parsed;
    std::vector<JointChannels> channelMap;
    TokenStream in(text);
    if(!parseHierarchy(in, parsed, channelMap, options, error)) return false;
    if(parsed.joints.empty()){ error = "BVH nema zglobova"; return false; }
    if(channelMap.empty()){ error = "BVH nema animacijskih kanala"; return false; }
    if(!parseMotion(in, parsed, channelMap, options, error)) return false;
    clip = std::move(parsed);
    error.clear();
    return true;
}

bool readKimodoBvh(const std::string& path, Clip& clip, std::string& error,
                   BvhImportOptions options){
    std::ifstream file(path);
    if(!file){ error = "ne mogu otvoriti " + path; return false; }
    std::stringstream buffer;
    buffer << file.rdbuf();
    if(!file && !file.eof()){ error = "ne mogu procitati " + path; return false; }
    return parseKimodoBvh(buffer.str(), clip, error, options);
}

}
