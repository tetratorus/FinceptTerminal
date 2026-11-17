# Release Smoke Test Suite

Run this suite on every major release candidate (and on minor releases when the
change touches the areas listed). It answers one question: **does a user who
installs this build get a working terminal?** It is not a regression suite —
deep coverage stays in the unit tests and `--selftest-*` checks.

Budget: Tier 1 is unattended (~10 min). Tiers 2–3 take one tester ~45 min per
platform. A release ships only when every **P0** case passes on every shipped
platform (Windows x64, Linux x64, macOS arm64).

| Tier | What | How |
|------|------|-----|
| 1 | Build health: unit tests, headless self-tests, every-screen construction walk | `.github/scripts/release_smoke.sh` |
| 2 | Install, launch, auth, core navigation — must work for every user | Manual, P0 |
| 3 | Core product flows — markets, watchlist, portfolio, trading, research, AI | Manual, P0/P1 |

Record results in a copy of the [results template](#results-template) and attach
it, plus a screen recording of Tiers 2–3, to the release PR / tag.

---

## Before you start

- Use the **release artifact** (installer / AppImage / .deb / .dmg), not a dev build.
- Test on a **clean machine or a fresh OS user** for SMK-01, and on a machine with
  the **previous release installed** for SMK-02.
- Have ready: a test Fincept account, one LLM API key (any provider), a CSV of
  3–5 symbols for the watchlist import, and network access to `api.fincept.in`.
- Never use a real broker account or LIVE mode for this suite. All trading cases
  are **PAPER** only.

## Tier 1 — Automated gate

```bash
cd fincept-qt
CC=gcc-12 CXX=g++-12 cmake --preset linux-release -DCMAKE_PREFIX_PATH=<qt>/6.8.3/gcc_64 -DFINCEPT_BUILD_TESTS=ON
cmake --build build/linux-release
bash ../.github/scripts/release_smoke.sh build/linux-release/FinceptTerminal build/linux-release release-smoke
```

On Windows/macOS pass the packaged binary (`FinceptTerminal.exe`, or
`FinceptTerminal.app/Contents/MacOS/FinceptTerminal`) and omit the build dir.

| ID | Check | Pass when |
|----|-------|-----------|
| T1-01 | Unit tests (`ctest`) | All suites pass |
| T1-02 | Headless self-tests (every flag from `--selftest-list`) | Every flag exits 0 |
| T1-03 | Screen walk (`--smoke-test`) | Every registered screen constructs; process exits 0 within 600 s |

The script writes `summary.md` and one log per check. A failing T1-03 names the
last screen it was constructing. Each run uses its own `--profile`
(`release-smoke*`), so it never touches a real user profile.

Do not start Tier 2 until Tier 1 is green.

## Tier 2 — Install, launch, auth (all P0)

| ID | Flow | Steps | Expected |
|----|------|-------|----------|
| SMK-01 | Clean install + first launch | Install the artifact on a clean machine. Launch from the Start menu / launcher / Applications. | App starts with no missing-DLL/library or Gatekeeper error. Python setup screen appears and installs the essentials (or "Continue without Python" works). Login screen follows. |
| SMK-02 | Upgrade in place | On a machine with the previous release and an existing profile, install over it and launch. | Version in **About** is the new one. Existing login, watchlists, portfolios, notes and settings are still there. No crash-recovery prompt. |
| SMK-03 | Register / login | Register a new account (or log in with the test account). Log out and log back in. | Login succeeds and lands on the Dashboard. Wrong password shows an inline error, not a crash. |
| SMK-04 | Forgot password | From Login, open **Forgot password**, submit the test email. | Confirmation shown; Back returns to Login. |
| SMK-05 | PIN lock | Settings → Security: set a PIN. Use **Lock Now**. Unlock with a wrong PIN, then the right one. | Terminal locks; wrong PIN rejected; right PIN restores the same layout. Restarting the app asks for the PIN. |
| SMK-06 | Dashboard | After login, wait 30 s. | Ticker bar and Market Pulse populate with live values; status bar shows connected; no error toasts. |
| SMK-07 | Navigation | Open 10 screens via the toolbar menus and 5 via the command bar (type the screen name, Enter): Markets, News, Watchlist, Portfolio, Equity Trading, Crypto Trading, Backtesting, AI Chat, Economics, Screener, Equity Research, Settings, Docs, About, Launchpad. | Each opens in a dock panel with content (no blank panel). Ctrl+Tab cycles panels. |
| SMK-08 | Workspace persistence | Open 4 panels, drag one to a new dock position, tear one off into a floating window. Quit and relaunch. | The same panels and positions are restored. |
| SMK-09 | Multi-window | Ctrl+Shift+N (or Launchpad → **New Window**). Open a screen in the new window. Close it. | Second window works independently; closing it leaves the primary running. |
| SMK-10 | Crash recovery | Kill the process (Task Manager / `kill -9`) with several panels open. Relaunch. | Crash-recovery dialog offers to restore; Restore brings back the panels. |

## Tier 3 — Core flows

| ID | Pri | Flow | Steps | Expected |
|----|-----|------|-------|----------|
| SMK-11 | P0 | Markets quotes | Open Markets. Pick 2 regions/asset classes. | Tables fill with prices; values update on F5 / refresh. |
| SMK-12 | P0 | Watchlist | Create a watchlist, add AAPL, MSFT and BTC-USD, remove one, restart the app. Import the holdings CSV via **Import CSV**. | Quotes show for each symbol; removed symbol stays gone after restart; CSV symbols appear. |
| SMK-13 | P0 | News | Open News. Use J/K to move, Enter to open, Esc to close. | Headlines load with timestamps; keyboard navigation works; article opens. |
| SMK-14 | P0 | Portfolio | **Create Portfolio**, add 2 holdings manually. Export it (⋯ → CSV/JSON), then **Import JSON…** into a new portfolio. Open the heatmap and performance views. | Holdings, P&L and charts render; the import matches the export; data survives a restart. |
| SMK-15 | P0 | Paper equity order | Equity Trading in **PAPER** mode: pick a symbol, place a market buy, then a limit sell away from the market, then cancel the limit. | Order confirm dialog shows PAPER; market order fills into positions/holdings; limit order appears as open and cancels cleanly. |
| SMK-16 | P1 | Paper crypto order | Crypto Trading in paper mode: place and cancel a small limit order. | Order book streams; order appears and cancels. |
| SMK-17 | P0 | Charting | From Equity Trading or Equity Research, open a chart for AAPL; switch timeframe and add one indicator. | Candles render; timeframe and indicator update the chart. |
| SMK-18 | P1 | Equity research | Equity Research → search MSFT. | Company profile, financials and key ratios load. |
| SMK-19 | P1 | Screener | Run a screen with 2 filters. | Results table populates and sorts. |
| SMK-20 | P1 | Economics data | Economics (or DBnomics / Gov Data): load one series (e.g. US CPI). | Series chart and table render. |
| SMK-21 | P0 | Backtest | Algo Trading → pick a built-in strategy → **Run Backtest** (or Backtesting screen with defaults). | Backtest completes; equity curve and metrics shown. Requires the Python env from SMK-01. |
| SMK-22 | P0 | AI Chat | Settings → LLM Config: add the test key. AI Chat (F9): ask "What is AAPL trading at?". | Streaming answer arrives; a tool call fetches a quote; no "provider not configured" warning. |
| SMK-23 | P1 | MCP servers | MCP Servers screen: list built-in tools; enable/disable one. | Tool list loads; toggle persists. |
| SMK-24 | P1 | Notes / Report builder | Create a note, then a report with one chart; export to PDF. | Note persists after restart; PDF opens. |
| SMK-25 | P0 | Settings persistence | Change theme (Appearance), one keybinding (Keybindings) and a log level (Logging). Restart. | All three changes persist; the new keybinding works. |
| SMK-26 | P1 | Profiles | Launchpad → **Switch Profile…** → create `smoke-2`. | App relaunches in an empty profile; switching back restores the original data. |
| SMK-27 | P1 | Update check | About / Settings → check for updates on the previous release build. | The new version from `updates.json` is offered with the right download link. |
| SMK-28 | P0 | Clean exit + uninstall | Quit from the menu. Uninstall. | Process exits within 5 s (no zombie); uninstaller removes the app. |

### Scope by change

For a release that touches only some areas, Tier 1 plus Tier 2 is always
required; add the Tier 3 rows for what changed:

| Area changed | Also run |
|--------------|----------|
| `src/trading`, `screens/equity_trading`, `screens/crypto_trading`, brokers | SMK-14 to SMK-17 |
| `src/python`, `scripts/`, Python setup | SMK-01, SMK-21, SMK-18 to SMK-20 |
| `src/mcp`, `screens/ai_chat`, LLM services | SMK-22, SMK-23 |
| `src/datahub`, market data services | SMK-06, SMK-11 to SMK-13 |
| `src/app`, docking, layout | SMK-07 to SMK-10 |
| `src/auth` | SMK-03 to SMK-05 |
| Packaging / installers / `release.yml` | SMK-01, SMK-02, SMK-27, SMK-28 on every platform |

## Filing failures

For each failed case file one issue titled `[release-smoke] SMK-xx <flow>: <symptom>`
with: build version and platform, the steps as run, expected vs actual, a
screenshot, and the log from the profile's log directory (Settings → Logging
shows the path). A P0 failure blocks the release.

## Results template

```markdown
## Release smoke — vX.Y.Z — <platform> — <tester> — <date>

Tier 1: PASS / FAIL (attach summary.md)

| ID | Result | Notes / issue |
|----|--------|---------------|
| SMK-01 | | |
| ... | | |

Recording: <link>
Verdict: SHIP / BLOCK (P0 failures: ...)
```
