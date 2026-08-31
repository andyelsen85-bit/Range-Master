---
name: German UI language
description: Project-wide language convention for Portal and terminal user interfaces.
---

Use natural, consistent German for all user-facing Portal and terminal labels, messages, warnings, dialogs, and accessibility text. Prefer formal `Sie` wording in Portal prose and concise uppercase German on the embedded terminal.

**Why:** Luxembourgish interface text accumulated spelling and consistency errors, so the operator chose German as the single default language rather than maintaining a language selector.

**How to apply:** Translate newly introduced visible text to German, but do not rename existing routes, API values, enum members, protocol identifiers, database fields, or code symbols solely for localization.

The terminal's built-in Montserrat fonts only contain basic ASCII and LVGL symbols. Use the shared UI font wrappers with their compact Latin fallback for every LVGL label; sanitize Portal-provided display strings through the same supported UTF-8 policy.

**Why:** Raw accented text otherwise renders as tiny missing-glyph boxes, while blanket ASCII transliteration damages German labels and names.

**How to apply:** Add any newly required non-ASCII glyph to every enabled fallback size and to the inbound display sanitizer together, then verify firmware image size and rendering on hardware.