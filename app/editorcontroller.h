#pragma once
#include "projectcodec.h"
#include "../platform/screencolorpicker.h"
#include "commandjobrunner.h"
#include "platformstorage.h"
#include "mapprojection.h"
#include <pandoeditor/selection.h>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <map>
#include <set>
#include <optional>

struct EditorControllerConfig {
#ifdef Q_OS_ANDROID
    bool mobileMode = true;
#else
    bool mobileMode = false;
#endif
    QString privateProjectPath;
};
struct WebImportSession;

class EditorController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* screenColorPicker READ screenColorPicker CONSTANT)
    Q_PROPERTY(QVariantMap objectProperties READ objectProperties NOTIFY propertyChanged)
    Q_PROPERTY(QString validFromDraft READ validFromDraft WRITE setValidFromDraft NOTIFY draftsChanged)
    Q_PROPERTY(QString validToDraft READ validToDraft WRITE setValidToDraft NOTIFY draftsChanged)
    Q_PROPERTY(bool colorEditOpen READ colorEditOpen NOTIFY colorEditChanged)
    Q_PROPERTY(QVariantList objectChooserCandidates READ objectChooserCandidates NOTIFY objectChooserChanged)
    Q_PROPERTY(bool objectChooserOpen READ objectChooserOpen NOTIFY objectChooserChanged)
    Q_PROPERTY(QVariantList selectionItems READ selectionItems NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap primaryObject READ primaryObject NOTIFY selectionChanged)
    Q_PROPERTY(qulonglong selectionRevision READ selectionRevision NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList objectRows READ objectRows NOTIFY stateChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    Q_PROPERTY(QVariantMap hoverObject READ hoverObject NOTIFY hoverChanged)
    Q_PROPERTY(qulonglong hoverRevision READ hoverRevision NOTIFY hoverChanged)
    Q_PROPERTY(bool webImportBusy READ webImportBusy NOTIFY webImportChanged)
    Q_PROPERTY(bool hasWebImportPreview READ hasWebImportPreview NOTIFY webImportChanged)
    Q_PROPERTY(QString webImportHash READ webImportHash NOTIFY webImportChanged)
    Q_PROPERTY(QString webImportError READ webImportError NOTIFY webImportChanged)
    Q_PROPERTY(QVariantList webImportReport READ webImportReport NOTIFY webImportChanged)
    Q_PROPERTY(QString webImportSummary READ webImportSummary NOTIFY webImportChanged)
    Q_PROPERTY(QString projectInstanceId READ projectInstanceId NOTIFY stateChanged)
    Q_PROPERTY(QString documentId READ documentId NOTIFY stateChanged)
    Q_PROPERTY(QVariantList paths READ paths NOTIFY geometryChanged)
    Q_PROPERTY(double mapWidth READ mapWidth NOTIFY geometryChanged)
    Q_PROPERTY(double mapHeight READ mapHeight NOTIFY geometryChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY visualChanged)
    Q_PROPERTY(QVariantMap countryVisuals READ countryVisuals NOTIFY visualChanged)
    Q_PROPERTY(QVariantMap layerVisuals READ layerVisuals NOTIFY visualChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY stateChanged)
    Q_PROPERTY(QVariantList countryRows READ countryRows NOTIFY stateChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY stateChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY stateChanged)
    Q_PROPERTY(bool selectedEditable READ selectedEditable NOTIFY stateChanged)
    Q_PROPERTY(QString countryLayerId READ countryLayerId NOTIFY stateChanged)
    Q_PROPERTY(QString selectedLayerId READ selectedLayerId NOTIFY stateChanged)
    Q_PROPERTY(bool canDeleteLayer READ canDeleteLayer NOTIFY stateChanged)
    Q_PROPERTY(QString nameDraft READ nameDraft WRITE setNameDraft NOTIFY draftsChanged)
    Q_PROPERTY(QString memoDraft READ memoDraft WRITE setMemoDraft NOTIFY draftsChanged)
    Q_PROPERTY(QString colorDraft READ colorDraft WRITE setColorDraft NOTIFY draftsChanged)
    Q_PROPERTY(QString layerNameDraft READ layerNameDraft WRITE setLayerNameDraft NOTIFY draftsChanged)
    Q_PROPERTY(double countryOpacity READ countryOpacity NOTIFY visualChanged)
    Q_PROPERTY(double layerOpacity READ layerOpacity NOTIFY visualChanged)
    Q_PROPERTY(bool jobBusy READ jobBusy NOTIFY jobChanged)
    Q_PROPERTY(int jobProgress READ jobProgress NOTIFY jobChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool hasPendingEdits READ hasPendingEdits NOTIFY draftsChanged)
    Q_PROPERTY(bool hasPreparedPreview READ hasPreparedPreview NOTIFY previewChanged)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY stateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QString documentNotice READ documentNotice NOTIFY stateChanged)
    Q_PROPERTY(bool mobileMode READ mobileMode CONSTANT)
    Q_PROPERTY(bool privateRecoveryRequired READ privateRecoveryRequired NOTIFY privateRecoveryRequiredChanged)
    Q_PROPERTY(QVariantMap structureState READ structureState NOTIFY structureChanged)
    Q_PROPERTY(QVariantList relationCountryOptions READ relationCountryOptions NOTIFY structureChanged)
    Q_PROPERTY(QVariantList relationParentOptions READ relationParentOptions NOTIFY structureChanged)
    Q_PROPERTY(bool structureDialogOpen READ structureDialogOpen NOTIFY structureChanged)
public:
    QObject* screenColorPicker() { return &screenColorPicker_; }
    QVariantMap objectProperties() const;
    QString validFromDraft() const { return validFromDraft_; }
    QString validToDraft() const { return validToDraft_; }
    void setValidFromDraft(const QString&);
    void setValidToDraft(const QString&);
    Q_INVOKABLE bool commitObjectField(const QString&);
    Q_INVOKABLE QString beginPropertyEdit(const QString&);
    Q_INVOKABLE bool updatePropertyEdit(const QString& token,const QString& value);
    Q_INVOKABLE bool confirmPropertyEdit(const QString& token);
    Q_INVOKABLE void endPropertyEdit(const QString& token);
    Q_INVOKABLE bool beginColorEdit();
    Q_INVOKABLE bool confirmColorEdit(const QString& color,bool reset=false);
    Q_INVOKABLE void cancelColorEdit();
    bool colorEditOpen() const { return colorSession_.has_value(); }
    Q_INVOKABLE bool toggleObjectLock();
    explicit EditorController(QObject* parent=nullptr);
    explicit EditorController(EditorControllerConfig config,QObject* parent=nullptr);
    QVariantList selectionItems() const;
    QVariantMap primaryObject() const;
    qulonglong selectionRevision() const { return selection_.revision(); }
    QVariantList objectRows() const;
    QString searchQuery() const { return searchQuery_; }
    void setSearchQuery(const QString& query);
    QVariantList searchResults() const;
    QVariantMap hoverObject() const;
    qulonglong hoverRevision() const { return hoverRevision_; }
    Q_INVOKABLE QVariantMap rangeAnchor(const QString& scope) const;
    Q_INVOKABLE bool selectObject(const QVariantMap& ref,const QString& mode="replace",const QString& scope="",const QVariantList& ordered={},bool additive=false);
    Q_INVOKABLE bool setSelection(const QVariantList& refs,const QVariantMap& primary={},const QString& scope="");
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool setHoverObject(const QVariantMap& ref,const QString& source="",const QString& expectedKey="");
    QVariantList objectChooserCandidates() const;
    bool objectChooserOpen() const { return chooserBase_.has_value() && chooserRefs_.size()>1; }
    Q_INVOKABLE void beginMapSelection(double x,double y,bool additive=false,double pixelsPerUnit=0);
    Q_INVOKABLE bool chooseMapCandidate(int index,bool toggle=false);
    Q_INVOKABLE void closeObjectChooser();
    Q_INVOKABLE QVariantMap pickObject(double x,double y) const;
    Q_INVOKABLE void selectMapAt(double x,double y,bool additive=false);
    Q_INVOKABLE bool focusObject(const QVariantMap& ref={});
    // Read-only canonical serialization for non-mutating inspection/tests.
    QByteArray documentBytes() const { return projectcodec::encode(project_); }
    bool webImportBusy() const;
    bool hasWebImportPreview() const;
    QString webImportHash() const;
    QString webImportError() const { return webImportError_; }
    QVariantList webImportReport() const;
    QString webImportSummary() const;
    QString projectInstanceId() const { return QString::fromStdString(project_.instanceId()); }
    QString documentId() const { return QString::fromStdString(project_.document().documentId); }
    Q_INVOKABLE bool prepareWebImport(const QUrl& url);
    Q_INVOKABLE bool confirmWebImport(const QString& candidateHash,const QString& disposition={},const QUrl& saveUrl={});
    Q_INVOKABLE void cancelWebImport();
    QVariantList paths() const { return projection_.paths; }
    double mapWidth() const { return projection_.width; }
    double mapHeight() const { return projection_.height; }
    QVariantMap colors() const;
    QVariantMap countryVisuals() const;
    QVariantMap layerVisuals() const;
    QVariantList layers() const;
    QVariantList countryRows() const;
    QString selectedId() const { return selected_; }
    QString selectedName() const;
    bool selectedEditable() const;
    QString countryLayerId() const;
    QString selectedLayerId() const { return selectedLayer_; }
    bool canDeleteLayer() const;
    QString nameDraft() const { return nameDraft_; }
    QString memoDraft() const { return memoDraft_; }
    QString colorDraft() const { return colorDraft_; }
    QString layerNameDraft() const { return layerNameDraft_; }
    void setNameDraft(const QString&);
    void setMemoDraft(const QString&);
    void setColorDraft(const QString&);
    void setLayerNameDraft(const QString&);
    double countryOpacity() const;
    double layerOpacity() const;
    QString fileName() const;
    QString documentNotice() const;
    bool dirty() const;
    bool jobBusy() const { return background_ && !background_->token().cancelled(); }
    int jobProgress() const { return background_ ? background_->token().progress() : -1; }
    bool hasPendingEdits() const;
    bool hasPreparedPreview() const { return pendingPreview_.has_value(); }
    qulonglong revision() const { return project_.revision(); }
    bool canUndo() const { return project_.canUndo(); }
    bool canRedo() const { return project_.canRedo(); }
    bool mobileMode() const { return mobileMode_; }
    bool privateRecoveryRequired() const { return privateRecoveryRequired_; }
    QVariantMap structureState() const;
    QVariantList relationCountryOptions() const;
    QVariantList relationParentOptions() const;
    bool structureDialogOpen() const { return structureSession_.has_value(); }
    Q_INVOKABLE bool changeSelectedParent(const QString& parentId);
    Q_INVOKABLE bool changeSelectedRegionSovereign(const QString& countryId);
    Q_INVOKABLE bool beginDeleteSelection();
    Q_INVOKABLE bool confirmStructureMutation();
    Q_INVOKABLE void cancelStructureMutation();
    Q_INVOKABLE bool beginTypeConversion();
    Q_INVOKABLE bool updateTypeConversionTarget(const QString& sovereignId,const QString& parentId);
    Q_INVOKABLE bool beginTerritorialCreate(const QString& type);
    Q_INVOKABLE bool updateTerritorialCreateSetup(const QString& name,const QString& sovereignId,const QString& parentId,const QString& sourceId);
    Q_INVOKABLE void selectAt(double x,double y);
    Q_INVOKABLE void selectCountry(const QString& id);
    Q_INVOKABLE void selectLayer(const QString& id);
    Q_INVOKABLE void setColor(const QString& color);
    Q_INVOKABLE void previewCountryOpacity(double value);
    Q_INVOKABLE void previewLayerOpacity(double value);
    Q_INVOKABLE bool commitPendingEdits();
    Q_INVOKABLE bool commitCountryField(const QString& field);
    Q_INVOKABLE bool applyPendingEditsAsync();
    Q_INVOKABLE bool preparePendingEditsAsync();
    Q_INVOKABLE void cancelBackgroundWork();
    Q_INVOKABLE bool preparePendingEdits();
    Q_INVOKABLE bool confirmPreview();
    Q_INVOKABLE void cancelPreview();
    Q_INVOKABLE void discardPendingEdits();
    Q_INVOKABLE void addLayer();
    Q_INVOKABLE void removeLayer();
    Q_INVOKABLE void moveLayer(int delta);
    Q_INVOKABLE void setLayerVisible(bool value);
    Q_INVOKABLE void setLayerLocked(bool value);
    Q_INVOKABLE void moveCountry(const QString& layerId);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool openFile(const QUrl& url);
    Q_INVOKABLE bool saveFile(const QUrl& url);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool hasFile() const { return mobileMode_ ? storage_.privateProjectExists() : !filePath_.isEmpty(); }
    Q_INVOKABLE bool restorePrivateProject();
    Q_INVOKABLE bool importProject(const QUrl& url);
    Q_INVOKABLE bool savePrivate();
    Q_INVOKABLE bool exportProject(const QUrl& url);
    Q_INVOKABLE bool confirmPrivateRecovery();
signals:
    void propertyChanged();
    void colorEditChanged();
    void objectChooserChanged();
    void selectionChanged();
    void searchChanged();
    void hoverChanged();
    void focusRequested(double left,double top,double width,double height,double maxZoom);
    void webImportChanged();
    void stateChanged();
    void jobChanged();
    void visualChanged();
    void draftsChanged();
    void dirtyChanged();
    void geometryChanged();
    void previewChanged();
    void errorOccurred(const QString& message);
    void privateRecoveryRequiredChanged();
    void structureChanged();
private:
    ScreenColorPicker screenColorPicker_;
    std::vector<pandoeditor::ObjectRef> mapCandidates(double x,double y,double pixelsPerUnit) const;
    std::vector<pandoeditor::ObjectRef> chooserRefs_;
    std::optional<pandoeditor::ProjectSnapshot> chooserBase_;
    bool chooserToggle_=false;
    struct CountryDraft { QString name,memo,color; std::optional<double> opacity; QString from,to; std::set<std::string> fields; };
    struct FieldSession { pandoeditor::ProjectSnapshot base; pandoeditor::ObjectRef ref; QString field; };
    std::map<QString,FieldSession> fieldSessions_;
    std::optional<pandoeditor::ProjectSnapshot> colorSession_;
    std::vector<pandoeditor::ObjectRef> colorTargets_;
    QString validFromDraft_,validToDraft_;
    bool runPropertyCommand(const std::string&,pandoeditor::CommandAction,const QString& changedField={});
    bool propertyBusy() const;
    const pandoeditor::TerritorialUnit* selectedUnit() const;
    void refreshDraftField(const pandoeditor::ObjectRef&,const QString&);
    struct LayerDraft { QString name; std::optional<double> opacity; };
    std::map<QString,CountryDraft> parkedCountryDrafts_;
    std::map<QString,LayerDraft> parkedLayerDrafts_;
    void parkDrafts();
    void restoreParkedDrafts();
    void clearParkedDrafts();
    void reconcileSelection();
    void applySelection(pandoeditor::SelectionState next);
    std::optional<pandoeditor::ObjectRef> existingObjectRef(const QVariantMap&) const;
    QVariantMap objectRefValue(const pandoeditor::ObjectRef&) const;
    bool objectVisible(const pandoeditor::ObjectRef&) const;
    pandoeditor::SelectionState selection_;
    std::optional<pandoeditor::ObjectRef> hover_;
    std::string selectionInstance_;
    QString searchQuery_,hoverSource_;
    qulonglong hoverRevision_=0;
    bool selectionTransition_=false;
    bool isProtectedWebSource(const QUrl& url) const;
    void webImportFailure(const QString& message);
    std::shared_ptr<WebImportSession> webImport_;
    QString webImportError_,protectedWebSource_;
    qulonglong importEditEpoch_=0;
    bool beginPendingWork(bool apply);
    bool collectPendingEdits(pandoeditor::CommandArguments& args);
    pandoeditor::CommandStatus prepareCommand(const std::string& commandId,pandoeditor::CommandArguments args);
    bool confirmCommand();
    bool executeCommand(const std::string& commandId,pandoeditor::CommandAction action);
    void commandError(pandoeditor::CommandError error,const QString& detail={});
    void publish(bool pruneSelection=true);
    void reloadDrafts();
    bool replaceFromBytes(const QByteArray& bytes,bool imported,const QString& path={});
    pandoeditor::Project project_;
    std::unique_ptr<CommandJobRunner> jobs_;
    std::optional<pandoeditor::JobTicket> background_;
    bool fieldCommitInProgress_=false;
    std::optional<pandoeditor::CommandPreview> pendingPreview_;
    struct StructureSession { pandoeditor::ProjectSnapshot base; pandoeditor::TerritorialMutationPlan plan; };
    std::optional<StructureSession> structureSession_;
    bool setStructurePlan(const pandoeditor::TerritorialMutationIntent&);
    MapProjection projection_;
    QString selected_,selectedLayer_="countries",filePath_;
    QString nameDraft_,memoDraft_,colorDraft_,layerNameDraft_;
    std::optional<double> opacityPreview_,layerOpacityPreview_;
    ProjectStorage storage_;
    bool mobileMode_=false;
    bool importedDirty_=false;
    bool privateRecoveryRequired_=false;
};
