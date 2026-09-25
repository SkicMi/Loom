# Loom engine skill za AI Chat

Ovaj dokument je jedini referentni opis Loomovih podataka i akcija za AI Chat. Loom ga pri pokretanju učitava iz docs/LOOM_AI_ENGINE_SKILL.md i cijeli tekst dodaje u system prompt OpenCode agenta.

## Granica ovlasti

Ovaj dokument je znanje o Loomu, a Loomova lokalna allowlista određuje što se smije izvršiti. OpenCode se pokreće kao odvojeni instalirani CLI proces. Koristi OpenCode provider/account konfiguraciju korisnika; Loom ne uključuje licencu, API ključ ili model i ne zaobilazi prijavu, kvote ni ograničenja provider računa. Ako CLI nije instaliran ili dostupan na PATH-u, AI Chat to prikaže i ne pokušava ga preuzeti.

Loom šalje vanjskom procesu tekst korisničke poruke i ovaj engine skill dokument. Ne šalje snapshot scene, odabir objekta, project/source sadržaj ni putanju projekta. Zato ID-jevi za akcije moraju biti navedeni u razgovoru; model ne smije glumiti da zna trenutno stanje scene. Odgovor OpenCodea mora biti JSON oblika {"answer":"...","actions":[]}. Loom prima samo tekst tog odgovora i predložene akcije. Ne izlaže Loom callbackove kao OpenCode alate.

Loom pokreće CLI iz privatne prazne privremene mape. OpenCode alatne dozvole postavljene su na deny; nema shell, source edit, project file, generički filesystem ili proizvoljni Python alat. Nepoznat action naziv, polje, ID ili nevaljan argument odbija Loom. Ne predlaži izravno uređivanje izvornog koda kao Loom akciju i ne tvrdi da je akcija uspjela prije Loomova izvještaja.

Svaki action paket prikazuje se u AI Chat panelu. Ništa se ne mijenja dok korisnik ne klikne APPLY ACTIONS; DISCARD ACTIONS odbacuje paket. Loom validira i izvršava predložene akcije lokalno na UI niti, kroz uski allow-listed dispatcher. Akcije unutar paketa izvršavaju se redom; ako kasnija akcija zakaže, prethodne uspješne akcije ostaju primijenjene i rezultat navodi koliko ih je uspjelo.

## AI action protokol

Ovo su jedine akcije koje Loom trenutačno prihvaća u JSON odgovoru OpenCodea. JSON mora imati točno answer i actions; svaki element actions mora imati točno name i arguments. Paket ima najviše osam akcija. arguments mora biti objekt. Loom odbija nepoznata polja, neispravne tipove i ID-jeve koji ne postoje.

| Action | Argumenti | Ponašanje i provjera |
|---|---|---|
| scene.create | {kind: "cube"|"plane"|"empty"|"camera", name?: string, parent_id?: decimal-ID-string} | Dodaje primitivu na položaj izveden iz aktualnog pogleda, prazan čvor ili kameru iz pogleda. Bez parent_id stvara se u rootu. Opcionalni naziv ima 1–128 ispisivih znakova i Loom ga čini jedinstvenim među braćom. Novi objekt postaje odabran. |
| scene.select | {entity_id: decimal-ID-string} | Odabire samo postojeći entitet. |
| scene.transform | {entity_id: decimal-ID-string, position?: [x,y,z], rotation_deg?: [x,y,z], scale?: [x,y,z]} | Postavlja navedene apsolutne lokalne kanale na aktualnom frameu. Mora biti prisutan barem jedan kanal. Brojevi su konačni i apsolutne vrijednosti do 1,000,000; scale je ne-nula i unutar ±10,000. Warp::Stage::setLocalAt upisuje ključeve kad objekt već ima odgovarajući track. |
| scene.set_visibility | {entity_id: decimal-ID-string, visible: boolean} | Postavlja vidljivost postojećeg entiteta. |
| scene.delete | {entity_id: decimal-ID-string} | Uklanja entitet i sve potomke. Izlazi iz camera viewa ako je izbrisana gledana kamera. Ne briše izvorne asset datoteke. |
| scene.reparent | {entity_id: decimal-ID-string, parent_id: decimal-ID-string|null} | Premješta pod postojeći roditelj ili na root. Loom odbija ciklus; statični transform čuva svjetski položaj, animirani trackovi ostaju lokalni. |
| timeline.seek | {frame: number} | Zaustavlja playback i postavlja frame unutar raspona stagea (vrijednost se ograničava na start/end). |
| timeline.keyframe | {entity_id: decimal-ID-string} | Poziva Stage::keyAll na zaokruženom aktualnom frameu za postojeći entitet. |
| camera.look_through | {entity_id: decimal-ID-string|null} | Ulazi u pogled postojeće kamere; null izlazi iz pogleda kamere. |
| project.save | {} | Poziva postojeći Save handler. Bez project patha koristi Loomov loom_project.usda, zatim brojeve s nastavkom; postojeću početnu datoteku ne prepisuje. |
| scene.undo | {} | Vraća cijeli prethodni Stage snapshot ako ga povijest ima. |
| scene.redo | {} | Vraća cijeli sljedeći Stage snapshot ako ga povijest ima. |

Nema actiona za otvaranje projekta, arbitrary path/import, izvršavanje shell/Python naredbe, source code uređivanje, solve/training jobove, materijale, splat cut ili export. Takve radnje AI može objasniti, ali ih ovaj action protokol ne može izvršiti. Modelu se ne šalje scena ni lista media zapisa, pa mu za scene akcije trebaš dati ID-jeve i ciljne vrijednosti u poruci.

## Model scene i zajednička pravila

- Scena je Warp::Stage: stablo entiteta s trajnim Warp::Id vrijednostima, komponentama i metapodacima projekta. ID se ne reciklira. Entitet može imati djecu; uklanjanje roditelja uklanja i potomke.
- Transformacije su lokalne prema roditelju. Svjetski transform dobiva se kroz hijerarhiju. Statično reparentanje čuva svjetski položaj; animirane key trackove reparentanje ne preračunava.
- Vrijeme je broj kadra (double), uz startFrame, endFrame i framesPerSecond na Stageu. Loom ne tretira frame kao sekundu.
- Solve scena nema poznatu fizičku metriku. glTF navodi metre, a motion modeli rade u metrima; Loom procjenjuje mjerilo iz visine kamere i umjetnik ga po potrebi korigira.
- Većina dugih poslova koristi jedan zajednički background job. Dok job.running vrijedi, drugi solve/train/motion/cleanup/proxy/auto-rig posao ne može krenuti.
- Uspješne promjene scene ulaze u UndoHistory kao snimka cijelog Stagea nakon završetka geste. Viewport kamera, selekcija i buffer rezanja splata nisu dio scene undo-a; cut ima zaseban undo.
- Projektna datoteka sprema Stage; moodboard ima zasebnu pomoćnu datoteku.

## Akcije projekta

### Spremi projekt
UI: Save / Save Project, Ctrl+S. Funkcija: saveProjectNow.
- Ako projekt još nema putanju, odabire loom_project.usda u trenutnoj browser mapi; ako postoji, bira loom_project_2.usda, loom_project_3.usda itd. Ne prepisuje prvi postojeći projekt.
- Warp::saveProject zapisuje cijeli Stage u .usda. Uspjeh sprema i moodboard sidecar, uklanja zastarjeli autosave, osvježava browser i postavlja spremljeni fingerprint.
- Greška vraća poruku Save failed i ne označava scenu spremljenom.

### Otvori projekt
UI: Project > Open ili Importer > Open Project.
- Warp::loadProject prvo učitava u privremeni Stage. Ako učitavanje ne uspije, postojeći Stage ostaje netaknut.
- Pri uspjehu Loom zamjenjuje scenu, učitava moodboard sidecar, poništava selekciju i viewport camera mode, postavlja frame na početak scene, uokviruje scenu i resetira undo history.
- Ako postoji noviji autosave, Loom nudi zaseban Restore Autosave korak. Otvaranje projekta sa nespremljenim izmjenama traži da ih korisnik spremi ili izričito odbaci.

### Nova prazna scena
UI: Project > New Empty Scene.
- Zamjenjuje Stage praznim Stageom, čisti put projekta i selekcije, resetira viewport, vraća frame 1 i resetira undo history.
- Ako postoje nespremljene izmjene, UI nudi Save and Create New Scene ili New Scene Without Saving. Ovo je zamjena scene, ne brisanje jedne stavke.

### Autosave
- Nakon približno 120 sekundi neprekinutih nespremljenih izmjena Loom asinkrono sprema kopiju uz projekt kao .<ime>.autosave.usda. Za projekt bez putanje koristi ~/.local/share/loom/.untitled.autosave.usda (ili privremenu Loom mapu ako HOME nije dostupan).
- Autosave piše kopiju Stagea u .part datoteku pa je preimenuje; ne prepisuje glavni projekt. Pri otvaranju Loom nudi autosave samo kad je noviji od projekta.
- Save uklanja zastarjeli autosave. UI ima Restore Autosave i Discard Autosave.

### Undo / Redo
UI: Ctrl+Z za Undo, Ctrl+Shift+Z ili Ctrl+Y za Redo.
- Vraća cijeli Stage snapshot, ne samo jednu komponentu. Jedna drag/text-edit gesta je jedan korak, ne jedan korak po render frameu.
- Povijest drži najmanje 20 koraka i odbacuje najstarije iznad približno 768 MB.
- Ne vraća viewport orbit, selekciju ni splat cut buffer.

## Mediji, solve i rezultat

### Dodaj snimku u Project Media
UI: Importer > Add to Project Media ili Media > Add.
- Podržani video nastavci u browseru: .mp4, .mov, .mkv, .avi (case-insensitive). Loom sprema putanju i probane dimenzije, broj frameova i fps u Stage media listu. Već dodana putanja se ponovno odabire umjesto dupliciranja.
- Uklanjanje iz projekta uklanja media referencu iz Stagea; izvorna video datoteka se ne briše.
- Solve rezultat je mapa uz snimku: <video stem>_loom, preciznije <video stem>_loom.

### Solve Cameras (Matchmove)
UI: odabrani media zapis > Solve Cameras.
Ulazi: odabrana snimka, frame stride 1/5/10/20 (početno 10), maksimalno 30–600 frameova (početno 231).
- Pokreće ./VideoSolve nad odabranom snimkom u <stem>_loom. Prije pokretanja uklanja zastarjeli napredak.bin iz te mape da se prethodni live preview ne prikaže kao nov.
- Koristi --samo-kamera: rezultat cilja kamere/solve bez izdvojenih slika za splat trening. Posao je asinkron; uspjeh uveze solve rezultat u scenu.
- Import rezultata stvara grupu s animiranom solve kamerom i point cloudom, pokušava poravnati grupu uspravno i postavlja 5. percentil visine točaka na pod y=0. Timeline dobiva broj kadrova kamere. Intrinsics dolaze iz cameras.txt, ili fallback 60° ako nedostaju.
- Ako je video pronađen uz mapu rezultata, kamera dobije video plate. Ako postoji kamera.usda, Loom koristi puni track kamere, raspon i fps iz te datoteke. Ako postoji scena.ply, uveze se i splat kao dijete grupe.
- ImportSolve mijenja cijeli trenutačni Stage dodavanjem rezultata; nije samo pregled.

### Solve + Gaussian Splat
UI: odabrani media zapis > Solve + Gaussian splat.
- Isti ulazi, rezultat i async job kao Solve Cameras, ali ne koristi --samo-kamera. Po završetku solvea pokreće tools/splat/train_splats.py nad izdvojenim slikama i scenom; broj training steps je 1000–30000 (početno 15000).
- Jedan Loom job traje kroz solve/training tijek. Pri uspjehu se solve importira, a trenirani splat dodaje u scenu.
- Potrebni su VideoSolve build i lokalno postavljeni Loom Python/splat dependencies. Greška procesa ide u terminal/job status.

### Train Splat from Result
UI: postojeći media solve result > Train Splat from Result.
- Zahtijeva rezultat s images direktorijem i neaktivan job. Ulaz je result mapa s scena.ply/points i slike; pokreće isti službeni tools/splat/train_splats.py s brojem training steps.
- Izlaz je scena.ply u result mapi; po uspjehu Loom dodaje/veže splat u Stage.

### Import Solve Result
UI: Importer > Solve result > Import Solve Result (camera + points).
- Učitava solve snapshot ili COLMAP datoteke iz odabrane mape. Mapa mora biti prepoznatljiv solve rezultat.
- Ne pokreće novi solve. Dodaje solve grupu u trenutačni Stage, fokusira novu kameru i postavlja timeline/view na taj rezultat.

## Objekti i transformacije

### Cube / Plane / Empty / Camera from View
UI: Scene Atlas +, View > Add, viewport context menu.
- Cube i Plane su Warp mesh primitive; Empty je samo hijerarhijski entitet bez mesh komponente.
- Cube/Plane se smještaju prema zraci kroz sredinu pogleda ili kliknuti piksel. Loom prvo traži rekonstruiranu površinu (solve points ili splat); ako je nema, pokušava pod y=0; ako ni pod nije dosegljiv, koristi sredinu scene. Kocka na podu stoji na podu. Početno mjerilo ovisi o udaljenosti u pogledu: oko 10% za kocku, 40% za plohu.
- Roditeljski ID je opcionalan u engine API-ju. Lokalna pozicija računa se iz svjetske pozicije kroz inverziju roditeljske matrice.
- Camera from View stvara kameru na aktualnom world viewu, s orijentacijom pogleda i focal lengthom izvedenim iz projekcije; osnovne dimenzije su 1920x1080, principal point centar, a kamera počinje na aktualnom frameu.
- Svaki novi entitet postaje selektiran. To mijenja Stage.

### Position / Rotation / Scale
UI: Instrument Deck > Transform i viewport move/rotate gizmo; W pomiče, E rotira.
- Uređuje lokalni transform selektiranog entiteta u aktualnom kadru. Position je Loom scena jedinica; Rotation se prikazuje u stupnjevima; Scale je XYZ. Uniform scale množi sve tri komponente samo kad je faktor pozitivan.
- Ako transform track već postoji, setLocalAt upisuje vrijednost kao key na aktualnom kadru. Animirane joint kosti upisuju key u aktivni Animator clip. Bez tracka vrijednost je statična.
- Brisanje/transformacija roditelja utječe na potomke preko hijerarhije.

### Vidljivost i uklanjanje
- Visible prebacuje prikaz selektiranog entiteta, ne briše ga.
- Delete/Del poziva Stage::remove i briše selektirani entitet zajedno sa svim potomcima. Ako se ukloni kamera kroz koju se gleda, Loom izlazi iz tog camera viewa. Ne briše vanjske izvorne asset datoteke.

### Scene Atlas / parenting
- Stage::create(name,parent) dodaje jedinstveno ime među braćom; Stage::reparent odbija ciklus. Statičan transform očuva svjetski položaj; animirane key vrijednosti ostaju u lokalnom prostoru i ne konvertiraju se automatski.
- ID-jevi su stabilni i nikad se ne recikliraju. scene.* akcije traže decimalni ID, lokalno potvrđuju da objekt postoji i odbijaju nevaljan roditeljski odnos.

## Viewport, kamere i timeline

### Viewport prikaz
- Frame All uokviruje scenu; izlazi iz Look Through Camera prije uokvirivanja. View meniji zasebno uključuju/isključuju point cloud, motion paths i grid. To su view state postavke, ne promjena sadržaja projekta.
- Look Through Camera ulazi/izlazi iz pogleda odabrane kamere. Show Video Plate i Plate Brightness mijenjaju prikaz ploče; brightness je 0–1. Video plate frame prati kameru/timeline.
- Open in Splat Viewer pokreće zaseban Loom preglednik za putanju trenutno odabranog splata.

### Timeline i keyframes
- Play/Pause reproducira unutar stage startFrame/endFrame. |<, <, >, >| skaču na početak, prethodni frame, sljedeći frame, kraj.
- Scrub postavlja frame i zaustavlja reprodukciju.
- Key (K) postavlja sve podržane transform kanale selektiranog entiteta na zaokruženi aktualni frame; Delete Key uklanja ključeve na tom frameu. <K i K> skaču na prethodni/sljedeći susjedni key.
- From/To postavljaju granicu timeline raspona uz zadržavanje najmanje framea prostora. All izračuna raspon iz transform i animator trackova scene.
- timeline.seek i timeline.keyframe dostupni su kroz potvrđeni AI action protokol; playback i Delete Key nisu izloženi AI-ju.

## Modeli, humanoidi i pokret

### Import Model
UI: Importer > Model > Import Model ili odabir modela u browseru.
- Podržani su .gltf i .glb.
- Čita glTF node hijerarhiju, mesh/material slotove i geometriju kao model asset; slike/mesh podaci se kasnije učitavaju rendererom. Stage sprema put do asseta i koji mesh/materijali koristi.
- U praznom Stageu model se uvozi na izvornoj glTF poziciji/rasponu i uokviri se. U postojećoj sceni model se postavlja na pod ispod središta pogleda ili na rekonstruiranu površinu. Procjena razmjera je visina kamere / 1.5 m; zato je samo heuristika, posebno za stativ/dron/kameru nisko pri tlu.
- Greška glTF čitanja vraća Model: <problem>; ne dodaje djelomični model.

### Import as Humanoid / Auto Rig
- Import glTF-a koji već ima skin koristi postojeći skeleton, priprema Animator i selektira uvezeni lik.
- Ako glTF nema skin, import humanoida otvara Auto Rig i pokreće UniRig backend, samo za .glb/.gltf i kad backend tools/autorig/.venv/bin/python + vendor/UniRig/run.py postoji. Dok je drugi job aktivan odbija pokretanje.
- Auto Rig piše novi rezultat u tools/autorig/outputs/rig_<timestamp>; original se ne prepisuje. Završetak traži complete.json; bend preview je zasebna uvozna akcija. Kvalitetu deformacije treba vizualno pregledati.
- Import selected scene model koristi put model asseta. Ako se odabere neskinnani model, bez backend instalacije humanoid uvoz neće krenuti.

### Import Motion (.bvh)
- Za BVH koji Loom prepoznaje kao WeaverMotion/Kimodo clip. Ako Stage nije prazan, novi motion kreće na aktualnom frameu, u vrijeme/fps-u scene i na podu pod središtem pogleda. Mjerilo se procjenjuje iz visine kamere (oko 1.5 m visina oka); bez pouzdane visine koristi fallback iz scene udaljenosti/visine kostura.
- Ako je odabran ili jedini rigged humanoid, clip se može mapirati u njegov Animator. Ako ih je više, korisnik mora odabrati ciljni lik; ne pogađa. Bez ciljnog lika stvara motion group i skeleton iz BVH-a.
- U praznoj sceni klip može usvojiti svoj fps i raspon. U sceni s postojećim timelineom prevodi se iz clip fps-a u scene fps.
- Ne čita generički bilo koji format pokreta; neprepoznatljiv/oštećen BVH daje grešku i ne dovršava import.

### Kimodo tekst-u-pokret
UI: Text to Motion / WeaverMotion panel.
- Zahtijeva lokalni Kimodo runner tools/weavermotion/.venv-clean/bin/python, kimodo_cli.py i instalirani kimodo_gen. Bez njih ne pokreće se.
- Traži barem jedan neprazan prompt/action. Promptovi imaju trajanje; model je 30 fps. Output ide u media folder/WeaverMotion, ili u trenutačnu mapu ako se već zove WeaverMotion; uz BVH se zapisuje .txt sidecar s promptima/trajanjem.
- Authored root waypoint ruta i vanjski constraints JSON su međusobno isključivi. Directed mode traži barem rutu ili pose key, validan raspon klipa i ispravno poredane constraints. Vanjska JSON datoteka mora postojati.
- Dugi job je asinkron i zauzima zajednički Loom job slot. Pri uspjehu generirani BVH se uveze/retargetira na ciljni lik; model inference radi na GPU-u, lokalni tekst encoder na CPU-u.
- Seed, diffusion steps, samples, transition frames, model, text/constraint guidance, foot cleanup/contact IK su generacijske opcije; vrijednosti dolaze iz Motion panela i MotionRequest.

### MotionBricks G1 generiranje
- Zahtijeva tools/motionbricks/.venv/bin/python, generate.py i službene G1 checkpointove. Pokreće se samo ako je job slot slobodan.
- Authored root path i external constraints JSON ne smiju istodobno biti uključeni. Trajanje se zbroji iz action trajanja, svako ograničeno na 1–10 sekundi, pa se cijeli klip ograniči na 1–30 sekundi. Stil dolazi iz MotionBricks style izbora; seed se šalje samo kad je fiksni seed uključen.
- Output je timestamped .bvh u WeaverMotion mapi s .txt sidecarom. Pri uspjehu Loom uveze clip.

### MotionBricks live recording
- Zahtijeva realtime.py, Python okruženje i službene G1 checkpointove, podržani realtime locomotion preset (Idle, Walk, Crawl ili Crouch), slobodan Loom job slot i jedan odabrani rigged humanoid s Animatorom.
- Ruta se validira ako je uključena. Zapis počinje na aktualnom frameu, generira live pose i snima put/seed/IK stanje. Stop traži final BVH; pri uspjehu Loom zamjenjuje privremeni live clip dovršenim clipom i završni BVH ostaje u WeaverMotion mapi.
- Ako lik/Animator nestane ili generator padne, snimanje se zaustavlja i poruka/log objašnjavaju ishod. Live generation mijenja Animator tijekom snimanja; nije samo preview.

## PBR materijali i teksture

### Material panel
Dostupan na selektiranom mesh/model entitetu.
- Svaki primitive modela ima svoj material slot. < / > ciklira kroz biblioteke materijala i Default (-1); New stvara novi materijal ili kopiju trenutno odabranog. Mesh ima jedan slot.
- Uređuje Base Color RGB [0,1], Metallic [0,1], Roughness [0,1], Emission RGB [0,1], Emission Strength [0,20], Alpha Mode Opaque/Mask/Blend, Opacity kad nije Opaque, Mask Cutoff kad je Mask, Double-Sided.
- Texture slotovi: Color, Metal/Rough, Normal, Occlusion, Emission. Choose Image naoruža slot; zatim se klikne slika u media browseru. Remove uklanja mapu. Normal Strength je 0–2, Occlusion Strength 0–1.
- Ne postoji samostalni materijal file format u importeru; materijali se spremaju u projektu, a putanje mapa ostaju putanje izvora.

### Create Material from textures
UI: Importer odabere jednu ili više slika > Create Material, opcionalno + Assign to selected.
- Podržane slike su .png, .jpg, .jpeg, .tga i .bmp. Imena razvrstava po tokenima basecolor/albedo/diffuse/color, normal/nor/nrm, roughness/rough/rgh, metallic/metalness/metal/mtl, ao/occlusion, emissive/emission/glow i orm/arm/metallicroughness.
- Prva datoteka pronađena za ulogu se koristi. Neprepoznata slika postaje base color ako base color nedostaje. Neupotrijebljene slike se ignoriraju.
- Packed ORM/metal-roughness slika postavlja Metallic/Roughness i Occlusion. Ako su roughness/metallic/AO odvojene, Loom ih resampla najbližim pikselom u jednu <material>_orm.png pokraj izvora: R=AO, G=roughness, B=metallic; nedostajući kanali dobiju AO=1, roughness=1, metallic=0.
- Kreirani PNG je vanjski side effect u source direktoriju; ako isti naziv postoji, packer zapisuje target putanju. Pitaj prije akcije u budućem action API-ju ako bi overwrite politika trebala biti drukčija.
- Ako nema usable teksture ili ne uspije packing, importer prikaže problem. Create Material + Assign dodjeljuje novi materijal selektiranom mesh/model entitetu; ostali modeli/primitivi ne mijenjaju se.

## Gaussian splatovi

### Import Gaussian Splat
- Browser prepoznaje .ply samo kad ga isGaussianPly validira kao Gaussian splat. Import stvara Warp Splat entitet s putanjom, sakrije druge splatove u prikazu i selektira novi. Asset datoteka se ne kopira.
- Render Visible/Not visible je view state. Open in Splat Viewer pokreće vanjski Loom preglednik za asset.

### Rezanje splata
Preduvjet: splat je uključen, vidljiv i trenutačno iscrtan u viewportu te nije u stanju učitavanja.
- Add Cut Box doda normalan cube entitet na orbit targetu. Početno mu je veličina iz splat bounds (ili scene extent fallback). Umjetnik ga može pomicati/rotirati/skalirati.
- Na selektiranoj kocki, uz vidljiv učitan splat: Delete Inside uklanja Gaussianse unutar boxa; Delete Outside uklanja one izvan. Broj uklonjenih i unsaved cut count prikazuju se u properties. Cut mijenja viewport splat cut buffer; ne mijenja izvorni splat dok se ne spremi. Undo Cut vraća posljednji cut korak.
- Save Cut Splat upisuje uz izvor <stem>_cut.ply, a ako izvor već završava _cut koristi istu putanju (može ga zamijeniti). Nakon uspjeha mijenja Stage splat path na izlaz, refresh-a browser i reload-a isti asset kad treba. Sačuvati cut obavezno prije clean-floaters akcije.

### Clean Floaters
- Treba splat uz COLMAP solve rezultate: images.txt, cameras.txt, points3D.txt i images/ moraju postojati u istoj mapi. Odbija se dok Loom job traje ili postoje nespremljeni cutovi.
- Pokreće tools/splat/clean_splats.py asinkrono; output je <stem>_clean.ply. Ako source već završava _clean, output je ista putanja i može se zamijeniti. Pri uspjehu Stage splat path se prebaci na clean output.

### Proxy Mesh / Scene Blockers
- Trebaju isti solve sidecar files kao clean floaters i slobodan job slot.
- Make Proxy Mesh (whole scene) generira detaljniji occlusion mesh; Make Scene Blockers generira jednostavne ravnine zida/poda. Box opcije traže selektirani cube i vidljiv učitan splat; koriste box transform u prostoru splata.
- Izlazi su pored splata: _proxy.glb, _proxy_box.glb, _blockers.glb ili _blocker_box.glb; zauzeto ime dobiva _2, _3 itd. Python alate poziva background job.
- Uspješan proxy/blocker postaje dijete splat entiteta. Nije texture/export replikacija splata.

## Importer, filesystem i ograničenja

- Importer pregledava samo otvorenu mapu. Filteri: All, Video, Model, Splat, Solve, Motion, Project, Material. Skriva dotfileove, prikazuje mape pa poznate assete sortirane po vrsti/imenu.
- Ulazak u podmapu/Up/Home/Clips/Project/Refresh samo mijenja browser lokaciju; ne uvozi ništa.
- Video: .mp4/.mov/.mkv/.avi. Model: .gltf/.glb. Splat: validni Gaussian .ply. Project: Warp prepoznati .usda. Solve: Loom prepoznata result mapa. Motion: prepoznati BVH. Material: izabrane raster slike.
- Importer se može otvoriti iz Ctrl+I, menija i browser akcija. Open Project prolazi kroz Project confirmation flow kad treba.
- Nema generičke AI filesystem akcije. U budućem tool API-ju ne izlagati arbitrary path, shell command, arbitrary file write, project source edit ili Python execution. Datoteku birati kroz odobren asset reference i provjeriti vrstu/putanju u Loomu.

## Mapa stvarnih engine/UI handlera

Ovo su trenutačni Loom callbackovi i pomoćne funkcije iza UI-ja. OpenCode ih ne poziva izravno: lokalni dispatcher prihvaća samo eksplicitne JSON akcije iz gornjeg allowlista i poziva odgovarajuće Loom handler-e.

| UI / funkcija | Handler ili engine API | Učinak |
|---|---|---|
| Save | saveProjectNow() → Warp::saveProject | Sprema Stage i moodboard sidecar |
| Open / New | openProject(path), newScene() | Uspješno učita-zamijeni Stage ili napravi prazan Stage |
| Delete selected | removeSelected(id) → Stage::remove | Uklanja entitet i cijelo podstablo |
| Add media | addVideoToProject(path) | Probe metapodataka i dodavanje/deselect duplikata |
| Solve Cameras / Solve + Splat | startSolve(mediaIndex, thenSplat) | Background VideoSolve; opcionalno slijedi splat training |
| Train from Result | startTrain(directory) | Background train_splats.py i dodavanje izlaznog splata |
| Import solve | importFolder(directory, givenPlate) → Loom::importResult | Dodaje kameru, točke, opcionalnu ploču/splat |
| Cube / Plane | addMeshAt(shape, parent, pixel, usePixel), addMesh(shape,parent) | Primitive na zraci/površini/podu |
| Empty | addEmpty(parent) → Stage::create | Prazan hijerarhijski čvor |
| Camera from View | addCameraHere(parent) → Loom::addCameraFromView | Kamera izvedena iz viewport projekcije |
| Import model | importModelAsset(path, viewport) → Loom::importModelAtView | Uvozi glTF/GLB u Stage |
| Import humanoid | importHumanoidAsset(path), startAutoRig() | Postojeći skin se koristi; inače opcionalni UniRig posao |
| Import splat | importSplatFile(path) | Dodaje Warp::Splat referencu i sakrije druge splatove |
| Import/generate motion | importMotion(path,target), startMotionGeneration(), startMotionBricksGeneration(), startMotionBricksLive() | BVH import, Kimodo generiranje, G1 clip ili live take |
| Materials | Loom::materialFromTextures(stage, images), Loom::assignMaterial(stage,id,index), Loom::materialPanel(...) | Novi PBR materijal, dodjela, uređivanje |
| Surface placement | Loom::surfaceToolMouse(...), Loom::placeOnSurface(stage,tool,shape) | Fit plohe iz izabranih točaka i dodavanje objekta na plohu |
| Cut / Clean / Proxy | Loom::ViewportSplat::cutBox/saveCut/undoCut; cleanFloatersCommand; proxyMeshCommand | Buffer rezanja ili background obrada splata |
| Transform/keying | Stage::setLocalAt, Stage::keyAll, Stage::eraseKeysAt | Lokalna transformacija i timeline ključevi |
| Undo / Redo | Loom::UndoHistory::undo/redo | Restore cijelog Stage snapshot-a |

UI callbackovi žive u src/loom_app.cpp i hvataju stanje prozora/job/viewporta. AI nema pristup closureovima ni proizvoljnim UI handlerima. applyAiEngineAction parsira uske tipizirane argumente, provjerava ID-jeve i poziva samo allow-listed Loom funkcije. Novi AI action mora se dodati u JSON prompt, dispatcher i ovaj dokument; ne dodavati generički path, command ili callback action.

## Izvor implementacijskih ugovora

Kada se ovaj dokument mijenja, provjeri stvarno ponašanje u:
- src/loom_app.cpp — UI akcije, preduvjeti, job pokretanje i import side effecti.
- src/LoomOpenCode.h — OpenCode konfiguracija, dozvole i event handling.
- src/LoomScene.h, src/LoomResult.h — solve import.
- src/LoomEditorTools.h, src/LoomModel.h, src/LoomImporter.h — objekti, modeli, teksture i importer.
- src/LoomWeaverMotion.h, src/LoomMotionPanel.h, src/LoomAutoRig.h — motion/rigging ugovori.
- src/LoomSplatCut.h, src/LoomSplat.h — splat cuts, cleaning, proxy output.
- src/LoomUndo.h, src/LoomAutosave.h, warp/src/Warp/Stage.h, warp/src/Warp/Project.h — scene, undo, autosave i projektni persistence.
