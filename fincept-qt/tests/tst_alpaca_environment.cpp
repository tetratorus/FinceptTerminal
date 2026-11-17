// Unit tests for src/trading/brokers/alpaca/AlpacaEnvironment.h
//
// Equity PAPER on Alpaca routes through the broker adapter to paper-api (upstream #392). The
// invariant pinned here: Alpaca paper credentials never reach the live host
// api.alpaca.markets — not for orders (trading_url), not for the native-paper gate
// (is_paper_environment), and not for the credential check (auth_environments), which used to
// retry a rejected PK* paper key against the live host.

#include "trading/brokers/alpaca/AlpacaEnvironment.h"

#include <QString>
#include <QStringList>
#include <QTest>

using namespace fincept::trading::alpaca_env;

class TstAlpacaEnvironment : public QObject {
    Q_OBJECT

  private slots:
    void paper_key_always_uses_paper_host();
    void additional_data_paper_uses_paper_host();
    void live_credentials_use_live_host();
    void paper_environment_implies_paper_host();
    void live_or_contradictory_credentials_are_not_paper();
    void paper_key_is_never_authenticated_on_live_host();
    void other_keys_fall_back_to_opposite_environment();
};

void TstAlpacaEnvironment::paper_key_always_uses_paper_host() {
    for (const QString& data : {QString(), QStringLiteral("paper"), QStringLiteral("live")}) {
        QCOMPARE(trading_url("PKTEST123", data), QString(kPaperTradingUrl));
        QCOMPARE(trading_url("pktest123", data), QString(kPaperTradingUrl));
    }
}

void TstAlpacaEnvironment::additional_data_paper_uses_paper_host() {
    QCOMPARE(trading_url("AKTEST123", "paper"), QString(kPaperTradingUrl));
    QCOMPARE(trading_url("XYZ123", "paper"), QString(kPaperTradingUrl));
    QCOMPARE(trading_url("XYZ123", ""), QString(kPaperTradingUrl));
}

void TstAlpacaEnvironment::live_credentials_use_live_host() {
    QCOMPARE(trading_url("AKTEST123", "live"), QString(kLiveTradingUrl));
    QCOMPARE(trading_url("AKTEST123", ""), QString(kLiveTradingUrl));
    QCOMPARE(trading_url("XYZ123", "live"), QString(kLiveTradingUrl));
}

void TstAlpacaEnvironment::paper_environment_implies_paper_host() {
    const QStringList keys = {"PKTEST", "pktest", "AKTEST", "XYZ", ""};
    const QStringList datas = {"", "paper", "live", "junk"};
    for (const auto& k : keys)
        for (const auto& d : datas)
            if (is_paper_environment(k, d))
                QCOMPARE(trading_url(k, d), QString(kPaperTradingUrl));
}

void TstAlpacaEnvironment::live_or_contradictory_credentials_are_not_paper() {
    QVERIFY(is_paper_environment("PKTEST", ""));
    QVERIFY(is_paper_environment("PKTEST", "paper"));
    QVERIFY(is_paper_environment("XYZ", "paper"));
    QVERIFY(!is_paper_environment("PKTEST", "live"));
    QVERIFY(!is_paper_environment("AKTEST", "paper"));
    QVERIFY(!is_paper_environment("AKTEST", ""));
    QVERIFY(!is_paper_environment("XYZ", ""));
    QVERIFY(!is_paper_environment("", "paper"));
}

void TstAlpacaEnvironment::paper_key_is_never_authenticated_on_live_host() {
    for (const QString& hint : {QStringLiteral("paper"), QStringLiteral("live"), QString()}) {
        const QStringList envs = auth_environments("PKTEST123", hint);
        QCOMPARE(envs, QStringList{"paper"});
        for (const auto& e : envs)
            QCOMPARE(base_url_for(e), QString(kPaperTradingUrl));
    }
}

void TstAlpacaEnvironment::other_keys_fall_back_to_opposite_environment() {
    QCOMPARE(auth_environments("AKTEST123", "paper"), (QStringList{"live", "paper"}));
    QCOMPARE(auth_environments("XYZ", "live"), (QStringList{"live", "paper"}));
    QCOMPARE(auth_environments("XYZ", "paper"), (QStringList{"paper", "live"}));
    QCOMPARE(base_url_for("live"), QString(kLiveTradingUrl));
    QCOMPARE(base_url_for("paper"), QString(kPaperTradingUrl));
}

QTEST_GUILESS_MAIN(TstAlpacaEnvironment)
#include "tst_alpaca_environment.moc"
