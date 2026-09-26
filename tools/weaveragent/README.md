# Loom Agent: lokalni runtime i trening

Loom Agent je lokalni Qwen3.5-4B s LoRA adapterom za upravljanje Loom scenom iz chata. Korisnik opiše zadatak na hrvatskom ili engleskom; model bira samo iz opisanih radnji, a aplikacija provjerava argumente i izvršava ih na glavnoj niti editora. Izvorni kod i dokumentacija dostupni su kroz lokalno pretraživanje projekta, tako da model dobiva aktualni Loom kontekst bez pokušaja da zapamti cijeli repozitorij u težinama.

Loom Agent v2 exposes a bounded scene-action API. WeaverProcedura now has a versioned Recipe graph, editable XZ grids, chained point-height operations, grid-to-mesh evaluation, and a hallway/rooms blockout preset. Those procedural operations are available in the editor and C++ API; the v2 model action protocol does not yet execute Recipe edits.

## Model, licenca i hardver

- Bazni model: Qwen/Qwen3.5-4B, revision 851bf6e806efd8d0a36b00ddf55e13ccb7b8cd0a.
- Lokalni bazni model: .cache/weaverprocedura/models/qwen3.5-4b.
- Apache-2.0 licenca i obavijesti o modelu spremljene su uz lokalni model; zadrži ih pri redistribuciji baznih težina ili spojenog modela. Adapter je LoRA dodatak treniran nad tom fiksnom verzijom modela.
- Potvrđen hardver: GeForce RTX 5070, 12,227 MiB, SM120.
- Runtime koristi izolirani tools/weaveragent/.venv, PyTorch s CUDA 12.8 i BF16.
- Trening: LoRA rank 16, alpha 16, batch 1, grad accumulation 8, kontekst 512, tri epohe i 8-bitni optimizer. Bazne težine nisu bile kvantizirane tijekom treninga; inferencija učitava 4-bitni model kako bi ostalo više VRAM-a za viewport.
- Model i adapter nalaze se u lokalnom, Git-ignoriranom .cache direktoriju. Loom ih koristi preko lokalnog Python servisa; težine nisu ugrađene u izvršnu LoomDesk datoteku.

## Trenirani adapter v2

Aktivni adapter je .cache/weaverprocedura/adapters/qwen3.5-4b-loom-v2. Trening je dovršio tri epohe nad 888 ručno pregledanih primjera; validacijski split ima 12 primjera. Skup je dvojezičan i sadrži radnje scene, kombinirane zahtjeve, odbijanja i slučajeve u kojima agent mora tražiti pojašnjenje. Nije korišten third-party instruction korpus.

Evidentirani rezultati na RTX 5070:

- 714.7 sekundi treninga; train loss 0.1715; tri dovršene epohe.
- Osnovni izdvojeni skup: 12/12 točnih akcija.
- Prošireni izdvojeni skup s 42 različita slučaja: 42/42 točne akcije.
- API protokol bio je valjan u 42/42 slučaja; prosječno vrijeme odgovora oko 1.96 s, p95 oko 2.36 s.
- Mjerenja se odnose na strukturirane radnje u tim skupovima; ne znače da je agent već naučen raditi WeaverProcedura.

Podaci su verzionirani u tools/weaveragent/data/v1 i tools/weaveragent/data/v2. Za buduće treniranje zadrži sve prethodno prihvaćene primjere, dodaj novu verziju skupa, provjeri je odvojeno i treniraj novi adapter od istog fiksiranog baznog modela. Usporedi iteracije evaluacijom kako bi smanjio zaboravljanje ranijih Loom naredbi.

## Priprema i ponovno treniranje

Za ponovnu izgradnju v2 skupa i trening:

    python3 tools/weaveragent/build_dataset.py --version v2
    tools/weaveragent/.venv/bin/python tools/weaveragent/validate_dataset.py \
      --input tools/weaveragent/data/v2/train.jsonl \
      --eval tools/weaveragent/data/v2/eval.jsonl \
      --tokenizer .cache/weaverprocedura/models/qwen3.5-4b
    tools/weaveragent/.venv/bin/python tools/weaveragent/index_project.py
    tools/weaveragent/.venv/bin/python tools/weaveragent/train.py \
      --data-dir tools/weaveragent/data/v2 \
      --output .cache/weaverprocedura/adapters/qwen3.5-4b-loom-v2
    tools/weaveragent/.venv/bin/python tools/weaveragent/evaluate_model.py \
      --adapter v2 --eval tools/weaveragent/data/v2/eval.jsonl
    tools/weaveragent/.venv/bin/python tools/weaveragent/evaluate_model.py \
      --adapter v2 --eval tools/weaveragent/data/v1/eval_extended.jsonl

Prije GPU treninga može se pokrenuti train.py s --dry-run za provjeru renderiranja podataka. Trening zapisuje checkpoint na kraju svake epohe, a evaluacija se pokreće zasebno kako bi se izbjegla velika alokacija full-vocabulary logits tijekom BF16 treninga. Manifest čuva broj primjera i SHA-256 kontrolne sume.

## Pokretanje u Loomu i API

Pokreni LoomDesk i pritisni F8 za AI Chat. Prvi upit učitava lokalni model; API bridge pokreće servis ako još nije aktivan. Chat prikazuje viewport i razgovor, dok su ostali editor paneli skriveni iz radnog prikaza.

Lokalni servis veže se samo na 127.0.0.1:8765:

- GET /health — status modela i lokalnog indeksa.
- GET /v1/tools — podržane radnje i sheme.
- POST /v1/agent/turn — poruka i neobavezni kontekst razgovora; vraća tekst i provjerene radnje.

Za ručno pokretanje:

    tools/weaveragent/.venv/bin/python tools/weaveragent/serve.py

Primjer JSON zahtjeva:

    {"message":"Dodaj kocku Block na (0, 1, 0)."}

Trenutačni agent alati: `scene.list_entities`, `scene.create_primitive`, `scene.set_transform`, `scene.rename`, `scene.set_visibility`, `scene.delete`, `timeline.set_playhead`, `viewport.frame_entity` i `procedura.create_recipe`. Procedura alat trenutačno izrađuje grid površinu s opcionalnim visinama točaka ili tlocrt hodnika i soba. Preview se prikaže u viewportu, a graf ostaje u Procedura panelu dok ga korisnik ne spremi. Brisanje traži potvrdu u chatu. API nema CORS, udaljeni bind ni alat za izvršavanje shell naredbi.

API predlaže strukturirane radnje, a Loom host ih provjerava prema whitelisti i tipiziranim argumentima. Samo host izvršava izmjene na niti editora; stvarno stanje scene vraća se u odgovor. Lokalni indeks služi kao retrieval kontekst za aktualni kod i dokumentaciju.

## API dokumentacija

- [Loom Agent API, alati i scene-context format](../../docs/WEAVER_AGENT_API.md)
- [WeaverProcedura Recipe, node ports, JSON format i Engine API](../../docs/WEAVER_PROCEDURA_API.md)

Treniranje na proceduralnim Recipe izmjenama još nije počelo. Prije sastavljanja podataka treba izmjeriti početnu točnost `procedura.create_recipe` tool callova, zatim skup proširiti na sigurno editiranje postojećih Recipea i napraviti held-out action benchmark.


## WeaverProcedura vocabulary and benchmark

The current v3 vocabulary describes Curve → Rectangle Profile → Sweep, grid deformation, Grid to Mesh, versioned Recipe files, and the hallway/rooms blockout. It keeps the v2 40-query report frozen for historical comparison; v3 needs its own live-model baseline.

Each vocabulary entry has at least one Croatian and one English model test. Run the deterministic alias/schema checks with:

    python3 -m unittest discover -s tools/weaveragent/tests

Run the live local-model baseline with:

    tools/weaveragent/.venv/bin/python tools/weaveragent/evaluate_procedura_vocabulary.py

The vocabulary evaluator records a result for each term and language, required-fact coverage, any unexpected action, latency, adapter/dataset identity, and vocabulary hash. See [the v3 benchmark definition](../../docs/WEAVER_PROCEDURA_VOCABULARY.md). It tests terminology and source-grounded explanations; the separate action benchmark below measures chat-driven Recipe creation.

Measure real tool-call selection and arguments independently:

    tools/weaveragent/.venv/bin/python tools/weaveragent/evaluate_procedura_actions.py

That held-out suite covers grid/interior creation, requested parameters, unsupported geometry, ambiguity, and unsupported Recipe editing. Its report records schema/prompt/adapter hashes, raw model action accuracy separately from deterministic host recovery, final tool/argument accuracy, unexpected actions, and latency under `.cache/weaverprocedura/benchmarks/procedura-actions-v1.json`. The frozen comparison point is [procedura-actions-baseline-v1.json](data/benchmarks/procedura-actions-baseline-v1.json); it identifies the model and evaluation inputs by hash. The suite is intentionally small, so treat it as a regression check, not a broad intelligence score.

### Comprehensive action and chat regression

Run the full prompt set across all nine Loom tools, including the v1/v2 held-out actions, eight Procedura action prompts, and grounded chat questions:

    tools/weaveragent/.venv/bin/python tools/weaveragent/evaluate_agent_turns.py \
      --output .cache/weaverprocedura/benchmarks/agent-comprehensive-v1.json

The evaluator supplies only explicitly mentioned scene entities (and selected-object data when needed), mirroring the request-scoped context sent to the model. To exercise the current checked-out host recovery rules over replies from an already-running model service, add `--apply-current-host-guard`; normal Loom startup applies those rules inside the service itself. The report records API actions before the local guard, final actions, guard recoveries, per-tool scores, prompt/source hashes, and latency. For requests targeting `selected`, an action using the live `selected_path` is counted equivalent to that symbolic target; another entity path never is. This benchmark intentionally reports raw misses instead of hiding them behind deterministic recovery.

### Training time and what it means

The v2 run took 714.7 seconds because it was a small supervised adapter run: 888 examples across three epochs, batch size 1 with accumulation 8, or about 333 optimizer steps. LoRA updated 21.2 million of 4.56 billion parameters (0.47%); the longest rendered example was 438 tokens. Qwen was already pretrained, so this run adapted response format and Loom action patterns instead of learning language or engine code from scratch. A short run is expected and is not evidence of broad procedural reasoning. Adding more hours to the same small set could overfit it; expand reviewed examples and evaluate against a frozen held-out suite instead.
