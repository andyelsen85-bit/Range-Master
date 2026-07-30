import React, { useState, useEffect } from 'react';

const BASE = import.meta.env.BASE_URL;
const img = (name: string) => `${BASE}screenshots/${name}`;

/* ── nav structure ──────────────────────────────────────────── */
const NAV = [
  { id: 'iwwerblick',    label: '① Iwwerblick' },
  { id: 'schéissdag',   label: '② Normalen Schéissdag' },
  { id: 'dashboard',    label: '③ Terminal – Dashboard' },
  { id: 'kredite',      label: '④ Terminal – Spiller vum Dag' },
  { id: 'start',        label: '⑤ Terminal – Spill Astellen' },
  { id: 'spill',        label: '⑥ Terminal – Spill spillen' },
  { id: 'spillerverwaltung', label: '⑦ Terminal – Spillerverwaltung' },
  { id: 'einstellungen', label: '⑧ Terminal – Astellungen' },
  { id: 'geschichte',   label: '⑨ Terminal – Spillgeschicht' },
  { id: 'portal',       label: '⑩ Portal' },
];

/* ── helpers ────────────────────────────────────────────────── */
function Section({ id, title, children }: { id: string; title: string; children: React.ReactNode }) {
  return (
    <section id={id} className="mb-16 scroll-mt-6">
      <h2 className="text-2xl font-bold text-gray-900 border-b-2 border-orange-500 pb-2 mb-6">{title}</h2>
      {children}
    </section>
  );
}

function Step({ n, title, children }: { n: number; title: string; children: React.ReactNode }) {
  return (
    <div className="flex gap-4 mb-5">
      <div className="shrink-0 w-8 h-8 rounded-full bg-orange-500 text-white flex items-center justify-center font-bold text-sm">{n}</div>
      <div>
        <div className="font-semibold text-gray-900 mb-1">{title}</div>
        <div className="text-gray-600 text-sm leading-relaxed">{children}</div>
      </div>
    </div>
  );
}

function Screenshot({ src, caption }: { src: string; caption: string }) {
  return (
    <figure className="my-6 rounded-xl overflow-hidden border border-gray-200 shadow-sm">
      <img src={src} alt={caption} className="w-full block" />
      <figcaption className="px-4 py-2 text-xs text-gray-500 bg-gray-50 border-t border-gray-200">{caption}</figcaption>
    </figure>
  );
}

function InfoBox({ children }: { children: React.ReactNode }) {
  return (
    <div className="bg-orange-50 border border-orange-200 rounded-lg p-4 mb-5 text-sm text-orange-900 leading-relaxed">{children}</div>
  );
}

function TipBox({ children }: { children: React.ReactNode }) {
  return (
    <div className="bg-blue-50 border border-blue-200 rounded-lg p-4 mb-5 text-sm text-blue-900 leading-relaxed">
      <span className="font-bold">💡 Tipp: </span>{children}
    </div>
  );
}

function Table({ rows }: { rows: [string, string][] }) {
  return (
    <table className="w-full text-sm mb-6 border-collapse">
      <tbody>
        {rows.map(([label, val]) => (
          <tr key={label} className="border-b border-gray-100">
            <td className="py-2 pr-4 font-semibold text-gray-700 whitespace-nowrap w-40">{label}</td>
            <td className="py-2 text-gray-600">{val}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

/* ── main ───────────────────────────────────────────────────── */
export default function App() {
  const [active, setActive] = useState('iwwerblick');

  useEffect(() => {
    const onScroll = () => {
      for (const sec of [...NAV].reverse()) {
        const el = document.getElementById(sec.id);
        if (el && el.getBoundingClientRect().top <= 80) {
          setActive(sec.id);
          break;
        }
      }
    };
    window.addEventListener('scroll', onScroll, { passive: true });
    return () => window.removeEventListener('scroll', onScroll);
  }, []);

  return (
    <div className="min-h-screen bg-gray-50 flex">

      {/* ── Sidebar ── */}
      <aside className="hidden lg:flex flex-col w-64 shrink-0 fixed top-0 left-0 h-screen bg-white border-r border-gray-200 overflow-y-auto z-20">
        <div className="px-5 py-5 border-b border-gray-100">
          <div className="flex items-center gap-2 mb-1">
            <div className="w-7 h-7 rounded-full bg-orange-500 flex items-center justify-center">
              <svg viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5" className="w-4 h-4">
                <circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="3"/>
              </svg>
            </div>
            <span className="font-bold text-gray-900 text-sm tracking-wide">RANGE-MASTER</span>
          </div>
          <p className="text-xs text-gray-400">Benotzerhandbuch · F.S.H.C.L. Wolz</p>
        </div>
        <nav className="flex-1 py-3 px-2">
          {NAV.map(item => (
            <a
              key={item.id}
              href={`#${item.id}`}
              onClick={() => setActive(item.id)}
              className={`block px-3 py-2 rounded-lg text-xs font-medium mb-0.5 transition-colors
                ${active === item.id
                  ? 'bg-orange-50 text-orange-700 font-semibold'
                  : 'text-gray-500 hover:text-gray-800 hover:bg-gray-50'}`}
            >
              {item.label}
            </a>
          ))}
        </nav>
        <div className="px-4 py-4 border-t border-gray-100 text-[10px] text-gray-400">
          Range-Master v1.5 · F.S.H.C.L. Sektioun Wolz
        </div>
      </aside>

      {/* ── Content ── */}
      <main className="flex-1 lg:ml-64 max-w-3xl mx-auto px-6 lg:px-10 py-10">

        {/* Header */}
        <div className="mb-12">
          <div className="flex items-start justify-between gap-4 flex-wrap">
            <div>
              <div className="inline-flex items-center gap-2 bg-orange-100 text-orange-700 text-xs font-bold px-3 py-1 rounded-full mb-4 uppercase tracking-widest">
                Benotzerhandbuch
              </div>
              <h1 className="text-4xl font-extrabold text-gray-900 mb-3">Range-Master</h1>
              <p className="text-lg text-gray-500">F.S.H.C.L. Sektioun Wolz — Vollstänneg Benotzerguide</p>
            </div>
            <button
              id="print-btn"
              onClick={() => window.print()}
              className="shrink-0 mt-1 inline-flex items-center gap-2 bg-orange-500 hover:bg-orange-600 active:bg-orange-700 text-white text-sm font-semibold px-4 py-2.5 rounded-lg transition-colors shadow-sm"
            >
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="w-4 h-4">
                <path d="M6 9V2h12v7"/>
                <path d="M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2"/>
                <rect x="6" y="14" width="12" height="8"/>
              </svg>
              Als PDF drécken
            </button>
          </div>
        </div>

        {/* ─────── 1. IWWERBLICK ─────── */}
        <Section id="iwwerblick" title="① Iwwerblick">
          <p className="text-gray-600 mb-5 leading-relaxed">
            Range-Master ass e komplette Schéissverwaltungssystem fir d'F.S.H.C.L. Sektioun Wolz. Et besteet aus zwee Haaptdeeler:
          </p>
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4 mb-6">
            <div className="bg-white border border-gray-200 rounded-xl p-5 shadow-sm">
              <div className="text-2xl mb-2">🖥️</div>
              <h3 className="font-bold text-gray-900 mb-1">Terminal (ESP32)</h3>
              <p className="text-sm text-gray-500">Den Touch-Terminal um Schéissstand — hei gi Spiller uginn, Spiller gespillt, a Resultater gespaichert.</p>
            </div>
            <div className="bg-white border border-gray-200 rounded-xl p-5 shadow-sm">
              <div className="text-2xl mb-2">🌐</div>
              <h3 className="font-bold text-gray-900 mb-1">Portal (Web)</h3>
              <p className="text-sm text-gray-500">D'Websäit — hei gesinn d'Memberden hier Statistiken, Ranglëschten, an d'Admins verwalt Memberen a Spiller.</p>
            </div>
          </div>
          <InfoBox>
            <strong>Wichteg:</strong> Terminal a Portal si mateneen verbonnen. No engem Schéissdag sync den Terminal seng Resultater an de Portal, wou se dann op der Ranglëscht an de Profilsäiten erschéngen.
          </InfoBox>
          <Table rows={[
            ['Portal URL', 'https://rangemaster.hostzone.lu'],
            ['Offline-Betrieb', 'Terminal funktionéiert ouni Internet — Sync méiglech nodeems Verbindung erëm do ass'],
            ['Kredittsystem', 'All Spiller brauch 1 Kredit pro Spill — muss virum Spill dobäigesat ginn'],
          ]} />
        </Section>

        {/* ─────── 2. NORMALEN SCHÉISSDAG ─────── */}
        <Section id="schéissdag" title="② Normalen Schéissdag — Schrëtt fir Schrëtt">
          <p className="text-gray-600 mb-6 leading-relaxed">
            Hei ass d'komplett Abrëll vun engem normale Schéissdag — vum Ufank bis zum Sync um Enn.
          </p>

          <h3 className="font-semibold text-gray-800 mb-3 text-base uppercase tracking-wider text-orange-600">Virum Schéissen</h3>
          <Step n={1} title="Portal opmaachen (optional)">
            Iwwerpréif ob all Schützen e Portal-Kont hunn. Wann net, kënne se um Terminal als lokal Spiller dobäigesat ginn.
          </Step>
          <Step n={2} title="Terminal astellen">
            Terminal aschalten a kontrolléieren datt den Écran de Dashboard weist. Den API-URL a Key muss ënner <em>Astellungen → Portal API</em> korrekt aginn sinn.
          </Step>
          <Step n={3} title="Spillerlëscht lueden">
            Op "Spiller vum Dag" klicken, dann op "Laden" — Terminal luet d'Spillerlëscht vum Portal.
          </Step>
          <Step n={4} title="Schützen fir den Dag asetzen a Kreditter verdeelen">
            All Schütz dee schéisse wëll: sichen, dobäisetzen, a Kreditten uginn (z.B. 3 Kreditten = 3 Spiller).
          </Step>

          <h3 className="font-semibold text-gray-800 mb-3 mt-7 text-base uppercase tracking-wider text-orange-600">Während dem Schéissen</h3>
          <Step n={5} title='"Spill Start" drécken'>
            Vum Dashboard op "Spill Start" klicken.
          </Step>
          <Step n={6} title="Schützen an Posten assiéieren">
            Fir all Posten (P1–P6) den entspriechende Schütz wielen. 1 Kredit gëtt pro Schütz automatesch ofgezunn beim Starten.
          </Step>
          <Step n={7} title="Modus a Schanzen wielen">
            Normal, Harakiri, oder Custom Modus auswielen. Schanzen kontrolléieren datt déi aktiv sinn déi gebraucht ginn.
          </Step>
          <Step n={8} title="Spill starten a Resultater aginn">
            Op "STARTEN" drécken. Fir all Taube: <strong>Getraff</strong> oder <strong>Net Getraff</strong> drécken. Doublette H gëtt als 2 Tauben gezielt.
          </Step>
          <Step n={9} title="Resultater kucken">
            No dem leschten Duerchgang erschéngen d'Resultater automatesch mam Ranking vun all Schützen.
          </Step>

          <h3 className="font-semibold text-gray-800 mb-3 mt-7 text-base uppercase tracking-wider text-orange-600">Um Enn vum Dag</h3>
          <Step n={10} title="Alles syncen">
            Um Dashboard op "Alles syncen" klicken — all Spiller, nei Schützen, a Kredittevenementer ginn an de Portal geschéckt.
          </Step>
          <Step n={11} title="Kontrolléieren am Portal">
            Am Portal aloggen a kucken datt d'Resultater an de Ranglëschten erschéngen.
          </Step>

          <TipBox>
            Den Terminal behält all Resultater lokal gespaichert och wann keng Internetverbindung do ass. De Sync kann och méi spéit gemaach ginn.
          </TipBox>
        </Section>

        {/* ─────── 3. DASHBOARD ─────── */}
        <Section id="dashboard" title="③ Terminal – Dashboard">
          <Screenshot src={img('dashboard.jpg')} caption="Den Haaptbildschierm vum Terminal (Dashboard)" />
          <p className="text-gray-600 mb-5 leading-relaxed">
            Den Dashboard ass den Haaptbildschierm vum Terminal. Hei gesitt dir d'leschte Spiller, an hutt Zougang zu allen Haaptfunktiounen.
          </p>
          <h3 className="font-semibold text-gray-800 mb-3">Knäppercher am Sidebar</h3>
          <Table rows={[
            ['Spill Start', 'Neie Spill astellen (Schützen, Modus, Schanzen wielen)'],
            ['Spiller vum Dag', 'Schützen fir haut dobäisetzen a Kreditten verdeelen'],
            ['Spiller', 'Spillernumm, Email oder Portal-Zougang änneren'],
            ['Astellungen', 'API-Verbindung, Schanzen, Custom Modi, System'],
            ['Spillgeschicht', 'Lëscht vun alle gespillten Spiller (läscht 50)'],
          ]} />
          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Offline Queue / Sync Panel</h3>
          <p className="text-sm text-gray-600 mb-3">Ënnen am Sidebar weist d'Offline Queue wéivill Spiller nach net synchroniséiert goufen. Mam Knapp "Alles syncen" ginn all Dateje geschéckt.</p>
          <Table rows={[
            ['0 Spiller', 'Alles synchroniséiert — keng pending Spiller'],
            ['X Spiller', 'X Spiller waarden nach op Sync — bei nächster Verbindung syncen'],
            ['X Nei', 'X nei lokal ugeluete Schütze mussen nach am Portal ugeluecht ginn'],
          ]} />
          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Leschte Spiller</h3>
          <p className="text-sm text-gray-600">Den Haaptberäich weist d'lescht 4 Spiller mat Datum, Modus, a Punkten vun all Schütz. D'Faarf vun de Punkten weist:
            <span className="text-green-600 font-bold"> Gréng ≥90 %</span>,
            <span className="text-orange-600 font-bold"> Orange ≥70 %</span>,
            <span className="text-gray-400 font-bold"> Grau ënnert 70 %</span>.
          </p>
        </Section>

        {/* ─────── 4. KREDITE ─────── */}
        <Section id="kredite" title="④ Terminal – Spiller vum Dag (Kreditten)">
          <Screenshot src={img('kredite-screen.jpg')} caption="Spiller vum Dag — Kredittverwaltung" />
          <p className="text-gray-600 mb-5 leading-relaxed">
            Hei gitt dir Schützen fir den aktuellen Dag dobäi a verdeelt Kreditten. All Spill käscht 1 Kredit — ouni Kredit kann keen Spiller um Spill deelhuelen.
          </p>
          <h3 className="font-semibold text-gray-800 mb-3">Spiller dobäisetzen</h3>
          <Step n={1} title="Spillerlëscht lueden">
            Op "Laden" klicken fir d'Spillerlëscht vum Portal ze lueden. Falls keng Verbindung: Terminal benotzt den lokale Cache.
          </Step>
          <Step n={2} title="Spiller sichen">
            Numm an der Sichbar aginn — d'Lëscht filtert automatesch. Op den Numm klicken fir en dobäizusetzen.
          </Step>
          <Step n={3} title="Kreditten uginn">
            Am rechten Panel erschéngt de Spiller. Op <strong>+</strong> klicken fir Kreditten ze ginn (z.B. 3× fir 3 Spiller).
          </Step>
          <h3 className="font-semibold text-gray-800 mb-3 mt-5">Kreditten verwalt</h3>
          <Table rows={[
            ['+ Kredit', 'Eng Kredittogebung dobäisetzen'],
            ['− Kredit', 'Eng Kredit manuell ofzéien (z.B. Remboursement)'],
            ['Läschen', "Den Spiller ganz vun haut senger Lëscht läschen (Feeler)"],
          ]} />
          <InfoBox>
            <strong>Wichteg:</strong> Kreditten sinn Dages-Kreditten — si ginn net op den nächste Dag iwwerdroe. All Dag muss nei ugefaange ginn.
          </InfoBox>
          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Neie lokale Spiller uleeën</h3>
          <p className="text-sm text-gray-600">Falls e Schütz nach net am Portal ageschriwwen ass, kann e direkt um Terminal ugeleecht ginn. Den Terminal generéiert automatesch eng WLZ-Nummer. Beim nächste Sync gëtt de Spiller an de Portal iwwerdroe.</p>
        </Section>

        {/* ─────── 5. SPILL ASTELLEN ─────── */}
        <Section id="start" title="⑤ Terminal – Spill Astellen">
          <Screenshot src={img('start-screen.jpg')} caption="Spill Astellen — Schützen, Modus a Schanzen" />
          <p className="text-gray-600 mb-5 leading-relaxed">
            Hei gëtt e neit Spill konfiguréiert. Lénks ginn d'Posten mat Schützen besat, riets gëtt de Modus a d'Schanzen gewielt.
          </p>
          <h3 className="font-semibold text-gray-800 mb-3">Schützen op Posten assiéieren</h3>
          <Step n={1} title="Posten opmaachen">
            Op den <strong>+</strong>-Knapp nieft engem Posten (P1–P6) klicken. Eng Sichbar erschéngt direkt ënnert dem Posten.
          </Step>
          <Step n={2} title="Schütz sichen a wielen">
            Numm aginn, op de Schütz klicken — en gëtt dem Posten zougewisen. Nëmme Schützen mat Kreditten fir haut sinn wëhlbar.
          </Step>
          <Step n={3} title="Schütz ersetzen oder ewechhuelen">
            Op dat <strong>🗑 Pabeierkorb</strong>-Symbol klicken fir en Schütz vun engem Posten ze läschen a en aneren ze wielen.
          </Step>
          <InfoBox>
            <strong>6 Schützen:</strong> Schütz op Posten P6 schéisst am Spill vu Posten 1 aus — d'Rotation erfolgt automatesch.
          </InfoBox>

          <h3 className="font-semibold text-gray-800 mb-3 mt-5">Spillmodus wielen</h3>
          <Table rows={[
            ['Normal', 'Schanzen A → G → H (Doublette) vun der Rei no'],
            ['Harakiri', 'Schanzen A–G zufälleg, H ëmmer um Enn'],
            ['Custom 1–4', 'Fräi konfiguréierbar (Astellungen → Custom Modi)'],
          ]} />

          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Schanzen aktivéieren</h3>
          <p className="text-sm text-gray-600 mb-3">D'Schanzen A–G sinn eenzel Déeggercher (1 Taube). Schanz H ass d'Doublette (2 Tauben). Op eng Schanz klicken fir se an/aus ze schalten. Nëmme fir Normal/Harakiri — Custom Modi benotzen hier eegen Sequenz.</p>

          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Spill starten</h3>
          <p className="text-sm text-gray-600">Wann mindestens 1 Schütz mat Kredit zougewisen ass, gëtt de <strong>STARTEN</strong>-Knapp aktiv. Op klicken — 1 Kredit pro Schütz gëtt automatesch ofgezunn an d'Spill fänkt un.</p>
        </Section>

        {/* ─────── 6. SPILL SPILLEN ─────── */}
        <Section id="spill" title="⑥ Terminal – Spill spillen">
          <p className="text-gray-600 mb-5 leading-relaxed">
            Während dem Spill weist den Terminal fir all Schütz: säin Numm, seng aktuell Posten-Positioun, d'Schanz déi fiert, an d'Resultatknäppercher.
          </p>
          <h3 className="font-semibold text-gray-800 mb-3">Oflaf vun engem Duerchgang</h3>
          <Step n={1} title="Schütz a Schanz">
            Den Terminal weist: Schützenum, Posten, Schanz-Buschtaf. De Schütz stellt sech un deen entspriechende Posten.
          </Step>
          <Step n={2} title="Resultat aginn — eenzeg Taube (A–G)">
            <strong>Getraff (2 Pkt)</strong> — éischte Schoss getraff.<br/>
            <strong>2. Schoss (1 Pkt)</strong> — éischte Schoss verfehlt, zweete getraff.<br/>
            <strong>Net Getraff (0 Pkt)</strong> — béid Schëss verfehlt.
          </Step>
          <Step n={3} title="Resultat aginn — Doublette (H)">
            Doublette H gëtt als 2 eenzel Tauben behandelt. Éischt Taube: Getraff / Net Getraff, dann zweete Taube.
          </Step>
          <Step n={4} title="Wiederholen / Iwwerspringen">
            <strong>Wiederholen:</strong> Falls Technik-Probleemer: aktuell Taube kann nach eng Kéier ofgeschoss ginn.<br/>
            <strong>Iwwerspringen:</strong> Taube gëtt iwwersprangen (0 Punkte).
          </Step>
          <Step n={5} title="Spill Ofbriechen">
            Mat dem Zréck-Knapp kann e laafendes Spill ofgebrach ginn. D'Resultater ginn net gespaichert.
          </Step>
          <h3 className="font-semibold text-gray-800 mb-3 mt-5">No dem leschten Duerchgang</h3>
          <p className="text-sm text-gray-600">D'Resultater erschéngen automatesch mat Ranking. De Spill gëtt an der Offline Queue gespaichert a beim nächste Sync an de Portal geschéckt.</p>
        </Section>

        {/* ─────── 7. SPILLERVERWALTUNG ─────── */}
        <Section id="spillerverwaltung" title="⑦ Terminal – Spillerverwaltung">
          <Screenshot src={img('spillerverwaltung-screen.jpg')} caption="Spillerverwaltung — Ännerungen a Passwuertsreset" />
          <p className="text-gray-600 mb-5 leading-relaxed">
            Hei kënnt dir Spillernimm, E-Mail-Adressen, a Portal-Zougang änneren — direkt vum Terminal aus.
          </p>
          <h3 className="font-semibold text-gray-800 mb-3">Spiller sichen a wielen</h3>
          <Step n={1} title="Spillerlëscht lueden">
            Op "Laden" klicken fir aktuell Spillerlëscht vum Portal ze lueden.
          </Step>
          <Step n={2} title="Spiller sichen">
            Numm an der Sichbar aginn — op de Spiller klicken fir en auszewielen.
          </Step>
          <Step n={3} title="Ännerunge maachen">
            Numm, E-Mail, oder Portal-Zougang (aktiv/inaktiv) änneren an op "Änneren" klicken. D'Ännerung gëtt an der Queue gespaichert.
          </Step>
          <Step n={4} title="Passwuert zrécksetzen">
            Op "Passwuert zrécksetzen" klicken — beim nächste Sync kritt de Spiller eng nei E-Mail mam neie Passwuert.
          </Step>
          <h3 className="font-semibold text-gray-800 mb-3 mt-5">Statuse vun Ännerungen</h3>
          <Table rows={[
            ['Pending', 'Ännerung nach net synchroniséiert'],
            ['Synced', 'Ännerung am Portal ukomm'],
            ['Email sent', 'Passwuertreset-E-Mail erfolgräich geschéckt'],
            ['Email failed', 'E-Mail net ukomm — SMTP kontrolléieren'],
          ]} />
          <TipBox>
            Mat "Erledegt läschen" ginn all ofgeschlossenen Ännerunge vun der Lëscht geläscht. Pendente Ännerunge bleiwen bis zum Sync.
          </TipBox>
        </Section>

        {/* ─────── 8. ASTELLUNGEN ─────── */}
        <Section id="einstellungen" title="⑧ Terminal – Astellungen">
          <Screenshot src={img('einstellungen-screen.jpg')} caption="Astellungen — Portal API Tab" />
          <p className="text-gray-600 mb-5 leading-relaxed">
            Déi Astellungen hunn 4 Tabs: <strong>Portal API</strong>, <strong>Schanzen</strong>, <strong>Custom Modi</strong>, a <strong>System</strong>.
          </p>

          <h3 className="font-semibold text-gray-800 mb-3">Tab: Portal API</h3>
          <p className="text-sm text-gray-600 mb-3">Hei ginn d'Verbindungsdate fir de Portal aginn.</p>
          <Table rows={[
            ['API Endpoint URL', 'https://rangemaster.hostzone.lu (ouni /api um Enn!)'],
            ['API Key', 'Den Terminal-API-Key (ufänkt mat tm_...)'],
          ]} />
          <p className="text-sm text-gray-600 mb-5">Nodeems Späicheren op "Verbindung testen" klicken fir ze kontrolléieren datt d'Verbindung funktionéiert.</p>

          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Tab: Schanzen</h3>
          <p className="text-sm text-gray-600 mb-3">Weist all 8 Schanzen (A–H). Op eng Schanz klicken fir se ze aktivéieren oder deaktiivéieren. Schanz H ass d'Doublette (2 Tauben). Dës Astellung gëllt fir Normal- a Harakiri-Modus.</p>

          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Tab: Custom Modi</h3>
          <p className="text-sm text-gray-600 mb-3">Fir Custom 1–4 kann eng eegen Schanzsequenz definéiert ginn. Op Buschtawen klicken fir se der Sequenz derbäizefügen — op e Badge an der Sequenz klicken fir en erauszehuelen. Unzuel vun de Läufe (1 oder 2) och wielbar.</p>
          <InfoBox>
            Custom Modi ignoréieren d'Schanzen-Aktiv-Astellung — nëmme déi an der Sequenz definéiert Schanzen ginn benotzt.
          </InfoBox>

          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Tab: System</h3>
          <p className="text-sm text-gray-600">Weist Software-Versioun, Hardware-Infos, a eng Erklärung vun all Spillmodus. Nëmme fir Informatiounszwecker.</p>

          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Quick-Start per URL (fortgeschratt)</h3>
          <p className="text-sm text-gray-600">Den Terminal kann mat virconfiguréierten Astellungen opgeruff ginn:</p>
          <code className="block bg-gray-100 rounded-lg px-4 py-3 text-xs font-mono text-gray-700 mb-3 break-all">
            https://rangemaster.hostzone.lu/emulator/?apiUrl=https://rangemaster.hostzone.lu&apiKey=tm_...
          </code>
          <p className="text-sm text-gray-600">URL a Key ginn automatesch gespaichert wann den Terminal opmaacht.</p>
        </Section>

        {/* ─────── 9. SPILLGESCHICHT ─────── */}
        <Section id="geschichte" title="⑨ Terminal – Spillgeschicht">
          <Screenshot src={img('geschichte-screen.jpg')} caption="Spillgeschicht — lokal gespaichert Spiller" />
          <p className="text-gray-600 mb-5 leading-relaxed">
            D'Spillgeschicht weist all lokal gespillten Spiller vum Terminal — onofhängeg ob se scho synchroniséiert goufen oder net. Maximal 50 Spiller ginn gespaichert.
          </p>
          <Table rows={[
            ['Datum / Auer', 'Wann de Spill gespillt gouf'],
            ['Modus', 'Normal, Harakiri, oder Custom 1–4'],
            ['Läufe × Tauben', 'Spillformat (z.B. 2 × 9 = max 36 Pkt)'],
            ['Schützen + Punkte', 'All Schütz mat senge Gesamtpunkte fir de Spill'],
          ]} />
          <TipBox>
            Falls e Spill net am Portal erschéngt, kann et nach eng Kéier vum Dashboard synct ginn. De Spill bleift an der Geschicht bis d'Grenz vun 50 Spiller erreecht ass.
          </TipBox>
        </Section>

        {/* ─────── 10. PORTAL ─────── */}
        <Section id="portal" title="⑩ Portal">
          <Screenshot src={img('portal-home.jpg')} caption="Portal Login — https://rangemaster.hostzone.lu" />
          <p className="text-gray-600 mb-5 leading-relaxed">
            De Portal ass d'Websäit vum Range-Master System. Jiddereen mam Kont kann sech aloggen a seng Statistiken gesinn. Admins hu Zougang zu all Funktiounen.
          </p>

          <h3 className="font-semibold text-gray-800 mb-3">Aloggen</h3>
          <Step n={1} title="Portal opmaachen">
            Browser opmaachen an op <strong>https://rangemaster.hostzone.lu</strong> goen.
          </Step>
          <Step n={2} title="E-Mail a Passwuert aginn">
            D'E-Mail-Adress a Passwuert aginn an op "Umellen" klicken. Bei éischter Umeldung gouf dat Passwuert per E-Mail geschéckt.
          </Step>
          <Step n={3} title="Passwuert vergiess?">
            Falls Passwuert vergiesst: En Admin muss vum Terminal aus e Passwuertreset maachen (Spillerverwaltung → Passwuert zrécksetzen).
          </Step>

          <h3 className="font-semibold text-gray-800 mb-3 mt-6">Fir Schützen (normal Member)</h3>
          <Table rows={[
            ['Mäi Profil', 'Perséinlech Statistiken, Trefferquote pro Schanz, Verlafschart'],
            ['Ranglëscht', 'Gesamt-Ranglëscht oder filtert no Modus oder Joer'],
            ['Spillgeschicht', 'All eegen Spiller mat Detailer'],
          ]} />

          <h3 className="font-semibold text-gray-800 mb-3 mt-4">Fir Admins</h3>
          <Table rows={[
            ['Spillerverwaltung', 'Nei Member uleeën, Kontoen aktivéieren/deaktiivéieren, Passwierter resetzen'],
            ['Spillverwaltung', 'All Spiller gesinn, Detailer kontrolléieren'],
            ['Astellungen', 'SMTP-Konfiguratioun fir E-Mail-Versand, API-Keys verwalt'],
            ['SMTP Test', "Test-E-Mail schécken fir d'E-Mail-Konfiguratioun ze kontrolléieren"],
          ]} />

          <InfoBox>
            <strong>Admin-Passwuert:</strong> Den Admin-Account gëtt bei der éischter Installatioun duerch de Seed-Skript ugeluecht. Falls verluer: En neie Seed muss am Server ausgeféiert ginn.
          </InfoBox>

          <h3 className="font-semibold text-gray-800 mb-3 mt-5">API-Keys verwalt (Admin)</h3>
          <p className="text-sm text-gray-600 mb-3">
            Jiddereen Terminal brauch säin eegenen API-Key fir sech um Portal z'authentifizéieren. Keys ginn am Admin-Beräich verwalt. All Key ufänkt mat <code className="bg-gray-100 rounded px-1">tm_</code> a muss am Terminal ënner <em>Astellungen → Portal API</em> aginn ginn.
          </p>

          <h3 className="font-semibold text-gray-800 mb-3 mt-5">Ranglëscht a Statistiken</h3>
          <p className="text-sm text-gray-600 mb-3">
            D'Ranglëscht berechent den Duerchschnëtt als Prozentsaz vum maximalem Score (cross-Modus verglach). E Spiller mat 30/36 am Normal-Modus a 14/18 am Custom-Modus gëtt fair vergläichbar — béides sinn 83 %.
          </p>

          <h3 className="font-semibold text-gray-800 mb-3 mt-5">Sync-Zyklus verstoen</h3>
          <div className="bg-gray-50 border border-gray-200 rounded-xl p-5 text-sm text-gray-600 leading-relaxed">
            <div className="flex items-start gap-2 mb-2"><span className="text-orange-500 font-bold shrink-0">1.</span><span>Terminal spillt a speichert Resultater lokal (Offline Queue)</span></div>
            <div className="flex items-start gap-2 mb-2"><span className="text-orange-500 font-bold shrink-0">2.</span><span>Operator dréckt "Alles syncen" um Terminal-Dashboard</span></div>
            <div className="flex items-start gap-2 mb-2"><span className="text-orange-500 font-bold shrink-0">3.</span><span>Terminal schéckt Spiller, nei Schützen, a Kredittevenementer un de Portal-API</span></div>
            <div className="flex items-start gap-2 mb-2"><span className="text-orange-500 font-bold shrink-0">4.</span><span>Portal späichert alles an der Datebank a berechent Statistiken nei</span></div>
            <div className="flex items-start gap-2"><span className="text-orange-500 font-bold shrink-0">5.</span><span>Schützen gesinn hire Rang a Statistiken um Portal aktualiséiert</span></div>
          </div>
        </Section>

        {/* Footer */}
        <div className="mt-16 pt-8 border-t border-gray-200 text-center text-xs text-gray-400">
          Range-Master v1.5 · F.S.H.C.L. Sektioun Wolz · Benotzerhandbuch
        </div>

      </main>
    </div>
  );
}
