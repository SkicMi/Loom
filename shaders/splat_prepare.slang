// Priprema splatova na kartici: kovarijanca, conic, polumjer, dubina i boja iz smjera pogleda.
//
// Ovo je bio najskuplji dio kadra dok je bio na procesoru - izmjereno 83 % - i to je bilo
// ocekivano: sve sto racuna ovisi o kameri, pa se ne da izracunati jednom i spremiti.
//
// SPLAT KOJI SE NE VIDI NE IZBACUJE SE IZ POLJA, nego mu se polumjer postavi na nulu. Zbijanje
// polja bi trazilo atomski brojac i promijenilo poredak, a poredak je ono na cemu sve dalje
// stoji. Nula je dovoljna: splat_count.slang na nju vec odgovara s nula pločica.
#include "include/SplatTiles.slang"
#include "include/SplatSH.slang"

//Sto se ne mijenja unutar kadra. U bufferu a ne u push konstanti, jer matrica pogleda sama
//pojede polovicu zajamcenih 128 bajtova
struct PrepareParams{
    float4x4 view;
    float4 cameraPosition;    //xyz
    float4 focalPrincipal;    //fx, fy, cx, cy - zariste i glavna tocka u pikselima
    float4 limitsBlur;        //x, y ogranicenje omjera; z blur; w nekoristeno
    uint4 counts;             //splatCount, shDegree, coeffsPerChannel, nekoristeno
}

[[vk::binding(0,0)]] RWStructuredBuffer<float4> rawSplats;   //cetiri float4 po splatu
[[vk::binding(1,0)]] RWStructuredBuffer<float> shRest;
[[vk::binding(2,0)]] RWStructuredBuffer<PrepareParams> params;
[[vk::binding(3,0)]] RWStructuredBuffer<float4> prepared;    //tri float4 po splatu

[shader("compute")]
[numthreads(256,1,1)]
void computeMain(uint3 threadId : SV_DispatchThreadID){
    const uint index = threadId.x;
    const PrepareParams p = params[0];

    if(index >= p.counts.x){
        return;
    }

    const float4 positionOpacity = rawSplats[index * 4 + 0];
    const float4 scaleUnused     = rawSplats[index * 4 + 1];
    const float4 rotation        = rawSplats[index * 4 + 2];
    const float4 dc              = rawSplats[index * 4 + 3];

    const float3 position = positionOpacity.xyz;
    const float3 viewPosition = mul(p.view, float4(position, 1.0)).xyz;
    const float depth = -viewPosition.z;

    //Iza kamere: polumjer nula i gotovo. Ostalo se ne mora ni racunati
    if(depth <= 0.0){
        prepared[index * 3 + 0] = float4(0,0,0,0);
        prepared[index * 3 + 1] = float4(0,0,0,0);
        prepared[index * 3 + 2] = float4(0,0,0,0);
        return;
    }

    //Kovarijanca u 3D: R*S*S'*R'. Kvaternion je vec jedinicne duljine - aktivacija se radi
    //jednom pri ucitavanju jer ne ovisi o kameri
    const float w = rotation.x, qx = rotation.y, qy = rotation.z, qz = rotation.w;

    //Isti raspored stupaca koji daje glm::mat3_cast
    const float3 r0 = float3(1.0 - 2.0*(qy*qy + qz*qz), 2.0*(qx*qy + w*qz),       2.0*(qx*qz - w*qy));
    const float3 r1 = float3(2.0*(qx*qy - w*qz),        1.0 - 2.0*(qx*qx + qz*qz), 2.0*(qy*qz + w*qx));
    const float3 r2 = float3(2.0*(qx*qz + w*qy),        2.0*(qy*qz - w*qx),        1.0 - 2.0*(qx*qx + qy*qy));

    const float3 m0 = r0 * scaleUnused.x;
    const float3 m1 = r1 * scaleUnused.y;
    const float3 m2 = r2 * scaleUnused.z;

    //cov = M * M', a M ima stupce m0,m1,m2. Simetricna je pa se racuna samo sest clanova
    const float cxx = m0.x*m0.x + m1.x*m1.x + m2.x*m2.x;
    const float cxy = m0.x*m0.y + m1.x*m1.y + m2.x*m2.y;
    const float cxz = m0.x*m0.z + m1.x*m1.z + m2.x*m2.z;
    const float cyy = m0.y*m0.y + m1.y*m1.y + m2.y*m2.y;
    const float cyz = m0.y*m0.z + m1.y*m1.z + m2.y*m2.z;
    const float czz = m0.z*m0.z + m1.z*m1.z + m2.z*m2.z;

    //Jakobijan projekcije, s ogranicenim omjerom prema osi. Vidi Core/Splat.cpp
    const float focalX = p.focalPrincipal.x;
    const float focalY = p.focalPrincipal.y;

    const float limitedX = clamp(viewPosition.x / depth, -p.limitsBlur.x, p.limitsBlur.x) * depth;
    const float limitedY = clamp(viewPosition.y / depth, -p.limitsBlur.y, p.limitsBlur.y) * depth;

    const float inverseDepth = 1.0 / depth;
    const float inverseDepth2 = inverseDepth * inverseDepth;

    //J je 3x3 s tri clana koja nisu nula; W je rotacija pogleda. T = J*W, i treba samo dva reda
    const float j00 = focalX * inverseDepth;
    const float j02 = focalX * limitedX * inverseDepth2;
    const float j11 = focalY * inverseDepth;
    const float j12 = focalY * limitedY * inverseDepth2;

    //STUPCI SE VADE MNOZENJEM, NE INDEKSIRANJEM. glm drzi matricu po stupcima a Slang je
    //indeksira po redovima, pa view[0] na dvije strane ne znaci isto - a mul() znaci, jer je
    //definiran matematicki. Mnozenje s jedinicnim vektorom vraca stupac, i to je jedini zapis
    //koji ne ovisi o tome tko kako slaze brojeve u memoriju.
    //
    //Prva verzija je indeksirala, i test je to uhvatio odmah: dubina se slagala na 4e-07 (jer
    //ona ide kroz mul) dok je conic bio potpuno drugi broj
    const float3 w0 = mul(p.view, float4(1,0,0,0)).xyz;
    const float3 w1 = mul(p.view, float4(0,1,0,0)).xyz;
    const float3 w2 = mul(p.view, float4(0,0,1,0)).xyz;

    //Redovi matrice T, u istom rasporedu u kojem glm drzi stupce
    const float3 t0 = float3(j00*w0.x + j02*w0.z, j00*w1.x + j02*w1.z, j00*w2.x + j02*w2.z);
    const float3 t1 = float3(j11*w0.y + j12*w0.z, j11*w1.y + j12*w1.z, j11*w2.y + j12*w2.z);

    //cov2 = T * cov3 * T', dva puta dva
    const float3 ct0 = float3(cxx*t0.x + cxy*t0.y + cxz*t0.z,
                              cxy*t0.x + cyy*t0.y + cyz*t0.z,
                              cxz*t0.x + cyz*t0.y + czz*t0.z);
    const float3 ct1 = float3(cxx*t1.x + cxy*t1.y + cxz*t1.z,
                              cxy*t1.x + cyy*t1.y + cyz*t1.z,
                              cxz*t1.x + cyz*t1.y + czz*t1.z);

    const float blur = p.limitsBlur.z;
    const float a = dot(t0, ct0) + blur;
    const float b = dot(t0, ct1);
    const float c = dot(t1, ct1) + blur;

    const float determinant = a*c - b*b;
    if(determinant <= 0.0){
        prepared[index * 3 + 0] = float4(0,0,0,0);
        prepared[index * 3 + 1] = float4(0,0,0,0);
        prepared[index * 3 + 2] = float4(0,0,0,0);
        return;
    }

    const float inverse = 1.0 / determinant;

    //Tri sigme po duzoj osi: veca svojstvena vrijednost matrice 2x2, bez ikakve iteracije
    const float mid = 0.5 * (a + c);
    const float spread = sqrt(max(0.1, mid*mid - determinant));
    const float radius = ceil(3.0 * sqrt(mid + spread));

    const float3 colour = shColor(shRest, index * p.counts.z * 3, p.counts.z,
                                  p.counts.y, dc.xyz, position - p.cameraPosition.xyz);

    prepared[index * 3 + 0] = float4(p.focalPrincipal.z + focalX * viewPosition.x / depth,
                                     p.focalPrincipal.w + focalY * viewPosition.y / depth,
                                     c * inverse,
                                    -b * inverse);
    prepared[index * 3 + 1] = float4(a * inverse, positionOpacity.w, depth, radius);
    prepared[index * 3 + 2] = float4(max(colour, float3(0,0,0)), 0.0);
}
