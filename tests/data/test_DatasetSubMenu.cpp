#include <QTest>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QAbstractListModel>
#include <QSignalSpy>
#include <QUrl>
#include <QDir>

class MockDatasetModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        DatasetIdRole = Qt::UserRole + 1,
        NameRole
    };

    struct Entry {
        int64_t dataset_id;
        QString name;
    };

    explicit MockDatasetModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(entries_.size());
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(entries_.size()))
            return QVariant();
        const auto &e = entries_[index.row()];
        if (role == DatasetIdRole) return QVariant::fromValue(e.dataset_id);
        if (role == NameRole) return e.name;
        return QVariant();
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {DatasetIdRole, "dataset_id"},
            {NameRole, "name"}
        };
    }

    void addDataset(int64_t id, const QString &name) {
        beginInsertRows(QModelIndex(), static_cast<int>(entries_.size()), static_cast<int>(entries_.size()));
        entries_.push_back({id, name});
        endInsertRows();
    }

    void removeDataset(int row) {
        if (row < 0 || row >= static_cast<int>(entries_.size())) return;
        beginRemoveRows(QModelIndex(), row, row);
        entries_.erase(entries_.begin() + row);
        endRemoveRows();
    }

    void clear() {
        if (entries_.empty()) return;
        beginResetModel();
        entries_.clear();
        endResetModel();
    }

private:
    std::vector<Entry> entries_;
};

class DatasetSubMenuTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
#ifdef DLT_BUILD_DIR
        const QString build_root = QStringLiteral(DLT_BUILD_DIR);
        engine_.addImportPath(build_root);
        engine_.addImportPath(build_root + QStringLiteral("/qml"));
#endif
    }

    void test_defaultProperties()
    {
        QQmlComponent component(&engine_, QUrl::fromLocalFile(QStringLiteral(DLT_SOURCE_DIR "/src/data/qml/component/DatasetSubMenu.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));

        QScopedPointer<QObject> obj(component.create());
        QVERIFY(obj != nullptr);

        QCOMPARE(obj->property("title").toString(), QStringLiteral("复制到"));
        QCOMPARE(obj->property("itemEnabled").toBool(), true);
        QCOMPARE(obj->property("count").toInt(), 0);
    }

    void test_dynamicModelInstantiationAndSignals()
    {
        QQmlComponent component(&engine_, QUrl::fromLocalFile(QStringLiteral(DLT_SOURCE_DIR "/src/data/qml/component/DatasetSubMenu.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));

        QScopedPointer<QObject> obj(component.create());
        QVERIFY(obj != nullptr);

        MockDatasetModel model;
        model.addDataset(101, QStringLiteral("Dataset Alpha"));
        model.addDataset(102, QStringLiteral("Dataset Beta"));

        obj->setProperty("datasetsModel", QVariant::fromValue(&model));

        QCOMPARE(obj->property("count").toInt(), 2);

        QQuickItem *item0 = nullptr;
        QMetaObject::invokeMethod(obj.data(), "itemAt", Q_RETURN_ARG(QQuickItem *, item0), Q_ARG(int, 0));
        QVERIFY(item0 != nullptr);
        QCOMPARE(item0->property("text").toString(), QStringLiteral("Dataset Alpha"));
        QCOMPARE(item0->property("enabled").toBool(), true);

        QQuickItem *item1 = nullptr;
        QMetaObject::invokeMethod(obj.data(), "itemAt", Q_RETURN_ARG(QQuickItem *, item1), Q_ARG(int, 1));
        QVERIFY(item1 != nullptr);
        QCOMPARE(item1->property("text").toString(), QStringLiteral("Dataset Beta"));
        QCOMPARE(item1->property("enabled").toBool(), true);

        // Test itemEnabled propagation
        obj->setProperty("itemEnabled", false);
        QCOMPARE(item0->property("enabled").toBool(), false);
        QCOMPARE(item1->property("enabled").toBool(), false);

        obj->setProperty("itemEnabled", true);
        QCOMPARE(item0->property("enabled").toBool(), true);

        // Test clicking item0 emits datasetTriggered(101, "Dataset Alpha")
        QSignalSpy spy(obj.data(), SIGNAL(datasetTriggered(int,QString)));
        QVERIFY(spy.isValid());

        bool clickedInvoked = QMetaObject::invokeMethod(item0, "clicked");
        if (!clickedInvoked) {
            QMetaObject::invokeMethod(item0, "triggered");
        }
        QCOMPARE(spy.count(), 1);
        QList<QVariant> arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).toInt(), 101);
        QCOMPARE(arguments.at(1).toString(), QStringLiteral("Dataset Alpha"));

        // Dynamic addition and removal
        model.addDataset(103, QStringLiteral("Dataset Gamma"));
        QCOMPARE(obj->property("count").toInt(), 3);

        model.removeDataset(0);
        QCOMPARE(obj->property("count").toInt(), 2);

        QQuickItem *newFirst = nullptr;
        QMetaObject::invokeMethod(obj.data(), "itemAt", Q_RETURN_ARG(QQuickItem *, newFirst), Q_ARG(int, 0));
        QVERIFY(newFirst != nullptr);
        QCOMPARE(newFirst->property("text").toString(), QStringLiteral("Dataset Beta"));
    }

private:
    QQmlEngine engine_;
};

QTEST_MAIN(DatasetSubMenuTest)

#include "test_DatasetSubMenu.moc"
