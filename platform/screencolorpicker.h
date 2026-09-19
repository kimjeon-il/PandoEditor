#pragma once
#include <QObject>
#include <memory>

// Captures only after the explicit eyedropper action, never writes/screenshares
// the capture. Unsupported compositor/mobile environments expose no button.
class ScreenColorPicker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit ScreenColorPicker(QObject* parent=nullptr);
    ~ScreenColorPicker() override;
    bool available() const;
    bool busy() const;
    Q_INVOKABLE bool start();
    Q_INVOKABLE void cancel();
signals:
    void busyChanged();
    void colorSelected(const QString& hex);
    void failed(const QString& message);
    void cancelled();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
