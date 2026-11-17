#include "SurfaceCsvImporter.h"

#include <QByteArray>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <climits>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace fincept::surface {

// ---- Generic CSV parser ----
std::vector<std::vector<std::string>> parse_csv_file(const QString& path, std::string& out_error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out_error = "Cannot open file: " + path.toStdString();
        return {};
    }
    std::vector<std::vector<std::string>> rows;
    QTextStream in(&file);
    bool first = true;
    int line_no = 0;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        ++line_no;
        if (line.isEmpty())
            continue;
        if (first) {
            first = false;
            continue;
        } // skip header
        QStringList parts = line.split(',');
        // Every per-surface loader below reads columns 0..2 of EVERY row but only
        // checks the width of the first, so one short line further down read past the
        // end of its vector (a crash on a malformed or truncated file). Reject it here
        // with the offending line number instead.
        if (parts.size() < 3) {
            out_error = "line " + std::to_string(line_no) + " has " + std::to_string(parts.size()) +
                        " column(s); every row needs at least 3 comma-separated values";
            return {};
        }
        std::vector<std::string> row;
        for (const QString& p : parts)
            row.push_back(p.trimmed().toStdString());
        rows.push_back(row);
    }
    if (rows.empty())
        out_error = "No data rows found";
    return rows;
}

// A numeric cell must parse in full ("12abc", "abc", "N/A" and "" are rejected rather
// than imported as 12 or 0). Parsing is locale-independent, so "1.5" means 1.5 whatever
// the user's system locale. Integer columns also accept integral decimals like "30.0".
static bool to_f(const std::string& s, float& out) {
    bool ok = false;
    out = QByteArray::fromStdString(s).toFloat(&ok);
    return ok;
}
static bool to_i(const std::string& s, int& out) {
    const QByteArray b = QByteArray::fromStdString(s);
    bool ok = false;
    out = b.toInt(&ok);
    if (ok)
        return true;
    const double d = b.toDouble(&ok);
    if (!ok || d != std::floor(d) || d < INT_MIN || d > INT_MAX)
        return false;
    out = static_cast<int>(d);
    return true;
}

static std::string bad_cell(size_t row, size_t col, const std::string& s) {
    return "row " + std::to_string(row + 1) + " after the header, column " + std::to_string(col + 1) + ": \"" + s +
           "\" is not a valid number";
}

// Reads rows[row][col] into `out`, or sets `err` naming the cell and returns false.
template <typename T>
static bool read_num(const std::vector<std::vector<std::string>>& rows, size_t row, size_t col, T& out,
                     std::string& err) {
    const std::string& s = rows[row][col];
    bool ok;
    if constexpr (std::is_same_v<T, int>)
        ok = to_i(s, out);
    else
        ok = to_f(s, out);
    if (!ok)
        err = bad_cell(row, col, s);
    return ok;
}

// ---- Vol Surface: strike, dte, iv ----
bool load_vol_surface(const std::vector<std::vector<std::string>>& rows, VolatilitySurfaceData& out, std::string& err) {
    if (rows.empty() || rows[0].size() < 3) {
        err = "Need columns: strike,dte,iv";
        return false;
    }
    out = {};
    std::vector<float> strikes_seen;
    std::vector<int> dtes_seen;
    std::map<std::pair<int, float>, float> data_map;
    for (size_t i = 0; i < rows.size(); ++i) {
        float strike;
        int dte;
        float iv;
        if (!read_num(rows, i, 0, strike, err) || !read_num(rows, i, 1, dte, err) || !read_num(rows, i, 2, iv, err))
            return false;
        if (std::find(strikes_seen.begin(), strikes_seen.end(), strike) == strikes_seen.end())
            strikes_seen.push_back(strike);
        if (std::find(dtes_seen.begin(), dtes_seen.end(), dte) == dtes_seen.end())
            dtes_seen.push_back(dte);
        data_map[{dte, strike}] = iv;
    }
    std::sort(strikes_seen.begin(), strikes_seen.end());
    std::sort(dtes_seen.begin(), dtes_seen.end());
    out.strikes = strikes_seen;
    out.expirations = dtes_seen;
    for (int dte : out.expirations) {
        std::vector<float> row;
        for (float s : out.strikes)
            row.push_back(data_map[{dte, s}]);
        out.z.push_back(row);
    }
    return true;
}

// ---- Greeks: same format as vol but greek_name differs ----
bool load_greeks_surface(const std::vector<std::vector<std::string>>& rows, GreeksSurfaceData& out, std::string& err,
                         const std::string& greek_name) {
    VolatilitySurfaceData tmp;
    if (!load_vol_surface(rows, tmp, err))
        return false;
    out.strikes = tmp.strikes;
    out.expirations = tmp.expirations;
    out.z = tmp.z;
    out.greek_name = greek_name;
    return true;
}

// ---- Skew: delta, dte, skew ----
bool load_skew_surface(const std::vector<std::vector<std::string>>& rows, SkewSurfaceData& out, std::string& err) {
    if (rows.empty() || rows[0].size() < 3) {
        err = "Need columns: delta,dte,skew";
        return false;
    }
    out = {};
    std::vector<float> deltas_seen;
    std::vector<int> dtes_seen;
    std::map<std::pair<int, int>, float> data_map;
    for (size_t i = 0; i < rows.size(); ++i) {
        float delta;
        int dte;
        float skew;
        if (!read_num(rows, i, 0, delta, err) || !read_num(rows, i, 1, dte, err) || !read_num(rows, i, 2, skew, err))
            return false;
        if (std::find(deltas_seen.begin(), deltas_seen.end(), delta) == deltas_seen.end())
            deltas_seen.push_back(delta);
        if (std::find(dtes_seen.begin(), dtes_seen.end(), dte) == dtes_seen.end())
            dtes_seen.push_back(dte);
        data_map[{dte, (int)delta}] = skew;
    }
    std::sort(deltas_seen.begin(), deltas_seen.end());
    std::sort(dtes_seen.begin(), dtes_seen.end());
    out.deltas = deltas_seen;
    out.expirations = dtes_seen;
    for (int dte : out.expirations) {
        std::vector<float> row;
        for (float d : out.deltas)
            row.push_back(data_map[{dte, (int)d}]);
        out.z.push_back(row);
    }
    return true;
}

bool load_local_vol(const std::vector<std::vector<std::string>>& rows, LocalVolData& out, std::string& err) {
    VolatilitySurfaceData tmp;
    if (!load_vol_surface(rows, tmp, err))
        return false;
    out.strikes = tmp.strikes;
    out.expirations = tmp.expirations;
    out.z = tmp.z;
    return true;
}

// ---- Generic 2D matrix loader: row_key, col_key, value ----
template <typename RowT, typename ColT>
static bool load_generic_matrix(const std::vector<std::vector<std::string>>& rows, std::vector<RowT>& out_rows,
                                std::vector<ColT>& out_cols, std::vector<std::vector<float>>& out_z, std::string& err) {
    if (rows.empty() || rows[0].size() < 3) {
        err = "Need 3 columns: row_key,col_key,value";
        return false;
    }
    std::vector<RowT> row_keys;
    std::vector<ColT> col_keys;
    std::map<std::pair<RowT, ColT>, float> data_map;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        RowT rk;
        ColT ck;
        if constexpr (std::is_same_v<RowT, int>) {
            if (!read_num(rows, i, 0, rk, err))
                return false;
        } else
            rk = r[0];
        if constexpr (std::is_same_v<ColT, int>) {
            if (!read_num(rows, i, 1, ck, err))
                return false;
        } else
            ck = r[1];
        float v;
        if (!read_num(rows, i, 2, v, err))
            return false;
        if (std::find(row_keys.begin(), row_keys.end(), rk) == row_keys.end())
            row_keys.push_back(rk);
        if (std::find(col_keys.begin(), col_keys.end(), ck) == col_keys.end())
            col_keys.push_back(ck);
        data_map[{rk, ck}] = v;
    }
    out_rows = row_keys;
    out_cols = col_keys;
    for (const auto& rk : out_rows) {
        std::vector<float> row;
        for (const auto& ck : out_cols)
            row.push_back(data_map[{rk, ck}]);
        out_z.push_back(row);
    }
    return true;
}

bool load_yield_curve(const std::vector<std::vector<std::string>>& rows, YieldCurveData& out, std::string& err) {
    out = {};
    return load_generic_matrix<int, int>(rows, out.time_points, out.maturities, out.z, err);
}
bool load_swaption_vol(const std::vector<std::vector<std::string>>& rows, SwaptionVolData& out, std::string& err) {
    out = {};
    return load_generic_matrix<int, int>(rows, out.option_expiries, out.swap_tenors, out.z, err);
}
bool load_capfloor_vol(const std::vector<std::vector<std::string>>& rows, CapFloorVolData& out, std::string& err) {
    if (rows.empty() || rows[0].size() < 3) {
        err = "Need: maturity,strike,vol";
        return false;
    }
    out = {};
    std::vector<int> mats;
    std::vector<float> strikes;
    std::map<std::pair<int, float>, float> dm;
    for (size_t i = 0; i < rows.size(); ++i) {
        int m;
        float s;
        float v;
        if (!read_num(rows, i, 0, m, err) || !read_num(rows, i, 1, s, err) || !read_num(rows, i, 2, v, err))
            return false;
        if (std::find(mats.begin(), mats.end(), m) == mats.end())
            mats.push_back(m);
        if (std::find(strikes.begin(), strikes.end(), s) == strikes.end())
            strikes.push_back(s);
        dm[{m, s}] = v;
    }
    std::sort(mats.begin(), mats.end());
    std::sort(strikes.begin(), strikes.end());
    out.maturities = mats;
    out.strikes = strikes;
    for (int m : out.maturities) {
        std::vector<float> row;
        for (float s : out.strikes)
            row.push_back(dm[{m, s}]);
        out.z.push_back(row);
    }
    return true;
}
bool load_bond_spread(const std::vector<std::vector<std::string>>& rows, BondSpreadData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.ratings, out.maturities, out.z, err);
}
bool load_ois_basis(const std::vector<std::vector<std::string>>& rows, OISBasisData& out, std::string& err) {
    out = {};
    return load_generic_matrix<int, int>(rows, out.time_points, out.tenors, out.z, err);
}
bool load_real_yield(const std::vector<std::vector<std::string>>& rows, RealYieldData& out, std::string& err) {
    out = {};
    return load_generic_matrix<int, int>(rows, out.time_points, out.maturities, out.z, err);
}
bool load_forward_rate(const std::vector<std::vector<std::string>>& rows, ForwardRateData& out, std::string& err) {
    out = {};
    return load_generic_matrix<int, int>(rows, out.start_tenors, out.forward_periods, out.z, err);
}
bool load_fx_vol(const std::vector<std::vector<std::string>>& rows, FXVolData& out, std::string& err) {
    if (rows.empty() || rows[0].size() < 3) {
        err = "Need: tenor,delta,vol";
        return false;
    }
    out = {};
    std::vector<int> tenors;
    std::vector<float> deltas;
    std::map<std::pair<int, float>, float> dm;
    for (size_t i = 0; i < rows.size(); ++i) {
        int t;
        float d;
        float v;
        if (!read_num(rows, i, 0, t, err) || !read_num(rows, i, 1, d, err) || !read_num(rows, i, 2, v, err))
            return false;
        if (std::find(tenors.begin(), tenors.end(), t) == tenors.end())
            tenors.push_back(t);
        if (std::find(deltas.begin(), deltas.end(), d) == deltas.end())
            deltas.push_back(d);
        dm[{t, (int)d}] = (float)v;
    }
    std::sort(tenors.begin(), tenors.end());
    std::sort(deltas.begin(), deltas.end());
    out.tenors = tenors;
    out.deltas = deltas;
    for (int t : out.tenors) {
        std::vector<float> row;
        for (float d : out.deltas)
            row.push_back(dm[{t, (int)d}]);
        out.z.push_back(row);
    }
    return true;
}
bool load_fx_forward(const std::vector<std::vector<std::string>>& rows, FXForwardPointsData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.pairs, out.tenors, out.z, err);
}
bool load_xccy_basis(const std::vector<std::vector<std::string>>& rows, CrossCurrencyBasisData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.pairs, out.tenors, out.z, err);
}
bool load_cds_spread(const std::vector<std::vector<std::string>>& rows, CDSSpreadData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.entities, out.tenors, out.z, err);
}
bool load_credit_trans(const std::vector<std::vector<std::string>>& rows, CreditTransitionData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, std::string>(rows, out.ratings, out.to_ratings, out.z, err);
}
bool load_recovery_rate(const std::vector<std::vector<std::string>>& rows, RecoveryRateData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, std::string>(rows, out.sectors, out.seniorities, out.z, err);
}
bool load_cmdty_forward(const std::vector<std::vector<std::string>>& rows, CommodityForwardData& out,
                        std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.commodities, out.contract_months, out.z, err);
}
bool load_cmdty_vol(const std::vector<std::vector<std::string>>& rows, CommodityVolData& out, std::string& err) {
    VolatilitySurfaceData tmp;
    if (!load_vol_surface(rows, tmp, err))
        return false;
    out.strikes = tmp.strikes;
    out.expirations = tmp.expirations;
    out.z = tmp.z;
    return true;
}
bool load_crack_spread(const std::vector<std::vector<std::string>>& rows, CrackSpreadData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.spread_types, out.contract_months, out.z, err);
}
bool load_contango(const std::vector<std::vector<std::string>>& rows, ContangoData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.commodities, out.contract_months, out.z, err);
}
bool load_correlation(const std::vector<std::vector<std::string>>& rows, CorrelationMatrixData& out, std::string& err) {
    if (rows.empty() || rows[0].size() < 3) {
        err = "Need: asset_i,asset_j,corr";
        return false;
    }
    out = {};
    std::vector<std::string> assets;
    std::map<std::pair<std::string, std::string>, float> dm;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        std::string a = r[0], b = r[1];
        float v;
        if (!read_num(rows, i, 2, v, err))
            return false;
        if (std::find(assets.begin(), assets.end(), a) == assets.end())
            assets.push_back(a);
        if (std::find(assets.begin(), assets.end(), b) == assets.end())
            assets.push_back(b);
        dm[{a, b}] = v;
    }
    out.assets = assets;
    std::vector<float> row;
    for (const auto& a : assets)
        for (const auto& b : assets)
            row.push_back(dm[{a, b}]);
    out.z.push_back(row);
    return true;
}
bool load_pca(const std::vector<std::vector<std::string>>& rows, PCAData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, std::string>(rows, out.assets, out.factors, out.z, err);
}
bool load_var(const std::vector<std::vector<std::string>>& rows, VaRData& out, std::string& err) {
    if (rows.empty() || rows[0].size() < 3) {
        err = "Need: confidence,horizon,var";
        return false;
    }
    out = {};
    std::vector<float> confs;
    std::vector<int> horizons;
    std::map<std::pair<float, int>, float> dm;
    for (size_t i = 0; i < rows.size(); ++i) {
        float c;
        int h;
        float v;
        if (!read_num(rows, i, 0, c, err) || !read_num(rows, i, 1, h, err) || !read_num(rows, i, 2, v, err))
            return false;
        if (std::find(confs.begin(), confs.end(), c) == confs.end())
            confs.push_back(c);
        if (std::find(horizons.begin(), horizons.end(), h) == horizons.end())
            horizons.push_back(h);
        dm[{c, h}] = v;
    }
    std::sort(confs.begin(), confs.end());
    std::sort(horizons.begin(), horizons.end());
    out.confidence_levels = confs;
    out.horizons = horizons;
    for (float c : out.confidence_levels) {
        std::vector<float> row;
        for (int h : out.horizons)
            row.push_back(dm[{c, h}]);
        out.z.push_back(row);
    }
    return true;
}
bool load_stress_test(const std::vector<std::vector<std::string>>& rows, StressTestData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, std::string>(rows, out.scenarios, out.portfolios, out.z, err);
}
bool load_factor_exposure(const std::vector<std::vector<std::string>>& rows, FactorExposureData& out,
                          std::string& err) {
    out = {};
    return load_generic_matrix<std::string, std::string>(rows, out.assets, out.factors, out.z, err);
}
bool load_liquidity(const std::vector<std::vector<std::string>>& rows, LiquidityData& out, std::string& err) {
    VolatilitySurfaceData tmp;
    if (!load_vol_surface(rows, tmp, err))
        return false;
    out.strikes = tmp.strikes;
    out.expirations = tmp.expirations;
    out.z = tmp.z;
    return true;
}
bool load_drawdown(const std::vector<std::vector<std::string>>& rows, DrawdownData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.assets, out.windows, out.z, err);
}
bool load_beta(const std::vector<std::vector<std::string>>& rows, BetaData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.assets, out.horizons, out.z, err);
}
bool load_implied_div(const std::vector<std::vector<std::string>>& rows, ImpliedDividendData& out, std::string& err) {
    VolatilitySurfaceData tmp;
    if (!load_vol_surface(rows, tmp, err))
        return false;
    out.strikes = tmp.strikes;
    out.expirations = tmp.expirations;
    out.z = tmp.z;
    return true;
}
bool load_inflation(const std::vector<std::vector<std::string>>& rows, InflationExpData& out, std::string& err) {
    out = {};
    return load_generic_matrix<int, int>(rows, out.time_points, out.horizons, out.z, err);
}
bool load_monetary(const std::vector<std::vector<std::string>>& rows, MonetaryPolicyData& out, std::string& err) {
    out = {};
    return load_generic_matrix<std::string, int>(rows, out.central_banks, out.meetings_ahead, out.z, err);
}

} // namespace fincept::surface
