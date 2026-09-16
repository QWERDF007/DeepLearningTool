#include <QSignalSpy>
#include <QTest>

#include "data/GlobalFilter.h"

using namespace dltool::data;

class TestGlobalFilterRegionSearch : public QObject
{
    Q_OBJECT

private slots:
    void testCustomConditionSpec();
    void testSetRegionSearchResults();
    void testAcceptsCustomLabelPreventsImageDiffusion();
    void testExplicitEmptyMatchKeepsReady();
    void testClearRegionSearchResults();
};

void TestGlobalFilterRegionSearch::testCustomConditionSpec()
{
    const auto conditions = GlobalFilter::customConditions();
    bool found = false;
    for (const auto &c : conditions)
    {
        if (c.id == static_cast<int64_t>(GlobalFilter::CustomCondition::RegionSearchResult))
        {
            found = true;
            QCOMPARE(c.text, QString("区域检索结果"));
            break;
        }
    }
    QVERIFY(found);
}

void TestGlobalFilterRegionSearch::testSetRegionSearchResults()
{
    GlobalFilter filter(nullptr);
    QVERIFY(!filter.regionResultsReady());
    QCOMPARE(filter.regionSearchResultIds().size(), 0);

    QSignalSpy spy(&filter, &GlobalFilter::regionSearchResultsChanged);

    filter.setRegionSearchResults({101, 102, 103}, true);
    QVERIFY(filter.regionResultsReady());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(filter.regionSearchResultIds().size(), 3);
    QVERIFY(filter.isFilterEnabled(GlobalFilter::FilterType::Custom));

    auto active_ids = filter.getActiveIds(GlobalFilter::FilterType::Custom);
    QVERIFY(std::find(active_ids.begin(), active_ids.end(),
                      static_cast<int64_t>(GlobalFilter::CustomCondition::RegionSearchResult)) != active_ids.end());
}

void TestGlobalFilterRegionSearch::testAcceptsCustomLabelPreventsImageDiffusion()
{
    GlobalFilter filter(nullptr);
    filter.setRegionSearchResults({101}, true);

    // Label 101 on image 1 is in region search results
    QVERIFY(filter.acceptsCustomLabel(101, 1));

    // Label 999 on image 1 is NOT in region search results.
    // It must NOT be accepted just because image 1 contains label 101!
    QVERIFY(!filter.acceptsCustomLabel(999, 1));

    // Label on another image
    QVERIFY(!filter.acceptsCustomLabel(202, 2));
}

void TestGlobalFilterRegionSearch::testExplicitEmptyMatchKeepsReady()
{
    GlobalFilter filter(nullptr);
    filter.setRegionSearchResults({101}, true);
    QVERIFY(filter.regionResultsReady());

    // Next round produces 0 results (completed empty)
    filter.setRegionSearchResults({}, false);
    QVERIFY(filter.regionResultsReady());
    QCOMPARE(filter.regionSearchResultIds().size(), 0);

    // When Custom condition is enabled with RegionSearchResult, nothing should be accepted
    filter.setFilter(GlobalFilter::FilterType::Custom,
                     {static_cast<int64_t>(GlobalFilter::CustomCondition::RegionSearchResult)});
    filter.setFilterEnabled(GlobalFilter::FilterType::Custom, true);

    QVERIFY(!filter.acceptsCustomLabel(101, 1));
    QVERIFY(!filter.acceptsCustomImage(1));
}

void TestGlobalFilterRegionSearch::testClearRegionSearchResults()
{
    GlobalFilter filter(nullptr);
    filter.setRegionSearchResults({101}, true);
    QVERIFY(filter.regionResultsReady());

    QSignalSpy spy(&filter, &GlobalFilter::regionSearchResultsChanged);
    filter.clearRegionSearchResults();

    QVERIFY(!filter.regionResultsReady());
    QCOMPARE(filter.regionSearchResultIds().size(), 0);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!filter.isFilterEnabled(GlobalFilter::FilterType::Custom));
}

QTEST_MAIN(TestGlobalFilterRegionSearch)
#include "test_GlobalFilterRegionSearch.moc"
