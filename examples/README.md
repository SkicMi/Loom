# Ciljni primjeri - specifikacija stepenice 1

Ovi fileovi su napisani **prije** biblioteke koja ih podrzava. Oni su specifikacija:
API stepenice 1 je gotov onda kad se ovo prevede, pokrene, i da istu sliku kao rucno
slozena verzija na stepenici 2.

Zato **jos nisu u buildu**. Kad budu, `examples/` postaje meta i svaki od njih dobiva
test koji ga usporeduje s ekvivalentnim programom na stepenici 2.

## Tri stepenice

| stepenica | sto vidis | za koga |
|---|---|---|
| 1 - preset | `Loom/Loom.h` i nista drugo. Nijedan Vulkan simbol | najcesci slucajevi, najbrzi put do slike |
| 2 - configs | `LoomConfig`, `PipelineConfig`, `RenderTargetConfig`, `ShadowConfig`, `RenderTarget`, `Material` | kad preset ne pogada, ali Loomov model odgovara |
| 3 - Vulkan | `VulkanDevice`, `VulkanBuffer`, `VulkanImage`, `vk::raii` | kad Loom jos ne pokriva ono sto ti treba |

Stepenice se **kombiniraju**, ne biraju. Preset ispunjava config; ti ga smijes dirnuti
prije nego se upotrijebi:

```cpp
#include <Loom/Preset_Advanced.h>   // tek ovaj header povlaci Vulkan

Loom::Scene scene(Loom::Preset::Lit3D, [](LoomConfig& config){
    config.rendererConfig.maxLights = 64;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
});
```

To je ono sto drzi stepenicu 1 malom dok Loom raste: **svako novo polje u bilo kojem
configu je overridable onog dana kad ga dodas, a stepenica 1 ne naraste ni za jedan
simbol.** Sto se tice zivih objekata, isti header daje `scene.loom()`.

## Sto je vec bez Vulkana

`Camera`, `Light`, `Environment` i `MaterialData` nemaju nijedan `vk::` simbol, pa ih
stepenica 1 izlaze **izravno** umjesto da im izmislja dvojnike. Zato u primjerima stoji
`scene.camera().lookAt(...)` - to nije bijeg na nizu stepenicu, to je stepenica 1.
