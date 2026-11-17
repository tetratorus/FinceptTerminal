#pragma once

#include <QString>
#include <QStringList>

/// Which Alpaca environment (live api.alpaca.markets vs paper-api.alpaca.markets) a credential
/// set belongs to. Pure functions of the stored key / additional_data so the invariant
/// "paper credentials never reach the live host" is unit-testable without the broker adapter.
namespace fincept::trading::alpaca_env {

inline constexpr auto kLiveTradingUrl = "https://api.alpaca.markets";
inline constexpr auto kPaperTradingUrl = "https://paper-api.alpaca.markets";

inline bool is_paper_key(const QString& api_key) {
    return api_key.startsWith("PK", Qt::CaseInsensitive);
}

inline bool is_live_key(const QString& api_key) {
    return api_key.startsWith("AK", Qt::CaseInsensitive);
}

/// Trading host for stored credentials. additional_data holds "live"/"paper" from
/// exchange_token; the key prefix (AK=live, PK=paper) covers stale/missing values.
inline QString trading_url(const QString& api_key, const QString& additional_data) {
    // A PK* key is a paper key and cannot authenticate on the live host — never send one
    // there, even if additional_data was stored as "live".
    if (is_paper_key(api_key))
        return kPaperTradingUrl;
    const bool is_live = (additional_data == "live") || (additional_data.isEmpty() && is_live_key(api_key));
    return is_live ? kLiveTradingUrl : kPaperTradingUrl;
}

/// A PAPER-mode order may reach Alpaca only for credentials positively identified as paper: a
/// PK* key not marked live, or a set that authenticated against paper-api (additional_data ==
/// "paper") and is not an AK* live key. Live, contradictory (a PK key stored as "live", an AK
/// key stored as "paper") and unidentifiable credentials are NOT paper. Every credential set
/// that returns true here resolves to paper-api in trading_url().
inline bool is_paper_environment(const QString& api_key, const QString& additional_data) {
    if (api_key.isEmpty() || additional_data == "live")
        return false;
    if (is_live_key(api_key))
        return false;
    return is_paper_key(api_key) || additional_data == "paper";
}

/// Environments exchange_token tries, in order. The key prefix wins over the UI hint; the
/// opposite environment is a fallback for a mis-selected combo — except for a PK* paper key,
/// which is never sent to the live host.
inline QStringList auth_environments(const QString& api_key, const QString& auth_code) {
    if (is_paper_key(api_key))
        return {"paper"};
    if (is_live_key(api_key))
        return {"live", "paper"};
    return auth_code == "live" ? QStringList{"live", "paper"} : QStringList{"paper", "live"};
}

inline QString base_url_for(const QString& env) {
    return env == "live" ? kLiveTradingUrl : kPaperTradingUrl;
}

} // namespace fincept::trading::alpaca_env
