
---

## 9. Monitoring (To Be Completed)

Serial monitoring is currently performed using:

- `picocom`
- `idf.py monitor` (optional)

USB-JTAG and monitoring behaviour under WSL2 varies by hardware and driver.

This section will be expanded once a **fully stable monitoring strategy** is finalised for:
- ESP32-C3
- ESP32-S3
- ESP-PROG (JTAG)

---

## 10. Known Pitfalls & Design Decisions

- Do **not** run `idf.py build` directly
- Do **not** use ESP-IDF project creation
- Do **not** use `idf.py set-target`
- Do **not** regenerate `sdkconfig` manually
- Always trust the flash command emitted by the Espruino build
- Keep ESP-IDF usage strictly as a toolchain

These decisions are intentional and prevent subtle build breakage.

---

## 11. Future Work

- ESP32-S3 support
- ESP-IDF v5 migration
- JTAG debugging via ESP-PROG
- Stable serial/JTAG monitoring under WSL2
- Optional VS Code integration (non-intrusive)

---

**Status:**  
✔ Build validated  
✔ Flash validated  
⬜ Monitor finalisation pending  
