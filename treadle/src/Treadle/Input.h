#pragma once
#include <cstdint>

//Treadle je UI sloj. Kao i Spool, ne zna nista o Loomu, nista o Vulkanu i nista o GLFW-u.
//
//Smjer ovisnosti je aplikacija -> Treadle i aplikacija -> Loom, nikad izmedju njih. Zato
//ulaz dolazi kao obican zapis brojeva, a izlaz odlazi kao obicni vrhovi (vidi Draw.h) - cim
//bi Treadle primio GLFWwindow* ili vratio Loomov Mesh, granica "sto korisnik radi" / "kako se
//to crta" bi nestala i UI se vise ne bi dao testirati bez prozora.
//
//A bas to je razlog zasto granica postoji: cijeli raspored, pogadjanje misem i racun klizaca
//provjeravaju se u testu koji ne otvara ni prozor ni karticu.
namespace Treadle{

//Koliko gumba misa Treadle razlikuje. Srednji je tu jer je u Blenderu glavni gumb za pogled,
//pa ga UI mora znati propustiti dalje
enum class MouseButton : uint32_t{
    Left = 0,
    Right = 1,
    Middle = 2,
    Count = 3
};

//Stanje ulaza u ovom kadru. Aplikacija ga puni iz onoga sto njezin prozor zna.
//
//POLOZAJ JE U PIKSELIMA, ishodiste gore lijevo - isto kako ga daju i GLFW i Vulkanov
//viewport, pa nema pretvorbe koja se moze zaboraviti.
//
//GUMB JE STANJE, NE DOGADJAJ: ovdje stoji je li pritisnut sada. Treadle sam pamti kako je
//bilo prosli kadar i iz razlike izvodi pritisak i otpustanje. Da aplikacija salje dogadjaje,
//morala bi ih ona skupljati i redati - a onda bi red dogadjaja bio dio ugovora
struct Input{
    float mouseX = 0.0f;
    float mouseY = 0.0f;

    bool down[uint32_t(MouseButton::Count)] = {false, false, false};

    //Koliko se kotacic okrenuo OD PROSLOG KADRA. Aplikacija ga skuplja i nulira sama, jer
    //GLFW ga javlja dogadjajem a ne stanjem
    float wheel = 0.0f;

    bool shift = false;
    bool ctrl = false;
    bool alt = false;

    bool isDown(MouseButton button) const {return down[uint32_t(button)];}
};

}
