// Shared part of binning: which tiles one splat covers.
//
// This is an include and not copy-pasted twice, and that is the only defence against an
// error that would not show up here: counting and expanding MUST produce the same bounds.
// If they drifted, the prefix sum would reserve one number of slots and expanding would
// write another - which would show up as missing splats or as foreign data in someone
// else's part of the array, both looking like a bug somewhere entirely different.

//What the rasterizer reads about one splat. Three float4s, same layout as SplatMath::PreparedSplat
struct SplatData{
    float4 centerConic;        //xy centre in pixels, zw conic.x and conic.y
    float4 conicOpacityDepth;  //x conic.z, y opacity, z depth, w radius in pixels
    float4 color;
};

SplatData readSplat(RWStructuredBuffer<float4> splats, uint index){
    SplatData splat;
    splat.centerConic = splats[index * 3 + 0];
    splat.conicOpacityDepth = splats[index * 3 + 1];
    splat.color = splats[index * 3 + 2];
    return splat;
}

//Bounds in tiles: x0, y0 inclusive, x1, y1 exclusive. Empty bounds (x1 <= x0) mean the
//splat touches no tile - it is behind the camera or projected off frame
int4 tileBounds(float2 center, float radius, uint2 grid, uint tileSize){
    const int2 first = int2(floor((center - radius) / float(tileSize)));
    const int2 last  = int2(floor((center + radius) / float(tileSize)));

    return int4(clamp(first.x, 0, int(grid.x)),
                clamp(first.y, 0, int(grid.y)),
                clamp(last.x + 1, 0, int(grid.x)),
                clamp(last.y + 1, 0, int(grid.y)));
}

float conicForm(float a, float b, float c, float x, float y){
    return a * x * x + 2.0 * b * x * y + c * y * y;
}

//CAN THE SPLAT DRAW A SINGLE PIXEL OF THIS TILE. False only when it surely cannot.
//
//The bounds above are a square around the three-sigma circle, and the visible blob is an
//ellipse - often elongated and translucent, so much smaller. Measured on train_7000: 61 %
//of the pairs from the bounds draw nothing anywhere in their tile, and drawing still visits
//them all, for every pixel.
//
//A pixel is drawn only if it is (1) inside the radius circle and (2) alpha >= 1/255. Alpha
//is at most opacity * exp(power), so (2) means the conic's quadratic form is q <= 2 ln(255
//opacity). Both sets are convex, so the NEAREST point of the pixel-centre rectangle
//suffices: if even it is outside, all are.
//
//Twin of SplatMath::touchesTile, same order of operations. Counting and expanding both call
//it - if they drifted, the sum would reserve one number of slots and expanding would write
//another.
bool splatTouchesTile(SplatData splat, int tx, int ty, uint tileSize){
    const float opacity = splat.conicOpacityDepth.y;
    if(opacity < 1.0 / 255.0){
        return false;   //alpha is not above the opacity even at the centre
    }

    //Offset = centre - point, from the first to the last pixel centre in the tile
    const float size = float(tileSize);
    const float xLo = splat.centerConic.x - (float(tx) * size + size - 0.5);
    const float xHi = splat.centerConic.x - (float(tx) * size + 0.5);
    const float yLo = splat.centerConic.y - (float(ty) * size + size - 0.5);
    const float yHi = splat.centerConic.y - (float(ty) * size + 0.5);

    const float nearX = max(0.0, max(xLo, -xHi));
    const float nearY = max(0.0, max(yLo, -yHi));
    const float radius = splat.conicOpacityDepth.w;
    if(nearX * nearX + nearY * nearY > radius * radius){
        return false;
    }

    if(xLo <= 0.0 && xHi >= 0.0 && yLo <= 0.0 && yHi >= 0.0){
        return true;   //the centre is in the tile
    }

    //Smallest q on the rectangle edge: each edge is a parabola, so its minimum is at the
    //vertex clamped to the edge
    const float a = splat.centerConic.z;
    const float b = splat.centerConic.w;
    const float c = splat.conicOpacityDepth.x;
    float best = conicForm(a, b, c, xLo, clamp(-b * xLo / c, yLo, yHi));
    best = min(best, conicForm(a, b, c, xHi, clamp(-b * xHi / c, yLo, yHi)));
    best = min(best, conicForm(a, b, c, clamp(-b * yLo / a, xLo, xHi), yLo));
    best = min(best, conicForm(a, b, c, clamp(-b * yHi / a, xLo, xHi), yHi));

    //Rounding headroom: rather one pair more than one pixel fewer
    const float threshold = 2.0 * log(255.0 * opacity);
    return best <= threshold * 1.001 + 1e-3;
}
