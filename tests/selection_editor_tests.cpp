#include "editorcontroller.h"
#include <QtTest>

class SelectionEditorTests : public QObject {
    Q_OBJECT
private slots:
    void selectingDoesNotCommitOrDiscardDrafts() {
        EditorController editor;
        const auto rows=editor.countryRows();
        QVERIFY(rows.size()>=2);
        const auto a=rows[0].toMap()["id"].toString();
        const auto b=rows[1].toMap()["id"].toString();
        editor.selectCountry(a);
        const auto revision=editor.revision();
        const bool undo=editor.canUndo(), redo=editor.canRedo();
        editor.setNameDraft(QStringLiteral("uncommitted selection regression"));
        const auto dirty=editor.dirty();
        editor.selectCountry(b);
        QCOMPARE(editor.revision(),revision);
        QCOMPARE(editor.canUndo(),undo);
        QCOMPARE(editor.canRedo(),redo);
        QCOMPARE(editor.dirty(),dirty);
        editor.selectCountry(a);
        QCOMPARE(editor.nameDraft(),QStringLiteral("uncommitted selection regression"));
    }
};
QTEST_GUILESS_MAIN(SelectionEditorTests)
#include "selection_editor_tests.moc"
