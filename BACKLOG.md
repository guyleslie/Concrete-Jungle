# Backlog

Fontossági sorrendben. Egy session = egy téma: előbb rövid leírás a célról (mérhető számokkal),
hozzá `--shot` teszt-forgatókönyv, megvalósítás, próbajáték, hangolás. Ha kész, kerüljön a „Kész” részbe.

## 1. Dokumentáció felülvizsgálata
- README.md, CREDITS.md, a forrásfájlok fejléc-kommentjei és az `assets/data/*.cfg` kommentjei összevetve a kóddal.
- A README ütközésfizika-része 2026-09-27-én frissült; a többit azóta nem ellenőriztük.
- Ami elavult, javítani; ami hiányzik, pótolni.

## 2. Járműkezelés (irányíthatóság)
- Visszajelzés: az autók nem elég jól irányíthatók, a kezelés matematikája nem jó.
- Jelenleg arcade modell (`VehicleForces`, vehicle.cpp): a kormány egy cél-fordulási sebességet ad,
  az oldalirányú tapadás exponenciális csillapítás. A `vehicles.cfg` gyorsulás- és fékértékei a
  valóságos 4–9-szeresei (pl. fék 90 m/s²).
- Javaslat: bicikli-modell (első/hátsó tengely csúszási szöge, tapadási görbe, súlyátvitel),
  paraméterek a `vehicles.cfg`-ben.
- Teszt ötletek: fékút 100 km/h-ról, 0–100 idő, körpálya (oldalgyorsulás), szlalom, kézifékes fordulás.
- Kapcsolódó konstansok: `SLIDE_DECEL`, `YAW_AUTHORITY` (vehicle.cpp) – az ütközés utáni csúszást is ezek adják.

## 3. Sérülésmodell
- Visszajelzés: nem kiforrott, mikor és hogyan megy tönkre egy autó.
- Jelenleg: egyetlen életerő érték; a sérülés a becsapódás Δv-jéből jön (`DV_*`, game.cpp);
  füst 50% alatt, tűz 0-nál, robbanás 3–5 s múlva; nincs látható horpadás.
- Ötletek: alrendszerek (motor, karosszéria, kerekek/defekt, lámpák), látható kár (horpadás,
  törött lámpa/üveg), fokozatos teljesítményvesztés, a kigyulladás szabályai, roncs; a sofőr
  sérülése ütközésnél (most `DV_INJURY`).

## 4. Egyéb nyitott pontok
- Grafika: a motorok és a civil gyalogosok procedurális helyettesítő sprite-ok – jobb, egységes
  minőségű asset kell (itch.io-t kézzel kell böngészni, a bot-ellenőrzés miatt).
- Ütközésfizika apróságai:
  - kaparás: most csak szikra + rövid `CrashSmall` hang, nincs folyamatos kaparás-hang;
  - falnak tolt autónál gázadásra nincs kerékkipörgés-füst;
  - a kidőlt oszlop csak dekoráció, nem akadály;
  - a szemeteskonténer merev – lehetne tolható test;
  - a 12 m-es busz kilökés után nehezen tér vissza a sávba (12 s után feladja);
  - a Δv-alapú sérülés egyensúlya játékban még nincs kipróbálva.
- Teszt-autopiloták: a `drive` autopilóta nem tolat ki, ha beragad.

## Kész
- 2026-09-27 – Ütközésfizika újraírva (`physics.cpp`): a beragadás és a helyben ugráló sprite
  megszűnt; letörhető utcabútorok; a kilökött AI-autók visszatérnek a sávba; `crash`/`derby` tesztek.
