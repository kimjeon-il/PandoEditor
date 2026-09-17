#include "editorcontroller.h"
#include "platformstorage.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <cstring>
#include <stdexcept>

namespace {
class UnknownSizeDevice final : public QIODevice {
public:
    explicit UnknownSizeDevice(qint64 byteCount) : remaining_(byteCount)
    {
        open(QIODevice::ReadOnly);
    }

    bool isSequential() const override { return true; }
    qint64 size() const override { return -1; }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        if (remaining_ == 0)
            return 0;
        const auto count = qMin(maxSize, remaining_);
        std::memset(data, 'x', static_cast<std::size_t>(count));
        remaining_ -= count;
        return count;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    qint64 remaining_;
};

class FailingDevice final : public QIODevice {
public:
    FailingDevice() { open(QIODevice::ReadOnly); }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        if (!sentPrefix_) {
            const QByteArray prefix("partial");
            const auto count = qMin(maxSize, prefix.size());
            std::memcpy(data, prefix.constData(), static_cast<std::size_t>(count));
            sentPrefix_ = true;
            return count;
        }
        setErrorString(QStringLiteral("simulated stream failure"));
        return -1;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    bool sentPrefix_ = false;
};

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray sampleProject()
{
    QFile file(QStringLiteral(":/assets/sample.pando.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray normalizedSampleProject()
{
    pandoeditor::Project project;
    project.replace(projectcodec::decode(sampleProject()));
    return projectcodec::encode(project);
}
} // namespace

class StorageTests : public QObject {
    Q_OBJECT

private slots:
    void v3InvalidCandidatesPreserveDesktopAndMobileSessions()
    {
        for(bool mobile:{false,true}) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            EditorController editor(EditorControllerConfig{mobile,dir.filePath("private.json")});
            editor.selectCountry("DEU"); editor.setColor("#112233"); editor.setMemoDraft("keep draft");
            const auto colors=editor.colors(); const auto selection=editor.selectedId();
            const auto paths=editor.paths(); const auto layers=editor.layers();
            const auto root=QJsonDocument::fromJson(normalizedSampleProject()).object();
            QList<QJsonObject> invalid;
            auto bad=root; bad["version"]=99; invalid.append(bad);
            bad=root; auto units=bad["units"].toArray(); units.append(units[0]); bad["units"]=units; invalid.append(bad);
            bad=root; bad["relations"]=QJsonArray{QJsonObject{
                {"id","broken"},{"unitRef",QJsonObject{{"domain","territorial"},{"id","DEU"}}},
                {"parentRef",QJsonObject{{"domain","territorial"},{"id","missing"}}},
                {"sovereignRef",QJsonValue::Null},{"mode","base"},
                {"validity",QJsonObject{{"from",QJsonValue::Null},{"to",QJsonValue::Null}}}}}; invalid.append(bad);
            for(const auto& candidate:invalid) {
                auto url=QUrl::fromLocalFile(dir.filePath("invalid.json"));
                QFile file(url.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(candidate).toJson()); file.close();
                QVERIFY(!(mobile?editor.importProject(url):editor.openFile(url)));
                QCOMPARE(editor.colors(),colors); QCOMPARE(editor.selectedId(),selection);
                QCOMPARE(editor.paths(),paths); QCOMPARE(editor.layers(),layers);
                QCOMPARE(editor.memoDraft(),QString("keep draft")); QVERIFY(editor.dirty()); QVERIFY(editor.canUndo()); QVERIFY(!editor.canRedo());
            }
            editor.setMemoDraft(""); editor.undo();
            QVERIFY(!editor.canUndo()); QVERIFY(editor.canRedo());
            QVERIFY(editor.colors()!=colors); editor.redo(); QCOMPARE(editor.colors(),colors);
        }
    }
    void openLegacyDoesNotRewriteUntilSaveAndBlockedDraftIsKept()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto path=dir.filePath("legacy.json"); const auto original=sampleProject();
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(original); file.close();
        EditorController editor; QVERIFY(editor.openFile(QUrl::fromLocalFile(path)));
        QCOMPARE(readFile(path),original); QVERIFY(!editor.dirty());
        QVERIFY(editor.documentNotice().contains("v3"));
        QVERIFY(editor.saveFile(QUrl::fromLocalFile(path)));
        QCOMPARE(QJsonDocument::fromJson(readFile(path)).object()["version"].toInt(),3);
        auto extended=original; extended.insert(extended.indexOf('{')+1,"\"future\":{\"x\":true},");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(extended); file.close();
        QVERIFY(editor.openFile(QUrl::fromLocalFile(path))); editor.selectCountry("DEU");
        editor.setNameDraft("safe name"); editor.setColorDraft("#123456");
        const auto colors=editor.colors();
        QVERIFY(!editor.commitPendingEdits()); QCOMPARE(editor.colors(),colors);
        QCOMPARE(editor.nameDraft(),QString("safe name")); QVERIFY(!editor.canUndo());
        editor.setColorDraft(colors["DEU"].toString()); QVERIFY(editor.commitPendingEdits());
        QVERIFY(editor.saveFile(QUrl::fromLocalFile(path))); QVERIFY(editor.openFile(QUrl::fromLocalFile(path)));
        editor.selectCountry("DEU"); QCOMPARE(editor.selectedName(),QString("safe name"));
    }
    void boundedReadRejectsUnknownLengthStreamsBeyond64MiB()
    {
        UnknownSizeDevice tooLarge(ProjectStorage::MaximumProjectBytes + 1);
        QVERIFY_EXCEPTION_THROWN(ProjectStorage::readBounded(tooLarge), std::runtime_error);

        UnknownSizeDevice exact(ProjectStorage::MaximumProjectBytes);
        QCOMPARE(ProjectStorage::readBounded(exact).size(), ProjectStorage::MaximumProjectBytes);
    }

    void boundedReadDoesNotAcceptTruncatedDataAfterStreamError()
    {
        FailingDevice failing;
        QVERIFY_EXCEPTION_THROWN(ProjectStorage::readBounded(failing), std::runtime_error);
    }

    void localWritesAreAtomicAndDoNotReplaceOnOpenFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("project.pando.json"));
        QFile initial(path);
        QVERIFY(initial.open(QIODevice::WriteOnly));
        QCOMPARE(initial.write("old"), qint64(3));
        initial.close();

        ProjectStorage storage(path);
        storage.writePrivateAtomic("new document");
        QCOMPARE(readFile(path), QByteArray("new document"));
        QVERIFY_EXCEPTION_THROWN(storage.writePrivateAtomic(QByteArray(ProjectStorage::MaximumProjectBytes+1,'x')), std::runtime_error);
        QCOMPARE(readFile(path), QByteArray("new document"));

        const auto missing = directory.filePath(QStringLiteral("missing/project.pando.json"));
        ProjectStorage unavailable(missing);
        QVERIFY_EXCEPTION_THROWN(unavailable.writePrivateAtomic("must not appear"), std::runtime_error);
        QVERIFY(!QFile::exists(missing));
    }

    void canceledAndFailedImportsPreserveDocumentDraftsAndHistory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EditorController editor(EditorControllerConfig{true, directory.filePath("private.pando.json")});
        QSignalSpy errors(&editor, &EditorController::errorOccurred);
        editor.selectCountry("DEU");
        editor.setColor("#123456");
        editor.setMemoDraft("unsaved draft");
        const auto colors = editor.colors();
        QVERIFY(editor.dirty());
        QVERIFY(editor.canUndo());

        QVERIFY(editor.importProject(QUrl()));
        QCOMPARE(errors.count(), 0);
        QCOMPARE(editor.colors(), colors);
        QCOMPARE(editor.memoDraft(), QString("unsaved draft"));
        QVERIFY(editor.canUndo());

        const auto invalidPath = directory.filePath("invalid.json");
        QFile invalid(invalidPath);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not json");
        invalid.close();
        QVERIFY(!editor.importProject(QUrl::fromLocalFile(invalidPath)));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(editor.colors(), colors);
        QCOMPARE(editor.memoDraft(), QString("unsaved draft"));
        QVERIFY(editor.dirty());
        QVERIFY(editor.canUndo());
    }

    void importedDocumentStaysDirtyUntilSuccessfulPrivateSave()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto importPath = directory.filePath("import.pando.json");
        QFile imported(importPath);
        QVERIFY(imported.open(QIODevice::WriteOnly));
        imported.write(sampleProject());
        imported.close();

        const auto missingPrivatePath = directory.filePath("missing/private.pando.json");
        EditorController editor(EditorControllerConfig{true, missingPrivatePath});
        QVERIFY(editor.importProject(QUrl::fromLocalFile(importPath)));
        QVERIFY(editor.dirty());
        QVERIFY(!editor.canUndo());
        QVERIFY(!editor.savePrivate());
        QVERIFY(editor.dirty());

        EditorController savable(EditorControllerConfig{true, directory.filePath("private.pando.json")});
        QVERIFY(savable.importProject(QUrl::fromLocalFile(importPath)));
        QVERIFY(savable.dirty());
        QVERIFY(savable.savePrivate());
        QVERIFY(!savable.dirty());
        QCOMPARE(readFile(directory.filePath("private.pando.json")), normalizedSampleProject());
    }

    void corruptRestoreIsPreservedAndBlocksSaveUntilExplicitRecovery()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto privatePath = directory.filePath("private.pando.json");
        QFile corrupt(privatePath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("corrupt project");
        corrupt.close();

        EditorController editor(EditorControllerConfig{true, privatePath});
        QSignalSpy errors(&editor, &EditorController::errorOccurred);
        QVERIFY(!editor.restorePrivateProject());
        QVERIFY(editor.privateRecoveryRequired());
        QCOMPARE(readFile(privatePath), QByteArray("corrupt project"));

        editor.selectCountry("DEU");
        editor.setColor("#123456");
        QVERIFY(!editor.savePrivate());
        QVERIFY(editor.dirty());
        QCOMPARE(readFile(privatePath), QByteArray("corrupt project"));
        QVERIFY(errors.count() >= 2);

        editor.confirmPrivateRecovery();
        QVERIFY(!editor.privateRecoveryRequired());
        QCOMPARE(readFile(privatePath + ".corrupt"), QByteArray("corrupt project"));
        QVERIFY(editor.savePrivate());
        QVERIFY(!editor.dirty());
        QVERIFY(readFile(privatePath).startsWith('{'));
    }

    void validRestoreIsCleanAndExportFailureKeepsPrivateSave()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto privatePath = directory.filePath("private.pando.json");
        ProjectStorage(privatePath).writePrivateAtomic(sampleProject());

        EditorController editor(EditorControllerConfig{true, privatePath});
        QVERIFY(editor.restorePrivateProject());
        QVERIFY(!editor.dirty());
        editor.selectCountry("DEU");
        editor.setColor("#123456");
        QVERIFY(editor.savePrivate());
        const auto savedSnapshot = readFile(privatePath);
        QVERIFY(!editor.dirty());

        QVERIFY(!editor.exportProject(QUrl::fromLocalFile(directory.filePath("missing/export.pando.json"))));
        QVERIFY(!editor.dirty());
        QCOMPARE(readFile(privatePath), savedSnapshot);

        QSignalSpy errors(&editor, &EditorController::errorOccurred);
        QVERIFY(editor.exportProject(QUrl()));
        QCOMPARE(errors.count(), 0);

        const auto exportPath = directory.filePath("export.pando.json");
        QVERIFY(editor.exportProject(QUrl::fromLocalFile(exportPath)));
        QCOMPARE(readFile(exportPath), savedSnapshot);
    }
};

QTEST_GUILESS_MAIN(StorageTests)
#include "storage_tests.moc"
