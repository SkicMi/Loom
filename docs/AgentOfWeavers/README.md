# AgentOfWeavers

Mali model, treniran iz nule, koji iz teksta piše **WeaverProcedura recept** (graf čvorova), a ne mesh.
Engine izvršava recept; rezultat ostaje proceduralan i editabilan.

```
prompt (hr/en) → zamrznuti višejezični text encoder → embedding
              → AgentOfWeavers (mali decoder, maskirane akcije) → ADD_NODE / CONNECT / SET_PARAM / OUTPUT
              → WeaverProcedura evaluate → mesh + atributi + metrike
```

Polazni dokument: `procedural_ai_pitch.md` (korisnikov pitch). Ovaj projekt je dogovorena kontra-verzija.

## Dogovorena načela

1. **Prvo knjižnica, onda trening.** Model ne može naučiti "kuću s krovom" dok engine ne zna napraviti krov.
2. **AI radi na srednjoj razini čvorova** (`Footprint`, `Roof(type)`, `Openings(count)`), ne na indeksima trokuta.
   Čvorovi niske razine (Extrude po faceIndexu, pojedinačna visina točke) ostaju za ljude.
3. **Provenance u `MeshData`**: svaki trokut nosi `createdBy` (čvor), `semantic` i `material`. USD izvoz kasnije iz istih atributa.
4. **Materijale model bira, ne generira.** Knjižnica PBR materijala s ponavljanjem, triplanar/box UV u metrima.
5. **Opisi**: predlošci iz parametara (uvijek točni) + parafraza; ~200 ručno pisanih promptova kao jedini pravi held-out test.
6. **Fail nije otpad**: (pokvaren recept + razlog + popravak) postaje primjer za učenje popravka.
7. **Mali model prvo**: 3–5M parametara; skaliranje samo ako krivulje učenja traže.
8. Stari Loom Agent (Qwen3.5-4B + LoRA, F8 chat, `tools/weaveragent`) je uklonjen 2026-09-26.
   Host sloj `src/LoomAgentActions.h` (whitelist + provjera akcija) je zadržan za novi model.

## Generator podataka (petlja sa sheme)

```
PROCEDURA (sampler iz gramatike + curriculum)
   ↓
lista API poziva (ADD_NODE / CONNECT / SET_PARAM / OUTPUT)
   ↓
generiranje: headless evaluate → mesh + atributi + metrike (+ render za 1–5 %)
   ↓
vizualna validacija + validacija pravila
   ├─ Pass → uzorak u dataset, statistika natrag sampleru
   └─ Fail → ZAŠTO? (strukturirani razlog) → Retry (popravi / ponovno uzorkuj) → PROCEDURA
```

Uzorak = `prompt + recept kao niz akcija + metrike + seed + generator_version`; mesh se ne sprema (reproducibilan).

## Faze

| # | Faza | Stanje |
|---|---|---|
| 0 | Projekt, uklanjanje starog agenta | gotovo |
| 1 | Infrastruktura: Merge, atributi/provenance, selekcija, UV/normale, materijali, Copy-to-Points | u tijeku |
| 2 | Čvorovi: kuća/zgrada, uže, lanac, cesta (+ testovi) | čeka |
| 3 | `ProceduraGen` (headless) + validatori + petlja Pass/Fail/Why/Retry, 10k uzoraka | čeka |
| 4 | Ručni held-out skup ~200 promptova (korisnik) | čeka |
| 5 | Model 3–5M, zamrznuti encoder, legality masking; metrike validnosti i točnosti | čeka |
| 6 | Skaliranje, kontrastni parovi za uređivanje, vizualni evaluator | čeka |

Dnevnik svih akcija (prošle, sadašnje, buduće): [AKCIJE.md](AKCIJE.md).
Stanje čvorova i što im fali: [CVOROVI.md](CVOROVI.md).
