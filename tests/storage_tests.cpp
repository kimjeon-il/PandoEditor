#include "editorcontroller.h"
#include "platformstorage.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
