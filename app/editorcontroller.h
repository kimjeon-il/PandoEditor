#pragma once
#include "worldresourcecache.h"
#include "resourcecachecoordinator.h"
#include "terrainimageprovider.h"
#include "terraindisplaystate.h"
#include "projectcodec.h"
#include "../platform/screencolorpicker.h"
#include "commandjobrunner.h"
#include "platformstorage.h"
#include "autosavecoordinator.h"
#include "projectpreviewcache.h"
#include "countrylabelanchors.h"
#include "labelplacementmodel.h"
#include "physicaldatastore.h"
#include "mapprojection.h"
#include "mapscenebridge.h"
#include <pandoeditor/map/mapscenebuilder.h>
#include <pandoeditor/map/renderquality.h>
#include <pandoeditor/map/mapcamera.h>
#include <pandoeditor/map/mappicker.h>
#include <pandoeditor/map/viewportresourcescheduler.h>
#include <pandoeditor/map/builtinhydrochannel.h>
#include <pandoeditor/map/labelengine.h>
#include "hydroruntimeprovider.h"
#include "../renderer/terrainprovider.h"
#include "giscontentimport.h"
#include <pandoeditor/historicalinstantiation.h>
#include <pandoeditor/selection.h>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <map>
#include <limits>
#include <set>
#include <optional>

namespace pandoeditor { struct HistoricalSource; }

struct EditorControllerConfig {
#ifdef Q_OS_ANDROID
    bool mobileMode = true;
#else
    bool mobileMode = false;
#endif
    QString privateProjectPath;
    bool bootstrapWorld=false;
    QString worldDataRoot=QStringLiteral(":/world");
    bool autosaveEnabled=false;
    QString autosaveProjectPath,autosaveViewPath;
    bool projectPreviewEnabled=false;
    QString projectPreviewCachePath;
    QByteArray projectPreviewSourceSha="c0bd31d13dc8495593d78cf51f7cc195de7c9469";
    QString appearancePath;
};
struct WebImportSession;

class EditorController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList presentationGroups READ presentationGroups NOTIFY presentationChanged)
    Q_PROPERTY(QVariantMap distributionDisplay READ distributionDisplay NOTIFY visualChanged)
    Q_PROPERTY(QVariantMap hydroDataStatus READ hydroDataStatus NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap terrainDataStatus READ terrainDataStatus NOTIFY terrainChanged)
    Q_PROPERTY(QObject* terrainResourceBridge READ terrainResourceBridge CONSTANT)
    Q_PROPERTY(QVariantList terrainTiles READ terrainTiles NOTIFY terrainChanged)
    Q_PROPERTY(bool hydroViewportLoaded READ hydroViewportLoaded NOTIFY hydroFrameChanged)
    Q_PROPERTY(QObject* hydroSource READ hydroSource CONSTANT)
    Q_PROPERTY(QVariantMap hydroProjection READ hydroProjection NOTIFY geometryChanged)
    Q_PROPERTY(QVariantMap hydroStyle READ hydroStyle NOTIFY visualChanged)
    Q_PROPERTY(QVariantList hiddenHydroIds READ hiddenHydroIds NOTIFY stateChanged)
    Q_PROPERTY(bool hydroCopyBusy READ hydroCopyBusy NOTIFY contentEditChanged)
    Q_PROPERTY(qulonglong presentationRevision READ presentationRevision NOTIFY presentationChanged)
    Q_PROPERTY(bool presentationRecoveryAvailable READ presentationRecoveryAvailable NOTIFY presentationRecoveryChanged)
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
    Q_PROPERTY(QVariantList historicalResults READ historicalResults NOTIFY historicalChanged)
    Q_PROPERTY(QVariantList historicalSnapshots READ historicalSnapshots NOTIFY historicalChanged)
    Q_PROPERTY(QVariantMap historicalPreview READ historicalPreview NOTIFY historicalChanged)
    Q_PROPERTY(QVariantMap historicalImpact READ historicalImpact NOTIFY historicalChanged)
    Q_PROPERTY(QVariantList historicalCountries READ historicalCountries NOTIFY historicalChanged)
    Q_PROPERTY(QVariantList historicalOwnershipNeeded READ historicalOwnershipNeeded NOTIFY historicalChanged)
    Q_PROPERTY(QString historicalStage READ historicalStage NOTIFY historicalChanged)
    Q_PROPERTY(QString historicalCatalogName READ historicalCatalogName NOTIFY historicalChanged)
    Q_PROPERTY(QString historicalError READ historicalError NOTIFY historicalChanged)
    Q_PROPERTY(qulonglong historicalSession READ historicalSession NOTIFY historicalChanged)
    Q_PROPERTY(QVariantMap gisImportState READ gisImportState NOTIFY gisImportChanged)
    Q_PROPERTY(QVariantList gisExportLayers READ gisExportLayers NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap gisExportState READ gisExportState NOTIFY gisExportChanged)
    Q_PROPERTY(QVariantMap projectGpkgState READ projectGpkgState NOTIFY projectGpkgChanged)
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
    Q_PROPERTY(QObject* mapSceneBridge READ mapSceneBridge CONSTANT)
    Q_PROPERTY(QString projectionMode READ projectionMode NOTIFY viewStateChanged)
    Q_PROPERTY(QVariantMap mapViewState READ mapViewState NOTIFY viewStateChanged)
    Q_PROPERTY(QVariantList placedLabels READ placedLabels NOTIFY labelLayoutChanged)
    Q_PROPERTY(QAbstractItemModel* placedLabelModel READ placedLabelModel CONSTANT)
    Q_PROPERTY(QString terrainMode READ terrainMode NOTIFY terrainChanged)
    Q_PROPERTY(QVariantMap appearancePreferences READ appearancePreferences NOTIFY appearanceChanged)
    Q_PROPERTY(bool appearancePreviewOpen READ appearancePreviewOpen NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantMap renderQuality READ renderQuality NOTIFY renderQualityChanged)
    Q_PROPERTY(QString worldStatus READ worldStatus NOTIFY worldStatusChanged)
    Q_PROPERTY(bool startupBusy READ startupBusy NOTIFY startupBusyChanged)
    Q_PROPERTY(double mapWidth READ mapWidth NOTIFY geometryChanged)
    Q_PROPERTY(double mapHeight READ mapHeight NOTIFY geometryChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY visualChanged)
    Q_PROPERTY(QVariantMap countryVisuals READ countryVisuals NOTIFY visualChanged)
    Q_PROPERTY(QString selectedFlagSource READ selectedFlagSource NOTIFY visualChanged)
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
    Q_PROPERTY(bool hasPreservedData READ hasPreservedData NOTIFY stateChanged)
    Q_PROPERTY(bool mobileMode READ mobileMode CONSTANT)
    Q_PROPERTY(bool privateRecoveryRequired READ privateRecoveryRequired NOTIFY privateRecoveryRequiredChanged)
    Q_PROPERTY(QVariantMap structureState READ structureState NOTIFY structureChanged)
    Q_PROPERTY(QVariantList relationCountryOptions READ relationCountryOptions NOTIFY structureChanged)
    Q_PROPERTY(QVariantList relationParentOptions READ relationParentOptions NOTIFY structureChanged)
    Q_PROPERTY(bool structureDialogOpen READ structureDialogOpen NOTIFY structureChanged)
    Q_PROPERTY(QVariantMap geometryEditState READ geometryEditState NOTIFY geometryEditChanged)
    Q_PROPERTY(QVariantMap contentEditState READ contentEditState NOTIFY contentEditChanged)
    Q_PROPERTY(QVariantList geometryDraftPaths READ geometryDraftPaths NOTIFY geometryEditChanged)
public:
    QVariantList historicalResults() const;
    Q_INVOKABLE QVariantList historicalRegionOptions() const;
    QVariantList historicalSnapshots() const;
    QVariantMap historicalPreview() const;
    QVariantMap historicalImpact() const {return historicalImpact_;}
    QVariantList historicalCountries() const;
    Q_INVOKABLE QVariantList historicalParents(const QString& countryId) const;
    QVariantList historicalOwnershipNeeded() const {return historicalOwnershipNeeded_;}
    QString historicalStage() const {return historicalStage_;}
    QString historicalCatalogName() const {return historicalCatalogName_;}
    QString historicalError() const {return historicalError_;}
    qulonglong historicalSession() const {return historicalSession_;}
    Q_INVOKABLE bool loadHistoricalLibrary(const QUrl& url);
    Q_INVOKABLE void searchHistorical(const QString& query,const QString& type,
                                     const QString& status,const QString& referenceDate,const QString& region);
    Q_INVOKABLE void selectHistorical(const QString& id,const QString& versionId="",
                                     const QString& referenceDate="");
    Q_INVOKABLE bool prepareHistoricalAdd(const QVariantMap& options);
    Q_INVOKABLE bool confirmHistoricalAdd(qulonglong session);
    Q_INVOKABLE void cancelHistoricalAdd();
    QVariantMap gisImportState() const;
    Q_INVOKABLE bool loadGisSource(const QUrl& url);
    Q_INVOKABLE bool prepareGisImport(int layerIndex,const QVariantMap& mapping);
    Q_INVOKABLE bool confirmGisImport(qulonglong session);
    Q_INVOKABLE bool backGisImport();
    QVariantList gisExportLayers() const;
    QVariantMap gisExportState() const;
    Q_INVOKABLE bool exportGisData(const QUrl& destination,const QString& format,
                                  const QStringList& selected);
    Q_INVOKABLE void cancelGisExport();
    Q_INVOKABLE void cancelGisImport();
    bool presentationRecoveryAvailable() const;
    Q_INVOKABLE bool restorePresentationRecovery();
    Q_INVOKABLE bool discardPresentationRecovery();
    Q_INVOKABLE bool flushPresentationRecovery();
    QVariantList presentationGroups() const;
    QVariantMap distributionDisplay() const;
    QVariantMap hydroDataStatus() const;
    QVariantMap terrainDataStatus() const;
    QVariantList terrainTiles() const {return terrainTiles_;}
    bool hydroViewportLoaded() const {return bool(hydroRuntime_.frame());}
    QObject* hydroSource() {return &hydroRuntime_;}
    QVariantMap hydroProjection() const;
    QVariantMap hydroStyle() const;
    QVariantList hiddenHydroIds() const;
    bool hydroCopyBusy() const{return hydroCopyBusy_;}
    Q_INVOKABLE bool copyBuiltinHydro();
    qulonglong presentationRevision() const { return project_.presentationRevision(); }
    Q_INVOKABLE bool setPresentationVisibility(const QString& key,bool visible);
    Q_INVOKABLE bool setPresentationOpacity(const QString& group,double opacity);
    Q_INVOKABLE bool setPresentationBoundary(const QString& group,bool visible);
    Q_INVOKABLE bool setPresentationColorVisible(const QString& group,bool visible);
    Q_INVOKABLE bool setDistributionDisplay(const QString& mode,bool boundaryVisible,const QString& activeLayerId=QString());
    QVariantList placedLabels() const {return placedLabels_;}
    QAbstractItemModel* placedLabelModel() {return &placedLabelModel_;}
    Q_INVOKABLE bool setLabelPinned(const QVariantMap& ref,bool pinned,double longitude=0,double latitude=0,bool hasPosition=false);
    Q_INVOKABLE bool setLabelMapPosition(const QVariantMap& ref,double mapX,double mapY);
    Q_INVOKABLE bool resetLabelPosition(const QVariantMap& ref);
    Q_INVOKABLE bool configureHydroData(const QUrl& path);
    Q_INVOKABLE bool toggleSelectionVisibility();
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
    Q_INVOKABLE void beginMapSelection(double x,double y,bool additive=false,double pixelsPerUnit=0,double zoom=1);
    Q_INVOKABLE void beginMapSelectionScreen(double x,double y,bool additive=false,double zoom=1);
    Q_INVOKABLE bool chooseMapCandidate(int index,bool toggle=false);
    Q_INVOKABLE void closeObjectChooser();
    Q_INVOKABLE QVariantMap pickObject(double x,double y,double pixelsPerUnit=1,double zoom=1) const;
    Q_INVOKABLE QVariantMap pickObjectScreen(double x,double y,double zoom=1) const;
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
    QObject* mapSceneBridge() {return &sceneBridge_;}
    QString projectionMode() const;
    QVariantMap mapViewState() const;
    Q_INVOKABLE bool publishMapView(const QVariantMap& view);
    Q_INVOKABLE bool setProjectionMode(const QString& mode);
    Q_INVOKABLE bool resizeMapCamera(double width,double height,double devicePixelRatio=1);
    Q_INVOKABLE bool zoomMapCameraAt(double factor,double x,double y);
    Q_INVOKABLE void beginMapCameraPan();
    Q_INVOKABLE bool updateMapCameraPan(double deltaX,double deltaY);
    Q_INVOKABLE void endMapCameraPan();
    Q_INVOKABLE bool fitMapCamera();
    Q_INVOKABLE bool focusMapCameraRect(double left,double top,double width,double height,double maxZoom);
    Q_INVOKABLE void setMapEditorActive(bool active);
    QString terrainMode() const;
    Q_INVOKABLE bool setTerrainMode(const QString& mode);
    QVariantMap appearancePreferences() const;
    bool appearancePreviewOpen() const {return appearancePreviewOpen_;}
    Q_INVOKABLE void beginAppearancePreview();
    Q_INVOKABLE bool previewAppearance(const QVariantMap& changes);
    Q_INVOKABLE void resetAppearancePreview();
    Q_INVOKABLE void cancelAppearancePreview();
    Q_INVOKABLE bool applyAppearancePreview();
    QVariantMap renderQuality() const;
    QObject* terrainResourceBridge() const;
    std::shared_ptr<TerrainTileProvider> terrainProviderSnapshot() const {return terrainProvider_;}
    Q_INVOKABLE void recordMapFrame(double milliseconds);
    Q_INVOKABLE void beginMapInteraction();
    Q_INVOKABLE void endMapInteraction();
    void setWorldResourceBudget(std::size_t bytes);
    Q_INVOKABLE void recordGpuResourceStats(QObject* source);
    QString worldStatus() const {return worldStatus_;}
    bool startupBusy() const {return startupBusy_;}
    double mapWidth() const { return projection_.width; }
    double mapHeight() const { return projection_.height; }
    QVariantMap colors() const;
    QVariantMap countryVisuals() const;
    QString selectedFlagSource() const;
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
    bool hasPreservedData() const { return !project_.document().extensions.empty(); }
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
    bool structureDialogOpen() const { return structureSession_.has_value() || conversionDraft_.has_value() || createDraft_.has_value(); }
    Q_INVOKABLE bool changeSelectedParent(const QString& parentId);
    Q_INVOKABLE bool transferSelectedSubunit(const QString& countryId);
    Q_INVOKABLE bool changeSelectedRegionSovereign(const QString& countryId);
    Q_INVOKABLE bool beginDeleteSelection();
    Q_INVOKABLE bool beginMergeSelection();
    Q_INVOKABLE bool beginAnnexGeometry();
    Q_INVOKABLE bool beginSplitGeometry();
    Q_INVOKABLE bool beginSharedBoundaryGeometry();
    Q_INVOKABLE bool beginCoastlineGeometry(const QString& authority="country");
    Q_INVOKABLE bool confirmStructureMutation();
    Q_INVOKABLE void cancelStructureMutation();
    Q_INVOKABLE bool beginTypeConversion();
    Q_INVOKABLE bool updateTypeConversionTarget(const QString& sovereignId,const QString& parentId);
    Q_INVOKABLE bool beginTerritorialCreate(const QString& type);
    Q_INVOKABLE bool updateTerritorialCreateSetup(const QString& name,const QString& sovereignId,const QString& parentId,const QString& sourceId);
    bool beginTerritorialCreatePrepared(const pandoeditor::CreateTerritorialIntent& intent);
    QVariantMap geometryEditState() const;
    QVariantList geometryDraftPaths() const;
    Q_INVOKABLE bool beginGeometryEdit(const QString& tool="edit");
    Q_INVOKABLE bool beginGeometryDraw();
    QVariantMap contentEditState() const;
    Q_INVOKABLE bool beginContentEdit(const QString& domain,const QString& type=QString(),bool create=false);
    Q_INVOKABLE bool updateContentField(const QString& field,const QVariant& value);
    Q_INVOKABLE bool commitContentField(const QString& field);
    Q_INVOKABLE bool loadContentFlag(const QUrl& url);
    Q_INVOKABLE QVariantList flagLibrary() const;
    Q_INVOKABLE bool beginContentGeometry();
    Q_INVOKABLE bool previewContentEdit(bool remove=false);
    Q_INVOKABLE bool confirmContentEdit();
    Q_INVOKABLE void cancelContentEdit();
    Q_INVOKABLE bool geometryAddPoint(double x,double y,double tolerance=0);
    Q_INVOKABLE bool geometrySelectNearest(double x,double y,double tolerance);
    Q_INVOKABLE bool geometryMoveSelectedVertex(double x,double y,double tolerance=0);
    Q_INVOKABLE bool geometryBeginVertexDrag();
    Q_INVOKABLE void geometryEndVertexDrag(bool cancel=false);
    Q_INVOKABLE bool geometrySetMoveMode(bool enabled);
    Q_INVOKABLE bool geometryBeginObjectDrag();
    Q_INVOKABLE bool geometryTranslateObject(double dx,double dy);
    Q_INVOKABLE void geometryEndObjectDrag(bool cancel=false);
    Q_INVOKABLE bool geometryInsertNearest(double x,double y,double tolerance);
    Q_INVOKABLE bool geometryDeleteSelectedVertex();
    Q_INVOKABLE bool geometryUndoDraft();
    Q_INVOKABLE bool geometryRedoDraft();
    Q_INVOKABLE bool requestGeometryPreview();
    Q_INVOKABLE bool geometryToggleProvider(const QVariantMap& object);
    Q_INVOKABLE bool geometryAdvanceStage();
    Q_INVOKABLE bool geometryBack();
    Q_INVOKABLE bool geometryChooseSplitResult(int createdCandidate);
    Q_INVOKABLE bool confirmGeometryEdit();
    Q_INVOKABLE void cancelGeometryEdit();
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
    Q_INVOKABLE bool newProject();
    Q_INVOKABLE bool openFile(const QUrl& url);
    Q_INVOKABLE bool openProjectGeoPackage(const QUrl& url);
    Q_INVOKABLE bool exportProjectGeoPackage(const QUrl& url);
    QVariantMap projectGpkgState() const;
    Q_INVOKABLE bool saveFile(const QUrl& url);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool hasFile() const { return mobileMode_ ? storage_.privateProjectExists() : !filePath_.isEmpty(); }
    Q_INVOKABLE bool restorePrivateProject();
    Q_INVOKABLE bool importProject(const QUrl& url);
    Q_INVOKABLE bool savePrivate();
    Q_INVOKABLE bool exportProject(const QUrl& url);
    Q_INVOKABLE bool confirmPrivateRecovery();
signals:
    void renderQualityChanged();
    void historicalChanged();
    void gisImportChanged();
    void gisExportChanged();
    void projectGpkgChanged();
    void presentationRecoveryChanged();
    void presentationChanged();
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
    void geometryEditChanged();
    void contentEditChanged();
    void hydroFrameChanged();
    void worldStatusChanged();
    void startupBusyChanged();
    void terrainChanged();
    void viewStateChanged();
    void appearanceChanged();
    void labelLayoutChanged();
private:
    QVariantMap computeObjectProperties() const;
    mutable std::optional<QVariantMap> objectPropertiesCache_;
    mutable std::optional<pandoeditor::ProjectSnapshot> countryRowsSnapshot_;
    mutable QVariantList countryRowsCache_;
    mutable std::optional<QVariantList> objectRowsCache_;
    mutable std::optional<QVariantList> layersCache_;
    void startAutosaveRecovery(bool useWorldBase);
    void startWorldBootstrap();
    void startCanonicalWorld(std::uint64_t generation);
    void startCanonicalWorldMesh(std::uint64_t generation);
    void cancelWorldBootstrap();
    std::shared_ptr<const pandoeditor::HistoricalLibrary> historicalLibrary_;
    std::shared_ptr<const pandoeditor::HistoricalSource> historicalSource_;
    pandoeditor::HistoricalSearch historicalFilter_;
    QString historicalSelectedId_,historicalVersionId_,historicalReferenceDate_;
    QVariantMap historicalImpact_;
    QVariantList historicalOwnershipNeeded_;
    QString historicalStage_=QStringLiteral("unloaded"),historicalError_;
    QString historicalCatalogName_;
    bool historicalCatalogBundled_=false;
    bool installHistoricalSource(const QByteArray&,const QString&,bool bundled);
    bool refreshHistoricalCatalog();
    qulonglong historicalSession_=0;
    std::optional<pandoeditor::CommandPreview> historicalCommandPreview_;
    struct GisLoadedLayer {
        QString name,target;
        pandoeditor::GisGeoJsonCollection collection;
    };
    struct GisImportSession {
        pandoeditor::ProjectSnapshot base;
        QString fileName,sourceKind;
        std::vector<GisLoadedLayer> layers;
        std::optional<pandoeditor::CommandPreview> preview;
        QString stage=QStringLiteral("reading"),error,summary;
        int selectedLayer=-1;
    };
    std::optional<GisImportSession> gisImport_;
    qulonglong gisImportToken_=0;
    QString gisExportStage_=QStringLiteral("idle"),gisExportError_,gisExportFileName_;
    qulonglong gisExportToken_=0;
    QString projectGpkgStage_=QStringLiteral("idle"),projectGpkgError_,projectGpkgFileName_;
    qulonglong projectGpkgToken_=0;
    QTimer presentationSaveTimer_;
    QFutureWatcher<std::pair<QByteArray,QString>>* presentationRecoveryWrite_=nullptr;
    std::uint64_t presentationRecoveryGeneration_=0;
    bool presentationRecoveryPending_=false;
    void startPresentationRecovery();
    std::string presentationSaveInstance_;
    QString presentationRecoveryPath() const;
    QString availablePresentationRecoveryPath() const;
    bool discardOwnPresentationRecovery();
    void publishPresentation();
    void syncHydroData();
    void initializePhysicalData();
    void ensureHydroBootstrap();
    void requestPhysicalAsset(const QString& relativePath);
    QString physicalAssetPath(const QString& relativePath) const;
    bool physicalAssetReady(const QString& relativePath) const;
    void loadAppearancePreferences();
    bool saveAppearancePreferences() const;
    ScreenColorPicker screenColorPicker_;
    MapPickContext mapPickContext(double zoom) const;
    std::vector<pandoeditor::ObjectRef> sortMapCandidates(std::vector<pandoeditor::ObjectRef>) const;
    std::vector<pandoeditor::ObjectRef> mapCandidates(double x,double y,double pixelsPerUnit,double zoom=1) const;
    std::vector<pandoeditor::ObjectRef> mapCandidatesScreen(double x,double y,double zoom=1) const;
    QVariantMap pickObjectFromCandidates(const std::vector<pandoeditor::ObjectRef>& hits) const;
    void beginMapSelectionCandidates(std::vector<pandoeditor::ObjectRef> refs,bool additive);
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
    void refreshTypedScene();
    void updateWorldDetail();
    MapCameraMetrics mapCameraMetrics() const;
    void syncMapCameraMetrics(bool publishCurrent=true);
    bool publishCameraView();
    void scheduleViewportResources(ViewportResourceKind resources=ViewportResourceKind::All);
    void invalidateViewportResources(ViewportResourceKind resources=ViewportResourceKind::All);
    void flushViewportResources();
    void executeTerrainResources(const ViewportResourceRequest&);
    void executeHydroResources(const ViewportResourceRequest&);
    void executeLabelResources(const ViewportResourceRequest&);
    void rebuildLabelSources();
    void reprojectLabelPlacements();
    void refreshPlacedLabelRows();
    QString labelFlagSource(const pandoeditor::ObjectRef&) const;
    void refreshBuiltinHydroScene();
    void noteAppliedImpact(const pandoeditor::ChangeImpact&);
    QString labelSourceId(const std::string& ownerId) const;
    void scheduleDerivedLabelAnchor(const pandoeditor::ObjectRef& owner);
    void reloadDrafts();
    bool replaceFromBytes(const QByteArray& bytes,bool imported,const QString& path={});
    pandoeditor::Project project_;
    std::unique_ptr<ProjectPreviewService> projectPreview_;
    QByteArray projectPreviewSourceSha_;
    std::unique_ptr<CountryLabelAnchors> labelAnchors_;
    HydroRuntimeProvider hydroRuntime_;
    std::shared_ptr<const BuiltinHydroRenderFrame> builtinHydroScene_;
    std::uint64_t builtinHydroRevision_=0;
    bool hydroCopyBusy_=false;
    std::unique_ptr<CommandJobRunner> jobs_;
    std::optional<pandoeditor::JobTicket> background_;
    bool fieldCommitInProgress_=false;
    std::optional<pandoeditor::CommandPreview> pendingPreview_;
    struct StructureSession {
        pandoeditor::ProjectSnapshot base;
        pandoeditor::TerritorialMutationPlan plan;
        std::optional<pandoeditor::JobTicket> job;
        std::optional<pandoeditor::CommandPreview> preview;
        MapProjection projection;
        QString error;
    };
    // A country-to-subunit conversion needs a destination sovereign and parent
    // before the core can create a valid immutable plan.  Keep those UI-only
    // choices out of the document until the user has supplied both values.
    struct ConversionDraft { pandoeditor::ProjectSnapshot base; pandoeditor::ConvertTerritorialTypeIntent intent; };
    struct CreateDraft { pandoeditor::ProjectSnapshot base; pandoeditor::CreateTerritorialIntent intent; };
    std::optional<StructureSession> structureSession_;
    std::optional<ConversionDraft> conversionDraft_;
    std::optional<CreateDraft> createDraft_;
    struct ContentSession {
        pandoeditor::ProjectSnapshot base;
        pandoeditor::ContentEdit edit;
        std::optional<pandoeditor::CommandPreview> preview;
        QString error;
        std::set<std::string> pendingFields;
    };
    std::optional<ContentSession> contentSession_;
    void refreshContentSession();
    struct GeometryEditSession {
        pandoeditor::ProjectSnapshot base;
        pandoeditor::ObjectRef target;
        pandoeditor::Geometry draft;
        std::vector<pandoeditor::Geometry> undo;
        std::vector<pandoeditor::Geometry> redo;
        int polygon=0,ring=0,vertex=-1;
        QString tool;
        // A draw session may create a new territorial unit.  The incomplete
        // setup stays outside ProjectDocument until a valid geometry has been
        // previewed and confirmed.
        std::optional<pandoeditor::CreateTerritorialIntent> createIntent;
        std::optional<pandoeditor::CommandPreview> preview;
        std::optional<pandoeditor::JobTicket> job;
        std::uint64_t request=0;
        QString error;
        pandoeditor::Ring lineDraft;
        std::optional<pandoeditor::AnnexTerritoryIntent> annexIntent;
        std::optional<pandoeditor::SplitTerritorialIntent> splitIntent;
        std::vector<pandoeditor::ObjectRef> boundaryOwners;
        std::optional<pandoeditor::CoastlineIntent> coastIntent;
        std::optional<pandoeditor::Geometry> dragBefore;
        bool objectDragMoved=false;
        std::optional<pandoeditor::Point> snapPoint;
        bool content=false;
        QString stage=QStringLiteral("selection");
        bool choosingProviders=false;
        std::optional<pandoeditor::MergeTerritorialIntent> mergeIntent;
    };
    std::optional<GeometryEditSession> geometryEdit_;
    bool setStructurePlan(const pandoeditor::TerritorialMutationIntent&);
    MapProjection projection_;
    GeometryPacketCache packetCache_;
    AdaptiveRenderQuality quality_;
    QElapsedTimer qualityClock_;
    int activeMapInteractions_=0;
    ViewportResourceScheduler viewportResources_;
    QTimer viewportResourceTimer_;
    MapLabelEngine labelEngine_;
    QVariantList placedLabels_;
    LabelPlacementModel placedLabelModel_{this};
    std::map<pandoeditor::ObjectRef,QString> labelFlagSources_;
    std::uint64_t labelSourceRevision_=0;
    bool labelSourcesDirty_=true;
    MapSceneBuilder sceneBuilder_{packetCache_};
    MapSceneBridge sceneBridge_;
    MapCamera camera_;
    QString appearancePath_;
    QVariantMap appearance_;
    QVariantMap appearanceOrigin_;
    bool appearancePreviewOpen_=false;
    QString terrainMode_=QStringLiteral("gray");
    std::shared_ptr<const WorldBaseFrame> worldBase_;
    WorldResourceCache worldResources_;
    mutable ResourceCacheCoordinator resourceCoordinator_;
    QPointer<QObject> gpuResourceSource_;
    bool worldReloading_[2]={false,false},worldReloadFailed_[2]={false,false};
    void cacheWorldFrame(std::shared_ptr<const WorldBaseFrame>);
    void reloadWorldDetail(WorldDetail);
    bool worldDetailCanonical_=false;
    bool worldFocusDetail_=false;
    bool mapEditorActive_=false;
    std::unique_ptr<PhysicalDataStore> physicalStore_;
    QHash<QString,PhysicalAssetSpec> physicalAssets_;
    QString physicalRoot_,physicalError_;
    int physicalActive_=0,physicalQueued_=0;
    std::shared_ptr<TerrainTileProvider> terrainProvider_;
    std::unique_ptr<TerrainImageBridge> terrainResourceBridge_;
    TerrainDisplayState terrainDisplay_;
    std::weak_ptr<TerrainTileProvider> terrainDisplaySource_;
    int terrainAssetPending_=0;
    QVariantList terrainTiles_;
    int terrainMissingTiles_=0;
    QString worldHydroNotice_;
    std::vector<WorldBaseRange> worldRanges_;
    std::uint64_t worldGeneration_=0;
    QString worldStatus_=QStringLiteral("disabled");
    bool bootstrapWorldEnabled_=false;
    QString worldDataRoot_;
    bool startupBusy_=false;
    mutable MapPicker mapPicker_;
    std::optional<pandoeditor::ChangeImpact> pendingSceneImpact_;
    std::uint64_t pendingSceneImpactRevision_=0;
    std::uint64_t scenePatchCount_=0,sceneFullBuildCount_=0;
    std::size_t lastEditAffectedObjects_=0,lastEditRetainedGeometries_=0,lastEditNewGeometryBytes_=0;
    std::string sceneInstance_;
    QString selected_,selectedLayer_="countries",filePath_;
    QString nameDraft_,memoDraft_,colorDraft_,layerNameDraft_;
    std::optional<double> opacityPreview_,layerOpacityPreview_;
    ProjectStorage storage_;
    std::unique_ptr<ProjectAutosave> autosave_;
    std::string autosaveInstance_;
    std::uint64_t autosaveRevision_=std::numeric_limits<std::uint64_t>::max();
    bool mobileMode_=false;
    bool importedDirty_=false;
    bool privateRecoveryRequired_=false;
};
