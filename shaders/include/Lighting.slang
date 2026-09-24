//Lighting: everything shared by whoever shades a surface.
//
//It used to sit twice, literally copy-pasted, in triangle.slang and textured.slang - ninety
//lines that had to agree down to the last bit for two images of the same scene to be the same
//image. Now it sits once, and relight over the G-buffer computes with the same code as drawing
//over the mesh.
//
//That is not tidiness but a claim that could not otherwise be proven: "the same light" means
//THE SAME CODE, not two pieces of code that happen to work the same.


struct FrameData{
    float4x4 view;
    float4x4 projection;
    float4 cameraPosition;
    float4 ambientColor;
    uint lightCount;
    uint padding0;
    uint padding1;
    uint padding2;
}

struct GpuLight{
    float4 positionOrDirection;
    float4 color;
    //x = range, y = kind of shadow (0 none, 1 flat map, 2 cube), z = bias, w = cube near
    float4 params;
    //x = which map of that kind, in the arrays on set 0
    float4 shadow;
    float4x4 lightViewProjection;
}

[[vk::binding(0,0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1,0)]] StructuredBuffer<GpuLight> lights;

//The shadow map, as a comparison sampler on set 0. SampleCmpLevelZero does not return a
//depth - it returns how much of the 2x2 neighbourhood passed "is my reference nearer than
//what is stored here", filtered by the same hardware that does bilinear. That free 2x2 is
//why a shadow edge is not a staircase
//The size comes from CMake via -D, the same number C++ sees. It used to sit here as a
//literal with a comment that it had to match - but if they drifted, the pipeline would still
//compile
[[vk::binding(2,0)]] Sampler2DShadow shadowMaps[LOOM_MAX_SHADOW_MAPS];

//1.0 fully lit, 0.0 fully shadowed. A light without a map bound (params.y == 0) returns 1.0
//without ever touching the sampler
float shadowVisibility(GpuLight light, float3 worldPos){
    if(light.params.y < 0.5) return 1.0;

    float4 clip = mul(light.lightViewProjection, float4(worldPos, 1.0));
    float3 proj = clip.xyz / clip.w;

    //Behind the light's far plane there is no map to consult, and the border is opaque white
    //so anything outside the box sideways already compares as lit
    if(proj.z > 1.0) return 1.0;

    float2 uv = proj.xy * 0.5 + 0.5;

    //The bias is subtracted from the reference, not added to the stored depth: a fragment
    //has to be nearer than the map by this much before it counts as lit
//The index comes from the light, and the loop over lights runs identically in every invocation,
//so it is dynamically uniform - exactly what shaderSampledImageArrayDynamicIndexing asks for
    return shadowMaps[int(light.shadow.x)].SampleCmpLevelZero(uv, proj.z - light.params.z);
}

//A point light's shadow cube. There is no matrix here on purpose: a cube is looked up with
//a direction, and the reference depth is rebuilt from how far along its longest axis the
//fragment is. That is the same number the face wrote, arrived at by arithmetic instead of
//by another matrix multiply
[[vk::binding(3,0)]] SamplerCubeShadow shadowCubes[LOOM_MAX_SHADOW_CUBES];

float cubeVisibility(GpuLight light, float3 worldPos){
    float3 toFragment = worldPos - light.positionOrDirection.xyz;
    float3 magnitude = abs(toFragment);

    //Whichever face this direction lands on, that face's depth axis is the largest component
    float axis = max(magnitude.x, max(magnitude.y, magnitude.z));

    float near = max(light.params.w, 1e-4);
    float far = max(light.params.x, near * 2.0);

    //Closer than the near plane there is no map, and nothing that close is occluded anyway
    if(axis <= near) return 1.0;

    //The same value a 90 degree projection with these planes writes, worked out by hand:
    //ndcZ = far * (axis - near) / ((far - near) * axis)
    float reference = (far * (axis - near)) / ((far - near) * axis);

    return shadowCubes[int(light.shadow.x)].SampleCmpLevelZero(toFragment, reference - light.params.z);
}


//One surface, all lights. base is albedo, worldPos and N are in world space, V points from
//the surface toward the camera.
//
//From here on it does not matter whether the surface came from a mesh or from a depth image -
//all this function knows is where the point is, which way it faces and what colour it has
//An occlusion that shadeSurface cannot compute itself.
//
//Loom already computes shadow-map shadows here. But when light is inserted into a photo,
//there is no shadow map - only what is visible in the frame, and the occluder has to be LOOKED
//FOR through that same depth. That computation belongs not to lighting but to the pass that
//has the depth at hand.
//
//That is why occlusion is a parameter. Mesh drawing passes NoOcclusion, which compiles to
//nothing, so that path does not pay a single instruction - and lighting stays one function
//for both paths
interface IOcclusion{
    float visibility(GpuLight light, float3 worldPos);
}

struct NoOcclusion : IOcclusion{
    float visibility(GpuLight light, float3 worldPos){ return 1.0; }
}

//Contribution of all lights, added on top of what was already there.
//
//start exists for compositing: when light is inserted into a photo, what is already in the
//image IS the starting value, so it accumulates in the same order as if the scene had been
//drawn at once. If compositing added at the end, the sum would differ in the last bit - and
//then "the same light" could no longer be proven, only asserted
float3 accumulateLight<T : IOcclusion>(float3 start, float3 base, float3 worldPos,
                                       float3 N, float3 V,
                                       float shininess, float specularStrength,
                                       float diffuseWrap,
                                       T occlusion){
    float3 lit = start;

    for(uint i = 0; i < frame.lightCount; ++i){
        GpuLight light = lights[i];

        float3 L;
        float attenuation = 1.0;

        if(light.positionOrDirection.w == 0.0){
            L = normalize(-light.positionOrDirection.xyz);
        }
        else{
            float3 toLight = light.positionOrDirection.xyz - worldPos;
            float dist = max(length(toLight), 1e-4);
            L = toLight / dist;

            float range = max(light.params.x, 1e-4);
            float window = saturate(1.0 - pow(dist / range, 4.0));
            attenuation = window * window / (dist * dist);
        }

        float3 H = normalize(L + V);

//WRAPPED DIFFUSE. Lambert drops to zero exactly at dot(N,L) = 0, so the terminator is a sharp
//edge and every normal error shows on it as a smudge. When the normals come from ESTIMATED
//depth that is not a rarity but the rule.
//
//Wrapping pushes that zero back and remaps the range into 0..1, so light creeps a little past
//the edge. Zero is exactly Lambert: (dot + 0) / (1 + 0) is dot
        float diffuse = max((dot(N,L) + diffuseWrap) / (1.0 + diffuseWrap), 0.0);
        float specular = pow(max(dot(N,H),0.0), shininess) * specularStrength;

        //Shadow kills the whole contribution of this light, diffuse and specular alike.
        //Ambient is untouched, which is why a shadowed surface is exactly its ambient term
        //and not black
        //params.y says which kind of map this light has, if any: 1 a single image, 2 a cube
        float visibility = 1.0;
        if(light.params.y > 1.5){
            visibility = cubeVisibility(light, worldPos);
        }
        else if(light.params.y > 0.5){
            visibility = shadowVisibility(light, worldPos);
        }

//Frame occlusion multiplies what the shadow map already said. Light blocked by either of the
//two paths is blocked - the other one cannot bring it back
        visibility *= occlusion.visibility(light, worldPos);

        lit += base * light.color.rgb * diffuse * attenuation * visibility;
        lit += light.color.rgb * specular * attenuation * visibility;
    }

    return lit;
}

float3 accumulateLight(float3 start, float3 base, float3 worldPos, float3 N, float3 V,
                       float shininess, float specularStrength, float diffuseWrap){
    return accumulateLight(start, base, worldPos, N, V, shininess, specularStrength,
                           diffuseWrap, NoOcclusion());
}

//A surface lit from scratch: ambient, then lights. What mesh drawing has always done
float3 shadeSurface(float3 base, float3 worldPos, float3 N, float3 V,
                    float shininess, float specularStrength, float diffuseWrap = 0.0){
    return accumulateLight(base * frame.ambientColor.rgb, base, worldPos, N, V,
                           shininess, specularStrength, diffuseWrap, NoOcclusion());
}
