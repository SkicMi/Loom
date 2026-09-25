//Suicelje: cetverokuti u zaslonskim koordinatama, koje fragment shader oblikuje.
//
//Svaki cetverokut nosi "rezim" u vrhu (mode):
//  0 ravan, bez ikakvog racuna - scena u pogledu editora (tocke, duzine, stranice kocke)
//  1 ispuna: ZAOBLJENI pravokutnik s izgladjivanjem ruba, polumjer u profile.x
//  2 tekst: kvadratic tinte iz atlasa (mode se uzorkuje u profile.x)
//  3 obrub: prsten unutarnje debljine (profile.y) oko iste konture kao ispuna
//  4 fluid marble: tileovi, sporo kretanje uzorka, fuge i stakleni odsjaj
//
//PRETVORBA IZ PIKSELA JE OVDJE, NAMJERNO. Treadle racuna raspored i pogadjanje misem u
//pikselima; da ih on pretvara u NDC, isti bi se racun radio na dva mjesta i mogao bi se
//raziici. Ovako postoji jedna definicija "gdje je nesto", a shader je samo pretvara.
//
//Udaljenost za izgladjivanje mjeri se U PIKSELLIMA, od polozaja fragmenta (gl_FragCoord,
//ishodiste gore lijevo - isto kao i Treadleov prostor) do konture cetverokuta. Tocka mjerenja
//i cetverokut dijele prostor, pa se brojcevi ne moraju uskladiti ni preko kakvine konverzije.
struct Push{
    float2 screenSize;

    //1 kad meta nosi sRGB format. Vulkan tada ono sto shader vrati SAM pretvara u sRGB, pa
    //broj 0.20 zavrsi na ekranu kao 0.48 - suicelje izadje isprano i svjetlije nego sto je
    //napisano. Boje u Treadle::Theme su zapisane onako kako trebaju IZGLEDATI, pa se ovdje
    //pretvaraju unatrag da ih kartica moze pretvoriti natrag.
    //
    //Meta bez sRGB formata ne radi nista od toga i tada je ovo nula - inace bi suicelje bilo
    //pretamno. Zato je ovo zastavica a ne uvijek ukljucen racun
    float encodeSrgb;
    float padding;
}

//Priblizna inverzna sRGB krivulja. 2.2 umjesto prave krivulje s linearnim dijelom pri dnu:
//razlika je najveca u najtamnijim tonovima i ispod jednog koraka od 256 - a ovdje se radi o
//boji plohe suicelja, ne o slici
float3 toLinear(float3 color, float enabled){
    return lerp(color, pow(max(color, 0.0), 2.2), enabled);
}

//Atlas slova suicelja: R8 pokrivenost, generiran u FontData.h. Veze se na set 1 binding 0,
//isto kao albedo u textured.slang. Linearni filter daje izgladjen tekst (atlas je crtan 11x
//veci od logickog piksela, pa filtriranje barem dvije celije po logickom pikselu)
[[vk::binding(0,1)]] Sampler2D atlas;

[[vk::push_constant]] ConstantBuffer<Push> push;

struct VSInput{
    [[vk::location(0)]] float2 position;   //pikseli, ishodiste gore lijevo
    [[vk::location(1)]] float4 color;
    [[vk::location(2)]] float2 center;     //srediste pravokutnika, pikseli
    [[vk::location(3)]] float4 uvHalf;     //u,v (tekst) + pola sirine i visine
    [[vk::location(4)]] float4 profile;    //radius, thickness, mode, padding
}

struct VSOutput{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 center : TEXCOORD0;
    float4 uvHalf : TEXCOORD1;
    float4 profile : TEXCOORD2;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input){
    VSOutput output;

    //Vulkanov NDC ima +Y prema DOLJE, isto kao pikseli, pa se Y ne okrece. Da se okrene,
    //suicelje bi bilo zrcaljeno okomito i pogadjanje misem bi promasivalo za visinu plohe
    output.position = float4(input.position / push.screenSize * 2.0 - 1.0, 0.0, 1.0);
    output.color = input.color;
    output.center = input.center;
    output.uvHalf = input.uvHalf;
    output.profile = input.profile;
    return output;
}

//Udaljenost od tocke p do obrisa zaobljenog pravokutnika u pikselima; negativna unutar,
//pozitivna izvan. Polumjer je vec ogranicen na pola manje stranice, pa i tanka crta ostaje
//crta a ne puca u lopticu
float roundedRectDistance(float2 p, float2 center, float2 half, float radius){
    float2 q = abs(p - center) - (half - radius);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

//Hash cellulaire sans texture: assez bon pour deux couches de noise sur les rares surfaces
//marblea, a jeftiniji je od CPU geometrije i ne dodaje novi descriptor ni draw call.
float hash21(float2 p){
    p = frac(p * float2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return frac(p.x * p.y);
}

float valueNoise(float2 p){
    const float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    const float a = hash21(cell);
    const float b = hash21(cell + float2(1.0, 0.0));
    const float c = hash21(cell + float2(0.0, 1.0));
    const float d = hash21(cell + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    const float mode = input.profile.z;
    float3 rgb = input.color.rgb;
    float alpha = input.color.a;

    if(mode > 3.5){
        //Fluidni zeleni mramor pod staklom: svaki panel je jedan quad. Sadrzaj plohe se dijeli
        //na tileove u prostoru piksela, a dvije spore domenski izoblicene noise razine crtaju
        //tok mramora. Sitne fuge i osvijetljeni gornji rubovi daju dojam staklenih plocica.
        const float2 half = input.uvHalf.zw;
        const float radius = min(input.profile.x, min(half.x, half.y));
        const float dist = roundedRectDistance(input.position.xy, input.center, half, radius);
        const float aa = max(fwidth(dist), 1e-3);
        const float shape = clamp(0.5 - dist / aa, 0.0, 1.0);
        const float2 panelOrigin = input.center - half;
        const float2 localPixel = input.position.xy - panelOrigin;
        const float2 tileSize = float2(104.0, 88.0);
        const float2 tileCoord = floor(localPixel / tileSize);
        const float2 tilePixel = frac(localPixel / tileSize) * tileSize;
        const float tileSeed = hash21(tileCoord + float2(7.2, 19.4));
        const float time = input.profile.w;
        //UV je vezan uz cijelu plohu, ne uz tileUv: mramorne zile i mrlje nastavljaju
        //neprekinuto ispod svih plocica umjesto da se uzorak ponavlja u svakoj od njih.
        const float2 p = localPixel / 118.0;

        const float warpX = valueNoise(p * 0.82 + float2(time * 0.020, -time * 0.010));
        const float warpY = valueNoise(p * 0.82 + float2(5.3, 11.7) + float2(-time * 0.012, time * 0.016));
        const float2 flow = p + (float2(warpX, warpY) - 0.5) * 0.72;
        const float slowLayer = valueNoise(flow * 1.10 + float2(time * 0.016, -time * 0.008));
        const float fineLayer = valueNoise(flow * 2.25 + float2(13.1, 4.7) - float2(time * 0.010, time * 0.012));
        const float marble = slowLayer * 0.72 + fineLayer * 0.28;

        const float ribbonField = flow.x * 1.35 + flow.y * 0.92 + marble * 1.65;
        const float ribbon = 1.0 - abs(sin(ribbonField * 3.14159));
        const float vein = smoothstep(0.86, 0.985, ribbon);
        const float veinHalo = smoothstep(0.66, 0.94, ribbon);
        const float pulse = 0.5 + 0.5 * sin(time * 0.22 + tileSeed * 6.283 + marble * 4.0);
        const float causticWave = sin(flow.x * 2.4 - time * 0.075 + marble * 5.0) *
                                  sin(flow.y * 2.0 + time * 0.055 - marble * 4.0);
        const float caustic = smoothstep(0.72, 0.97, causticWave);

        const float pool = smoothstep(0.18, 0.84, marble);
        const float3 deepSage = float3(0.025, 0.043, 0.031);
        const float3 poolSage = float3(0.070, 0.125, 0.080);
        float3 fluid = lerp(deepSage, poolSage, pool * 0.92);
        fluid = lerp(fluid, input.color.rgb, veinHalo * 0.20 * input.color.a);
        fluid += input.color.rgb * vein * (0.045 + pulse * 0.035) * input.color.a;
        fluid += float3(0.23, 0.52, 0.31) * caustic * 0.018;

        //Tanka tamna fuga izmedju plocica; gornji/lijevi rub hvata zeleno stakleno svjetlo.
        const float seamDistance = min(min(tilePixel.x, tileSize.x - tilePixel.x),
                                       min(tilePixel.y, tileSize.y - tilePixel.y));
        const float seam = 1.0 - smoothstep(0.7, 2.2, seamDistance);
        const float topLeft = 1.0 - smoothstep(1.5, 5.0, min(tilePixel.x, tilePixel.y));
        const float bottomRight = 1.0 - smoothstep(1.5, 5.0,
            min(tileSize.x - tilePixel.x, tileSize.y - tilePixel.y));
        fluid = lerp(fluid, float3(0.018, 0.029, 0.022), seam * input.profile.y * 0.30);
        fluid += input.color.rgb * topLeft * 0.035;
        fluid -= float3(0.018, 0.026, 0.019) * bottomRight * 0.16;

        //Refleksija staklene ploce prati zaobljeni rub; usporeni odsjaj na venama se samo
        //mijenja po vremenu i ne zahtijeva blur, dodatnu teksturu ni prolaz kroz framebuffer.
        const float glassRim = 1.0 - smoothstep(1.0, 7.0, -dist);
        fluid += float3(0.018, 0.030, 0.021) * glassRim;
        fluid += input.color.rgb * vein * pulse * 0.035 * input.color.a;
        rgb = fluid;
        alpha = shape;
    }
    else if(mode > 2.5){
        //Obrub: prsten na istoj konturi kao ispuna, debljine profile.y prema UNUTRA. Kao
        //kontura se koristi udaljenost obrisa, pa ugao izlazi bez dvostrukog poklapanja s
        //prozirnim bojama - cetiri trake iz rane verzije su ga crtale dvaput
        const float2 half = input.uvHalf.zw;
        const float radius = min(input.profile.x, min(half.x, half.y));
        const float dist = roundedRectDistance(input.position.xy, input.center, half, radius);
        const float ring = abs(dist + input.profile.y * 0.5) - input.profile.y * 0.5;
        const float aa = max(fwidth(ring), 1e-3);
        alpha *= clamp(0.5 - ring / aa, 0.0, 1.0);
    }
    else if(mode > 1.5){
        //Tekst: cestica tinte iz atlasa, vec izgladjena linearnim filterom - ovdje nema
        //racuna udaljenosti, samo uzorkovanje
        const float cover = atlas.Sample(input.uvHalf.xy).r;
        alpha *= cover;
    }
    else if(mode > 0.5){
        //Ispuna: zaobljeni pravokutnik. fwidth mjeri koliko se udaljenost mijenja po pikselu
        //i daje izgladjivanje koje ne ovisi o mjerilu niti o kutu
        const float2 half = input.uvHalf.zw;
        const float radius = min(input.profile.x, min(half.x, half.y));
        const float dist = roundedRectDistance(input.position.xy, input.center, half, radius);
        const float aa = max(fwidth(dist), 1e-3);
        alpha *= clamp(0.5 - dist / aa, 0.0, 1.0);
    }

    return float4(toLinear(rgb, push.encodeSrgb) * alpha, alpha);
}