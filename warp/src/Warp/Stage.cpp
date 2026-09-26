#include "Warp/Stage.h"

#include <algorithm>
#include <cctype>

namespace Warp{

glm::mat4 Transform::matrix() const{
    glm::mat4 m = glm::mat4_cast(rotation);
    m[0] *= scale.x;
    m[1] *= scale.y;
    m[2] *= scale.z;
    m[3] = glm::vec4(translation, 1.0f);
    return m;
}

namespace{

//Rastav matrice natrag u pomak, rotaciju i mjerilo. Smicanje se ne vraca - matrica koju dobijemo
//od roditelja s nejednakim mjerilom i zakrenutog djeteta ga moze imati, i tada je rastav
//najblizi, ne tocan. Za kocke i kamere to se ne dogadja
Transform decompose(const glm::mat4& m){
    Transform t;
    t.translation = glm::vec3(m[3]);
    glm::vec3 x(m[0]), y(m[1]), z(m[2]);
    t.scale = glm::vec3(glm::length(x), glm::length(y), glm::length(z));
    if(glm::dot(glm::cross(x, y), z) < 0.0f){ t.scale.x = -t.scale.x; x = -x; }
    const glm::mat3 r(x / std::abs(t.scale.x), y / t.scale.y, z / t.scale.z);
    t.rotation = glm::normalize(glm::quat_cast(r));
    return t;
}

}

std::vector<Id>& Stage::siblingsOf(Id parent){
    if(parent == None) return rootIds;
    return entities.at(parent).children;
}

std::string Stage::uniqueName(const std::string& wanted, Id parent, Id except) const{
    //USD ime: slova, brojke i podvlaka, ne pocinje brojkom. Razmak iz imena datoteke postaje _
    std::string base;
    for(char c : wanted) base += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    if(base.empty()) base = "Entitet";
    if(std::isdigit(static_cast<unsigned char>(base[0]))) base = "_" + base;

    const std::vector<Id>& siblings = parent == None ? rootIds : entities.at(parent).children;
    auto taken = [&](const std::string& name){
        for(Id sibling : siblings){
            if(sibling != except && entities.at(sibling).name == name) return true;
        }
        return false;
    };
    if(!taken(base)) return base;
    for(int suffix = 1;; ++suffix){
        const std::string candidate = base + std::to_string(suffix);
        if(!taken(candidate)) return candidate;
    }
}

Id Stage::create(const std::string& name, Id parent){
    if(parent != None && !contains(parent)) parent = None;
    Entity entity;
    entity.id = nextId++;
    entity.name = uniqueName(name, parent, None);
    entity.parent = parent;
    const Id id = entity.id;
    entities.emplace(id, std::move(entity));
    siblingsOf(parent).push_back(id);
    return id;
}

Entity* Stage::get(Id id){
    auto found = entities.find(id);
    return found == entities.end() ? nullptr : &found->second;
}

const Entity* Stage::get(Id id) const{
    auto found = entities.find(id);
    return found == entities.end() ? nullptr : &found->second;
}

size_t Stage::remove(Id id){
    if(!contains(id)) return 0;
    std::vector<Id>& siblings = siblingsOf(entities.at(id).parent);
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());

    //Djeca prvo, iterativno - dubina stabla ne smije srusiti stog
    size_t removed = 0;
    std::vector<Id> pending{id};
    while(!pending.empty()){
        const Id current = pending.back();
        pending.pop_back();
        const Entity& entity = entities.at(current);
        pending.insert(pending.end(), entity.children.begin(), entity.children.end());
        entities.erase(current);
        ++removed;
    }
    return removed;
}

bool Stage::reparent(Id id, Id newParent){
    if(!contains(id) || (newParent != None && !contains(newParent))) return false;
    for(Id walk = newParent; walk != None; walk = entities.at(walk).parent){
        if(walk == id) return false;               //roditelj bi bio vlastiti potomak
    }
    Entity& entity = entities.at(id);
    if(entity.parent == newParent) return true;

    //Mjesto u svijetu se cuva za kadar na pocetku timelinea; mirni entitet je isti u svakom
    const glm::mat4 world = worldMatrix(id, startFrame);

    std::vector<Id>& oldSiblings = siblingsOf(entity.parent);
    oldSiblings.erase(std::remove(oldSiblings.begin(), oldSiblings.end(), id), oldSiblings.end());
    entity.parent = newParent;
    entity.name = uniqueName(entity.name, newParent, id);
    siblingsOf(newParent).push_back(id);

    if(!entity.animated()){
        const glm::mat4 parentWorld = newParent == None ? glm::mat4(1.0f) : worldMatrix(newParent, startFrame);
        entity.local = decompose(glm::inverse(parentWorld) * world);
    }
    return true;
}

bool Stage::rename(Id id, const std::string& name){
    Entity* entity = get(id);
    if(!entity) return false;
    entity->name = uniqueName(name, entity->parent, id);
    return true;
}

std::string Stage::path(Id id) const{
    std::string result;
    for(Id walk = id; walk != None;){
        const Entity* entity = get(walk);
        if(!entity) return "";
        result = "/" + entity->name + result;
        walk = entity->parent;
    }
    return result;
}

Id Stage::find(const std::string& wanted) const{
    if(wanted.empty() || wanted[0] != '/') return None;
    Id current = None;
    size_t at = 1;
    while(at <= wanted.size()){
        const size_t slash = std::min(wanted.find('/', at), wanted.size());
        const std::string name = wanted.substr(at, slash - at);
        const std::vector<Id>& children = current == None ? rootIds : entities.at(current).children;
        Id next = None;
        for(Id child : children){
            if(entities.at(child).name == name){ next = child; break; }
        }
        if(next == None) return None;
        current = next;
        at = slash + 1;
    }
    return current;
}

void Stage::walk(const std::function<void(const Entity&, int)>& visit) const{
    std::vector<std::pair<Id, int>> pending;
    for(auto root = rootIds.rbegin(); root != rootIds.rend(); ++root) pending.push_back({*root, 0});
    while(!pending.empty()){
        const auto [id, depth] = pending.back();
        pending.pop_back();
        const Entity& entity = entities.at(id);
        visit(entity, depth);
        for(auto child = entity.children.rbegin(); child != entity.children.rend(); ++child){
            pending.push_back({*child, depth + 1});
        }
    }
}

Transform Stage::localAt(Id id, double frame) const{
    const Entity* entity = get(id);
    if(!entity) return Transform{};
    Transform t = entity->local;
    if(!entity->translationKeys.empty()) t.translation = entity->translationKeys.at(frame);
    if(!entity->rotationKeys.empty()) t.rotation = entity->rotationKeys.at(frame);
    if(!entity->scaleKeys.empty()) t.scale = entity->scaleKeys.at(frame);

    //The nearest enabled Animator owns playback for its rig subtree. Animator keys override the
    //legacy entity tracks so the selected clip is what both the viewport and renderers evaluate.
    for(Id owner = id; owner != None;){
        const Entity* candidate = get(owner);
        if(!candidate) break;
        if(candidate->animator && candidate->animator->enabled &&
           candidate->animator->activeAnimation < candidate->animator->animations.size()){
            const AnimationClip& clip = candidate->animator->animations[candidate->animator->activeAnimation];
            for(const AnimatorTrack& track : clip.tracks){
                if(track.target != id) continue;
                if(!track.translationKeys.empty() && !(track.rootMotion && clip.inPlace))
                    t.translation = track.translationKeys.at(frame);
                if(!track.rotationKeys.empty()) t.rotation = track.rotationKeys.at(frame);
                if(!track.scaleKeys.empty()) t.scale = track.scaleKeys.at(frame);
                break;
            }
            break;
        }
        owner = candidate->parent;
    }
    return t;
}

AnimatorTrack* Stage::activeAnimatorTrack(Id id){
    for(Id owner = id; owner != None;){
        Entity* candidate = get(owner);
        if(!candidate) break;
        if(candidate->animator && candidate->animator->enabled &&
           candidate->animator->activeAnimation < candidate->animator->animations.size()){
            AnimationClip& clip = candidate->animator->animations[candidate->animator->activeAnimation];
            for(AnimatorTrack& track : clip.tracks) if(track.target == id) return &track;
            return nullptr;
        }
        owner = candidate->parent;
    }
    return nullptr;
}

const AnimatorTrack* Stage::activeAnimatorTrack(Id id) const{
    for(Id owner = id; owner != None;){
        const Entity* candidate = get(owner);
        if(!candidate) break;
        if(candidate->animator && candidate->animator->enabled &&
           candidate->animator->activeAnimation < candidate->animator->animations.size()){
            const AnimationClip& clip = candidate->animator->animations[candidate->animator->activeAnimation];
            for(const AnimatorTrack& track : clip.tracks) if(track.target == id) return &track;
            return nullptr;
        }
        owner = candidate->parent;
    }
    return nullptr;
}

AnimationClip* Stage::activeAnimationClip(Id id){
    for(Id owner = id; owner != None;){
        Entity* candidate = get(owner);
        if(!candidate) break;
        if(candidate->animator && candidate->animator->enabled &&
           candidate->animator->activeAnimation < candidate->animator->animations.size())
            return &candidate->animator->animations[candidate->animator->activeAnimation];
        owner = candidate->parent;
    }
    return nullptr;
}

AnimatorTrack* Stage::ensureAnimatorTrack(Id id){
    Entity* target = get(id);
    AnimationClip* clip = target && target->joint ? activeAnimationClip(id) : nullptr;
    if(!clip) return nullptr;
    for(AnimatorTrack& track : clip->tracks) if(track.target == id) return &track;
    AnimatorTrack track;
    track.target = id;
    track.targetPath = path(id);
    clip->tracks.push_back(std::move(track));
    return &clip->tracks.back();
}

void Stage::setLocalAt(Id id, double frame, const Transform& transform){
    Entity* entity = get(id);
    if(!entity) return;
    if(AnimatorTrack* track = ensureAnimatorTrack(id)){
        track->translationKeys.set(frame, transform.translation);
        track->rotationKeys.set(frame, transform.rotation);
        track->scaleKeys.set(frame, transform.scale);
        if(AnimationClip* clip = activeAnimationClip(id)){
            clip->startFrame = std::min(clip->startFrame, frame);
            clip->endFrame = std::max(clip->endFrame, frame);
        }
        return;
    }
    if(entity->translationKeys.empty()) entity->local.translation = transform.translation;
    else entity->translationKeys.set(frame, transform.translation);
    if(entity->rotationKeys.empty()) entity->local.rotation = transform.rotation;
    else entity->rotationKeys.set(frame, transform.rotation);
    if(entity->scaleKeys.empty()) entity->local.scale = transform.scale;
    else entity->scaleKeys.set(frame, transform.scale);
}

void Stage::keyAll(Id id, double frame){
    Entity* entity = get(id);
    if(!entity) return;
    const Transform now = localAt(id, frame);
    if(AnimatorTrack* track = ensureAnimatorTrack(id)){
        track->translationKeys.set(frame, now.translation);
        track->rotationKeys.set(frame, now.rotation);
        track->scaleKeys.set(frame, now.scale);
        if(AnimationClip* clip = activeAnimationClip(id)){
            clip->startFrame = std::min(clip->startFrame, frame);
            clip->endFrame = std::max(clip->endFrame, frame);
        }
        return;
    }
    entity->translationKeys.set(frame, now.translation);
    entity->rotationKeys.set(frame, now.rotation);
    entity->scaleKeys.set(frame, now.scale);
}

size_t Stage::eraseKeysAt(Id id, double frame){
    Entity* entity = get(id);
    if(!entity) return 0;
    const Transform held = localAt(id, frame);
    if(AnimatorTrack* track = activeAnimatorTrack(id)){
        size_t erased = 0;
        if(track->translationKeys.erase(frame)){ ++erased; if(track->translationKeys.empty()) entity->local.translation = held.translation; }
        if(track->rotationKeys.erase(frame)){ ++erased; if(track->rotationKeys.empty()) entity->local.rotation = held.rotation; }
        if(track->scaleKeys.erase(frame)){ ++erased; if(track->scaleKeys.empty()) entity->local.scale = held.scale; }
        return erased;
    }
    size_t erased = 0;
    if(entity->translationKeys.erase(frame)){ ++erased; if(entity->translationKeys.empty()) entity->local.translation = held.translation; }
    if(entity->rotationKeys.erase(frame)){ ++erased; if(entity->rotationKeys.empty()) entity->local.rotation = held.rotation; }
    if(entity->scaleKeys.erase(frame)){ ++erased; if(entity->scaleKeys.empty()) entity->local.scale = held.scale; }
    return erased;
}

bool Stage::neighbourKey(Id id, double frame, int direction, double& found) const{
    const Entity* entity = get(id);
    if(!entity) return false;
    bool any = false;
    auto consider = [&](const std::vector<double>& times){
        for(double t : times){
            const bool ahead = direction > 0 ? t > frame + 1e-9 : t < frame - 1e-9;
            if(!ahead) continue;
            if(!any || (direction > 0 ? t < found : t > found)){ found = t; any = true; }
        }
    };
    if(const AnimatorTrack* track = activeAnimatorTrack(id)){
        consider(track->translationKeys.times);
        consider(track->rotationKeys.times);
        consider(track->scaleKeys.times);
    }else{
        consider(entity->translationKeys.times);
        consider(entity->rotationKeys.times);
        consider(entity->scaleKeys.times);
    }
    return any;
}

glm::mat4 Stage::localMatrix(Id id, double frame) const{
    return localAt(id, frame).matrix();
}

glm::mat4 Stage::worldMatrix(Id id, double frame) const{
    glm::mat4 world(1.0f);
    for(Id walk = id; walk != None;){
        const Entity* entity = get(walk);
        if(!entity) break;
        world = localMatrix(walk, frame) * world;
        walk = entity->parent;
    }
    return world;
}

namespace{

//FNV-1a preko bajtova. Nije kriptografski - trazi se samo da promjena gotovo sigurno promijeni broj
struct Hasher{
    uint64_t value = 1469598103934665603ull;
    void bytes(const void* data, size_t size){
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for(size_t i = 0; i < size; ++i){ value ^= p[i]; value *= 1099511628211ull; }
    }
    template<class T> void add(const T& v){ bytes(&v, sizeof(T)); }
    void text(const std::string& s){ add(s.size()); bytes(s.data(), s.size()); }
    template<class T> void track(const Track<T>& t){
        add(t.size());
        if(!t.empty()){ bytes(t.times.data(), t.times.size() * sizeof(double)); bytes(t.values.data(), t.values.size() * sizeof(T)); }
    }
};

}

uint64_t Stage::fingerprint() const{
    Hasher h;
    h.add(startFrame); h.add(endFrame); h.add(framesPerSecond);
    walk([&](const Entity& e, int depth){
        h.add(depth);
        h.text(e.name);
        h.add(e.visible);
        h.add(e.local.translation); h.add(e.local.rotation); h.add(e.local.scale);
        h.track(e.translationKeys); h.track(e.rotationKeys); h.track(e.scaleKeys);
        h.add(e.camera.has_value());
        if(e.camera){
            h.add(e.camera->focalPixels); h.add(e.camera->centreX); h.add(e.camera->centreY);
            h.add(e.camera->width); h.add(e.camera->height); h.text(e.camera->plate); h.add(e.camera->plateFirstFrame);
            h.add(e.camera->distortionFx); h.add(e.camera->distortionFy); h.add(e.camera->distortionCx);
            h.add(e.camera->distortionCy); h.add(e.camera->k1); h.add(e.camera->k2);
        }
        h.add(e.light.has_value());
        if(e.light){
            const Light& l = *e.light;
            h.add(int(l.type)); h.add(l.color); h.add(l.intensity); h.add(l.radius); h.add(l.angle); h.add(l.coneAngle);
            h.add(l.coneSoftness); h.add(l.width); h.add(l.height); h.text(l.texture); h.add(l.skyTop); h.add(l.skyBottom);
        }
        h.add(e.points.has_value());
        if(e.points){
            h.add(e.points->positions.size()); h.add(e.points->colours.size());
            if(!e.points->positions.empty()){ h.add(e.points->positions.front()); h.add(e.points->positions.back()); }
        }
        h.add(e.mesh.has_value());
        if(e.mesh){ h.add(e.mesh->shape); h.add(e.mesh->colour); }
        h.add(e.splat.has_value());
        if(e.splat) h.text(e.splat->path);
        h.add(e.joint.has_value());
        if(e.joint) h.add(e.joint->colour);
        if(e.mesh) h.add(e.mesh->material);
        h.add(e.model.has_value());
        if(e.model){ h.text(e.model->path); h.add(e.model->mesh); h.add(e.model->skin); h.add(e.model->materials.size());
                     for(int m : e.model->materials) h.add(m);
                     h.add(e.model->skinJointPaths.size()); for(const std::string& path : e.model->skinJointPaths) h.text(path); }
        h.add(e.animator.has_value());
        if(e.animator){
            h.add(e.animator->enabled); h.add(e.animator->activeAnimation); h.add(e.animator->relaxedUniRigPose); h.add(e.animator->animations.size());
            for(const AnimationClip& clip : e.animator->animations){
                h.text(clip.name); h.add(clip.startFrame); h.add(clip.endFrame); h.add(clip.loop); h.add(clip.inPlace); h.add(clip.tracks.size());
                for(const AnimatorTrack& track : clip.tracks){
                    h.text(contains(track.target) ? path(track.target) : track.targetPath);
                    h.add(track.rootMotion);
                    h.track(track.translationKeys); h.track(track.rotationKeys); h.track(track.scaleKeys);
                }
            }
        }
    });
    h.add(materials.size());
    for(const Material& m : materials){
        h.text(m.name); h.add(m.baseColor); h.add(m.metallic); h.add(m.roughness); h.add(m.emissive); h.add(m.emissiveStrength);
        h.add(m.alphaMode); h.add(m.alphaCutoff); h.add(m.doubleSided);
        h.add(m.transmission); h.add(m.ior); h.add(m.specular); h.add(m.clearcoat); h.add(m.clearcoatRoughness);
        for(const TextureSlot* t : {&m.baseColorMap, &m.metallicRoughnessMap, &m.normalMap, &m.occlusionMap, &m.emissiveMap}){
            h.text(t->source); h.add(t->image); h.add(t->texCoord); h.add(t->amount);
        }
    }
    h.add(media.size());
    for(const Media& m : media){
        h.text(m.path); h.text(m.result); h.add(m.frames); h.add(m.framesPerSecond); h.add(m.width); h.add(m.height);
    }
    return h.value;
}

int Stage::addMaterial(Material material){
    if(material.name.empty()) material.name = "Materijal";
    const std::string base = material.name;
    for(int suffix = 1;; ++suffix){
        bool taken = false;
        for(const Material& m : materials) if(m.name == material.name) taken = true;
        if(!taken) break;
        material.name = base + std::to_string(suffix);
    }
    materials.push_back(std::move(material));
    return int(materials.size()) - 1;
}

void Stage::removeMaterial(int index){
    if(index < 0 || size_t(index) >= materials.size()) return;
    materials.erase(materials.begin() + index);
    auto fix = [&](int& reference){
        if(reference == index) reference = -1;
        else if(reference > index) --reference;
    };
    for(auto& [id, entity] : entities){
        if(entity.mesh) fix(entity.mesh->material);
        if(entity.model) for(int& m : entity.model->materials) fix(m);
    }
}

}
