//Suicelje: obojani trokuti u zaslonskim koordinatama.
//
//Nema teksture i nema descriptora. Slovo je skup pravokutnika (vidi Weaver/Draw.cpp), pa je
//sve sto ovaj cjevovod treba jedan vrh s bojom i velicina slike u push constantu.
//
//PRETVORBA IZ PIKSELA JE OVDJE, NAMJERNO. Weaver racuna raspored i pogadjanje misem u
//pikselima; da ih on pretvara u NDC, isti bi se racun radio na dva mjesta i mogao bi se
//raziici. Ovako postoji jedna definicija "gdje je nesto", a shader je samo pretvara.
struct Push{
    float2 screenSize;

    //1 kad meta nosi sRGB format. Vulkan tada ono sto shader vrati SAM pretvara u sRGB, pa
    //broj 0.20 zavrsi na ekranu kao 0.48 - suicelje izadje isprano i svjetlije nego sto je
    //napisano. Boje u Weaver::Theme su zapisane onako kako trebaju IZGLEDATI, pa se ovdje
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

[[vk::push_constant]] ConstantBuffer<Push> push;

struct VSInput{
    [[vk::location(0)]] float2 position;   //pikseli, ishodiste gore lijevo
    [[vk::location(1)]] float4 color;
}

struct VSOutput{
    float4 position : SV_Position;
    float4 color : COLOR;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input){
    VSOutput output;

    //Vulkanov NDC ima +Y prema DOLJE, isto kao pikseli, pa se Y ne okrece. Da se okrene,
    //suicelje bi bilo zrcaljeno okomito i pogadjanje misem bi promasivalo za visinu plohe
    output.position = float4(input.position / push.screenSize * 2.0 - 1.0, 0.0, 1.0);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target{
    return float4(toLinear(input.color.rgb, push.encodeSrgb), input.color.a);
}
