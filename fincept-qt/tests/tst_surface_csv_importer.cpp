// Unit tests for src/screens/surface_analytics/SurfaceCsvImporter.{h,cpp}
//
// SurfaceCsvImporter.cpp includes only its own header, which includes only
// SurfaceTypes.h (plain structs over std types). One extra translation unit,
// Qt Core only (QFile/QTextStream for parse_csv_file).
//
// The headline case: numeric cells used to go through std::stof/std::stoi with
// every exception mapped to 0 and partial parses accepted, so "N/A", "" or
// "12abc" were imported as 0 (or 12) and silently corrupted the surface. They
// must now fail the import with an error that names the cell.

#include "screens/surface_analytics/SurfaceCsvImporter.h"

#include <QString>
#include <QTemporaryFile>
#include <QTest>

#include <string>
#include <vector>

using namespace fincept::surface;

namespace {

using Rows = std::vector<std::vector<std::string>>;

Rows parse(const QByteArray& csv) {
    QTemporaryFile f;
    if (!f.open())
        qFatal("cannot create temp file");
    f.write(csv);
    f.flush();
    std::string err;
    Rows rows = parse_csv_file(f.fileName(), err);
    if (!err.empty())
        qFatal("parse_csv_file: %s", err.c_str());
    return rows;
}

QString q(const std::string& s) {
    return QString::fromStdString(s);
}

} // namespace

class TstSurfaceCsvImporter : public QObject {
    Q_OBJECT

  private slots:
    void valid_vol_surface_imports_unchanged();
    void integral_decimal_is_accepted_in_integer_column();
    void invalid_numeric_cell_is_an_error_naming_the_cell_data();
    void invalid_numeric_cell_is_an_error_naming_the_cell();
    void fractional_value_in_integer_column_is_rejected();
    void matrix_loader_rejects_invalid_value();
    void label_columns_are_not_parsed_as_numbers();
    void correlation_rejects_invalid_value();
};

void TstSurfaceCsvImporter::valid_vol_surface_imports_unchanged() {
    const Rows rows = parse("strike,dte,iv\n100,30,0.25\n110,30,0.22\n100,60,0.27\n110,60,-1.5e-1\n");
    VolatilitySurfaceData d;
    std::string err;
    QVERIFY2(load_vol_surface(rows, d, err), err.c_str());
    QCOMPARE(d.strikes, (std::vector<float>{100.f, 110.f}));
    QCOMPARE(d.expirations, (std::vector<int>{30, 60}));
    QCOMPARE(d.z, (std::vector<std::vector<float>>{{0.25f, 0.22f}, {0.27f, -0.15f}}));
}

void TstSurfaceCsvImporter::integral_decimal_is_accepted_in_integer_column() {
    VolatilitySurfaceData d;
    std::string err;
    QVERIFY2(load_vol_surface(parse("strike,dte,iv\n100,30.0,0.25\n"), d, err), err.c_str());
    QCOMPARE(d.expirations, (std::vector<int>{30}));
}

void TstSurfaceCsvImporter::invalid_numeric_cell_is_an_error_naming_the_cell_data() {
    QTest::addColumn<QByteArray>("csv");
    QTest::addColumn<QString>("where");
    QTest::addColumn<QString>("value");
    QTest::newRow("N/A iv") << QByteArray("strike,dte,iv\n100,30,0.25\n110,30,N/A\n")
                            << QStringLiteral("row 2 after the header, column 3") << QStringLiteral("\"N/A\"");
    QTest::newRow("empty iv") << QByteArray("strike,dte,iv\n100,30,\n")
                              << QStringLiteral("row 1 after the header, column 3") << QStringLiteral("\"\"");
    QTest::newRow("partial strike") << QByteArray("strike,dte,iv\n12abc,30,0.25\n")
                                    << QStringLiteral("row 1 after the header, column 1")
                                    << QStringLiteral("\"12abc\"");
    QTest::newRow("word dte") << QByteArray("strike,dte,iv\n100,30,0.2\n100,abc,0.25\n")
                              << QStringLiteral("row 2 after the header, column 2") << QStringLiteral("\"abc\"");
    QTest::newRow("stray separator") << QByteArray("strike,dte,iv\n100,30,0;25\n")
                                     << QStringLiteral("row 1 after the header, column 3")
                                     << QStringLiteral("\"0;25\"");
}

void TstSurfaceCsvImporter::invalid_numeric_cell_is_an_error_naming_the_cell() {
    QFETCH(QByteArray, csv);
    QFETCH(QString, where);
    QFETCH(QString, value);
    VolatilitySurfaceData d;
    std::string err;
    QVERIFY(!load_vol_surface(parse(csv), d, err));
    QVERIFY2(q(err).contains(where), err.c_str());
    QVERIFY2(q(err).contains(value), err.c_str());
}

void TstSurfaceCsvImporter::fractional_value_in_integer_column_is_rejected() {
    SkewSurfaceData d;
    std::string err;
    QVERIFY(!load_skew_surface(parse("delta,dte,skew\n25,30.5,0.1\n"), d, err));
    QVERIFY2(q(err).contains(QStringLiteral("column 2")), err.c_str());
}

void TstSurfaceCsvImporter::matrix_loader_rejects_invalid_value() {
    YieldCurveData d;
    std::string err;
    QVERIFY(!load_yield_curve(parse("t,maturity,yield\n0,12,4.1\n0,24,n/a\n"), d, err));
    QVERIFY2(q(err).contains(QStringLiteral("row 2 after the header, column 3")), err.c_str());
}

void TstSurfaceCsvImporter::label_columns_are_not_parsed_as_numbers() {
    BondSpreadData d;
    std::string err;
    QVERIFY2(load_bond_spread(parse("rating,maturity,spread\nAAA,12,45\nBB+,12,310\n"), d, err), err.c_str());
    QCOMPARE(d.ratings, (std::vector<std::string>{"AAA", "BB+"}));
    QCOMPARE(d.maturities, (std::vector<int>{12}));
    QCOMPARE(d.z, (std::vector<std::vector<float>>{{45.f}, {310.f}}));

    QVERIFY(!load_bond_spread(parse("rating,maturity,spread\nAAA,1Y,45\n"), d, err));
    QVERIFY2(q(err).contains(QStringLiteral("column 2")), err.c_str());
}

void TstSurfaceCsvImporter::correlation_rejects_invalid_value() {
    CorrelationMatrixData d;
    std::string err;
    QVERIFY(!load_correlation(parse("a,b,corr\nSPY,QQQ,0.9\nSPY,TLT,--\n"), d, err));
    QVERIFY2(q(err).contains(QStringLiteral("row 2 after the header, column 3")), err.c_str());
}

QTEST_GUILESS_MAIN(TstSurfaceCsvImporter)
#include "tst_surface_csv_importer.moc"
