Here is a concise quick reference guide in Markdown covering basic GSM/LTE modem initialization, SIM card verification, modem information queries, and network registration status check commands based on the **SIMCom A76XX Series AT Command Manual**.

---

# GSM/LTE Modem Quick Reference Guide (SIMCom A76XX Series)

## 1. Overview of AT Command Syntax Types

AT commands are sent from the Terminal Equipment (TE) to the Terminal Adapter (TA). Commands can be grouped on a single line starting with the `AT` prefix and separated by semicolons (`;`).

| Command Type          | Syntax         | Description                                                                 |
| :-------------------- | :------------- | :-------------------------------------------------------------------------- |
| **Test Command**      | `AT+<x>=?`     | Queries the range of supported user-definable parameters.                   |
| **Read Command**      | `AT+<x>?`      | Returns the currently set values of the parameters.                         |
| **Write Command**     | `AT+<x>=<...>` | Sets user-definable parameter values.                                       |
| **Execution Command** | `AT+<x>`       | Reads non-variable parameters affected by internal processes in the module. |

---

## 2. Basic Initialization & Modem Information

### `AT` — Basic Command Test
Checks communication between the TE and the modem.

| Command Type          | Command | Response |
| :-------------------- | :------ | :------- |
| **Execution Command** | `AT`    | `OK`     |

* **Example:**
  ```text
  AT
  OK
  ```

---

### `ATE` — Enable / Disable Command Echo
Controls whether the modem echoes typed characters back to the terminal.

| Command Type          | Command        | Response       |
| :-------------------- | :------------- | :------------- |
| **Execution Command** | `ATE[<value>]` | `OK` / `ERROR` |

* **Defined Values:**
  * `<value>`: `0` = Echo mode off; `1` = Echo mode on.
* **Example:**
  ```text
  ATE1
  OK
  ```

---

### `ATI` — Display Product Identification
Requests basic product information, including manufacturer, model, revision, and IMEI.

| Command Type          | Command | Response                                                                                                                     |
| :-------------------- | :------ | :--------------------------------------------------------------------------------------------------------------------------- |
| **Execution Command** | `ATI`   | `Manufacturer: <manufacturer>`<br>`Model: <model>`<br>`Revision: <revision>`<br>`IMEI: <sn>`<br>`+GCAP: <name>s`<br><br>`OK` |

* **Example:**
  ```text
  ATI
  Manufacturer: INCORPORATED
  Model: A7600C
  Revision: A7600C_V1.0
  IMEI: 351602000330570
  +GCAP: +CGSM,+FCLASS,+DS

  OK
  ```

---

### `AT+CGMM` — Request Model Identification
Queries the specific module model string.

| Command Type          | Command     | Response              |
| :-------------------- | :---------- | :-------------------- |
| **Test Command**      | `AT+CGMM=?` | `OK`                  |
| **Execution Command** | `AT+CGMM`   | `<model>`<br><br>`OK` |

* **Example:**
  ```text
  AT+CGMM
  A7600E

  OK
  ```

---

### `AT+CGMR` — Request Revision Identification
Requests product firmware revision identification text.

| Command Type          | Command   | Response                 |
| :-------------------- | :-------- | :----------------------- |
| **Execution Command** | `AT+CGMR` | `<revision>`<br><br>`OK` |

---

### `AT+CGSN` — Request Product Serial Number (IMEI)
Requests the International Mobile station Equipment Identity (IMEI).

| Command Type          | Command   | Response           |
| :-------------------- | :-------- | :----------------- |
| **Execution Command** | `AT+CGSN` | `<sn>`<br><br>`OK` |

---

### `AT+CFUN` — Set Phone Functionality
Sets the level of modem functionality or resets the module.

| Command Type      | Command                 | Response                                                                     |
| :---------------- | :---------------------- | :--------------------------------------------------------------------------- |
| **Test Command**  | `AT+CFUN=?`             | `+CFUN: (range of supported <fun>s),(range of supported <rst>s)`<br><br>`OK` |
| **Read Command**  | `AT+CFUN?`              | `+CFUN: <fun>`<br><br>`OK`                                                   |
| **Write Command** | `AT+CFUN=<fun>[,<rst>]` | `OK` / `ERROR` / `+CME ERROR: <err>`                                         |

* **Defined Values:**
  * `<fun>`:
    * `0`: Minimum functionality.
    * `1`: Full functionality (online mode).
    * `4`: Disable phone transmit and receive RF circuits.
    * `6`: Reset module.
    * `7`: Offline mode.
    * `8`: Disable SIM.
  * `<rst>`: `0` = Do not reset before setting power level; `1` = Reset before setting power level.
* **Example:**
  ```text
  AT+CFUN?
  +CFUN: 1

  OK
  ```

---

## 3. SIM Card Status Commands

### `AT+CPIN` — Enter PIN / Check SIM Status
Checks whether a password (PIN/PUK) is required to operate the SIM card.

| Command Type      | Command                    | Response                                          |
| :---------------- | :------------------------- | :------------------------------------------------ |
| **Test Command**  | `AT+CPIN=?`                | `OK`                                              |
| **Read Command**  | `AT+CPIN?`                 | `+CPIN: <code>`<br><br>`OK` / `+CME ERROR: <err>` |
| **Write Command** | `AT+CPIN=<pin>[,<newpin>]` | `OK` / `+CME ERROR: <err>`                        |

* **Defined Values (`<code>`):**
  * `READY`: ME is not pending for any password (SIM is unlocked and ready).
  * `SIM PIN`: ME is waiting for SIM PIN to be entered.
  * `SIM PUK`: ME is waiting for SIM PUK to be entered.
  * `SIM CRASH`: SIM initialization failed or encountered a serious error.
* **Example:**
  ```text
  AT+CPIN?
  +CPIN: READY

  OK
  ```

---

### `AT+CICCID` — Read ICCID from SIM Card
Reads the Integrated Circuit Card Identifier (20-digit serial number) from the inserted SIM card.

| Command Type          | Command       | Response                      |
| :-------------------- | :------------ | :---------------------------- |
| **Test Command**      | `AT+CICCID=?` | `OK`                          |
| **Execution Command** | `AT+CICCID`   | `+ICCID: <ICCID>`<br><br>`OK` |

* **Example:**
  ```text
  AT+CICCID
  +ICCID: 89860318760238610932

  OK
  ```

---

## 4. Network Status & Registration Commands

### `AT+CSQ` — Query Signal Quality
Queries the received signal strength indication (`<rssi>`) and channel bit error rate (`<ber>`).

| Command Type          | Command    | Response                                                                     |
| :-------------------- | :--------- | :--------------------------------------------------------------------------- |
| **Test Command**      | `AT+CSQ=?` | `+CSQ: (range of supported <rssi>s),(range of supported <ber>s)`<br><br>`OK` |
| **Execution Command** | `AT+CSQ`   | `+CSQ: <rssi>,<ber>`<br><br>`OK` / `ERROR`                                   |

* **Defined Values:**
  * `<rssi>`:
    * `0`: -113 dBm or less.
    * `1`: -111 dBm.
    * `2...30`: -109 dBm to -53 dBm.
    * `31`: -51 dBm or greater.
    * `99`: Not known or not detectable.
  * `<ber>`: `0...7` (Bit error rate percentage); `99` = Not known or not detectable.
* **Example:**
  ```text
  AT+CSQ
  +CSQ: 31,99

  OK
  ```

---

### `AT+CREG` — Circuit-Switched Network Registration
Checks or enables reporting for circuit-switched (GSM/UMTS) network registration status.

| Command Type      | Command       | Response                                       |
| :---------------- | :------------ | :--------------------------------------------- |
| **Test Command**  | `AT+CREG=?`   | `+CREG: (range of supported <n>s)`<br><br>`OK` |
| **Read Command**  | `AT+CREG?`    | `+CREG: <n>,<stat>[,<lac>,<ci>]`<br><br>`OK`   |
| **Write Command** | `AT+CREG=<n>` | `OK` / `ERROR`                                 |

* **Defined Values:**
  * `<n>`: `0` = Disable URC; `1` = Enable URC (`+CREG: <stat>`); `2` = Enable URC with location info.
  * `<stat>`:
    * `0`: Not registered; ME is not currently searching for a new operator.
    * `1`: Registered, home network.
    * `2`: Not registered, but ME is currently searching for a new operator.
    * `3`: Registration denied.
    * `4`: Unknown.
    * `5`: Registered, roaming.
* **Example:**
  ```text
  AT+CREG?
  +CREG: 0,1

  OK
  ```

---

### `AT+CGREG` — Packet Domain (GPRS) Network Registration Status
Queries GPRS packet service network registration status.

| Command Type      | Command        | Response                                      |
| :---------------- | :------------- | :-------------------------------------------- |
| **Read Command**  | `AT+CGREG?`    | `+CGREG: <n>,<stat>[,<lac>,<ci>]`<br><br>`OK` |
| **Write Command** | `AT+CGREG=<n>` | `OK` / `ERROR`                                |

* **Defined Values (`<stat>`):**
  * `0`: Not registered.
  * `1`: Registered, home network.
  * `2`: Not registered, trying to attach/searching.
  * `3`: Registration denied.
  * `5`: Registered, roaming.
* **Example:**
  ```text
  AT+CGREG?
  +CGREG: 0,1

  OK
  ```

---

### `AT+CEREG` — EPS (LTE) Network Registration Status
Queries EPS/LTE network registration status.

| Command Type      | Command        | Response                                      |
| :---------------- | :------------- | :-------------------------------------------- |
| **Read Command**  | `AT+CEREG?`    | `+CEREG: <n>,<stat>[,<tac>,<ci>]`<br><br>`OK` |
| **Write Command** | `AT+CEREG=<n>` | `OK` / `ERROR`                                |

* **Example:**
  ```text
  AT+CEREG?
  +CEREG: 0,1

  OK
  ```

---

### `AT+COPS` — Operator Selection
Forces network registration or queries the currently registered network operator.

| Command Type     | Command    | Response                                              |
| :--------------- | :--------- | :---------------------------------------------------- |
| **Read Command** | `AT+COPS?` | `+COPS: <mode>[,<format>,<oper>[,<AcT>]]`<br><br>`OK` |

* **Defined Values:**
  * `<AcT>` (Access Technology): `0` = GSM, `2` = UTRAN, `7` = EUTRAN (LTE).
* **Example:**
  ```text
  AT+COPS?
  +COPS: 0,0,"CHN-UNICOM",7

  OK
  ```

---

### `AT+CPSI` — Inquire System Information
Inquires UE system information, including operational mode, serving cell IDs, band, and radio signal measurements.

| Command Type     | Command    | Response                                                                                                                                                              |
| :--------------- | :--------- | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Read Command** | `AT+CPSI?` | LTE: `+CPSI: <System Mode>,<Operation Mode>[,<MCC>-<MNC>,<TAC>,<SCellID>,<PCellID>,<Frequency Band>,<earfcn>,<dlbw>,<ulbw>,<RSRQ>,<RSRP>,<RSSI>,<RSSNR>]`<br><br>`OK` |

* **Example:**
  ```text
  AT+CPSI?
  +CPSI: LTE,Online,460-01,0x230A,175499523,318,EUTRAN-BAND3,1650,5,0,21,67,255,19

  OK
  ```

---

## 5. Recommended Basic Initialization & Registration Sequence

When bringing up a modem programmatically, send commands in the following sequence:

1. **`AT`** — Verify UART connection.
2. **`ATE0`** — Disable echo for predictable host parsing.
3. **`ATI`** / **`AT+CGMM`** — Confirm module model and firmware version.
4. **`AT+CPIN?`** — Ensure SIM status returns `+CPIN: READY`.
5. **`AT+CFUN?`** — Verify module functionality is set to `1` (Full functionality).
6. **`AT+CSQ`** — Check signal strength.
7. **`AT+CEREG?`** / **`AT+CREG?`** — Check network registration (`<stat>` should be `1` for Home or `5` for Roaming).
8. **`AT+CPSI?`** — Confirm connected network technology (e.g., LTE) and serving cell details.

---
