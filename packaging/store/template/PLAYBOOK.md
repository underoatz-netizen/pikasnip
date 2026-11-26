# Microsoft Store Release Playbook (Reusable)

Copy this `template/` folder into `packaging/store/template/` of any OZ LAB Windows app and follow
the 8 steps below. Replace every `{{PLACEHOLDER}}` token; delete nothing else. Facts must come from
the shipped artifact and the source tree, never from memory.

Gate before uploading: `powershell -ExecutionPolicy Bypass -File scripts\store-preflight.ps1`
must end `PASS` (WARN lines are acceptable, `FAIL` is not). Exit code 1 means "do not upload".

---

## 1. Fact sheet (`facts.md`)

Fill every row from real evidence: built `.exe` size/flags, measured idle RAM, bundled runtimes and
model files, hotkeys read from the source, data paths, and network/telemetry claims.

Rules:
- Measure the **shipped bits** (release build), not the debug tree.
- Copy hotkeys and menu labels out of `src/` — do not retype from a screenshot.
- Record artifact SHA-256 values after the final build; re-hash after any rebuild.

## 2. Manifest / capability check (`packaging/msix/AppxManifest.xml`)

| Item | Requirement |
|---|---|
| `Identity Name` | exactly the Partner Center `Package/Identity/Name` |
| `Identity Publisher` | exactly `CN=<GUID from Partner Center>` (brand name is **not** here) |
| `Properties PublisherDisplayName` | brand name, character-for-character |
| `Identity Version` | 4 parts, 4th part = `0` for Store submission |
| `ProcessorArchitecture` | `x64` for a desktop app |
| `rescap:Capability Name="runFullTrust"` | required for native Win32 apps |
| `desktop:StartupTask Enabled="false"` | startup must be opt-in by default |

The package uploaded to Partner Center must be signed with a certificate whose **subject equals
`Identity Publisher`** (self-signed is fine; the Store re-signs). A dev certificate with a different
subject is sideload-only. Keep the Store build in a separate file (`*_Store.msix`).

## 3. Logo / art sizes

| Asset | Size | Where |
|---|---|---|
| `StoreLogo.png` | 50×50 | package `Assets\` |
| `Square44x44Logo.png` | 44×44 | package `Assets\` |
| `Square150x150Logo.png` | 150×150 | package `Assets\` |
| `StoreLogo-300x300.png` | 300×300 | Store listing upload (outside the package) |

Generate from one master PNG, keep PNG format, verify real pixel dimensions before upload.
`store-preflight.ps1` check 6 fails if the 300×300 asset is missing or the wrong size.

## 4. Screenshots (4 × 1920×1080 PNG)

Follow `screenshots-shotlist.md`. Requirements: real interactive desktop (not a headless session),
16:9 PNG, clean background, no personal data, no tokens/keys, no unshipped UI. Filenames are fixed
so the preflight check can verify them.

## 5. Listing copy EN + TH (`listing-copy.md`)

Fill: product name, short description (keep under 200 characters), full description, What's new,
7 feature bullets, search terms, category, privacy URL. Both languages are mandatory for a global
listing; numbers and file sizes in the copy must match `facts.md`.

## 6. Privacy policy live **before** submit

`privacy-policy.html` in this folder is a 0-JS, inline-CSS, zero-external-asset template. Publish it
at `https://ozlab.pages.dev/privacy-<app>` and confirm HTTP 200 **before** pasting the URL into
Partner Center. Cloudflare Pages redirects `/page.html` → `/page`, so test the exact URL you submit.

## 7. Restricted capability `runFullTrust`

Paste `runfulltrust-485.txt` (after substituting `{{APP_NAME}}`) into the restricted-capability
field. The field truncates mid-word at roughly 500 characters, so **stay ≤ 485 characters** and
re-verify the count after substituting a longer app name.

Full detail — reviewer steps, data paths, `StartupTask` behaviour — goes into **Notes for
certification**, which has no short limit. Never shorten the technical reasons to fit; move them.

## 8. Post-submit: hold the Store ID

Do **not** publish the Store ID (`9PG…`), `apps.microsoft.com/detail/<ID>`, or
`ms-windows-store://pdp/?productid=<ID>` anywhere (README, website, release notes) until the listing
status is **Published**. Before publication those links return 404 and look like broken links.

After publication: add badge + deep link, then re-audit every page and asset for HTTP 200, and only
then update release notes. `store-preflight.ps1` check 4 fails if a Store ID leaks into tracked text
files that are meant to be public.

---

## Common pitfalls

1. **500-character truncation.** The restricted-capability field cuts at ~499 characters, mid-word.
   Keep the justification ≤ 485 and re-count after every edit.
2. **Privacy URL not live.** A 404 privacy URL blocks certification. Deploy first, then submit; keep
   the policy reachable for the whole certification window.
3. **Wrong package uploaded.** Upload only `*_Store.msix`. The dev/sideload package carries the dev
   publisher and is rejected. Never rename one to the other.
4. **Reproducible hashes.** A signed MSIX is **not** reproducible (signature timestamps change bytes).
   Never claim reproducible builds or a stable hash across rebuilds; publish per-build `SHA256SUMS`.
5. **Secrets and binaries.** Never commit `.pfx`, `.pvk`, `.onnx`, or `.dll` files, and never paste
   account GUIDs, tokens, or local user paths into shipped copy or screenshots.
6. **Identity drift.** `Identity Name` / `Publisher` / `PublisherDisplayName` must be copied from
   Partner Center, not typed by hand. One wrong character changes the package family name.
7. **Startup task left enabled.** `Enabled="true"` in the manifest triggers a policy review; keep it
   `false` and let the user opt in from the tray menu.
8. **Telemetry wording.** If the app truly makes zero network calls, say so consistently in the
   listing, privacy policy, and certification notes — contradictions stall certification.
