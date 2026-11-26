# Store Listing Copy — `{{APP_NAME}}`

Fill both language blocks. Numbers, file sizes, and capability claims must match `facts.md` and the
shipped artifact. Leave the placeholder visible if a value is not verified yet — never invent one.

## Shared fields

| Field | Value |
|---|---|
| Product name | `{{APP_NAME}}` |
| Category | `{{Utilities & tools}}` |
| Pricing | `{{Free — no ads, no in-app purchases}}` |
| Markets | `{{all available markets}}` |
| Privacy policy URL | `{{https://ozlab.pages.dev/privacy-<app>}}` (must be HTTP 200 before submit) |
| Support / contact URL | `{{CONTACT_URL}}` |
| Restricted capability | `runFullTrust` — see `runfulltrust-485.txt` |

---

## English (`en-US`)

### Product name

```text
{{APP_NAME}}
```

### Short description (target ≤ 200 characters; Partner Center shows ~2 lines)

```text
{{One sentence: what it does + the single strongest differentiator (offline, native, tiny).}}
```

Character count: `{{N}} / 200`

### Description

```text
{{APP_NAME}} is a {{one-line positioning}} for Windows 10 and Windows 11 (x64), built natively in {{stack}}.

{{Paragraph 2 — architecture and resource facts: executable size, idle RAM with the measurement
condition, what is NOT bundled (no Electron/WebView2/Qt/.NET), and when any heavy component loads.}}

Key capabilities:
• {{Capability 1 — headline action and the exact hotkey.}}
• {{Capability 2 — differentiator with a concrete number (range, count, limit).}}
• {{Capability 3 — workflow that saves the user time.}}
• {{Capability 4 — precision or power-user feature.}}
• {{Capability 5 — privacy/offline statement tied to a real behaviour.}}
• {{Capability 6 — secondary workflow (batch, delayed, export).}}
• {{Capability 7 — closing privacy or performance claim.}}
```

### What's new in `{{VERSION}}`

```text
• {{Change 1 — user-visible, one line.}}
• {{Change 2 — fix with the symptom described, not the internal cause.}}
• {{Change 3 — performance or compatibility note.}}
```

### Feature bullets (7 required — short, no trailing period)

1. `{{Native {{stack}} {{core capability}} with {{constraint handled}}}}`
2. `{{Resource claim with the measured number}}`
3. `{{Offline / local processing claim}}`
4. `{{Power-user precision feature}}`
5. `{{Tool or format coverage, enumerated}}`
6. `{{Secondary workflow}}`
7. `{{Zero telemetry / zero network / opt-in startup}}`

### Search terms (7 required, lowercase, no duplicates)

```text
{{term1}}, {{term2}}, {{term3}}, {{term4}}, {{term5}}, {{term6}}, {{term7}}
```

---

## Thai (`th-TH`)

### ชื่อผลิตภัณฑ์

```text
{{APP_NAME}}
```

### คำอธิบายสั้น (ไม่เกิน 200 ตัวอักษร)

```text
{{หนึ่งประโยค: ทำอะไร + จุดเด่นที่สุด (ออฟไลน์ / native / เบา)}}
```

จำนวนตัวอักษร: `{{N}} / 200`

### คำอธิบายแบบเต็ม

```text
{{APP_NAME}} คือ{{คำระบุประเภท}}สำหรับ Windows 10 และ Windows 11 (x64) พัฒนาแบบ native ด้วย {{stack}}

{{ย่อหน้าที่ 2 — ข้อเท็จจริงด้านสถาปัตยกรรม: ขนาดไฟล์, แรมขณะรอใช้งาน พร้อมเงื่อนไขการวัด,
สิ่งที่ไม่พ่วงมา (Electron/WebView2/Qt/.NET) และโหลดส่วนหนักเมื่อใด}}

ความสามารถหลัก:
• {{ความสามารถ 1 — การทำงานหลัก พร้อมคีย์ลัดจริง}}
• {{ความสามารถ 2 — จุดต่าง พร้อมตัวเลขจริง (ช่วง/จำนวน)}}
• {{ความสามารถ 3 — งานที่ประหยัดเวลา}}
• {{ความสามารถ 4 — ฟีเจอร์สำหรับผู้ใช้ระดับสูง}}
• {{ความสามารถ 5 — ความเป็นส่วนตัว/ออฟไลน์ ที่อ้างอิงพฤติกรรมจริง}}
• {{ความสามารถ 6 — งานเสริม (บันทึกอัตโนมัติ/หน่วงเวลา/ส่งออก)}}
• {{ความสามารถ 7 — ปิดท้ายด้วยประสิทธิภาพหรือความเป็นส่วนตัว}}
```

### มีอะไรใหม่ใน `{{VERSION}}`

```text
• {{รายการที่ 1 — สิ่งที่ผู้ใช้เห็น}}
• {{รายการที่ 2 — สิ่งที่แก้ พร้อมอาการที่ผู้ใช้เจอ}}
• {{รายการที่ 3 — ประสิทธิภาพหรือความเข้ากันได้}}
```

### คุณสมบัติเด่น (7 ข้อ)

1. `{{ความสามารถหลักแบบ native พร้อมข้อจำกัดที่รองรับ}}`
2. `{{ตัวเลขทรัพยากรที่วัดจริง}}`
3. `{{ออฟไลน์/ประมวลผลในเครื่อง}}`
4. `{{ฟีเจอร์ความละเอียดสำหรับผู้ใช้ระดับสูง}}`
5. `{{เครื่องมือ/รูปแบบไฟล์ที่รองรับ ครบทุกตัว}}`
6. `{{งานเสริม}}`
7. `{{ไม่มี telemetry / ไม่ต่อเน็ต / เริ่มพร้อม Windows เป็นตัวเลือกปิดไว้}}`

### คำค้นหา (7 คำ)

```text
{{คำค้น 1}}, {{คำค้น 2}}, {{คำค้น 3}}, {{คำค้น 4}}, {{คำค้น 5}}, {{คำค้น 6}}, {{คำค้น 7}}
```

---

## Pre-submit copy checklist

- [ ] Every number matches `facts.md` (size, RAM, version, counts).
- [ ] Hotkeys and menu labels match the shipped build.
- [ ] No claim of reproducible builds, no Store ID, no secrets, no local user paths.
- [ ] Short description ≤ 200 characters in both languages.
- [ ] Exactly 7 feature bullets and 7 search terms per language.
- [ ] Privacy URL verified HTTP 200 on the exact URL submitted.
- [ ] `runFullTrust` field ≤ 485 characters; full detail in Notes for certification.
