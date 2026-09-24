//What both relight passes share: what a surface is and how to get to one.
//
//They differ only in what they do with the result - one draws its own light, the other adds
//it to a photo that is already there.

#include "Lighting.slang"

struct RelightData{
    //Points and normals are in view space, because the depth they came from was. Lights are
    //in world space. This is the bridge between the two, and the only reason relight needs
    //the camera
    float4x4 inverseView;

    //And back: the shadow trace walks the position image, which is in view space, so the
    //light has to be brought to it instead of taking every point into world space
    float4x4 view;

    float4 baseColor;
    float4 surface;      //x = shininess, y = specularStrength
    float4 intrinsics;   //fx, fy, cx, cy - used to project a point back to a pixel
    float4 shadow;       //x = steps (0 = no shadow), y = reach, z = thickness, w = bias
    float4 shadowSlope;  //x = bias growth, y = free, z = thickness growth, w = front band
    float4 occlusion;    //x = strength, y = near ring (px), z = far ring (px), w = scale (m)
    float4 occluder;     //x = only the mask occludes, y = occluder smoothing radius (px),
                         //z = ray count, w = light radius (m)
    float4 imageSize;    //xy
}

//Shadow traced through the image itself.
//
//There is no shadow map here and there cannot be: depth gives a SHELL, not geometry - only
//the front surface of what is visible exists. So occlusion is found by marching: from the
//point toward the light, in steps, and at every step we look at what the image holds at that
//spot. If it is closer to the camera than our point, something is in between.
//
//The limit is fair to state: only what is IN THE FRAME casts a shadow. An object behind the
//camera or off the edge does not exist in any image, so it cannot occlude. That is a limit
//of the technique, not its bug - and it is why this is called a contact shadow and not a
//shadow.
//Median of five values, via the known identity:
//    median5 = median3( max(min(a,b), min(c,d)), min(max(a,b), max(c,d)), e )
//
//Median, not mean: a mean across a silhouette blends a person with a wall two metres behind
//him and invents a slope that does not exist, while the median takes whichever side there is
//more of
float median3(float x, float y, float z){
    return max(min(x, y), min(max(x, y), z));
}

float median5(float a, float b, float c, float d, float e){
    return median3(max(min(a, b), min(c, d)), min(max(a, b), max(c, d)), e);
}

struct ScreenTrace : IOcclusion{
    float3 point;        //the point being shadowed, in view space
    float2 pixel;        //where it is on screen - for breaking up banding
    RelightData data;

    //Returns the pixel in which a view-space point would be seen. Same as unprojection, only
    //the other way
    float2 project(float3 view){
        float depth = -view.z;
        return float2(data.intrinsics.x * view.x / depth + data.intrinsics.z,
                      data.intrinsics.y * view.y / depth + data.intrinsics.w);
    }

    //One march toward one point of light. Returns 0 or 1 - deliberately hard, because a hard
    //decision is the only one comparable to analytic geometry down to the last pixel
    float march(float3 toLight, float reach, float lightDepth, float dither){
        int steps = int(data.shadow.x);
        float stride = reach / float(steps);
        float growth = data.shadowSlope.x;

        for(int i = 0; i < steps; ++i){
            float t = (float(i) + dither) * stride + data.shadow.w;
            float3 sample = point + toLight * t;

            //Behind the camera there is no projecting
            if(sample.z > -1e-3) break;

            float2 at = project(sample);
            if(at.x < 0.0 || at.y < 0.0 || at.x >= data.imageSize.x || at.y >= data.imageSize.y){
                //Off the frame: what is outside does not exist in any image
                break;
            }

            float2 uv = at / data.imageSize.xy;

            //WHO IS ALLOWED TO OCCLUDE. Without a mask, everything visible occludes, including
            //the wall to itself
            if(data.occluder.x > 0.5 && occluderMask.SampleLevel(uv, 0.0).r < 0.5) continue;

            float4 stored = positionMap.SampleLevel(uv, 0.0);
            if(stored.w < 0.5) continue;   //sky: there is no surface to occlude with

            //THE GEOMETRY THAT CASTS THE SHADOW IS NOT THE GEOMETRY BEING SHADOWED: a fabric
            //fold is just a smudge to the trace, so it may read a five-sample median instead
            //of a single point
            float storedDistance = -stored.z;

            if(data.occluder.y > 0.0){
                float radius = data.occluder.y;
                float2 stepX = float2(radius, 0.0) / data.imageSize.xy;
                float2 stepY = float2(0.0, radius) / data.imageSize.xy;

                float4 left = positionMap.SampleLevel(uv - stepX, 0.0);
                float4 right = positionMap.SampleLevel(uv + stepX, 0.0);
                float4 up = positionMap.SampleLevel(uv - stepY, 0.0);
                float4 down = positionMap.SampleLevel(uv + stepY, 0.0);

                float far = 1e6;
                storedDistance = median5(storedDistance,
                    left.w  > 0.5 ? -left.z  : far,
                    right.w > 0.5 ? -right.z : far,
                    up.w    > 0.5 ? -up.z    : far,
                    down.w  > 0.5 ? -down.z  : far);
            }

            //How much nearer the stored surface is to the camera than the point we marched to. The
            //sign is the only thing that decides here: flipped, everything is in shadow
            float inFront = (-sample.z) - storedDistance;

            //Bias grows with the path, because false occlusion grows with it
            float bias = data.shadow.w + t * growth;

            //The farther we marched, the less is known about what is behind the sampled
            //surface
            float thickness = data.shadow.z * (1.0 + (t / reach) * data.shadowSlope.z);

            //A surface nearer the camera than the light cannot occlude - the light is in
            //front of it
            bool ahead = true;
            if(data.shadowSlope.w > 0.0 && lightDepth > 0.0){
                ahead = (storedDistance - lightDepth) > 0.5 * data.shadowSlope.w;
            }

            if(ahead && inFront > bias && inFront < thickness) return 0.0;
        }

        return 1.0;
    }

    float visibility(GpuLight light, float3 worldPos){
        int steps = int(data.shadow.x);
        if(steps <= 0) return 1.0;

        //Deterministic banding break: without an offset every pixel would sample at the same
        //distances and the shadow edge would be a staircase. Per-pixel hash, not a random
        //number - the same image twice must give the same shadow
        float dither = frac(sin(dot(pixel, float2(12.9898, 78.233))) * 43758.5453);

        float lightDepth = (light.positionOrDirection.w == 0.0) ? -1.0
            : -mul(data.view, float4(light.positionOrDirection.xyz, 1.0)).z;

        //LIGHT AS A DISC, NOT A POINT.
        //
        //A point gives only a hard shadow; penumbra exists because part of the source is
        //visible and part is not. So we march several times, each time toward a DIFFERENT
        //point on a disc of radius lightRadius, and count how many get through. The core
        //stays fully dark because no ray passes, and the edge gets a width that grows with
        //the light size - so from physics, not from the step size.
        //
        //We can afford this because the image is allowed to take time: realtime would pay N
        //traces per pixel, for us it is the only cost
        int rays = max(int(data.occluder.z), 1);
        float lightRadius = data.occluder.w;

        float3 toLight;
        float reach = data.shadow.y;
        float distance = 0.0;

        if(light.positionOrDirection.w == 0.0){
            //Directional light: the direction is the same everywhere, so we march a fixed distance
            toLight = normalize(-mul(data.view, float4(light.positionOrDirection.xyz, 0.0)).xyz);
        }
        else{
            float3 lightView = mul(data.view, float4(light.positionOrDirection.xyz, 1.0)).xyz;
            float3 away = lightView - point;
            distance = length(away);
            if(distance < 1e-4) return 1.0;

            toLight = away / distance;

            //Past the light there is nothing to occlude with - marching past it means finding an
            //occluder that is behind the source
            reach = min(reach, distance);
        }

        if(rays <= 1 || lightRadius <= 0.0){
            return march(toLight, reach, lightDepth, dither);
        }

        //Two axes perpendicular to the direction toward the light, so the disc can be spanned
        float3 helper = abs(toLight.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        float3 axisU = normalize(cross(helper, toLight));
        float3 axisV = cross(toLight, axisU);

        float reached = 0.0;

        for(int ray = 0; ray < rays; ++ray){
            //Vogel's spiral layout: points spread evenly over the disc, no random numbers.
            //The per-pixel offset still breaks banding, and the same image twice gives the
            //same shadow
            float index = float(ray) + 0.5;
            float radius = lightRadius * sqrt(index / float(rays));
            float angle = index * 2.39996323 + dither * 6.28318531;

            float3 offset = axisU * (radius * cos(angle)) + axisV * (radius * sin(angle));

            //Every ray starts from a DIFFERENT place inside the step. Otherwise all N traces
            //sample at the same distances, their quantization errors line up, and the sum is
            //just as grainy as a single trace - the disc spreads, the step does not

            //Ray toward an offset point on the source. For a directional light the offset
            //is angular, so the radius is read as a tangent - the source is infinitely far
            //away and there is nowhere to shift it
            float3 rayDirection = (light.positionOrDirection.w == 0.0)
                ? normalize(toLight + offset)
                : normalize(toLight * distance + offset);

            reached += march(rayDirection, reach, lightDepth,
                             frac(dither + index / float(rays)));
        }

        return reached / float(rays);
    }
}

[[vk::binding(0,1)]] Sampler2D positionMap;
[[vk::binding(1,1)]] Sampler2D normalMap;

//Who is allowed to occlude. White means "everyone", so an image that is never set is a
//white pixel and everything stays as it was
[[vk::binding(2,1)]] Sampler2D occluderMask;

struct VSOutput{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
}

//The surface under this pixel, moved into world space. exists is false where there is no
//surface
struct Surface{
    float3 worldPos;
    float3 viewPos;      //the same point before the transform - the shadow trace walks in this space
    float3 N;
    float3 V;
    bool exists;
}

//AMBIENT OCCLUSION FROM THE DEPTH MAP ITSELF.
//
//What makes inserted light look SEATED: without it an object floats over the background,
//because nothing darkens where they touch. It needs no light - it is computed from geometry.
//
//It is computed here, in the fragment shader, not into a separate texture. TypeGPU
//recomputes it because it must fit in eight milliseconds; our image is allowed to take time,
//so 32 extra samples per pixel saves an entire image, its binding and its maintenance.
//
//Two rings: the near one catches contact, the far one the crevice shadow. A neighbour counts
//for how much it is NEARER (cleared) and for how much it is still touching (contact) - the
//second without the first would declare every distant background an occluder
float ambientOcclusion(float2 texCoord, float4 settings, float2 imageSize){
    if(settings.x <= 0.0) return 0.0;

    float4 centre = positionMap.SampleLevel(texCoord, 0.0);
    if(centre.w < 0.5) return 0.0;

    float centreDistance = -centre.z;
    float scale = max(settings.w, 1e-4);

    //Threshold below which a difference is noise, and reach past which a neighbour is no
    //longer in contact. Both are tied to the scale, so one number is tuned and not three
    float floorDistance = 0.1 * scale;
    float range = 4.0 * scale;

    float total = 0.0;
    float count = 0.0;

    for(int ring = 0; ring < 2; ++ring){
        float radius = (ring == 0) ? settings.y : settings.z;

        for(int step = 0; step < 8; ++step){
            float angle = float(step) * 0.7853981634;   //an eighth of the circle
            float2 offset = float2(cos(angle), sin(angle)) * radius;

            float4 neighbour = positionMap.SampleLevel(texCoord + offset / imageSize, 0.0);
            if(neighbour.w < 0.5) continue;

            //Positive means the neighbour is NEARER to the camera, so something sticks out toward us
            float difference = centreDistance - (-neighbour.z);

            float contact = 1.0 - saturate(abs(difference) / range);
            float cleared = max(difference - floorDistance, 0.0);

            total += saturate(cleared / scale) * contact;
            count += 1.0;
        }
    }

    if(count < 0.5) return 0.0;
    return saturate(settings.x * total / count);
}

Surface surfaceAt(float2 texCoord, float4x4 inverseView){
    Surface out;

    float4 point = positionMap.SampleLevel(texCoord, 0.0);
    float4 normal = normalMap.SampleLevel(texCoord, 0.0);

    out.exists = point.w > 0.5 && normal.w > 0.5;
    out.viewPos = point.xyz;
    if(!out.exists){
        out.worldPos = float3(0,0,0);
        out.N = float3(0,0,1);
        out.V = float3(0,0,1);
        return out;
    }

    out.worldPos = mul(inverseView, float4(point.xyz, 1.0)).xyz;

    //A normal is a direction, not a position: w = 0, so the translation leaves it alone
    out.N = normalize(mul(inverseView, float4(normal.xyz, 0.0)).xyz);
    out.V = normalize(frame.cameraPosition.xyz - out.worldPos);
    return out;
}
