//Suicelje: cetverokuti u zaslonskim koordinatama, koje fragment shader oblikuje.
//
//Svaki cetverokut nosi "rezim" u vrhu (mode):
//  0 ravan, bez ikakvog racuna - scena u pogledu editora (tocke, duzine, stranice kocke)
//  1 ispuna: ZAOBLJENI pravokutnik s izgladjivanjem ruba, polumjer u profile.x
//  2 tekst: kvadratic tinte iz atlasa (mode se uzorkuje u profile.x)
//  3 obrub: prsten unutarnje debljine (profile.y) oko iste konture kao ispuna
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

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    const float mode = input.profile.z;
    float3 rgb = input.color.rgb;
    float alpha = input.color.a;

    if(mode > 2.5){
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