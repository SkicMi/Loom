// Zajednicki dio binninga: koje pločice jedan splat zahvaca.
//
// Ovo je include a ne dva puta prepisano, i to je jedina obrana od greske koja se ovdje ne bi
// vidjela: brojanje i sirenje MORAJU dati isti okvir. Da se razidju, prefiksni zbroj bi
// rezervirao jedan broj mjesta a sirenje zapisalo drugi - i to bi se pojavilo kao splatovi
// koji nedostaju ili kao tudji podaci u tudjem dijelu polja, sto oboje izgleda kao greska
// negdje sasvim drugdje.

//Sto rasterizator cita o jednom splatu. Tri float4, isti raspored kao SplatMath::PreparedSplat
struct SplatData{
    float4 centerConic;        //xy sredina u pikselima, zw conic.x i conic.y
    float4 conicOpacityDepth;  //x conic.z, y neprozirnost, z dubina, w polumjer u pikselima
    float4 color;
};

SplatData readSplat(RWStructuredBuffer<float4> splats, uint index){
    SplatData splat;
    splat.centerConic = splats[index * 3 + 0];
    splat.conicOpacityDepth = splats[index * 3 + 1];
    splat.color = splats[index * 3 + 2];
    return splat;
}

//Okvir u pločicama: x0, y0 ukljucivo, x1, y1 iskljucivo. Prazan okvir (x1 <= x0) znaci da
//splat ne dira nijednu pločicu - iza kamere je ili je ispao izvan kadra
int4 tileBounds(float2 center, float radius, uint2 grid, uint tileSize){
    const int2 first = int2(floor((center - radius) / float(tileSize)));
    const int2 last  = int2(floor((center + radius) / float(tileSize)));

    return int4(clamp(first.x, 0, int(grid.x)),
                clamp(first.y, 0, int(grid.y)),
                clamp(last.x + 1, 0, int(grid.x)),
                clamp(last.y + 1, 0, int(grid.y)));
}

uint tilesTouched(int4 bounds){
    const int width = max(0, bounds.z - bounds.x);
    const int height = max(0, bounds.w - bounds.y);
    return uint(width * height);
}
