# Texas-Tech-University (ECE) ---Enigma-Encryption-Typewriter-Project 

# Enigma Encryption Brother SX-4000 Typewriter 
### A real-time hardware cryptographic interceptor built on a Brother SX-4000 typewriter and MSP432E401Y microcontroller

---

## What it is

A functioning Enigma machine built into a real typewriter. The Brother SX-4000's keyboard connector is physically intercepted by an MSP432E401Y LaunchPad. Every key pressed goes through the microcontroller, which can:

- **PASSTHROUGH** — inject the same character to the printer (normal typing)
- **ENCRYPT** — run the Wehrmacht Enigma I cipher and print the encrypted character
- **DECRYPT** — reverse-decrypt ciphertext back to plaintext (Enigma is self-inverse)

The typewriter physically prints on paper. Space and Return always pass through unchanged.

---

## Hardware

| Component | Part | Role |
|---|---|---|
| Microcontroller | MSP432E401Y LaunchPad | 120 MHz ARM Cortex-M4F — scan, cipher, inject, UI |
| Typewriter | Brother SX-4000 | Keyboard (8×8 passive matrix) + daisy-wheel printer |
| Analog mux ×2 | CD4051BE | Select col and row on FPC2 for injection |
| Bilateral switch | CD4066BD | Gate injection path — enable permanently HIGH |
| LCD display | NHD-0420D3Z-NSW-BBW | 20×4 serial LCD — shows mode, rotors, plaintext/ciphertext |
| Push-buttons ×5 | Tactile | MODE / SELECT / UP / DOWN / ENTER |
| LEDs ×3 | 3mm standard | Green (ENCRYPT), Red (DECRYPT), Keypress indicator |
| Plugboard jacks ×26 | 3.5mm mono NC | One per letter A–Z, physical cable pairs |

---

## Firmware modules

```
main.c            System orchestrator. Startup, keyboard scan, process_key(), main loop.
enigma.c/h        Complete Enigma I engine. 5 rotors, 3 reflectors, dual plugboard, double-step anomaly.
control_panel.c/h 6-state FSM. Button navigation, mode switching, rotor/ring/plugboard config, history log.
LCD.c/h           NHD-0420D3Z driver. UART6, 9600 baud, command-prefix protocol.
uart_comm.c/h     UART0 debug at 115200 baud. Trace output to PuTTY.
plugboardhw.c/h   26 jack GPIO scan. NC-jack pre-filter + drive-LOW confirmation.
config.h          Single source of truth. Every pin, constant, and lookup table.
mitm.h            Legacy stub. Empty inlines. Satisfies include in control_panel.c.
system_check.c/h  Boot diagnostics. Validates reflector symmetry at startup.
system_msp432e401y.c  TI SDK clock and vector table.
```

---

## GPIO pin assignment

### Keyboard scan inputs — active row-drive, col WPU idle HIGH

| Signal | Pin | Connector | Keys on this line |
|---|---|---|---|
| col 0 | PE4 | J1-2 | B C V |
| col 1 | PC4 | J1-3 | M N X |
| col 2 | PC5 | J1-4 | I K L U |
| col 3 | PC6 | J1-5 | D O P S |
| col 4 | PE5 | J1-6 | H J T Y |
| col 5 | PD3 | J1-7 | E F G R |
| col 6 | PC7 | J1-8 | A Q W Z Return |
| col 7 | PB2 | J1-9 | Space |
| row 0 | PE0 | J3-23 | unused |
| row 1 | PE1 | J3-24 | Return (col6) Space (col7) |
| row 2 | PE2 | J3-25 | A D G J L |
| row 3 | PE3 | J3-26 | B I M P R W Y |
| row 4 | **PD7 ⚠ NMI** | J3-28 | C F H K S X Z |
| row 5 | PM1 | J8-2 | E N O Q T U V |
| row 6 | PM4 | J3-29 | unused |
| row 7 | PM5 | J3-30 | unused |

> **PD7 NMI unlock required** — write `0x4C4F434B` to GPIOLOCK (offset `0x520`), set bit 7 in GPIOCR (offset `0x524`), re-lock. `GPIO_O_LOCK` / `GPIO_O_CR` are not in the MSP432E4 SDK.

> **PM1** — Row 5 reassigned from PD6 (scan unreliable on this hardware).

### Injection outputs — CBA addresses to CD4051 muxes

| Signal | Pin | Role |
|---|---|---|
| OUT_COL_C | PK0 | Col CBA bit 2 MSB |
| OUT_COL_B | PK1 | Col CBA bit 1 |
| OUT_COL_A | PK2 | Col CBA bit 0 LSB |
| OUT_ROW_C | PK3 | Row CBA bit 2 MSB |
| OUT_ROW_B | PA4 | Row CBA bit 1 |
| OUT_ROW_A | PA5 | Row CBA bit 0 LSB |
| EN_PIN | PB3 | CD4066 enable — HIGH permanently from boot |
| KP_LED | PM0 | ON while key held, OFF on release |

### Control panel

| Signal | Pin | Function |
|---|---|---|
| LED_GREEN | PB4 | ENCRYPT mode indicator. Blinks x3 at boot. |
| LED_RED | PB5 | DECRYPT mode indicator. Blinks x3 at boot. |
| BTN_MODE | PD2 | Cycle PASSTHROUGH -> ENCRYPT -> DECRYPT |
| BTN_SELECT | PQ0 | Confirm selection |
| BTN_UP | PP4 | Increment |
| BTN_DOWN | PN5 | Decrement |
| BTN_ENTER | PN4 | Enter config submenu |
| UART0_TX | PA1 | 115200 baud -> PuTTY |
| UART6_TX | PP1 | 9600 baud -> LCD |

### Plugboard jacks A-Z (input + WPU, NC jack)

```
A=PD0  B=PM3  C=PH2  D=PH3  E=PD1  F=PN2  G=PN3  H=PP2  I=PL3  J=PL2  K=PL1  L=PL0  M=PL5
N=PL4  O=PG0  P=PF3  Q=PF2  R=PF1  S=PM7  T=PP5  U=PA7  V=PQ2  W=PQ3  X=PQ1  Y=PM6  Z=PG1
```

---

## Power-on sequence

```
1. 120 MHz PLL + SysTick (1 ms)
2. Startup blink — PB4 + PB5 flash x3
3. UART0 init — PuTTY active
4. LCD boot screen — "System Check..." held 5 seconds (typewriter boot window)
5. Enigma init — UKW-B, rotors I-II-III, positions A-A-A
6. Control panel init — LCD switches to PASSTHROUGH idle display
7. MITM GPIO init — PB3 goes HIGH (injection path live)
8. Plugboard HW init
9. Main loop — Ready
```

---

## Enigma cipher

- 5 rotors (I–V) with verified historical wirings and notch positions
- 3 reflectors (UKW-A, UKW-B default, UKW-C)
- Dual-stage plugboard — up to 13 pairs, applied before and after rotor stack
- Double-step anomaly correctly implemented
- Self-inverse — ENCRYPT and DECRYPT use the same function

---

## PuTTY output

Connect at **115200 baud 8N1** to the LaunchPad USB virtual COM port.

```
[ENC] H => X  (7 changes)
  1. Rfwd: H->Q
  2. Mfwd: Q->E
  3. Lfwd: E->R
  4. Refl: R->B
  5. Lrev: B->T
  6. Mrev: T->W
  7. Rrev: W->X

[INJECT ON]  KEY_IN=H KEY_OUT=X  COL_CBA=100  ROW_CBA=100
[INJECT OFF]
```

---

## Build

- Code Composer Studio 12.8.1
- SimpleLink MSP432E4 SDK 4.20.00.12
- TI ARM Compiler 20.2.7.LTS
- Target: `MSP432E401Y`

---

## Known hardware notes

| Issue | Fix |
|---|---|
| PD7 ignores GPIO writes (NMI lock) | Raw register unlock at boot |
| PK0-PK3 need digital mode (analog pins) | `GPIOPinTypeGPIOOutput()` sets DEN automatically |
| PD6 row scan unreliable | Reassigned row 5 to PM1 |
| `GPIO_O_LOCK` undefined in SDK | Use raw offsets 0x520 / 0x524 with HWREG() |
| `control_panel.c` needs `mitm.h` | Stub header provided |

---

## Team — Group 19, Texas Tech University

- **Deo Mwala** — Software, Enigma engine, MITM firmware
- **Andy** — Hardware design, system integration, plugboard
- **Dustyn** — MITM PCB design and fabrication
