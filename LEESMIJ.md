# FM-1 STUDIO: firmware voor de M-VAVE FM-1

Een Roland **Alpha Juno**-synth, een **TB-303**, een **TR-909**-drumcomputer en een **Mackie**-kanaalstrip (gain en EQ)
op elke track. Gebouwd op [Felucca](https://github.com/hugelton/Felucca) 1.6.1.

Je hoeft geen programmeur te zijn om dit te bouwen. Je kopieert een paar regels, plakt ze en drukt op Enter. Hieronder
staat precies wat.

---

## Wat zit erin

De FM-1 heeft 4 tracks. Op elke track kies je een van deze drie engines:

| Engine | Wat het is | EDIT 1 | EDIT 2 |
| --- | --- | --- | --- |
| **JUNO** | Alpha Juno-1: pulse / saw / sub-oscillator, HPF, 24 dB-filter, stereo-chorus, 6 stemmen | PULS SAW SUB SUBL | CUT RES HPF PWM |
| **ACID** | TB-303: saw/square, het 303-filter, accent en slide | CUT RES ENV DEC | ACC WAVE DRV DTYP |
| **TR909** | TR-909: BD SD CP CH OH TOM RS RIDE op het drumgrid | BD TN, BD DC, BD AT, SNAP | TONE HAT TOM ACC |

Bij het aanzetten staan de tracks zo: **1 = ACID** (303-bas), **2 = JUNO** Warm Pad, **3 = JUNO** PWM Lead, **4 = TR909**.

**De mixer.** Druk een paar keer op **HOME**: HOME → MIXER → **CHANNEL** → **CHANNEL EQ** → CLOCK.

- **CHANNEL**: GAIN (−20 tot +20 dB; hard opendraaien laat de preamp warm verzadigen, net als een Mackie in het rood),
  LOCUT (75 Hz, 18 dB/oct), LEVEL, PAN
- **CHANNEL EQ**: HIGH (12 kHz), MID (±15 dB), FREQ (MID-frequentie, 100 Hz – 8 kHz), LOW (80 Hz). Het scherm laat de
  EQ-curve zien.

Bij de JUNO is **CHO** (op de FX-pagina) de eigen stereo-chorus van de Juno: 0 = uit, hoger = sneller.
Bij de ACID: zet **ACC** in de sequencer voor accent en **SLIDE** voor glijden, net als op een echte 303.
Bij de TR909: houd **EDIT** ingedrukt en tik een zwarte toets 1–8 om een drumpartij te muten.

---

## Snelste route: alles via de browser (ook op een Chromebook)

Je hebt geen terminal of Linux nodig. GitHub (gratis) bouwt de firmware voor je in de cloud en maakt er meteen een
installatiewebsite bij. Die open je in Chrome.

1. Maak een gratis account op **github.com** (als je er nog geen hebt).
2. Maak een nieuwe, **lege, openbare** repository, bijvoorbeeld `fm1-studio`
   (rechtsboven **+** → **New repository** → naam invullen → **Public** → **Create repository**).
3. In die repository: **Settings** → **Pages** → bij "Source" kies **GitHub Actions**.
4. Zet deze projectbestanden in de repository (Claude kan dat voor je doen als je GitHub koppelt in claude.ai,
   Settings → Connectors).
5. Wacht ongeveer 5 minuten. Onder het tabblad **Actions** zie je de bouw lopen; een groen vinkje betekent klaar.
6. Sluit de FM-1 met USB aan en open in **Chrome**:
   **https://JOUWNAAM.github.io/fm1-studio/webapp/installer/**
   Klik **Install** en geef Chrome toestemming voor het MIDI-apparaat.

Iets veranderen? Pas een bestand aan op github.com (potloodje → **Commit changes**). GitHub bouwt dan opnieuw en de
website heeft na een paar minuten de nieuwe firmware.

---

## Andere route: zelf bouwen op een Linux-pc (of Windows met WSL)

### Stap 1: Een Linux-terminal

Het bouwen gaat in een **terminal**, een venster waarin je opdrachten typt.

**Heb je Windows?** Installeer dan eenmalig "WSL" (Linux binnen Windows):
1. Klik op Start, typ `PowerShell`, klik met de rechtermuisknop op **Windows PowerShell** en kies
   **Als administrator uitvoeren**.
2. Typ `wsl --install` en druk op Enter. Wacht tot het klaar is en **start je pc opnieuw op**.
3. Na de herstart opent "Ubuntu" vanzelf (of zoek "Ubuntu" in het Startmenu). Kies een gebruikersnaam en wachtwoord.
   Je ziet niets terwijl je het wachtwoord typt; dat is normaal.

Je hebt nu een Ubuntu-venster. Dat is je terminal.

**Heb je al Linux** (Ubuntu, Mint, Debian)? Open dan gewoon "Terminal".

> Een Mac werkt ook, maar dan heb je Docker nodig. Zie `BUILDING.md`.

### Stap 2: Het project in de terminal zetten

**Windows (WSL):** zet de zip `fm1-studio.zip` in je map **Downloads**. Typ dan in het Ubuntu-venster (je
Windows-gebruikersnaam in plaats van `JOUWNAAM`):

```
cp /mnt/c/Users/JOUWNAAM/Downloads/fm1-studio.zip ~
cd ~
sudo apt-get install -y unzip
unzip fm1-studio.zip
cd fm1-studio
```

**Linux:** pak de zip uit in je thuismap en typ `cd ~/fm1-studio`.

### Stap 3: Bouwen (één opdracht)

```
sh studio-setup.sh
```

Wat er gebeurt: het script installeert wat nodig is (het vraagt één keer je wachtwoord), downloadt de compiler van JieLi
(de maker van de chip in de FM-1) en bouwt de firmware. De eerste keer duurt dat een paar minuten. Bij succes zie je:

```
==> Klaar: .../build/felucca.fwsc
```

Dat bestand is je firmware.

### Stap 4: Op de FM-1 zetten

> **Lees dit eerst.** Firmware zetten is op eigen risico. Maak in de M-VAVE Workbench of de Felucca-editor eerst een
> backup van je projecten. Houd de FM-1 aan de USB tijdens het installeren. Terug naar de originele firmware kan altijd
> via de M-VAVE Workbench (<https://open.m-vave.com/fm1-workbench>), knop "original V15".

1. Sluit de FM-1 met USB aan op je pc.
2. Typ in de terminal:
   ```
   sh studio-setup.sh --install
   ```
3. Open **Chrome** of **Edge** (op Windows gewoon de normale browser) en ga naar
   **http://localhost:8000/webapp/installer/**
4. Klik **Install**, geef de browser toestemming voor MIDI-apparaten en wacht tot het klaar is.
5. Terug in de terminal: druk **Ctrl + C** om de pagina te stoppen.

## Iets veranderen en opnieuw bouwen

Pas iets aan in `firmware/src/` (bijvoorbeeld een preset in `eng_juno.c`) en typ:

```
sh studio-setup.sh --build
```

Zet daarna het nieuwe bestand er weer op met stap 4.

Testen zonder FM-1 (op de pc, maakt ook WAV-demo's in `build/studio_demo/`):

```
sh tests/run_host_tests.sh
```

## Als er iets misgaat

| Melding | Oplossing |
| --- | --- |
| `curl: ... 403` of `could not resolve` bij stap 2/3 van het script | Je internet of een firewall blokkeert JieLi of Gitee (servers in China). Probeer een ander netwerk. |
| `FOUT: ... x86_64` | De compiler draait alleen op een gewone Intel/AMD-pc, niet op een Raspberry Pi of een Mac met M-chip (gebruik daar Docker, zie BUILDING.md). |
| `image ... exceeds the app slot` of `headroom` | De firmware is te groot voor de FM-1. Dat kan als je veel toevoegt. |
| De installatiepagina vindt de FM-1 niet | Gebruik Chrome of Edge (geen Firefox of Safari), sluit de M-VAVE Workbench en andere MIDI-programma's en sluit de FM-1 opnieuw aan. |
| De FM-1 start niet meer | Zet de originele firmware terug met de M-VAVE Workbench. Lukt dat niet: houd OCT− en OCT+ 5 seconden vast voor de updatemodus. Zie ook <https://github.com/kurogedelic/FM-1-transporter>. |

## Goed om te weten

- **Nog niet op een echte FM-1 getest.** Alles is getest op de pc, met dezelfde code als in de firmware (zie hieronder),
  maar het bouwen voor de chip en de belasting van de processor op het apparaat zie je pas bij jouw eerste build. Hapert
  het geluid als alle vier de tracks tegelijk spelen? Zet dan MENU > SYSTEM > 2ND CORE aan, of gebruik minder JUNO-stemmen.
- Met **2ND CORE aan**: zet niet op twee tracks tegelijk een TR909. De 909-code deelt een paar hulpwaarden tussen
  kits, wat tikjes in de panning kan geven. Eén TR909 is prima.
- De **web-editor van Felucca** kent JUNO, ACID en TR909 niet. Gebruik de knoppen op de FM-1 zelf.
- Je oude Felucca-projecten laden gewoon: een DRUM-track wordt de TR909, andere Felucca-engines worden JUNO.

## Waar zit wat (voor als je verder wilt)

| Bestand | Wat |
| --- | --- |
| `firmware/juno/alpha_juno.c` | jouw Alpha Juno-synth |
| `firmware/src/eng_juno.c` | de JUNO-engine: knoppen, presets, koppeling met Felucca |
| `firmware/src/eng_acid.c`, `firmware/x0x/bass303.c` | de 303 |
| `firmware/src/eng_tr909.c`, `firmware/x0x/drum909.c` | de 909 |
| `firmware/src/strip.c` | de Mackie-kanaalstrip (gain, low cut, EQ) |
| `tests/studio_test.c` | tests voor alles hierboven |

Licentie: GPL-3.0 (zie `LICENSING.md`). Deel je de firmware, deel dan ook de broncode.
