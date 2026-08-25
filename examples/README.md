# Ciljni primjeri - specifikacija stepenice 1

Ovi fileovi su napisani **prije** biblioteke koja ih podrzava. Oni su specifikacija:
API stepenice 1 je gotov onda kad se ovo prevede, pokrene, i da istu sliku kao rucno
slozena verzija na stepenici 2.

Od koraka 2 se **grade s ostatkom projekta**, i linkaju **samo `LoomPreset`**. To je
namjerno: primjer koji bi morao linkati `Loom` ili `Spool` izravno bio bi primjer koji je
sisao stepenicu, a da to nigdje ne pise. Ne pokrecu se kao testovi jer traze assete i
prozor - ali primjer koji se ne prevodi je specifikacija koja laze, pa se prevodi provjerava.

Ekvivalentnost sa stepenicom 2 dokazuju `tests/test_tier1_preset.cpp` (Offscreen) i
`tests/test_tier1_presets.cpp` (Lit3D, Flat2D, `Loom::Sequence`): isti prizor napisan oba
puta, i slika mora biti **bajt za bajt** ista. Tier 2 polovica svakog od tih testova je
ujedno i dokumentacija - to je ono u sto se preset razvija.

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
