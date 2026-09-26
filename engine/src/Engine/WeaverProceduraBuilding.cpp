// Buildings and roads for WeaverProcedura (schema 7): footprints, walls with openings,
// slabs, roofs, stairs and road cross-sections. Every triangle leaves here tagged with a
// semantic and a default material; SetMaterial with a semantic filter restyles them.
#include "WeaverProcedura.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace Engine::WeaverProcedura{
namespace{

constexpr double pi = 3.1415926535897932384626433832795;

bool finite(float value){return std::isfinite(value);}
bool finite(const glm::vec2& value){return finite(value.x) && finite(value.y);}

double cross2(const glm::vec2& a, const glm::vec2& b){return double(a.x) * b.y - double(a.y) * b.x;}

double twiceArea(const std::vector<glm::vec2>& polygon){
    double sum = 0.0;
    for(std::size_t i = 0; i < polygon.size(); ++i) sum += cross2(polygon[i], polygon[(i + 1) % polygon.size()]);
    return sum;
}

glm::vec3 at(const glm::vec2& xz, float y){return {xz.x, y, xz.y};}

glm::vec2 rotateXZ(const glm::vec2& p, float radians){
    const float c = std::cos(radians), s = std::sin(radians);
    return {p.x * c + p.y * s, -p.x * s + p.y * c};   // right-handed turn about +Y
}

// Proper or touching intersection of segments ab and cd.
bool segmentsTouch(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const glm::vec2& d){
    const double d1 = cross2(b - a, c - a), d2 = cross2(b - a, d - a);
    const double d3 = cross2(d - c, a - c), d4 = cross2(d - c, b - c);
    const double eps = 1e-9;
    if(((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) && ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps))) return true;
    auto onSegment = [&](const glm::vec2& p, const glm::vec2& q, const glm::vec2& r, double side){
        return std::abs(side) <= eps && std::min(p.x, q.x) - 1e-6f <= r.x && r.x <= std::max(p.x, q.x) + 1e-6f &&
               std::min(p.y, q.y) - 1e-6f <= r.y && r.y <= std::max(p.y, q.y) + 1e-6f;
    };
    return onSegment(a, b, c, d1) || onSegment(a, b, d, d2) || onSegment(c, d, a, d3) || onSegment(c, d, b, d4);
}

bool simplePolygon(const std::vector<glm::vec2>& polygon){
    const std::size_t n = polygon.size();
    for(std::size_t i = 0; i < n; ++i){
        for(std::size_t j = i + 1; j < n; ++j){
            if(j == i + 1 || (i == 0 && j == n - 1)) continue;   // neighbours share a corner
            if(segmentsTouch(polygon[i], polygon[(i + 1) % n], polygon[j], polygon[(j + 1) % n])) return false;
        }
    }
    return true;
}

// Consecutive duplicates and the closing duplicate removed.
std::vector<glm::vec2> cleanOutline(const std::vector<glm::vec2>& source){
    std::vector<glm::vec2> points;
    for(const glm::vec2& p : source)
        if(points.empty() || glm::length(points.back() - p) > 1e-5f) points.push_back(p);
    while(points.size() > 1 && glm::length(points.front() - points.back()) <= 1e-5f) points.pop_back();
    return points;
}

bool finishFootprint(std::vector<glm::vec2> outline, Footprint& output, std::string& error){
    outline = cleanOutline(outline);
    if(outline.size() < 3 || outline.size() > 1024){ error = "footprint needs 3 to 1024 distinct corners"; return false; }
    for(const glm::vec2& p : outline) if(!finite(p)){ error = "footprint corners must be finite"; return false; }
    const double area = twiceArea(outline) * 0.5;
    if(std::abs(area) < 0.25){ error = "footprint must enclose at least 0.25 square meters"; return false; }
    if(area < 0.0) std::reverse(outline.begin(), outline.end());
    if(!simplePolygon(outline)){ error = "footprint outline must not cross itself"; return false; }
    output.outline = std::move(outline);
    return true;
}

// Flat-shaded triangles with face normals, box UVs in meters and per-triangle attributes.
struct Builder{
    MeshData mesh;
    std::size_t maxVertices;
    bool overflow = false;

    explicit Builder(std::size_t limit) : maxVertices(limit){}

    // desired only picks the side; the stored normal is the true face normal.
    void tri(const glm::vec3& a, glm::vec3 b, glm::vec3 c, const glm::vec3& desired,
             uint16_t semantic, uint16_t material){
        glm::vec3 normal = glm::cross(b - a, c - a);
        const float length = glm::length(normal);
        if(!(length > 1e-10f)) return;
        normal /= length;
        if(glm::dot(normal, desired) < 0.0f){ std::swap(b, c); normal = -normal; }
        if(mesh.vertices.size() + 3 > maxVertices){ overflow = true; return; }
        const glm::vec3 axis = glm::abs(normal);
        auto uv = [&](const glm::vec3& p) -> glm::vec2{
            if(axis.y >= axis.x && axis.y >= axis.z) return {p.x, p.z};
            if(axis.x >= axis.z) return {p.z, p.y};
            return {p.x, p.y};
        };
        const uint32_t base = uint32_t(mesh.vertices.size());
        for(const glm::vec3& p : {a, b, c}) mesh.vertices.push_back({p, normal, uv(p)});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
        mesh.triangles.push_back({0, semantic, material});
    }

    void quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
              const glm::vec3& desired, uint16_t semantic, uint16_t material){
        tri(a, b, c, desired, semantic, material);
        tri(a, c, d, desired, semantic, material);
    }

    // Axis-aligned box.
    void box(const glm::vec3& low, const glm::vec3& high, uint16_t semantic, uint16_t material){
        const glm::vec3 p[8] = {{low.x, low.y, low.z}, {high.x, low.y, low.z}, {high.x, high.y, low.z}, {low.x, high.y, low.z},
                                {low.x, low.y, high.z}, {high.x, low.y, high.z}, {high.x, high.y, high.z}, {low.x, high.y, high.z}};
        quad(p[0], p[1], p[2], p[3], {0, 0, -1}, semantic, material);
        quad(p[4], p[5], p[6], p[7], {0, 0, 1}, semantic, material);
        quad(p[0], p[4], p[7], p[3], {-1, 0, 0}, semantic, material);
        quad(p[1], p[5], p[6], p[2], {1, 0, 0}, semantic, material);
        quad(p[3], p[2], p[6], p[7], {0, 1, 0}, semantic, material);
        quad(p[0], p[1], p[5], p[4], {0, -1, 0}, semantic, material);
    }

    // Box from end center a to end center b, halfWidth sideways and halfHeight up.
    void beam(const glm::vec3& a, const glm::vec3& b, float halfWidth, float halfHeight,
              uint16_t semantic, uint16_t material){
        const glm::vec3 forward = glm::normalize(b - a);
        glm::vec3 side = glm::cross(forward, glm::vec3(0, 1, 0));
        if(glm::length(side) < 1e-4f) side = glm::cross(forward, glm::vec3(1, 0, 0));
        side = glm::normalize(side) * halfWidth;
        const glm::vec3 up = glm::normalize(glm::cross(side, forward)) * halfHeight;
        const glm::vec3 p[8] = {a - side - up, a + side - up, a + side + up, a - side + up,
                                b - side - up, b + side - up, b + side + up, b - side + up};
        quad(p[0], p[1], p[2], p[3], -forward, semantic, material);
        quad(p[4], p[5], p[6], p[7], forward, semantic, material);
        quad(p[0], p[4], p[7], p[3], -side, semantic, material);
        quad(p[1], p[5], p[6], p[2], side, semantic, material);
        quad(p[3], p[2], p[6], p[7], up, semantic, material);
        quad(p[0], p[1], p[5], p[4], -up, semantic, material);
    }

    // Vertical prism over a positive-area polygon; top and bottom from its triangulation.
    void prism(const std::vector<glm::vec2>& polygon, const std::vector<uint32_t>& triangles, float low, float high,
               uint16_t topSemantic, uint16_t sideSemantic, uint16_t bottomSemantic, uint16_t material, bool bottom = true,
               bool sides = true){
        for(std::size_t i = 0; i + 2 < triangles.size(); i += 3){
            const glm::vec2 &a = polygon[triangles[i]], &b = polygon[triangles[i + 1]], &c = polygon[triangles[i + 2]];
            tri(at(a, high), at(b, high), at(c, high), {0, 1, 0}, topSemantic, material);
            if(bottom) tri(at(a, low), at(b, low), at(c, low), {0, -1, 0}, bottomSemantic, material);
        }
        for(std::size_t i = 0; sides && i < polygon.size(); ++i){
            const glm::vec2 &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
            const glm::vec2 edge = b - a;
            quad(at(a, low), at(b, low), at(b, high), at(a, high), {edge.y, 0, -edge.x}, sideSemantic, material);
        }
    }

    bool finish(MeshData& output, std::string& error, const char* what){
        if(overflow){ error = std::string(what) + " exceeds the configured vertex limit"; return false; }
        if(mesh.indices.empty()){ error = std::string(what) + " produced no geometry"; return false; }
        output = std::move(mesh);
        error.clear();
        return true;
    }
};

struct Opening{
    float a, b, bottom, top;   // along the edge (meters from its first corner) and in Y
    bool door;
};

// Sorted, de-duplicated values inside [from, to], always including both ends.
std::vector<float> breaksWithin(const std::vector<float>& values, float from, float to){
    std::vector<float> result{from, to};
    for(float v : values) if(v > from + 1e-4f && v < to - 1e-4f) result.push_back(v);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end(), [](float x, float y){ return y - x <= 1e-4f; }), result.end());
    return result;
}

// One wall face as a grid over shared break lines, minus the cells inside openings. Faces
// that meet (floor above, next edge, reveals, caps) use the same breaks, so every shared
// edge has the same vertices and no T-junction cracks open between them.
void wallFace(Builder& builder, const std::function<glm::vec2(float)>& point, const glm::vec3& normal,
              const std::vector<float>& sBreaks, const std::vector<float>& yBreaks, const std::vector<Opening>& openings,
              uint16_t semantic, uint16_t material){
    for(std::size_t i = 0; i + 1 < sBreaks.size(); ++i){
        const float s0 = sBreaks[i], s1 = sBreaks[i + 1], sm = (s0 + s1) * 0.5f;
        const glm::vec2 p0 = point(s0), p1 = point(s1);
        for(std::size_t j = 0; j + 1 < yBreaks.size(); ++j){
            const float y0 = yBreaks[j], y1 = yBreaks[j + 1], ym = (y0 + y1) * 0.5f;
            const bool open = std::any_of(openings.begin(), openings.end(), [&](const Opening& o){
                return sm > o.a && sm < o.b && ym > o.bottom && ym < o.top;
            });
            if(!open) builder.quad(at(p0, y0), at(p1, y0), at(p1, y1), at(p0, y1), normal, semantic, material);
        }
    }
}

// Triangle strip between two polylines that both advance along the edge (s ascending).
void stitch(Builder& builder, const std::vector<glm::vec3>& outer, const std::vector<float>& outerS,
            const std::vector<glm::vec3>& inner, const std::vector<float>& innerS, const glm::vec3& normal,
            uint16_t semantic, uint16_t material){
    std::size_t i = 0, j = 0;
    while(i + 1 < outer.size() || j + 1 < inner.size()){
        if(j + 1 >= inner.size() || (i + 1 < outer.size() && outerS[i + 1] <= innerS[j + 1])){
            builder.tri(outer[i], outer[i + 1], inner[j], normal, semantic, material); ++i;
        }else{
            builder.tri(outer[i], inner[j + 1], inner[j], normal, semantic, material); ++j;
        }
    }
}

}  // namespace

bool triangulatePolygon(const std::vector<glm::vec2>& polygon, std::vector<uint32_t>& output, std::string& error){
    if(polygon.size() < 3){ error = "polygon needs at least three corners"; return false; }
    if(twiceArea(polygon) <= 0.0){ error = "polygon must have positive area"; return false; }
    std::vector<uint32_t> remaining(polygon.size());
    for(uint32_t i = 0; i < remaining.size(); ++i) remaining[i] = i;
    std::vector<uint32_t> result;
    result.reserve((polygon.size() - 2) * 3);
    auto inside = [](const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c){
        return cross2(b - a, p - a) >= -1e-9 && cross2(c - b, p - b) >= -1e-9 && cross2(a - c, p - c) >= -1e-9;
    };
    while(remaining.size() > 3){
        bool clipped = false;
        const std::size_t n = remaining.size();
        for(std::size_t i = 0; i < n && !clipped; ++i){
            const uint32_t ia = remaining[(i + n - 1) % n], ib = remaining[i], ic = remaining[(i + 1) % n];
            const glm::vec2 &a = polygon[ia], &b = polygon[ib], &c = polygon[ic];
            const double turn = cross2(b - a, c - b);
            if(std::abs(turn) <= 1e-10 && glm::dot(b - a, c - b) > 0.0f){
                remaining.erase(remaining.begin() + std::ptrdiff_t(i));   // straight corner: no triangle
                clipped = true;
                break;
            }
            if(turn <= 1e-10) continue;
            bool empty = true;
            for(uint32_t other : remaining){
                if(other == ia || other == ib || other == ic) continue;
                const glm::vec2& p = polygon[other];
                if(glm::length(p - a) < 1e-6f || glm::length(p - b) < 1e-6f || glm::length(p - c) < 1e-6f) continue;
                if(inside(p, a, b, c)){ empty = false; break; }
            }
            if(!empty) continue;
            result.insert(result.end(), {ia, ib, ic});
            remaining.erase(remaining.begin() + std::ptrdiff_t(i));
            clipped = true;
        }
        if(!clipped){ error = "polygon could not be triangulated"; return false; }
    }
    if(cross2(polygon[remaining[1]] - polygon[remaining[0]], polygon[remaining[2]] - polygon[remaining[1]]) > 1e-10)
        result.insert(result.end(), {remaining[0], remaining[1], remaining[2]});
    if(result.empty()){ error = "polygon could not be triangulated"; return false; }
    output = std::move(result);
    error.clear();
    return true;
}

bool offsetPolygon(const std::vector<glm::vec2>& polygon, float distance, std::vector<glm::vec2>& output,
                   std::string& error){
    const std::size_t n = polygon.size();
    if(n < 3 || twiceArea(polygon) <= 0.0 || !finite(distance)){ error = "offset needs a positive-area polygon"; return false; }
    std::vector<glm::vec2> result(n);
    for(std::size_t i = 0; i < n; ++i){
        const glm::vec2 previous = polygon[(i + n - 1) % n], corner = polygon[i], next = polygon[(i + 1) % n];
        const glm::vec2 d0 = glm::normalize(corner - previous), d1 = glm::normalize(next - corner);
        const glm::vec2 in0{-d0.y, d0.x}, in1{-d1.y, d1.x};   // left of the edge is inside
        const glm::vec2 p0 = corner + in0 * distance, p1 = corner + in1 * distance;
        const double denominator = cross2(d0, d1);
        if(std::abs(denominator) < 1e-9) result[i] = p1;
        else{
            const double t = cross2(p1 - p0, d1) / denominator;
            result[i] = p0 + d0 * float(t);
        }
        if(!finite(result[i]) || glm::length(result[i] - corner) > 10.0f * std::abs(distance) + 1e-4f){
            error = "offset corner is too sharp"; return false;
        }
    }
    const double originalArea = twiceArea(polygon);
    const double area = twiceArea(result);
    if(area <= 1e-6 || (distance > 0.0f && area >= originalArea) || !simplePolygon(result)){
        error = "offset is too large for this outline"; return false;
    }
    for(std::size_t i = 0; i < n; ++i){
        // An edge that flipped direction means the inset swallowed it.
        if(glm::dot(result[(i + 1) % n] - result[i], polygon[(i + 1) % n] - polygon[i]) <= 0.0f){
            error = "offset is too large for this outline"; return false;
        }
    }
    output = std::move(result);
    error.clear();
    return true;
}

bool makeFootprint(const FootprintNode& settings, Footprint& output, std::string& error){
    const float w = settings.width, d = settings.depth, a = settings.wingWidth;
    if(!finite(w) || !finite(d) || w < 1.0f || d < 1.0f || w > 500.0f || d > 500.0f ||
       !finite(settings.center) || !finite(settings.rotationDegrees)){
        error = "footprint width and depth must be between 1 and 500 meters"; return false;
    }
    const float hw = w * 0.5f, hd = d * 0.5f;
    std::vector<glm::vec2> outline;
    std::vector<FootprintPart> parts;
    std::vector<LocalRect> zones;   // tile the outline, for RoomSplit
    // Each outline starts on a +Z facing edge so doorEdge 0 is the front door.
    if(settings.shape == FootprintShape::Rectangle){
        outline = {{hw, hd}, {-hw, hd}, {-hw, -hd}, {hw, -hd}};
        parts.push_back({{0, 0}, {hw, hd}, {1, 0}});
        zones.push_back({{-hw, -hd}, {hw, hd}});
    }else{
        const bool u = settings.shape == FootprintShape::UShape;
        if(settings.shape != FootprintShape::LShape && !u){ error = "unknown footprint shape"; return false; }
        // Arms must stand out at least 1.5 m and a U keeps a courtyard of at least 2 m: narrower
        // gaps close under a roof overhang and are not rooms anyone could use.
        if(!finite(a) || a < 1.0f || d - a < 1.5f || (u ? w - 2.0f * a < 2.0f : w - a < 1.5f)){
            error = u ? "U footprint needs arms of at least 1 m that stand out 1.5 m (depth - wing) and a courtyard of 2 m (width - 2 wing)"
                      : "L footprint needs an arm of at least 1 m that stands out 1.5 m in both directions (width - wing, depth - wing)";
            return false;
        }
        const float barZ = -hd + a;
        // Arms start at the bar's ridge line so their roofs meet it under the ridge.
        const float armStart = -hd + a * 0.5f;
        const FootprintPart bar{{0, -hd + a * 0.5f}, {hw, a * 0.5f}, {1, 0}};
        // across of axis (1, 0) is +Z, so an arm meets the bar on its -across side (bit 2).
        const FootprintPart left{{-hw + a * 0.5f, (armStart + hd) * 0.5f}, {a * 0.5f, (hd - armStart) * 0.5f}, {1, 0}, 4};
        const FootprintPart right{{hw - a * 0.5f, (armStart + hd) * 0.5f}, {a * 0.5f, (hd - armStart) * 0.5f}, {1, 0}, 4};
        if(u){
            outline = {{hw, hd}, {hw - a, hd}, {hw - a, barZ}, {-hw + a, barZ}, {-hw + a, hd}, {-hw, hd}, {-hw, -hd}, {hw, -hd}};
            parts = {bar, left, right};
            zones = {{{-hw, -hd}, {hw, barZ}}, {{-hw, barZ}, {-hw + a, hd}}, {{hw - a, barZ}, {hw, hd}}};
        }else{
            outline = {{-hw + a, hd}, {-hw, hd}, {-hw, -hd}, {hw, -hd}, {hw, barZ}, {-hw + a, barZ}};
            parts = {bar, left};
            zones = {{{-hw, -hd}, {hw, barZ}}, {{-hw, barZ}, {-hw + a, hd}}};
        }
    }
    const float turn = settings.rotationDegrees * float(pi / 180.0);
    for(glm::vec2& p : outline) p = rotateXZ(p, turn) + settings.center;
    for(FootprintPart& part : parts){
        part.center = rotateXZ(part.center, turn) + settings.center;
        part.axis = rotateXZ(part.axis, turn);
    }
    Footprint made;
    if(!finishFootprint(outline, made, error)) return false;
    made.parts = std::move(parts);
    made.zones = std::move(zones);
    made.frameCenter = settings.center;
    made.frameAxis = rotateXZ({1.0f, 0.0f}, turn);
    output = std::move(made);
    return true;
}

bool footprintFromCurve(const Curve& curve, Footprint& output, std::string& error){
    std::vector<glm::vec2> outline;
    for(const glm::vec3& p : curve.points) outline.emplace_back(p.x, p.z);
    Footprint made;
    if(!finishFootprint(outline, made, error)) return false;
    output = std::move(made);
    return true;
}

namespace{

// Points on an edge's outer and inner line. The ends return the stored corners exactly:
// a + along * length differs from the next edge's first corner in the last float bit,
// and that is enough to open pixel cracks down every building corner.
struct Edge{
    glm::vec2 a, b, innerA, innerB, along, outward;
    float length, innerFrom, innerTo;
    glm::vec2 outer(float s) const{ return s <= 0.0f ? a : s >= length ? b : a + along * s; }
    glm::vec2 inside(float s) const{
        return s <= innerFrom ? innerA : s >= innerTo ? innerB : innerA + along * (s - innerFrom);
    }
};

// Windows from the interior plan: each room gets windows only on its own stretch of facade,
// by its type (a bathroom a small high one, a corridor only at its end, storage none), so no
// window ever lands on a partition. The front door goes where the plan put the entrance.
bool planOpenings(const Footprint& footprint, const WallsNode& settings, const std::vector<Edge>& edges,
                  std::vector<std::vector<std::vector<Opening>>>& openings, std::string& error){
    const InteriorPlan& plan = footprint.plan;
    const float h = footprint.floorHeight;
    const std::size_t n = edges.size();
    for(uint32_t floor = 0; floor < footprint.floors; ++floor){
        const float low = footprint.elevation + float(floor) * h;
        if(settings.door && floor == 0){
            const Edge& e = edges[plan.entranceEdge % n];
            const float lo = std::max(0.0f, e.innerFrom) + 0.3f, hi = std::min(e.length, e.innerTo) - 0.3f;
            if(hi - lo < settings.doorWidth){ error = "door does not fit on outline edge " + std::to_string(plan.entranceEdge); return false; }
            const float center = std::clamp(plan.entranceCenter, lo + settings.doorWidth * 0.5f, hi - settings.doorWidth * 0.5f);
            openings[0][plan.entranceEdge % n].push_back({center - settings.doorWidth * 0.5f, center + settings.doorWidth * 0.5f,
                                                          low, low + settings.doorHeight, true});
        }
        if(!settings.windows) continue;
        for(const Room& room : plan.rooms){
            if(room.floor != floor || room.type == RoomType::Storage) continue;
            const LocalRect& r = room.rect;
            const glm::vec2 corners[4] = {footprint.toWorld(r.min), footprint.toWorld({r.max.x, r.min.y}),
                                          footprint.toWorld(r.max), footprint.toWorld({r.min.x, r.max.y})};
            for(int side = 0; side < 4; ++side){
                const glm::vec2 p = corners[side], q = corners[(side + 1) % 4];
                for(std::size_t i = 0; i < n; ++i){
                    const Edge& e = edges[i];
                    auto off = [&](const glm::vec2& x){ return std::abs(glm::dot(x - e.a, e.outward)); };
                    if(off(p) > 1e-3f || off(q) > 1e-3f) continue;
                    float s0 = glm::dot(p - e.a, e.along), s1 = glm::dot(q - e.a, e.along);
                    if(s0 > s1) std::swap(s0, s1);
                    const float lo = std::max({s0 + 0.35f, std::max(0.0f, e.innerFrom) + 0.3f});
                    const float hi = std::min({s1 - 0.35f, std::min(e.length, e.innerTo) - 0.3f});
                    const float length = hi - lo;
                    float width = settings.windowWidth, bottom = settings.sillHeight, height = settings.windowHeight;
                    int count = 0;
                    switch(room.type){
                        case RoomType::Bathroom:
                            width = 0.6f; height = std::min(0.6f, height); bottom = settings.sillHeight + settings.windowHeight - height;
                            count = length >= width ? 1 : 0; break;
                        case RoomType::Corridor: case RoomType::Hall:
                            width = std::min(width, length - 0.1f);
                            count = s1 - s0 <= 3.2f && width >= 0.6f ? 1 : 0; break;
                        case RoomType::Stairs:
                            count = length >= width ? 1 : 0; break;
                        case RoomType::Kitchen:
                            bottom = std::max(bottom, 1.05f); height = std::min(height, settings.sillHeight + settings.windowHeight - bottom);
                            [[fallthrough]];
                        default:
                            count = length >= width ? std::max(1, int(std::floor(length / settings.windowSpacing))) : 0;
                    }
                    std::vector<Opening>& list = openings[floor][i];
                    for(int k = 0; k < count; ++k){
                        const float center = lo + length * (float(k) + 0.5f) / float(count);
                        const Opening candidate{center - width * 0.5f, center + width * 0.5f, low + bottom, low + bottom + height, false};
                        const bool clashes = std::any_of(list.begin(), list.end(), [&](const Opening& o){
                            return candidate.a < o.b + 0.3f && candidate.b > o.a - 0.3f;
                        });
                        if(!clashes) list.push_back(candidate);
                    }
                }
            }
        }
    }
    return true;
}

}  // namespace

bool makeWalls(const Footprint& footprint, const WallsNode& settings, MeshData& output, std::string& error,
               std::size_t maxVertices){
    const float h = footprint.floorHeight, t = settings.thickness;
    if(settings.windows && settings.sillHeight + settings.windowHeight > h - 0.2f){
        error = "windows do not fit in the storey height (sill + window must stay 0.2 m below it)"; return false;
    }
    if(settings.door && settings.doorHeight > h - 0.1f){ error = "door is taller than the storey"; return false; }
    std::vector<glm::vec2> inner;
    if(!offsetPolygon(footprint.outline, t, inner, error)){ error = "walls are too thick for this footprint"; return false; }

    const uint16_t exterior = semanticId("wall_exterior"), interior = semanticId("wall_interior");
    const uint16_t frame = semanticId("frame"), window = semanticId("window"), door = semanticId("door");
    const uint16_t plaster = materialId("plaster"), glass = materialId("glass"), wood = materialId("wood_planks");
    const std::size_t n = footprint.outline.size();
    const uint32_t floors = footprint.floors;
    const uint32_t doorEdge = settings.doorEdge % uint32_t(n);

    std::vector<Edge> edges(n);
    for(std::size_t i = 0; i < n; ++i){
        Edge& e = edges[i];
        e.a = footprint.outline[i];
        e.b = footprint.outline[(i + 1) % n];
        e.innerA = inner[i];
        e.innerB = inner[(i + 1) % n];
        e.length = glm::length(e.b - e.a);
        e.along = (e.b - e.a) / e.length;
        e.outward = {e.along.y, -e.along.x};
        e.innerFrom = glm::dot(inner[i] - e.a, e.along);
        e.innerTo = glm::dot(inner[(i + 1) % n] - e.a, e.along);
    }

    // Openings first: every face needs the break lines of its neighbours.
    std::vector<std::vector<std::vector<Opening>>> openings(floors, std::vector<std::vector<Opening>>(n));
    if(footprint.hasPlan){
        if(!planOpenings(footprint, settings, edges, openings, error)) return false;
    }
    for(uint32_t floor = 0; floor < floors && !footprint.hasPlan; ++floor){
        const float low = footprint.elevation + float(floor) * h;
        for(std::size_t i = 0; i < n; ++i){
            const Edge& e = edges[i];
            const float lo = std::max(0.0f, e.innerFrom) + 0.3f, hi = std::min(e.length, e.innerTo) - 0.3f;
            std::vector<Opening>& list = openings[floor][i];
            if(settings.door && floor == 0 && i == doorEdge){
                if(hi - lo < settings.doorWidth){
                    error = "door does not fit on outline edge " + std::to_string(i); return false;
                }
                const float center = (lo + hi) * 0.5f;
                list.push_back({center - settings.doorWidth * 0.5f, center + settings.doorWidth * 0.5f,
                                low, low + settings.doorHeight, true});
            }
            const float usable = hi - lo;
            if(settings.windows && usable >= settings.windowWidth){
                const int count = std::max(1, int(std::floor(usable / settings.windowSpacing)));
                for(int k = 0; k < count; ++k){
                    const float center = lo + usable * (float(k) + 0.5f) / float(count);
                    const Opening candidate{center - settings.windowWidth * 0.5f, center + settings.windowWidth * 0.5f,
                                            low + settings.sillHeight, low + settings.sillHeight + settings.windowHeight, false};
                    const bool clashes = std::any_of(list.begin(), list.end(), [&](const Opening& o){
                        return candidate.a < o.b + 0.3f && candidate.b > o.a - 0.3f;
                    });
                    if(!clashes) list.push_back(candidate);
                }
            }
        }
    }
    std::vector<std::vector<float>> edgeBreaks(n);
    for(std::size_t i = 0; i < n; ++i){
        std::vector<float> values{0.0f, edges[i].length, edges[i].innerFrom, edges[i].innerTo};
        for(uint32_t floor = 0; floor < floors; ++floor)
            for(const Opening& o : openings[floor][i]){ values.push_back(o.a); values.push_back(o.b); }
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end(), [](float x, float y){ return y - x <= 1e-4f; }), values.end());
        edgeBreaks[i] = values;
    }
    // Opening edges computed in different ways can differ in the last bit ("top of a small
    // bathroom window" vs "top of a window"); both must use the one break the faces use, or
    // the reveal and the face miss each other by a crack.
    auto snap = [](float v, const std::vector<float>& list){
        for(float x : list) if(std::abs(x - v) <= 1e-4f) return x;
        return v;
    };
    for(uint32_t floor = 0; floor < floors; ++floor){
        std::vector<float> heights;
        for(std::size_t i = 0; i < n; ++i)
            for(const Opening& o : openings[floor][i]){ heights.push_back(o.bottom); heights.push_back(o.top); }
        std::sort(heights.begin(), heights.end());
        heights.erase(std::unique(heights.begin(), heights.end(), [](float x, float y){ return y - x <= 1e-4f; }), heights.end());
        for(std::size_t i = 0; i < n; ++i)
            for(Opening& o : openings[floor][i]){
                o.a = snap(o.a, edgeBreaks[i]); o.b = snap(o.b, edgeBreaks[i]);
                o.bottom = snap(o.bottom, heights); o.top = snap(o.top, heights);
            }
    }

    Builder builder(maxVertices);
    for(uint32_t floor = 0; floor < floors; ++floor){
        // Both computed the same way as the next floor's low, so the seam shares its vertices.
        const float low = footprint.elevation + float(floor) * h, high = footprint.elevation + float(floor + 1) * h;
        std::vector<float> heights;
        for(std::size_t i = 0; i < n; ++i)
            for(const Opening& o : openings[floor][i]){ heights.push_back(o.bottom); heights.push_back(o.top); }
        const std::vector<float> yBreaks = breaksWithin(heights, low, high);
        for(std::size_t i = 0; i < n; ++i){
            const Edge& e = edges[i];
            const std::vector<Opening>& list = openings[floor][i];
            const glm::vec3 out3{e.outward.x, 0.0f, e.outward.y};
            const glm::vec3 along3{e.along.x, 0.0f, e.along.y};
            const auto outerPoint = [&e](float s0){ return e.outer(s0); };
            const auto innerPoint = [&e](float s0){ return e.inside(s0); };
            wallFace(builder, outerPoint, out3, breaksWithin(edgeBreaks[i], 0.0f, e.length), yBreaks, list, exterior, plaster);
            wallFace(builder, innerPoint, -out3, breaksWithin(edgeBreaks[i], e.innerFrom, e.innerTo), yBreaks,
                     list, interior, plaster);
            for(const Opening& o : list){
                auto outerAt = [&](float s0, float y){ return at(e.outer(s0), y); };
                auto innerAt = [&](float s0, float y){ return at(e.inside(s0), y); };
                const std::vector<float> ys = breaksWithin(yBreaks, o.bottom, o.top);
                const std::vector<float> ss = breaksWithin(edgeBreaks[i], o.a, o.b);
                for(std::size_t k = 0; k + 1 < ys.size(); ++k){
                    builder.quad(outerAt(o.a, ys[k]), outerAt(o.a, ys[k + 1]), innerAt(o.a, ys[k + 1]), innerAt(o.a, ys[k]),
                                 along3, frame, plaster);
                    builder.quad(outerAt(o.b, ys[k]), outerAt(o.b, ys[k + 1]), innerAt(o.b, ys[k + 1]), innerAt(o.b, ys[k]),
                                 -along3, frame, plaster);
                }
                for(std::size_t k = 0; k + 1 < ss.size(); ++k){
                    builder.quad(outerAt(ss[k], o.top), outerAt(ss[k + 1], o.top), innerAt(ss[k + 1], o.top), innerAt(ss[k], o.top),
                                 {0, -1, 0}, frame, plaster);
                    if(!o.door)   // a door has no wall under it: the slab is the threshold
                        builder.quad(outerAt(ss[k], o.bottom), outerAt(ss[k + 1], o.bottom), innerAt(ss[k + 1], o.bottom),
                                     innerAt(ss[k], o.bottom), {0, 1, 0}, frame, plaster);
                }
                // Pane set back a third of the wall, visible from both sides.
                const glm::vec2 p0 = e.a + e.along * o.a - e.outward * (t * 0.35f);
                const glm::vec2 p1 = e.a + e.along * o.b - e.outward * (t * 0.35f);
                const uint16_t semantic = o.door ? door : window, material = o.door ? wood : glass;
                builder.quad(at(p0, o.bottom), at(p1, o.bottom), at(p1, o.top), at(p0, o.top), out3, semantic, material);
                builder.quad(at(p0, o.bottom), at(p1, o.bottom), at(p1, o.top), at(p0, o.top), -out3, semantic, material);
            }
        }
    }
    // Caps on top of the last floor and under the ground floor, stitched to the face breaks.
    const float top = footprint.elevation + float(floors) * h;
    for(std::size_t i = 0; i < n; ++i){
        const Edge& e = edges[i];
        for(const float y : {top, footprint.elevation}){
            // Spans of the cap; the bottom one skips the door, which has no wall under it.
            std::vector<std::pair<float, float>> outerSpans{{0.0f, e.length}}, innerSpans{{e.innerFrom, e.innerTo}};
            if(y != top)
                for(const Opening& o : openings[0][i]){
                    if(!o.door) continue;
                    outerSpans = {{0.0f, o.a}, {o.b, e.length}};
                    innerSpans = {{e.innerFrom, o.a}, {o.b, e.innerTo}};
                }
            for(std::size_t span = 0; span < outerSpans.size(); ++span){
                const std::vector<float> outerS = breaksWithin(edgeBreaks[i], outerSpans[span].first, outerSpans[span].second);
                const std::vector<float> innerS = breaksWithin(edgeBreaks[i], innerSpans[span].first, innerSpans[span].second);
                std::vector<glm::vec3> outerPoints, innerPoints;
                for(float s0 : outerS) outerPoints.push_back(at(e.outer(s0), y));
                for(float s0 : innerS) innerPoints.push_back(at(e.inside(s0), y));
                stitch(builder, outerPoints, outerS, innerPoints, innerS, {0, y == top ? 1.0f : -1.0f, 0}, exterior, plaster);
            }
        }
    }
    // Steps up to a door on a raised ground floor: 0.3 m treads, risers of at most 0.18 m.
    const float rise = footprint.elevation;
    if(settings.door && rise > 0.05f){
        for(const Opening& o : openings[0][doorEdge]){
            if(!o.door) continue;
            const Edge& e = edges[doorEdge];
            const int steps = int(std::ceil(rise / 0.18f));
            const float riser = rise / float(steps), tread = 0.3f, half = (o.b - o.a) * 0.5f + 0.3f;
            const float center = (o.a + o.b) * 0.5f;
            const uint16_t stairs = semanticId("stairs"), concrete = materialId("concrete");
            // Step k reaches riser * (steps - k) and runs from the facade out to (k + 1) treads.
            for(int k = 0; k < steps; ++k){
                const float top = riser * float(steps - k), far = tread * float(k + 1) + 0.05f;
                const glm::vec2 c0 = e.a + e.along * (center - half), c1 = e.a + e.along * (center + half);
                const glm::vec2 corners[4] = {c0, c1, c1 + e.outward * far, c0 + e.outward * far};
                const glm::vec3 out3{e.outward.x, 0.0f, e.outward.y}, along3{e.along.x, 0.0f, e.along.y};
                builder.quad(at(corners[0], top), at(corners[1], top), at(corners[2], top), at(corners[3], top), {0, 1, 0}, stairs, concrete);
                builder.quad(at(corners[3], 0.0f), at(corners[2], 0.0f), at(corners[2], top), at(corners[3], top), out3, stairs, concrete);
                builder.quad(at(corners[0], 0.0f), at(corners[3], 0.0f), at(corners[3], top), at(corners[0], top), -along3, stairs, concrete);
                builder.quad(at(corners[1], 0.0f), at(corners[2], 0.0f), at(corners[2], top), at(corners[1], top), along3, stairs, concrete);
            }
        }
    }
    return builder.finish(output, error, "walls");
}

bool makeSlabs(const Footprint& footprint, const SlabNode& settings, MeshData& output, std::string& error,
               std::size_t maxVertices){
    std::vector<glm::vec2> polygon = footprint.outline;
    if(settings.inset > 0.0f && !offsetPolygon(footprint.outline, settings.inset, polygon, error)){
        error = "slab inset is too large for this footprint"; return false;
    }
    std::vector<uint32_t> triangles;
    if(!triangulatePolygon(polygon, triangles, error)) return false;
    const uint16_t floorTag = semanticId("floor"), ceiling = semanticId("ceiling"), foundation = semanticId("foundation");
    const uint16_t concrete = materialId("concrete");
    // An inset slab's edges are inside the walls; drawing them there would z-fight with the
    // facade a few centimeters in front.
    const bool sides = settings.inset <= 0.0f;
    Builder builder(maxVertices);
    if(footprint.hasPlan && footprint.plan.hasStairs && footprint.floors > 1){
        // Rectilinear in the footprint frame: cut the slab into cells on every corner line and
        // leave out the stairwell (the core without its arrival strip) above the ground floor.
        std::vector<glm::vec2> local;
        const glm::vec2 across{-footprint.frameAxis.y, footprint.frameAxis.x};
        for(const glm::vec2& p : polygon){
            const glm::vec2 d = p - footprint.frameCenter;
            local.push_back({glm::dot(d, footprint.frameAxis), glm::dot(d, across)});
        }
        LocalRect hole = footprint.plan.stairCore;
        if(footprint.plan.stairsAlongX) hole.min.x += 0.9f; else hole.min.y += 0.9f;
        std::vector<float> xs{hole.min.x, hole.max.x}, zs{hole.min.y, hole.max.y};
        for(const glm::vec2& p : local){ xs.push_back(p.x); zs.push_back(p.y); }
        auto unique = [](std::vector<float>& v){
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end(), [](float a, float b){ return b - a < 1e-4f; }), v.end());
        };
        unique(xs); unique(zs);
        auto inside = [&](const glm::vec2& p){
            bool in = false;
            for(std::size_t i = 0, j = local.size() - 1; i < local.size(); j = i++)
                if((local[i].y > p.y) != (local[j].y > p.y) &&
                   p.x < (local[j].x - local[i].x) * (p.y - local[i].y) / (local[j].y - local[i].y) + local[i].x) in = !in;
            return in;
        };
        const std::size_t nx = xs.size() - 1, nz = zs.size() - 1;
        for(uint32_t floor = 0; floor < footprint.floors; ++floor){
            const float top = footprint.elevation + float(floor) * footprint.floorHeight, bottom = top - settings.thickness;
            auto cell = [&](long i, long j){
                if(i < 0 || j < 0 || i >= long(nx) || j >= long(nz)) return 0;          // 0 outside
                const glm::vec2 c{(xs[i] + xs[i + 1]) * 0.5f, (zs[j] + zs[j + 1]) * 0.5f};
                if(!inside(c)) return 0;
                if(floor > 0 && c.x > hole.min.x && c.x < hole.max.x && c.y > hole.min.y && c.y < hole.max.y) return 2;   // stairwell
                return 1;
            };
            for(long i = 0; i < long(nx); ++i)
                for(long j = 0; j < long(nz); ++j){
                    if(cell(i, j) != 1) continue;
                    const glm::vec2 p00 = footprint.toWorld({xs[i], zs[j]}), p10 = footprint.toWorld({xs[i + 1], zs[j]});
                    const glm::vec2 p11 = footprint.toWorld({xs[i + 1], zs[j + 1]}), p01 = footprint.toWorld({xs[i], zs[j + 1]});
                    builder.quad(at(p00, top), at(p10, top), at(p11, top), at(p01, top), {0, 1, 0}, floorTag, concrete);
                    builder.quad(at(p00, bottom), at(p10, bottom), at(p11, bottom), at(p01, bottom), {0, -1, 0}, ceiling, concrete);
                    // Edges toward the stairwell are seen; edges at the facade hide in the walls.
                    const struct{ long di, dj; glm::vec2 a, b; } sidesOf[4] = {{-1, 0, p00, p01}, {1, 0, p10, p11}, {0, -1, p00, p10}, {0, 1, p01, p11}};
                    for(const auto& side : sidesOf){
                        const int other = cell(i + side.di, j + side.dj);
                        if(other == 1 || (other == 0 && !sides)) continue;
                        const glm::vec2 mid = (side.a + side.b) * 0.5f, center = (p00 + p11) * 0.5f;
                        const glm::vec2 out = mid - center;
                        builder.quad(at(side.a, bottom), at(side.b, bottom), at(side.b, top), at(side.a, top), {out.x, 0, out.y},
                                     floorTag, concrete);
                    }
                }
        }
    }else
    for(uint32_t floor = 0; floor < footprint.floors; ++floor){
        const float top = footprint.elevation + float(floor) * footprint.floorHeight;
        builder.prism(polygon, triangles, top - settings.thickness, top, floorTag, floorTag, ceiling, concrete, true, sides);
    }
    if(settings.topCeiling){
        // Just under the wall tops, so it never shares a plane with a roof or wall cap.
        const float top = footprint.elevation + float(footprint.floors) * footprint.floorHeight - 0.01f;
        builder.prism(polygon, triangles, top - settings.thickness, top, ceiling, ceiling, ceiling, concrete, true, sides);
    }
    if(settings.foundation && footprint.elevation > 0.01f){
        // Sides from the ground, and on top only the ring outside the ground floor slab: a full
        // top would lie in the slab's plane and flicker through the floor.
        std::vector<glm::vec2> plinth;
        if(!offsetPolygon(footprint.outline, -0.05f, plinth, error)) return false;
        const float y = footprint.elevation;
        for(std::size_t i = 0; i < plinth.size(); ++i){
            const std::size_t j = (i + 1) % plinth.size();
            const glm::vec2 edge = plinth[j] - plinth[i];
            builder.quad(at(plinth[i], 0.0f), at(plinth[j], 0.0f), at(plinth[j], y), at(plinth[i], y), {edge.y, 0, -edge.x},
                         foundation, concrete);
            builder.quad(at(plinth[i], y), at(plinth[j], y), at(polygon[j], y), at(polygon[i], y), {0, 1, 0}, foundation, concrete);
        }
    }
    return builder.finish(output, error, "slabs");
}

bool makeRoof(const Footprint& footprint, const RoofNode& settings, MeshData& output, std::string& error,
              std::size_t maxVertices){
    const float base = footprint.elevation + float(footprint.floors) * footprint.floorHeight;
    const float overhang = settings.overhang;
    const uint16_t roof = semanticId("roof"), wall = semanticId("wall_exterior"), trim = semanticId("trim");
    const uint16_t tiles = materialId("roof_tiles"), plaster = materialId("plaster");
    const uint16_t wood = materialId("wood_planks"), concrete = materialId("concrete");
    Builder builder(maxVertices);

    if(settings.type == RoofType::Flat){
        std::vector<glm::vec2> polygon = footprint.outline;
        if(overhang > 0.0f && !offsetPolygon(footprint.outline, -overhang, polygon, error)) return false;
        std::vector<uint32_t> triangles;
        if(!triangulatePolygon(polygon, triangles, error)) return false;
        const float top = base + settings.thickness;
        builder.prism(polygon, triangles, base, top, roof, trim, trim, concrete);
        if(settings.parapetHeight > 0.0f){
            std::vector<glm::vec2> inner;
            if(!offsetPolygon(polygon, 0.2f, inner, error)){ error = "parapet does not fit on this roof"; return false; }
            const float high = top + settings.parapetHeight;
            const std::size_t n = polygon.size();
            for(std::size_t i = 0; i < n; ++i){
                const glm::vec2 a = polygon[i], b = polygon[(i + 1) % n], c = inner[(i + 1) % n], d = inner[i];
                const glm::vec2 edge = b - a;
                const glm::vec3 outward{edge.y, 0.0f, -edge.x};
                builder.quad(at(a, top), at(b, top), at(b, high), at(a, high), outward, wall, plaster);
                builder.quad(at(d, top), at(c, top), at(c, high), at(d, high), -outward, wall, plaster);
                builder.quad(at(a, high), at(b, high), at(c, high), at(d, high), {0, 1, 0}, wall, plaster);
            }
        }
        return builder.finish(output, error, "roof");
    }

    if(footprint.parts.empty()){
        error = "gable, hip and shed roofs need a footprint made of rectangles; use a flat roof for traced outlines";
        return false;
    }
    if(settings.type == RoofType::Shed && footprint.parts.size() != 1){
        error = "a shed roof needs a rectangular footprint; use gable or hip for L and U shapes";
        return false;
    }
    const float slope = std::tan(settings.pitchDegrees * float(pi / 180.0));
    // The slopes pass 5 cm above the walls' outer top edge, so that edge is inside the roof
    // instead of exactly on its surface (where it would flicker through the tiles).
    const float pitchedBase = base + 0.05f;
    const glm::vec3 up{0, 1, 0};
    for(const FootprintPart& part : footprint.parts){
        glm::vec2 u = part.axis, v{-part.axis.y, part.axis.x};
        float hu = part.halfSize.x, hv = part.halfSize.y;
        bool joinedStart = part.joined & 1, joinedEnd = part.joined & 2;
        if(hv > hu){   // ridge along the long side
            std::swap(hu, hv); std::swap(u, v);
            joinedStart = part.joined & 4; joinedEnd = part.joined & 8;
        }
        // Ridge runs from u0 to u1; a joined end has no overhang and no hip.
        const float u0 = -hu - (joinedStart ? 0.0f : overhang), u1 = hu + (joinedEnd ? 0.0f : overhang);
        const float sh = hv + overhang;
        auto p = [&](float su, float sv, float y){ return at(part.center + u * su + v * sv, y); };
        const glm::vec3 u3{u.x, 0.0f, u.y}, v3{v.x, 0.0f, v.y};

        if(settings.type == RoofType::Shed){
            const float yl = pitchedBase - overhang * slope, yh = pitchedBase + (2.0f * hv + overhang) * slope;
            const glm::vec3 e1 = p(u0, -sh, yl), e2 = p(u1, -sh, yl), e3 = p(u1, sh, yh), e4 = p(u0, sh, yh);
            const glm::vec3 b3 = p(u1, sh, yl), b4 = p(u0, sh, yl);
            builder.quad(e1, e2, e3, e4, up, roof, tiles);
            builder.quad(e3, e4, b4, b3, v3, wall, plaster);
            builder.tri(e1, e4, b4, -u3, wall, plaster);
            builder.tri(e2, b3, e3, u3, wall, plaster);
            builder.quad(e1, e2, b3, b4, -up, trim, wood);
            continue;
        }
        const float ye = pitchedBase - overhang * slope, yr = pitchedBase + hv * slope;
        const bool hip = settings.type == RoofType::Hip;
        const bool hipStart = hip && !joinedStart, hipEnd = hip && !joinedEnd;
        float r0 = hipStart ? u0 + sh : u0, r1u = hipEnd ? u1 - sh : u1;
        if(r0 > r1u) r0 = r1u = (r0 + r1u) * 0.5f;   // square part: pyramid
        const glm::vec3 e1 = p(u0, -sh, ye), e2 = p(u1, -sh, ye), e3 = p(u1, sh, ye), e4 = p(u0, sh, ye);
        const glm::vec3 r1 = p(r0, 0.0f, yr), r2 = p(r1u, 0.0f, yr);
        builder.quad(e1, e2, r2, r1, up - v3, roof, tiles);
        builder.quad(e3, e4, r1, r2, up + v3, roof, tiles);
        // A joined end's gable is hidden inside the neighbouring roof, so it is roof, not facade.
        if(hipStart) builder.tri(e4, e1, r1, up - u3, roof, tiles);
        else builder.tri(e1, r1, e4, -u3, joinedStart ? roof : wall, joinedStart ? tiles : plaster);
        if(hipEnd) builder.tri(e2, e3, r2, up + u3, roof, tiles);
        else builder.tri(e2, e3, r2, u3, joinedEnd ? roof : wall, joinedEnd ? tiles : plaster);
        builder.quad(e1, e2, e3, e4, -up, trim, wood);
    }
    return builder.finish(output, error, "roof");
}

bool makeStairs(const StairsNode& settings, MeshData& output, std::string& error){
    const float w = settings.width, depth = settings.treadDepth;
    const float riser = settings.totalRise / float(settings.steps);
    const uint16_t stairs = semanticId("stairs"), railing = semanticId("railing");
    const uint16_t concrete = materialId("concrete"), metal = materialId("metal");
    Builder builder(1'000'000);
    for(uint32_t k = 0; k < settings.steps; ++k)
        builder.box({-w * 0.5f, 0.0f, float(k) * depth}, {w * 0.5f, float(k + 1) * riser, float(k + 1) * depth}, stairs, concrete);
    if(settings.railing){
        const float railHeight = 0.9f;
        for(float side : {-1.0f, 1.0f}){
            const float x = side * (w * 0.5f - 0.06f);
            for(uint32_t k = 0; k < settings.steps; ++k){
                if(k % 4 != 0 && k + 1 != settings.steps) continue;
                const float y = float(k + 1) * riser, z = (float(k) + 0.5f) * depth;
                builder.box({x - 0.025f, y, z - 0.025f}, {x + 0.025f, y + railHeight, z + 0.025f}, railing, metal);
            }
            const glm::vec3 start{x, riser + railHeight, 0.5f * depth};
            const glm::vec3 end{x, float(settings.steps) * riser + railHeight, (float(settings.steps) - 0.5f) * depth};
            builder.beam(start, end, 0.03f, 0.03f, railing, metal);
        }
    }
    return builder.finish(output, error, "stairs");
}

bool roadFromCurve(const Curve& curve, const RoadFromCurveNode& settings, MeshData& output, std::string& error){
    struct Strip{ float from, to, top; const char* semantic; const char* material; };
    const float half = settings.roadWidth * 0.5f, curb = 0.2f, depth = -0.2f;
    std::vector<Strip> strips = {{-half, half, 0.0f, "road", "asphalt"}};
    if(settings.sidewalks){
        const float outer = half + settings.sidewalkWidth, top = settings.curbHeight;
        for(float side : {-1.0f, 1.0f}){
            const float curbFrom = side * half, curbTo = side * (half + curb), walkTo = side * outer;
            strips.push_back({std::min(curbFrom, curbTo), std::max(curbFrom, curbTo), top + 0.02f, "curb", "concrete"});
            strips.push_back({std::min(curbTo, walkTo), std::max(curbTo, walkTo), top, "sidewalk", "paving"});
        }
    }
    std::vector<MeshData> pieces;
    pieces.reserve(strips.size());
    SweepSettings sweepSettings;
    sweepSettings.sampleSpacing = settings.sampleSpacing;
    for(const Strip& strip : strips){
        const Profile profile{{{strip.from, depth}, {strip.to, depth}, {strip.to, strip.top}, {strip.from, strip.top}}};
        MeshData piece;
        if(!sweep(curve, profile, piece, error, sweepSettings)) return false;
        piece.triangles.assign(piece.indices.size() / 3, {0, semanticId(strip.semantic), materialId(strip.material)});
        pieces.push_back(std::move(piece));
    }
    std::vector<const MeshData*> inputs;
    for(const MeshData& piece : pieces) inputs.push_back(&piece);
    return mergeMeshes(inputs, output, error, 2'000'000);
}

}  // namespace Engine::WeaverProcedura
