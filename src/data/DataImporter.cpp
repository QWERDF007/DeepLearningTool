#include "data/DataImporter.h"

namespace dltool::data {

DataImporter::DataImporter(ProjectDataBase *database, QObject *parent)
    : QObject(parent)
    , database_(database)
{
}

DataImporter::~DataImporter() {}

} // namespace dltool::data
