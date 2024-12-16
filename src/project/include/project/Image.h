#include <QObject>

#include <QAbstractListModel>
#include <QtQml>

namespace dltool::project {

class ImageInstance: public QObject
{
public:
    ImageInstance(QObject* parent = nullptr) : QObject(parent) {}
};

class ImageInstancesListModel : public QAbstractListModel
{

};

} // namespace dltool::project
