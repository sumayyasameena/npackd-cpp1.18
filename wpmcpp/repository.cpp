#include <windows.h>
#include <shlobj.h>

#include "qtemporaryfile.h"
#include "downloader.h"
#include "qsettings.h"
#include "qdom.h"
#include "qdebug.h"

#include "repository.h"
#include "downloader.h"
#include "packageversionfile.h"
#include "wpmutils.h"
#include "version.h"
#include "msi.h"
#include "fileextensionhandler.h"

Repository* Repository::def = 0;

Repository::Repository()
{
    this->installedGraph = 0;
}

QList<PackageVersion*> Repository::getInstalled()
{
    QList<PackageVersion*> ret;

    QDir aDir = getDirectory();
    if (aDir.exists()) {
        QFileInfoList entries = aDir.entryInfoList(
                QDir::NoDotAndDotDot |
                QDir::Dirs);
        int count = entries.size();
        for (int idx = 0; idx < count; idx++) {
            QFileInfo entryInfo = entries[idx];
            QString fn = entryInfo.fileName();
            int p = fn.lastIndexOf('-');
            if (p > 0) {
                QString package = fn.left(p);
                if (Package::isValidName(package)) {
                    QString version_ = fn.right(fn.length() - p - 1);
                    Version version;
                    if (version.setVersion(version_)) {
                        PackageVersion* pv =
                                this->findPackageVersion(package, version);
                        if (!pv) {
                            pv = new PackageVersion(package);
                            pv->version = version;
                            this->packageVersions.append(pv);
                        }
                        ret.append(pv);
                    }
                }
            }
        }
    }

    Repository* r = Repository::getDefault();
    for (int i = 0; i < r->packageVersions.count(); i++) {
        PackageVersion* pv = r->packageVersions.at(i);
        if (pv->external) {
            ret.append(pv);
        }
    }

    return ret;
}

Digraph* Repository::getInstalledGraph()
{
    if (!this->installedGraph) {
        this->installedGraph = new Digraph();
        Node* user = this->installedGraph->addNode(0);
        for (int i = 0; i < this->packageVersions.count(); i++) {
            PackageVersion* pv = this->packageVersions.at(i);
            if (pv->installed()) {
                Node* n = this->installedGraph->addNode(pv);
                user->to.append(n);
            }
        }

        for (int i = 1; i < this->installedGraph->nodes.count(); i++) {
            Node* n = this->installedGraph->nodes.at(i);
            PackageVersion* pv = (PackageVersion*) n->userData;
            for (int j = 0; j < pv->dependencies.count(); j++) {
                Dependency* d = pv->dependencies.at(j);
                PackageVersion* pv2 = d->findHighestInstalledMatch();
                Node* n2 = this->installedGraph->findNodeByUserData(pv2);
                if (!n2) {
                    n2 = this->installedGraph->addNode(pv2);
                }
                n->to.append(n2);
            }
        }
    }
    return this->installedGraph;
}

void Repository::somethingWasInstalledOrUninstalled()
{
    if (this->installedGraph) {
        delete this->installedGraph;
        this->installedGraph = 0;
    }
}

Repository::~Repository()
{
    delete this->installedGraph;
    qDeleteAll(this->packages);
    qDeleteAll(this->packageVersions);
    qDeleteAll(this->licenses);
}

PackageVersion* Repository::findNewestPackageVersion(const QString &name)
{
    PackageVersion* r = 0;

    for (int i = 0; i < this->packageVersions.count(); i++) {
        PackageVersion* p = this->packageVersions.at(i);
        if (p->package == name) {
            if (r == 0 || p->version.compare(r->version) > 0) {
                r = p;
            }
        }
    }
    return r;
}

PackageVersion* Repository::findNewestInstalledPackageVersion(QString &name)
{
    PackageVersion* r = 0;

    for (int i = 0; i < this->packageVersions.count(); i++) {
        PackageVersion* p = this->packageVersions.at(i);
        if (p->package == name && p->installed()) {
            if (r == 0 || p->version.compare(r->version) > 0) {
                r = p;
            }
        }
    }
    return r;
}

PackageVersion* Repository::createPackageVersion(QDomElement* e)
{
    // qDebug() << "Repository::createPackageVersion.1" << e->attribute("package");

    PackageVersion* a = new PackageVersion(
            e->attribute("package"));
    QString url = e->elementsByTagName("url").at(0).
                  firstChild().nodeValue();
    a->download.setUrl(url);
    QString name = e->attribute("name", "1.0");
    a->version.setVersion(name);
    a->version.normalize();

    QDomNodeList sha1 = e->elementsByTagName("sha1");
    if (sha1.count() > 0)
        a->sha1 = sha1.at(0).firstChild().nodeValue().trimmed();

    QString type = e->attribute("type", "zip");
    if (type == "one-file")
        a->type = 1;
    else
        a->type = 0;

    QDomNodeList ifiles = e->elementsByTagName("important-file");
    for (int i = 0; i < ifiles.count(); i++) {
        QDomElement e = ifiles.at(i).toElement();
        QString p = e.attribute("path", "");
        if (p.isEmpty())
            p = e.attribute("name", "");
        a->importantFiles.append(p);

        QString title = e.attribute("title", p);
        a->importantFilesTitles.append(title);
    }

    QDomNodeList nl = e->elementsByTagName("file-handler");
    for (int i = 0; i < nl.count(); i++) {
        QDomElement e = nl.at(i).toElement();
        QString prg = e.elementsByTagName("executable").at(0).firstChild().
                      nodeValue().trimmed();
        QString title = e.elementsByTagName("title").at(0).firstChild().
                      nodeValue().trimmed();
        FileExtensionHandler* fh = new FileExtensionHandler(prg);
        fh->title = title;
        a->fileHandlers.append(fh);

        QDomNodeList nl2 = e.elementsByTagName("extension");
        for (int j = 0; j < nl2.count(); j++) {
            QString ext = nl2.at(j).firstChild().nodeValue().trimmed();
            fh->extensions.append(ext);
        }
    }

    QDomNodeList files = e->elementsByTagName("file");
    for (int i = 0; i < files.count(); i++) {
        QDomElement e = files.at(i).toElement();
        a->files.append(createPackageVersionFile(&e));
    }

    QDomNodeList deps = e->elementsByTagName("dependency");
    for (int i = 0; i < deps.count(); i++) {
        QDomElement e = deps.at(i).toElement();
        Dependency* d = createDependency(&e);
        if (d)
            a->dependencies.append(d);
    }

    // qDebug() << "Repository::createPackageVersion.2";
    return a;
}

Package* Repository::createPackage(QDomElement* e)
{
    QString name = e->attribute("name");
    Package* a = new Package(name, name);
    QDomNodeList nl = e->elementsByTagName("title");
    if (nl.count() != 0)
        a->title = nl.at(0).firstChild().nodeValue();
    nl = e->elementsByTagName("url");
    if (nl.count() != 0)
        a->url = nl.at(0).firstChild().nodeValue();
    nl = e->elementsByTagName("description");
    if (nl.count() != 0)
        a->description = nl.at(0).firstChild().nodeValue();
    nl = e->elementsByTagName("icon");
    if (nl.count() != 0) {
        a->icon = nl.at(0).firstChild().nodeValue();
    }
    nl = e->elementsByTagName("license");
    if (nl.count() != 0) {
        a->license = nl.at(0).firstChild().nodeValue();
    }

    return a;
}

PackageVersionFile* Repository::createPackageVersionFile(QDomElement* e)
{
    QString path = e->attribute("path");
    QString content = e->firstChild().nodeValue();
    PackageVersionFile* a = new PackageVersionFile(path, content);

    return a;
}

License* Repository::createLicense(QDomElement* e)
{
    QString name = e->attribute("name");
    License* a = new License(name, name);
    QDomNodeList nl = e->elementsByTagName("title");
    if (nl.count() != 0)
        a->title = nl.at(0).firstChild().nodeValue();
    nl = e->elementsByTagName("url");
    if (nl.count() != 0)
        a->url = nl.at(0).firstChild().nodeValue();
    nl = e->elementsByTagName("description");
    if (nl.count() != 0)
        a->description = nl.at(0).firstChild().nodeValue();

    return a;
}

Dependency* Repository::createDependency(QDomElement* e)
{
    // qDebug() << "Repository::createDependency";

    QString package = e->attribute("package").trimmed();

    Dependency* d = new Dependency();
    d->package = package;
    if (d->setVersions(e->attribute("versions")))
        return d;
    else {
        delete d;
        return 0;
    }

    // qDebug() << d->toString();

    return d;
}

License* Repository::findLicense(const QString& name)
{
    for (int i = 0; i < this->licenses.count(); i++) {
        if (this->licenses.at(i)->name == name)
            return this->licenses.at(i);
    }
    return 0;
}

Package* Repository::findPackage(const QString& name)
{
    for (int i = 0; i < this->packages.count(); i++) {
        if (this->packages.at(i)->name == name)
            return this->packages.at(i);
    }
    return 0;
}

int Repository::countUpdates()
{
    int r = 0;
    for (int i = 0; i < this->packageVersions.count(); i++) {
        PackageVersion* p = this->packageVersions.at(i);
        if (p->installed()) {
            PackageVersion* newest = findNewestPackageVersion(p->package);
            if (newest->version.compare(p->version) > 0 && !newest->installed())
                r++;
        }
    }
    return r;
}

void Repository::recognize(Job* job)
{
    job->setProgress(0);

    job->setHint("Detecting Windows");
    if (!this->findPackage("com.microsoft.Windows")) {
        Package* p = new Package("com.microsoft.Windows", "Windows");
        p->url = "http://www.microsoft.com/windows/";
        p->description = "Operating system";
        this->packages.append(p);
    }
    OSVERSIONINFO osvi;
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&osvi);
    Version v;
    v.setVersion(osvi.dwMajorVersion, osvi.dwMinorVersion,
            osvi.dwBuildNumber);
    PackageVersion* pv = this->findPackageVersion(
            "com.microsoft.Windows", v);
    if (!pv) {
        pv = new PackageVersion("com.microsoft.Windows");
        v.normalize();
        pv->version = v;
        this->packageVersions.append(pv);
    }
    pv->external = true;
    somethingWasInstalledOrUninstalled();
    job->setProgress(0.5);

    if (!job->isCancelled()) {
        job->setHint("Detecting JRE");
        detectJRE(false);
        if (WPMUtils::is64BitWindows())
            detectJRE(true);
        job->setProgress(0.75);
    }

    if (!job->isCancelled()) {
        job->setHint("Detecting JDK");
        detectJDK(false);
        if (WPMUtils::is64BitWindows())
            detectJDK(true);
        job->setProgress(0.8);
    }

    if (!job->isCancelled()) {
        job->setHint("Detecting .NET");
        detectDotNet();
        job->setProgress(0.9);
    }

    if (!job->isCancelled()) {
        job->setHint("Detecting .NET");
        detectMSIProducts();
        job->setProgress(0.95);
    }

    if (!job->isCancelled()) {
        job->setHint("Detecting Windows Installer");
        detectMicrosoftInstaller();
        job->setProgress(0.97);
    }

    if (!job->isCancelled()) {
        job->setHint("Detecting Microsoft Core XML Services (MSXML)");
        detectMSXML();
        job->setProgress(1);
    }

    job->complete();
}

void Repository::detectJRE(bool w64bit)
{
    if (!this->findPackage("com.oracle.JRE")) {
        Package* p = new Package("com.oracle.JRE", "JRE");
        p->url = "http://www.java.com/";
        p->description = "Java runtime";
        this->packages.append(p);
    }
    HKEY hk;
    const REGSAM KEY_WOW64_64KEY = 0x0100;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
            L"Software\\JavaSoft\\Java Runtime Environment",
            0, KEY_READ | (w64bit ? KEY_WOW64_64KEY : 0),
            &hk) == ERROR_SUCCESS) {
        WCHAR name[255];
        int index = 0;
        while (true) {
            DWORD nameSize = sizeof(name) / sizeof(name[0]);
            LONG r = RegEnumKeyEx(hk, index, name, &nameSize,
                    0, 0, 0, 0);
            if (r == ERROR_SUCCESS) {
                QString v_;
                v_.setUtf16((ushort*) name, nameSize);
                v_ = v_.replace('_', '.');
                Version v;
                if (v.setVersion(v_) && v.getNParts() > 2) {
                    PackageVersion* pv =
                            this->findPackageVersion("com.oracle.JRE", v);
                    if (!pv) {
                        pv = new PackageVersion("com.oracle.JRE");
                        v.normalize();
                        pv->version = v;
                        pv->external = true;
                        this->packageVersions.append(pv);
                    } else {
                        if (!pv->installed())
                            pv->external = true;
                    }
                    somethingWasInstalledOrUninstalled();
                }
            } else if (r == ERROR_NO_MORE_ITEMS) {
                break;
            }
            index++;
        }
        RegCloseKey(hk);
    }
}

void Repository::detectJDK(bool w64bit)
{
    if (!this->findPackage("com.oracle.JDK")) {
        Package* p = new Package("com.oracle.JDK", "JDK");
        p->url = "http://www.oracle.com/technetwork/java/javase/overview/index.html";
        p->description = "Java development kit";
        this->packages.append(p);
    }
    HKEY hk;
    const REGSAM KEY_WOW64_64KEY = 0x0100;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
            L"Software\\JavaSoft\\Java Development Kit",
            0, KEY_READ | (w64bit ? KEY_WOW64_64KEY : 0),
            &hk) == ERROR_SUCCESS) {
        WCHAR name[255];
        int index = 0;
        while (true) {
            DWORD nameSize = sizeof(name) / sizeof(name[0]);
            LONG r = RegEnumKeyEx(hk, index, name, &nameSize,
                    0, 0, 0, 0);
            if (r == ERROR_SUCCESS) {
                QString v_;
                v_.setUtf16((ushort*) name, nameSize);
                v_ = v_.replace('_', '.');
                Version v;
                if (v.setVersion(v_) && v.getNParts() > 2) {
                    PackageVersion* pv =
                            this->findPackageVersion("com.oracle.JDK", v);
                    if (!pv) {
                        pv = new PackageVersion("com.oracle.JDK");
                        v.normalize();
                        pv->version = v;
                        pv->external = true;
                        this->packageVersions.append(pv);
                    } else {
                        if (!pv->installed())
                            pv->external = true;
                    }
                    somethingWasInstalledOrUninstalled();
                }
            } else if (r == ERROR_NO_MORE_ITEMS) {
                break;
            }
            index++;
        }
        RegCloseKey(hk);
    }
}

void Repository::versionDetected(const QString &package, const Version &v)
{
    PackageVersion* pv = findPackageVersion(package, v);
    if (pv) {
        if (!pv->installed())
            pv->external = true;
    } else {
        pv = new PackageVersion(package);
        pv->version = v;
        pv->version.normalize();
        pv->external = true;
        this->packageVersions.append(pv);
    }
    somethingWasInstalledOrUninstalled();
}

void Repository::detectOneDotNet(HKEY hk2, const QString& keyName)
{
    QString packageName("com.microsoft.DotNetRedistributable");
    Version keyVersion;

    Version oneOne(1, 1);
    Version four(4, 0);
    Version two(2, 0);

    Version v;
    bool found = false;
    if (keyName.startsWith("v") && keyVersion.setVersion(
            keyName.right(keyName.length() - 1))) {
        if (keyVersion.compare(oneOne) < 0) {
            // not yet implemented
        } else if (keyVersion.compare(two) < 0) {
            v = keyVersion;
            found = true;
        } else if (keyVersion.compare(four) < 0) {
            QString value_ = WPMUtils::regQueryValue(hk2, "Version");
            if (v.setVersion(value_)) {
                found = true;
            }
        } else {
            HKEY hk3;
            if (RegOpenKeyExW(hk2, L"Full",
                    0, KEY_READ, &hk3) == ERROR_SUCCESS) {
                QString value_ = WPMUtils::regQueryValue(hk3, "Version");
                if (v.setVersion(value_)) {
                    found = true;
                }
                RegCloseKey(hk2);
            }
        }
    }

    if (found) {
        PackageVersion* pv = this->findPackageVersion(
                packageName, v);
        if (!pv) {
            pv = new PackageVersion(packageName);
            v.normalize();
            pv->version = v;
            pv->external = true;
            this->packageVersions.append(pv);
        } else {
            if (!pv->installed())
                pv->external = true;
        }
        somethingWasInstalledOrUninstalled();
    }
}

void Repository::detectMSIProducts()
{
    QStringList guids = WPMUtils::findInstalledMSIProducts();

    // Detecting VisualC++ runtimes:
    // http://blogs.msdn.com/b/astebner/archive/2009/01/29/9384143.aspx
    if (guids.contains("{FF66E9F6-83E7-3A3E-AF14-8DE9A809A6A4}")) {
        this->versionDetected("com.microsoft.VisualCPPRedistributable",
                Version("9.0.21022.8"));
    }
    if (guids.contains("{9A25302D-30C0-39D9-BD6F-21E6EC160475}")) {
        this->versionDetected("com.microsoft.VisualCPPRedistributable",
                Version("9.0.30729.17"));
    }
    if (guids.contains("{1F1C2DFC-2D24-3E06-BCB8-725134ADF989}")) {
        this->versionDetected("com.microsoft.VisualCPPRedistributable",
                Version("9.0.30729.4148"));
    }
}

void Repository::detectDotNet()
{
    QString packageName("com.microsoft.DotNetRedistributable");

    // http://stackoverflow.com/questions/199080/how-to-detect-what-net-framework-versions-and-service-packs-are-installed
    if (!this->findPackage(packageName)) {
        Package* p = new Package("com.microsoft.DotNetRedistributable",
                ".NET redistributable runtime");
        p->url = "http://www.microsoft.com/downloads/details.aspx?FamilyID=0856eacb-4362-4b0d-8edd-aab15c5e04f5&amp;displaylang=en";
        p->description = ".NET runtime";
        this->packages.append(p);
    }
    HKEY hk;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\NET Framework Setup\\NDP",
            0, KEY_READ, &hk) == ERROR_SUCCESS) {
        WCHAR name[255];
        int index = 0;
        while (true) {
            DWORD nameSize = sizeof(name) / sizeof(name[0]);
            LONG r = RegEnumKeyEx(hk, index, name, &nameSize,
                    0, 0, 0, 0);
            if (r == ERROR_SUCCESS) {
                QString v_;
                v_.setUtf16((ushort*) name, nameSize);
                Version v;
                if (v_.startsWith("v") && v.setVersion(
                        v_.right(v_.length() - 1))) {
                    HKEY hk2;
                    if (RegOpenKeyExW(hk, (WCHAR*) v_.utf16(),
                            0, KEY_READ, &hk2) == ERROR_SUCCESS) {
                        detectOneDotNet(hk2, v_);
                        RegCloseKey(hk2);
                    }
                }
            } else if (r == ERROR_NO_MORE_ITEMS) {
                break;
            }
            index++;
        }
        RegCloseKey(hk);
    }
}

void Repository::detectMicrosoftInstaller()
{
    if (!this->findPackage("com.microsoft.WindowsInstaller")) {
        Package* p = new Package("com.microsoft.WindowsInstaller",
                "Windows Installer");
        p->url = "http://msdn.microsoft.com/en-us/library/cc185688(VS.85).aspx";
        p->description = "Package manager";
        this->packages.append(p);
    }

    Version v = WPMUtils::getDLLVersion("MSI.dll");
    Version nullNull(0, 0);
    if (v.compare(nullNull) > 0) {
        this->versionDetected("com.microsoft.WindowsInstaller", v);
    }
}

void Repository::detectMSXML()
{
    if (!this->findPackage("com.microsoft.MSXML")) {
        Package* p = new Package("com.microsoft.MSXML",
                "Microsoft Core XML Services (MSXML)");
        p->url = "http://www.microsoft.com/downloads/en/details.aspx?FamilyID=993c0bcf-3bcf-4009-be21-27e85e1857b1#Overview";
        p->description = "XML library";
        this->packages.append(p);
    }

    Version v = WPMUtils::getDLLVersion("msxml.dll");
    Version nullNull(0, 0);
    if (v.compare(nullNull) > 0) {
        this->versionDetected("com.microsoft.MSXML", v);
    }
    v = WPMUtils::getDLLVersion("msxml2.dll");
    if (v.compare(nullNull) > 0) {
        this->versionDetected("com.microsoft.MSXML", v);
    }
    v = WPMUtils::getDLLVersion("msxml3.dll");
    if (v.compare(nullNull) > 0) {
        v.prepend(3);
        this->versionDetected("com.microsoft.MSXML", v);
    }
    v = WPMUtils::getDLLVersion("msxml4.dll");
    if (v.compare(nullNull) > 0) {
        this->versionDetected("com.microsoft.MSXML", v);
    }
    v = WPMUtils::getDLLVersion("msxml5.dll");
    if (v.compare(nullNull) > 0) {
        this->versionDetected("com.microsoft.MSXML", v);
    }
    v = WPMUtils::getDLLVersion("msxml6.dll");
    if (v.compare(nullNull) > 0) {
        this->versionDetected("com.microsoft.MSXML", v);
    }
}

QDir Repository::getDirectory()
{
    QDir d(WPMUtils::getInstallationDirectory());
    return d;
}

PackageVersion* Repository::findPackageVersion(const QString& package,
        const Version& version)
{
    PackageVersion* r = 0;

    for (int i = 0; i < this->packageVersions.count(); i++) {
        PackageVersion* p = this->packageVersions.at(i);
        if (p->package == package && p->version.compare(version) == 0) {
            r = p;
            break;
        }
    }
    return r;
}

void Repository::process(Job *job, const QList<InstallOperation *> &install)
{
    int n = install.count();

    for (int i = 0; i < install.count(); i++) {
        InstallOperation* op = install.at(i);
        PackageVersion* pv = op->packageVersion;
        if (op->install)
            job->setHint(QString("Installing %1").arg(
                    pv->toString()));
        else
            job->setHint(QString("Uninstalling %1").arg(
                    pv->toString()));
        Job* sub = job->newSubJob(1.0 / n);
        if (op->install)
            pv->install(sub);
        else
            pv->uninstall(sub);
        if (!sub->getErrorMessage().isEmpty())
            job->setErrorMessage(sub->getErrorMessage());
        delete sub;

        if (!job->getErrorMessage().isEmpty())
            break;
    }

    job->complete();
}

void Repository::addUnknownExistingPackages()
{
    QDir aDir = getDirectory();
    if (aDir.exists()) {
        QFileInfoList entries = aDir.entryInfoList(
                QDir::NoDotAndDotDot |
                QDir::Dirs);
        int count = entries.size();
        for (int idx = 0; idx < count; idx++) {
            QFileInfo entryInfo = entries[idx];
            QString fn = entryInfo.fileName();
            QStringList sl = fn.split('-');
            if (sl.count() == 2) {
                QString package = sl.at(0);
                if (Package::isValidName(package)) {
                    QString version_ = sl.at(1);
                    Version version;
                    if (version.setVersion(version_)) {
                        if (this->findPackage(package) == 0) {
                            QString title = package +
                                    " (unknown in current repositories)";
                            Package* p = new Package(package,
                                    title);
                            this->packages.append(p);
                        }
                        PackageVersion* pv;
                        pv = this->findPackageVersion(package, version);
                        if (pv == 0) {
                            pv = new PackageVersion(package);
                            version.normalize();
                            pv->version = version;
                            pv->external = !version.isNormalized();
                            this->packageVersions.append(pv);
                            somethingWasInstalledOrUninstalled();
                        } else {
                            if (!version.isNormalized())
                                pv->external = true;
                        }
                    }
                }
            }
        }
    }
}

void Repository::load(Job* job)
{
    qDeleteAll(this->packages);
    this->packages.clear();
    qDeleteAll(this->packageVersions);
    this->packageVersions.clear();
    delete this->installedGraph;

    QList<QUrl*> urls = getRepositoryURLs();
    if (urls.count() > 0) {
        for (int i = 0; i < urls.count(); i++) {
            job->setHint(QString("Repository %1 of %2").arg(i + 1).
                         arg(urls.count()));
            Job* s = job->newSubJob(0.9 / urls.count());
            loadOne(urls.at(i), s);
            if (!s->getErrorMessage().isEmpty()) {
                job->setErrorMessage(QString(
                        "Error loading the repository %1: %2").arg(
                        urls.at(i)->toString()).arg(
                        s->getErrorMessage()));
                delete s;
                break;
            }
            delete s;

            if (job->isCancelled())
                break;
        }
    } else {
        job->setErrorMessage("No repositories defined");
        job->setProgress(0.9);
    }

    // qDebug() << "Repository::load.3";

    job->setHint("Scanning for installed package versions");
    addUnknownExistingPackages();

    qDeleteAll(urls);
    urls.clear();

    job->setHint("Detecting software");
    Job* sub = job->newSubJob(0.1);
    recognize(sub);
    delete sub;

    job->complete();
}

void Repository::loadOne(QUrl* url, Job* job) {
    job->setHint("Downloading");

    QTemporaryFile* f = 0;
    if (job->getErrorMessage().isEmpty() && !job->isCancelled()) {
        Job* djob = job->newSubJob(0.90);
        f = Downloader::download(djob, *url);
        if (!djob->getErrorMessage().isEmpty())
            job->setErrorMessage(QString("Download failed: %2").
                    arg(djob->getErrorMessage()));
        delete djob;
    }

    QDomDocument doc;
    if (job->getErrorMessage().isEmpty() && !job->isCancelled()) {
        job->setHint("Parsing the content");
        // qDebug() << "Repository::loadOne.2";
        int errorLine;
        int errorColumn;
        QString errMsg;
        if (!doc.setContent(f, &errMsg, &errorLine, &errorColumn))
            job->setErrorMessage(QString("XML parsing failed: %1").
                                 arg(errMsg));
    }

    QDomElement root;
    if (job->getErrorMessage().isEmpty() && !job->isCancelled()) {
        root = doc.documentElement();
        QDomNodeList nl = root.elementsByTagName("spec-version");
        if (nl.count() != 0) {
            QString specVersion = nl.at(0).firstChild().nodeValue();
            Version specVersion_;
            if (!specVersion_.setVersion(specVersion)) {
                job->setErrorMessage(QString(
                        "Invalid repository specification version: %1").
                        arg(specVersion));
            } else {
                if (specVersion_.compare(Version(3,0)) >= 0)
                    job->setErrorMessage(QString(
                            "Incompatible repository specification version: %1").
                            arg(specVersion));
            }
        }
    }

    if (job->getErrorMessage().isEmpty() && !job->isCancelled()) {
        for (QDomNode n = root.firstChild(); !n.isNull();
                n = n.nextSibling()) {
            if (n.isElement()) {
                QDomElement e = n.toElement();
                if (e.nodeName() == "version") {
                    PackageVersion* pv = createPackageVersion(&e);
                    if (this->findPackageVersion(pv->package, pv->version))
                        delete pv;
                    else {
                        this->packageVersions.append(pv);
                        somethingWasInstalledOrUninstalled();
                    }
                } else if (e.nodeName() == "package") {
                    Package* p = createPackage(&e);
                    if (this->findPackage(p->name))
                        delete p;
                    else
                        this->packages.append(p);
                } else if (e.nodeName() == "license") {
                    License* p = createLicense(&e);
                    if (this->findLicense(p->name))
                        delete p;
                    else
                        this->licenses.append(p);
                }
            }
        }
        job->setProgress(1);
    }

    delete f;

    job->complete();
}


QList<QUrl*> Repository::getRepositoryURLs()
{
    QList<QUrl*> r;
    QSettings s1("Npackd", "Npackd");
    int size = s1.beginReadArray("repositories");
    for (int i = 0; i < size; ++i) {
        s1.setArrayIndex(i);
        QString v = s1.value("repository").toString();
        r.append(new QUrl(v));
    }
    s1.endArray();

    if (size == 0) {
        QSettings s("WPM", "Windows Package Manager");

        int size = s.beginReadArray("repositories");
        for (int i = 0; i < size; ++i) {
            s.setArrayIndex(i);
            QString v = s.value("repository").toString();
            r.append(new QUrl(v));
        }
        s.endArray();

        if (size == 0) {
            QString v = s.value("repository", "").toString();
            if (v != "") {
                r.append(new QUrl(v));
            }
        }
        setRepositoryURLs(r);
    }
    
    return r;
}

void Repository::setRepositoryURLs(QList<QUrl*>& urls)
{
    QSettings s("Npackd", "Npackd");
    s.beginWriteArray("repositories", urls.count());
    for (int i = 0; i < urls.count(); ++i) {
        s.setArrayIndex(i);
        s.setValue("repository", urls.at(i)->toString());
    }
    s.endArray();
}

Repository* Repository::getDefault()
{
    if (!def) {
        def = new Repository();
    }
    return def;
}


