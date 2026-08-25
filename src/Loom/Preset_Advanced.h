#pragma once
//=============================================================================================
//  Vrata sa stepenice 1 na stepenicu 2.
//
//  Ovaj header NAMJERNO povlaci Vulkan. To nije propust nego cijela svrha: <Loom/Loom.h> je
//  cist i to se mjeri testom, a tko treba config ili zive objekte ukljuci ovaj i time
//  svjesno side stepenicu. Dva zahtjeva koja se na prvu iskljucuju - "stepenica 1 ne vidi
//  Vulkan" i "sa stepenice 1 se moze sici" - zajedno vrijede samo ovako.
//
//  Test "tier1_advanced_is_dirty" trazi da ovdje Vulkan STVARNO bude, jer vrata koja nikamo
//  ne vode izgledaju isto kao vrata koja vode.
//=============================================================================================
#include "Loom.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include <functional>
#include <utility>

namespace Loom{

//Preset ISPUNJAVA config; ovo je trenutak izmedu toga i njegove upotrebe.
//
//Zato stepenica 1 ne mora imati setter za svaku Loomovu mogucnost: svako polje koje bilo
//koji config ikad dobije je overridable onog dana kad ga dodas, a <Loom/Loom.h> ne naraste
//ni za jedan simbol. To je jedini nacin da preset ostane malen dok biblioteka raste
class ConfigOverride{
    public:
    ConfigOverride() = default;

    //Prima sto god se da pozvati s LoomConfig&, pa se lambda na pozivnom mjestu pretvara
    //sama i korisnik nikad ne izgovori ime ovog tipa
    template<typename Callable>
    ConfigOverride(Callable&& callable) : apply(std::forward<Callable>(callable)) {}

    void operator()(LoomConfig& config) const {
        if(apply) apply(config);
    }

    bool isSet() const {return static_cast<bool>(apply);}

    private:
    std::function<void(LoomConfig&)> apply;
};

}
