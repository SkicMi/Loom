# Weaver Agent vocabulary v2

Ovaj vokabular povezuje hrvatske i engleske izraze s tipiziranim Loom operacijama. To je dogovor protokola i podataka za adapter, a ne novi tokenizer ili skriptni jezik za korisnika. Engine je mjerodavan za značenje i stanje.

| Korisnički izraz | Kanonsko značenje u Loomu | v2 radnja |
|---|---|---|
| scena, scene, stage | Hijerarhija Warp::Stage | scene.list_entities |
| objekt, entitet, object, entity | Imenovani Warp::Entity u sceni | Putanja poput /World/Block ili selected kad je odabir izričit |
| roditelj, parent | Putanja roditelja; null znači korijen scene | scene.create_primitive.parent |
| kocka, cube | Jedinična kocka centrirana na lokalnom ishodištu | scene.create_primitive |
| ravnina, plane, pod | Ravnina u XZ | scene.create_primitive |
| položaj, lokacija, position, translation, pomak | Lokalna translacija u jedinicama scene | scene.set_transform.translation |
| rotacija, rotation | Lokalni Euler kutovi u stupnjevima; host ih pretvara u kvaternion | scene.set_transform.rotation_degrees |
| mjerilo, scale, size | Pozitivno lokalno mjerilo; size se koristi samo kad je skala izričita | scene.set_transform.scale |
| kadar, frame | Broj kadra na timelineu | timeline.set_playhead |
| pogled, viewport, uokviri, focus | Uokviravanje entiteta u viewportu | viewport.frame_entity |
| prikaži/sakrij, show/hide, visible | Vidljivost entiteta | scene.set_visibility |
| preimenuj, rename | Ime; Loom osigurava jedinstvenost među susjednim entitetima | scene.rename |
| obriši, delete, remove | Uklanja entitet i potomke | scene.delete uz potvrdu korisnika |

## Granice v2

Agent radi samo kroz ograničeni action API. Nepoznati alati, pogrešni tipovi, ne-finite brojevi, preveliki batch zahtjevi i transformacije izvan granica odbijaju se. Dodatna provjera traži vrijednosti koje nedostaju, ne pogađa nedovoljno određene zahtjeve i provjerava odgovara li predložena radnja korisnikovoj namjeri.

v2 ne stvara ulice, zgrade, krivulje, užad, lance, proceduralne grafove, profile mreže ni materijale. Te mogućnosti postaju dostupne tek kad dobiju verzioniranu shemu, native executor, viewport kontrole, primjere i evaluaciju.

## API odgovor

Odgovor sadrži Loom Agent action objekt verzije 1, kratak tekst i do 16 tipiziranih radnji. Host pretvara validirane podatke u typed executor u src/LoomAgentActions.h i poziva ga na niti editora. Brisanje se može predložiti, ali se ne izvršava dok ga korisnik ne potvrdi.
