# Terminal UI layout acceptance

These changes target the real ESP32-P4 terminal only. The emulator was not
modified or used for acceptance. Existing score colors and fonts are retained.

## Automated checks

```sh
python artifacts/firmware/tests/test_ui_layout.py
python artifacts/firmware/tests/test_history_scorecard.py
python artifacts/firmware/tests/test_harakiri_players.py
python artifacts/firmware/tests/test_font_coverage.py
```

The pure layout test covers Luxembourg summer/winter timestamp conversion,
equal shot-cell widths, exact table width, the measured TOTAL width rule,
the 4+3 mode grid, queue captions and the three sync-card lines. Scorecard tests
exercise the shared rendering code, digit colors and saved machine labels.
Font coverage checks the German text and symbols used by the UI.

`TOTAL` is measured using the selected LVGL font and allocated enough width to
remain on one line. This avoids needing the optional sigma fallback, which
is absent from the existing fonts. Missing machine headings show `-`, never an
invented machine or numeric position.

## Physical acceptance — not performed in this workspace

No hardware is attached. A successful target build and host tests do not prove
the appearance or touch behavior. Do not call these screens visually verified
until real-terminal photographs have been reviewed.

After syncing all pending data, flash normally without erasing NVS/FAT. Capture:

1. Ergebnisse with one player: local timestamp, complete machine headings,
   centered colored digits, equal cells and one-line TOTAL.
2. Ergebnisse with six players and eighteen shots: complete table without
   scrolling, no clipped total/border, readable shortened names.
3. Spielmodus: equal 4+3 grid, white selected text, centered uppercase queue
   button, complete A–H machine row including the left edge of A.
4. Spieler des Tages with at least four players: single-line header, fixed
   status area, single-row card controls and 56 px touch targets.
5. Dashboard after a successful sync and with a pending action: three distinct
   lines, correct Aktion/Aktionen wording, local last-sync time, inventory
   warning in its own orange row rather than between buttons.

Photographs and a brief observation for each screen are still required.
