#include "python/ServicesSelfTest.h"

#include "python/PythonRunner.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>

namespace fincept::python {

namespace {

PythonResult wait_for(const std::function<void(PythonRunner::Callback)>& start, int timeout_ms = 120000) {
    PythonResult out;
    out.error = QStringLiteral("selftest: no callback within %1 ms").arg(timeout_ms);
    QEventLoop loop;
    bool done = false;
    start([&](PythonResult r) {
        out = std::move(r);
        done = true;
        loop.quit();
    });
    if (!done) {
        QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return out;
}

PythonResult run_script(const QString& script, const QStringList& args) {
    return wait_for([&](PythonRunner::Callback cb) { PythonRunner::instance().run(script, args, std::move(cb)); });
}

} // namespace

int run_services_selftest() {
    int failures = 0;
    auto check = [&](const QString& label, bool ok, const QString& detail = {}) {
        std::printf("[%s] %s%s\n", ok ? "PASS" : "FAIL", qPrintable(label),
                    ok || detail.isEmpty() ? "" : qPrintable(QStringLiteral(" — ") + detail.left(300)));
        std::fflush(stdout);
        if (!ok)
            ++failures;
    };

    auto& runner = PythonRunner::instance();
    check(QStringLiteral("FINCEPT_SERVICES_URL set (") + runner.remote_url() + ")", runner.remote_enabled());
    if (!runner.remote_enabled())
        return 1;

    const QString down = qEnvironmentVariable("FINCEPT_SELFTEST_DOWN_SERVICE");
    auto expect_up = [&](const QString& service, const QString& label, const PythonResult& r,
                         const std::function<bool(const PythonResult&)>& ok) {
        if (service == down) {
            const bool clean = !r.success && r.error.contains(service) && r.error.contains(QStringLiteral("HTTP 503"));
            check(QStringLiteral("%1: %2 down → clean error").arg(label, service), clean, r.error);
        } else {
            check(QStringLiteral("%1 via %2").arg(label, service), r.success && ok(r), r.error);
        }
    };

    QElapsedTimer t;
    t.start();
    auto quote = run_script(QStringLiteral("yfinance_data.py"), {QStringLiteral("quote"), QStringLiteral("AAPL")});
    expect_up(QStringLiteral("market-data"), QStringLiteral("AAPL quote"), quote, [](const PythonResult& r) {
        return QJsonDocument::fromJson(r.output.toUtf8()).object().value("price").toDouble() > 0;
    });
    std::printf("       quote round trip %lld ms\n", static_cast<long long>(t.elapsed()));

    auto hist =
        run_script(QStringLiteral("yfinance_data.py"), {QStringLiteral("historical_period"), QStringLiteral("MSFT"),
                                                        QStringLiteral("1mo"), QStringLiteral("1d")});
    expect_up(QStringLiteral("market-data"), QStringLiteral("MSFT 1mo history"), hist, [](const PythonResult& r) {
        const auto doc = QJsonDocument::fromJson(r.output.toUtf8());
        return doc.isArray() ? doc.array().size() > 10 : doc.object().size() > 0;
    });

    auto rsi = run_script(QStringLiteral("Analytics/technical_analysis/momentum_indicators.py"),
                          {QStringLiteral("AAPL"), QStringLiteral("rsi"), QStringLiteral("{\"timeframe\":\"3mo\"}")});
    expect_up(QStringLiteral("analytics"), QStringLiteral("AAPL RSI"), rsi, [](const PythonResult& r) {
        return QJsonDocument::fromJson(r.output.toUtf8()).object().value("values").toArray().size() > 20;
    });

    auto cell = wait_for([](PythonRunner::Callback cb) {
        PythonRunner::instance().run_code(QStringLiteral("import numpy as np\nprint(int(np.arange(7).sum() * 2))"),
                                          std::move(cb));
    });
    expect_up(QStringLiteral("analytics"), QStringLiteral("notebook code cell"), cell,
              [](const PythonResult& r) { return r.output.trimmed() == QLatin1String("42"); });

    auto fx = run_script(QStringLiteral("ecb_data.py"), {QStringLiteral("currency_rates")});
    expect_up(QStringLiteral("connectors"), QStringLiteral("ECB FX rates"), fx, [](const PythonResult& r) {
        return QJsonDocument::fromJson(r.output.toUtf8()).object().value("data").toObject().contains("USD");
    });

    // Error envelope from a service is still a failure on the client.
    auto bad = run_script(QStringLiteral("ecb_data.py"), {QStringLiteral("no_such_command")});
    if (down != QLatin1String("connectors"))
        check(QStringLiteral("script error envelope surfaces as failure"), !bad.success && !bad.error.isEmpty(),
              bad.output);

    // Local-only script: the gateway says "unrouted" and the client runs it in
    // local Python (which fails here if no local Python is installed — that is
    // fine, it must just not be a gateway error).
    auto local = run_script(QStringLiteral("spreadsheet.py"), {QStringLiteral("--help")});
    check(QStringLiteral("local-only script falls back to the desktop"),
          !local.error.contains(QStringLiteral("HTTP")) && !local.error.contains(QStringLiteral("unreachable")),
          local.error);

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace fincept::python
