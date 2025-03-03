# Smart Blinds

Tento projekt slouží k ovládání žaluzií pomocí krokového motoru řízeného přes standard Matter a protokol Thread.  
Kód obsahuje dva režimy:
1. **Kalibrační režim**  
2. **Matter režim**

Zařízení ukládá koncové polohy (min/max) a aktuální polohu do EEPROM, což umožňuje zachovat nastavení i po vypnutí napájení.

## Hlavní rysy projektu

1. **Nízká spotřeba** – Po delší nečinnosti se driver motoru uspává.  
2. **Jednoduché řízení kroků** – Používá se ručně řízené krokování (bez knihovny AccelStepper).  
3. **Podpora Matter** – Komunikuje s Matter Window Covering (např. Home Assistant, Apple Home, Google Home apod.).  
4. **Kalibrace v terénu** – Mechanickým přepínačem a dvěma tlačítky lze snadno nastavit minimální a maximální polohu.  
5. **Zápis do EEPROM** – Konfigurace a aktuální stav se ukládají jen po určité době, aby se minimalizoval počet zápisů.

## Struktura souborů

- **blinds_with_matter.ino**  
  Obsahuje veškerý kód včetně:  
  - Definice pinů, konstant a proměnných  
  - `setup()` a `loop()` cykly  
  - Funkce pro kalibrační režim, Matter režim, řízení kroků, EEPROM, atd.

- **README.md** *(tento soubor)*  
  Vysvětluje cíle projektu, strukturu kódu, instalaci a konfiguraci.

## Závislosti / Knihovny

- **Arduino Core** pro příslušnou desku (např. Seeed Studio XIAO MG24 nebo jiné Arduino-kompatibilní MCU).  
- **EEPROM.h**: Standardní knihovna pro čtení/zápis do EEPROM.  
- **Matter.h**, **MatterWindowCovering.h**: Knihovny (SDK) pro implementaci protokolu Matter na MCU.  
  *(Konkrétní instalace závisí na výrobci čipu a integraci Thread/Matter; pro XIAO MG24 je třeba postupovat dle SeeedStudio dokumentace.)*

## Hardwarové zapojení (stručně)

| Pin              | Popis                                 |
|------------------|---------------------------------------|
| D0 (MOTOR_ENABLE_PIN) | Aktivace/deaktivace driveru (active LOW) |
| D1 (STEP_PIN)         | Generování krokových pulzů (posun motoru) |
| D2 (DIRECTION_PIN)    | Řídí směr otáčení motoru (HIGH/LOW)       |
| D5 (LED_RED)          | Červená LED (signál různých stavů)        |
| D6 (LED_GREEN)        | Zelená LED (signál různých stavů)         |
| D7 (MOVE_UP_BUTTON)   | Tlačítko pro ruční posun vzhůru            |
| D8 (MOVE_DOWN_BUTTON) | Tlačítko pro ruční posun dolů              |
| D9 (SLEEP_PIN)        | Uspání / probuzení driveru (HIGH = awake) |
| D10 (MODE_SWITCH_PIN) | Přepínač mezi kalibračním a Matter režimem |

## Popis režimů

1. **Kalibrační režim (MODE_SWITCH_PIN = HIGH)**  
   - **Krátký stisk tlačítek** UP/DOWN: motor se posune o několik kroků (např. 5).  
   - **Dlouhý stisk obou tlačítek (3 s)**: uloží aktuální pozici jako min (horní) nebo max (spodní).  
   - LED indikují stav: zelená LED = nastavuje se MIN, červená LED = nastavuje se MAX.

2. **Matter režim (MODE_SWITCH_PIN = LOW)**  
   - Zařízení čte cílovou polohu z Matter sítě (uživatel zadává procentuální otevření).  
   - Krokový motor se přesune do požadované pozice a stav se odesílá zpět do Matter.  
   - LED jsou většinu času vypnuté, červená LED se rozsvítí pouze po dobu pohybu.

## Jak kód používat

1. **Naklonovat / stáhnout** tento repozitář.  
2. **Otevřít** projekt v Arduino IDE (nebo jiném kompatibilním prostředí).  
3. **Zkontrolovat** definice pinů a konstant (případně upravit podle vašeho hardware).  
4. **Nainstalovat** potřebné knihovny (zejména Matter SDK, EEPROM apod.).  
5. **Zkompilovat** a **nahrát** do zařízení.  
6. **Sledovat** výpis v sériovém monitoru (115200 baud).  
7. **Kalibrace**:  
   - Nastavte režimový přepínač do polohy HIGH (kalibrační režim).  
   - Nastavte horní/spodní mez žaluzií dle potřeby (pomocí tlačítek a dlouhého stisku obou tlačítek pro uložení).  
   - Přepněte zpět do režimu LOW (Matter).  
8. **Matter párování**:  
   - Postupujte podle návodu pro váš Matter hub (Home Assistant, Google Home, Apple HomeKit atd.).  
   - V sériovém výpisu se zobrazí párovací informace (manuální kód, QR kód).  
   - Po úspěšném spárování lze zařízení ovládat přes příslušnou aplikaci nebo hlasovým asistentem.

## Tipy a další úpravy

- **Změna rychlosti krokování**  
  Upravte konstanty `STEP_DELAY_CALIBRATION` a `STEP_DELAY_MATTER` v `blinds_with_matter.ino`.
- **Změna počtu kroků na stisk v kalibračním režimu**  
  Upravte `STEPS_PER_PRESS`.
- **Interval uspání / ukládání**  
  Upravte `MOTOR_DISABLE_INTERVAL` a `AUTO_SAVE_INTERVAL` podle preferencí.  
- **Rozšíření o senzory**  
  Přidejte analogové čidlo světla či teploty a řiďte žaluzie podle reálných podmínek (např. automatické stínění).

## Licence

Distributed under the **MIT License**. See the [LICENSE](#license-contents) section below for details.

---

### License Contents

