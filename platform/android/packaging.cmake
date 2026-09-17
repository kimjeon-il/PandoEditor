# Included from app/ before Qt's deferred target finalization.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.6)
    qt_policy(SET QTP0002 NEW)
endif()
set_target_properties(pandoeditor PROPERTIES
    QT_ANDROID_PACKAGE_NAME "org.pandolab.pandoeditor"
    QT_ANDROID_APP_NAME "Pandoeditor"
    QT_ANDROID_VERSION_CODE 1
    QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 35
    QT_ANDROID_SDK_BUILD_TOOLS_REVISION "35.0.0"
    QT_ANDROID_PACKAGE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/platform/android"
)
