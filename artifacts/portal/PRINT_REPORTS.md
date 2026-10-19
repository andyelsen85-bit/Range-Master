# Personal results and print reports

## Privacy contract

Personal results use the authenticated owner scorecard endpoint. Its SQL reads
only the selected player's participants and shots; it does not fetch other
players and then hide them in React. An administrator can inspect a selected
player. Another ordinary player's request receives 403.

The existing shared scorecard endpoint remains unchanged. Selected-player kiosk
print reports also contain only the selected player's row; they are not
club-wide reports. Kiosk device/PIN authorization is unchanged.

## Routes

New, authenticated:

- `GET /api/spieler/:id/spiele/:spielId/scorecard`
- `GET /api/spieler/:id/print-days`
- `GET /api/spieler/:id/print`

Extended kiosk routes:

- `GET /api/public/player/:id/days` additionally returns available `years`.
- `GET /api/public/player/:id/print` supports `zeitraum=jahr&jahr=YYYY`.

Print queries use either `tag=YYYY-MM-DD` or `zeitraum=jahr&jahr=YYYY`.
The private UI is `/resultater/druck`; an administrator can add `spieler=ID`.
The kiosk UI remains `/live/print/:id`.

After deployment inspect `GET /api/health`: its `build.commit`, `build.dirty`
and `reportRoutes` identify the actual running build and mounted report routes.
No new migration is needed: reports read the existing visibility views.

## Statistics and layout

Annual reports use PostgreSQL aggregates over Luxembourg-local year/month
boundaries, not a client-side download of all games. Hidden games and excluded
participants follow the same visible-result rules as daily reports. Annual
reports contain no individual game cards. The twelve month rows include empty
months; an empty year shows “Keine Spiele in diesem Jahr”.

The annual machine breakdown shows A–H and the clay breakdown shows 1–9.
For custom games with later clay positions, all shots still count in the
overall and monthly totals; the displayed 1–9 breakdown remains bounded.

A4 uses 10 mm margins and fixed table layout. Summary columns have explicit
widths so run totals and TOTAL POINTS cannot consume the shot columns.
`usePrintFit` measures real intrinsic widths in a temporary print-layout mode,
reserves fixed columns/padding, applies `--print-scale` only to A4 fonts
(minimum 7 pt) and selects a named landscape page when needed. It runs on
layout, resize, `beforeprint`, `afterprint` and the print button.
The 80 mm receipt rules remain separate.

## Verification

```sh
pnpm --filter @workspace/api-server test:player-reports
pnpm --filter @workspace/api-server test:queue
pnpm --filter @workspace/api-server test:sync-contract
pnpm --filter @workspace/api-server typecheck
pnpm --filter @workspace/portal exec tsc --noEmit
```

The isolated PostgreSQL report test covers owner/admin/403 behavior, no other
names, Luxembourg year/month boundaries, hidden games, per-player exclusions,
unchanged raw results, twelve months, an empty year and equality with daily
statistics. CI includes the report test.

For an interactive development-only browser acceptance fixture:

```sh
pnpm --filter @workspace/db exec node tests/report-browser-fixture.mjs
# Use the synthetic fixture accounts/IDs from the ignored generated-artifacts
# fixture file. The fixture-only login is documented in the script.
# Run browser checks and headless page.pdf, then always clean up:
pnpm --filter @workspace/db exec node tests/report-browser-fixture.mjs --cleanup
```

Never run this fixture on production. It creates only synthetic accounts and
scored games, not credit, billing, inventory or terminal records. Cleanup checks
the fixture's ownership before deleting only those created records.

The six-player, two-run print stress fixture deliberately renders a shared
fixture scorecard through a browser-intercepted report response. This is a layout
test, not an alternative private API. Actual private HTTP responses and DOM
privacy are tested separately with one player row. PDF evidence is generated
under `generated-artifacts/verification/` and is not committed or deployed.
