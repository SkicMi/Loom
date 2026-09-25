# Animator UX — To-do lista

Redoslijed rada: dovršiti jednu stavku prije početka sljedeće.

## 1. Vođeni Create / Direct / Review tok — dovršeno

- [x] Zamijeniti nejasne workflow nazive s **Create**, **Direct** i **Review**.
- [x] Create zadržava prompt i preset tok; Direct zadržava uređivanje putanje i poza.
- [x] Review prikazuje spremljene takeove i postojeću usporedbu Source BVH / Rig bez IK / Rig s IK.
- [x] Sakriti putanju i pose constraint lane u Review kako bi timeline ostao usredotočen na animaciju.
- [x] Sačuvati zasebne Create i Direct putanje pri prebacivanju modova.

## 2. Jedinstveni timeline za animaciju — dovršeno

- [x] Uskladiti trajanja akcija, root path waypointe i pose keyeve s kadrovima scene na istoj vremenskoj skali.
- [x] Urediti trajanje akcije povlačenjem njezina desnog ruba; pomicati waypoint i pose key povlačenjem.
- [x] Označiti aktivnu akciju, odabrani waypoint i odabrani pose key te zasebno imenovati trake.
- [x] Odvojiti scrub traku od motion traka i zadržati vlasništvo nad gestom do otpuštanja miša.

### 2a. Overwrite Pose na aktualnom kadru — dovršeno

- [x] Pokrenuti reverzibilno uređivanje poze na aktivnom Animator clipu i kadru.
- [x] Prikazati Control Rig u viewportu tijekom Edit Pose sesije i zadržati ga pri odabiru pojedinih kostiju.
- [x] Omogućiti promjenu kostiju Control Rig, viewport i Transform kontrolama.
- [x] Dodati **No Blend** za izravan pose key i **Smart Blend** s podesivim ulazom, zadržavanjem i izlazom (u kadrovima). Na odabranom kadru čuva točnu pozu, a nakon izlaza vraća izvorni motion.
- [x] Dodati Cancel koji vraća izvorni clip.

## 3. Lakše rig kontrole — na čekanju

- [ ] Grupirati kontrole po namjeri (ruke, noge, trup, glava), s razumljivim nazivima.
- [ ] Dodati vidljive pin/lock, mirror i neutral/reset radnje.
- [ ] U viewportu jasno pokazati aktivnu kontrolu i utjecaj constrainta.
- [ ] Očuvati postojeće rig i animation podatke tijekom izmjena.

## 4. Pregled kvalitete i popravci — na čekanju

- [ ] Prikazati konkretne probleme (kontakt stopala, klizanje, doseg zglobova, skokovi u putanji).
- [ ] Za svaki problem ponuditi razumljivu korektivnu radnju.
- [ ] Omogućiti A/B pregled izvora, retargetinga i popravljenog rezultata.
- [ ] Sačuvati originalni take i jasno odvojiti popravljenu verziju.

## 5. Biblioteka poza i prijelaza — na čekanju

- [ ] Spremati odabrane poze i prijelaze kao ponovo upotrebljive elemente.
- [ ] Omogućiti pregled, preimenovanje i primjenu na aktivni rig.
- [ ] Uvesti osnovne kolekcije za Idle, Walk, Run, Jump, Crawl i Crouch.
- [ ] Omogućiti kombiniranje spremljenih elemenata u jedan take.
