#include <QSharedPointer>
#include <QDebug>

#include <QApplication>

#include "license.h"
#include "packageitemmodel.h"
#include "abstractrepository.h"
#include "mainwindow.h"
#include "fileloaderitem.h"

PackageItemModel::PackageItemModel(const QList<Package *> packages) :
        obsoleteBrush(QColor(255, 0xc7, 0xc7))
{
    this->packages = packages;
}

PackageItemModel::~PackageItemModel()
{
    qDeleteAll(packages);
}

int PackageItemModel::rowCount(const QModelIndex &parent) const
{
    return this->packages.count();
}

int PackageItemModel::columnCount(const QModelIndex &parent) const
{
    return 6;
}

PackageItemModel::Info* PackageItemModel::createInfo(
        const QString& package) const
{
    Info* r = new Info();

    AbstractRepository* rep = AbstractRepository::getDefault_();

    // error is ignored here
    QString err;
    QList<PackageVersion*> pvs = rep->getPackageVersions_(package, &err);

    PackageVersion* newestInstallable = 0;
    PackageVersion* newestInstalled = 0;
    for (int j = 0; j < pvs.count(); j++) {
        PackageVersion* pv = pvs.at(j);
        if (pv->installed()) {
            if (!r->installed.isEmpty())
                r->installed.append(", ");
            r->installed.append(pv->version.getVersionString());
            if (!newestInstalled ||
                    newestInstalled->version.compare(pv->version) < 0)
                newestInstalled = pv;
        }

        if (pv->download.isValid()) {
            if (!newestInstallable ||
                    newestInstallable->version.compare(pv->version) < 0)
                newestInstallable = pv;
        }
    }

    if (newestInstallable)
        r->avail = newestInstallable->version.getVersionString();

    r->up2date = !(newestInstalled && newestInstallable &&
            newestInstallable->version.compare(
            newestInstalled->version) > 0);

    qDeleteAll(pvs);
    pvs.clear();

    return r;
}

QVariant PackageItemModel::data(const QModelIndex &index, int role) const
{
    Package* p = this->packages.at(index.row());
    QVariant r;
    AbstractRepository* rep = AbstractRepository::getDefault_();
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 1:
                r = p->title;
                break;
            case 2:
                r = p->description;
                break;
            case 3: {
                Info* cached = this->cache.object(p->name);
                if (!cached) {
                    cached = createInfo(p->name);
                    r = cached->avail;
                    this->cache.insert(p->name, cached);
                } else {
                    r = cached->avail;
                }
                break;
            }
            case 4: {
                Info* cached = this->cache.object(p->name);
                if (!cached) {
                    cached = createInfo(p->name);
                    r = cached->installed;
                    this->cache.insert(p->name, cached);
                } else {
                    r = cached->installed;
                }
                break;
            }
            case 5: {
                // the error message is ignored
                QString err;
                QSharedPointer<License> lic(rep->findLicense_(
                        p->license, &err));
                if (lic)
                    r = lic->title;
                break;
            }
        }
    } else if (role == Qt::UserRole) {
        switch (index.column()) {
            case 0:
                r = qVariantFromValue(p->icon);
                break;
            default:
                r = qVariantFromValue((void*) p);
        }
    } else if (role == Qt::DecorationRole) {
        switch (index.column()) {
            case 0: {
                MainWindow* mw = MainWindow::getInstance();
                if (!p->icon.isEmpty()) {
                    if (mw->icons.contains(p->icon)) {
                        QIcon icon = mw->icons[p->icon];
                        r = qVariantFromValue(icon);
                    } else {
                        FileLoaderItem it;
                        it.url = p->icon;
                        // qDebug() << "MainWindow::loadRepository " << it.url;
                        mw->fileLoader.addWork(it);
                        r = qVariantFromValue(MainWindow::waitAppIcon);
                    }
                } else {
                    r = qVariantFromValue(MainWindow::genericAppIcon);
                }
                break;
            }
        }
    } else if (role == Qt::BackgroundRole) {
        switch (index.column()) {
            case 4: {
                Info* cached = this->cache.object(p->name);
                bool up2date;
                if (!cached) {
                    cached = createInfo(p->name);
                    up2date = cached->up2date;
                    this->cache.insert(p->name, cached);
                } else {
                    up2date = cached->up2date;
                }
                if (!up2date)
                    r = qVariantFromValue(obsoleteBrush);
                break;
            }
        }
    } else if (role == Qt::StatusTipRole) {
        switch (index.column()) {
            case 1:
                r = p->name;
                break;
        }
    }
    return r;
}

QVariant PackageItemModel::headerData(int section, Qt::Orientation orientation,
        int role) const
{
    QVariant r;
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            switch (section) {
                case 0:
                    r = QApplication::tr("Icon");
                    break;
                case 1:
                    r = QApplication::tr("Title");
                    break;
                case 2:
                    r = QApplication::tr("Description");
                    break;
                case 3:
                    r = QApplication::tr("Available");
                    break;
                case 4:
                    r = QApplication::tr("Installed");
                    break;
                case 5:
                    r = QApplication::tr("License");
                    break;
            }
        } else {
            r = QString("%1").arg(section + 1);
        }
    }
    return r;
}

void PackageItemModel::setPackages(const QList<Package *> packages)
{
    this->beginResetModel();
    qDeleteAll(this->packages);
    this->packages = packages;
    this->endResetModel();
}

void PackageItemModel::iconUpdated(const QString &url)
{
    for (int i = 0; i < this->packages.count(); i++) {
        Package* p = this->packages.at(i);
        if (p->icon == url) {
            this->dataChanged(this->index(i, 0), this->index(i, 0));
        }
    }
}

void PackageItemModel::installedStatusChanged(const QString& package,
        const Version& version)
{
    //qDebug() << "PackageItemModel::installedStatusChanged" << package <<
    //        version.getVersionString();
    this->cache.remove(package);
    for (int i = 0; i < this->packages.count(); i++) {
        Package* p = this->packages.at(i);
        if (p->name == package) {
            this->dataChanged(this->index(i, 4), this->index(i, 4));
        }
    }
}

void PackageItemModel::clearCache()
{
    this->cache.clear();
    this->dataChanged(this->index(0, 3),
            this->index(this->packages.count() - 1, 4));
}
