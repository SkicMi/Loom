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

float conicForm(float a, float b, float c, float x, float y){
    return a * x * x + 2.0 * b * x * y + c * y * y;
}

//MOZE LI SPLAT NACRTATI IJEDAN PIKSEL OVE PLOČICE. False samo kad je sigurno da ne moze.
//
//Okvir iznad je kvadrat oko kruga od tri sigme, a vidljiva mrlja je elipsa - cesto izduzena i
//poluprozirna, pa puno manja. Izmjereno na train_7000: 61 % parova iz okvira nigdje u svojoj
//pločici ne nacrta nista, a crtanje ih svejedno obidje za svaki piksel.
//
//Piksel se nacrta samo ako je (1) unutar kruga polumjera i (2) alfa >= 1/255. Alfa je najvise
//opacity * exp(power), pa (2) znaci da je kvadratni oblik conica q <= 2 ln(255 opacity). Oba
//skupa su konveksna, pa je dovoljna NAJBLIZA tocka pravokutnika centara piksela: ako je i ona
//izvan, izvan su svi.
//
//Blizanac SplatMath::touchesTile, isti redoslijed operacija. Brojanje i sirenje ga zovu oboje -
//da se razidju, zbroj bi rezervirao jedan broj mjesta a sirenje zapisalo drugi.
bool splatTouchesTile(SplatData splat, int tx, int ty, uint tileSize){
    const float opacity = splat.conicOpacityDepth.y;
    if(opacity < 1.0 / 255.0){
        return false;   //alfa nije veca od neprozirnosti ni u samom sredistu
    }

    //Pomak = sredina - tocka, od prvog do zadnjeg centra piksela u pločici
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
        return true;   //sredina je u pločici
    }

    //Najmanji q na rubu pravokutnika: na svakom rubu je to parabola, pa joj je minimum u tjemenu
    //odrezanom na rub
    const float a = splat.centerConic.z;
    const float b = splat.centerConic.w;
    const float c = splat.conicOpacityDepth.x;
    float best = conicForm(a, b, c, xLo, clamp(-b * xLo / c, yLo, yHi));
    best = min(best, conicForm(a, b, c, xHi, clamp(-b * xHi / c, yLo, yHi)));
    best = min(best, conicForm(a, b, c, clamp(-b * yLo / a, xLo, xHi), yLo));
    best = min(best, conicForm(a, b, c, clamp(-b * yHi / a, xLo, xHi), yHi));

    //Rub za zaokruzivanje: radije par vise nego piksel manje
    const float threshold = 2.0 * log(255.0 * opacity);
    return best <= threshold * 1.001 + 1e-3;
}
