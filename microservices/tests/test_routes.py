"""routes.json ownership rules (no services needed)."""
from routes import load_routes, owner_of

R = load_routes()


def test_market_data_scripts():
    for s in ["yfinance_data.py", "screener_data.py", "fetch_peers.py", "cboe_options.py"]:
        assert owner_of(s, R) == "market-data", s


def test_analytics_tree():
    assert owner_of("Analytics/technical_analysis/momentum_indicators.py", R) == "analytics"
    assert owner_of("Analytics/corporateFinance/lbo/lbo_model.py", R) == "analytics"


def test_connectors_catch_all_top_level():
    for s in ["ecb_data.py", "fred_data.py", "imf_data.py"]:
        assert owner_of(s, R) == "connectors", s


def test_local_only_and_unrouted():
    for s in ["spreadsheet.py", "aisstream_data.py", "databento_ohlcv.py", "prediction_kalshi.py",
              "agents/finagent_core/main.py", "ai_quant_lab/qlib_rl.py"]:
        assert owner_of(s, R) is None, s


def test_path_escape_rejected():
    for s in ["../etc/passwd.py", "/etc/passwd.py", "Analytics/../../x.py"]:
        assert owner_of(s, R) is None, s
