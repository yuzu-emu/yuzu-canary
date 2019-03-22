// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QLabel>
#include <QPushButton>
#include "video_core/memory_manager.h"
#include "video_core/textures/texture.h"
#include "yuzu/debugger/graphics/graphics_breakpoint_observer.h"

class QComboBox;
class QSpinBox;
class CSpinBox;

class GraphicsSurfaceWidget;

class SurfacePicture : public QLabel {
    Q_OBJECT

public:
    explicit SurfacePicture(QWidget* parent = nullptr,
                            GraphicsSurfaceWidget* surface_widget = nullptr);
    ~SurfacePicture() override;

protected slots:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    GraphicsSurfaceWidget* surface_widget;
};

class GraphicsSurfaceWidget : public BreakPointObserverDock {
    Q_OBJECT

    using Event = Tegra::DebugContext::Event;

    enum class Source {
        RenderTarget0 = 0,
        RenderTarget1 = 1,
        RenderTarget2 = 2,
        RenderTarget3 = 3,
        RenderTarget4 = 4,
        RenderTarget5 = 5,
        RenderTarget6 = 6,
        RenderTarget7 = 7,
        ZBuffer = 8,
        Custom = 9,
    };

public:
    explicit GraphicsSurfaceWidget(std::shared_ptr<Tegra::DebugContext> debug_context,
                                   QWidget* parent = nullptr);
    void Pick(int x, int y);

public slots:
    void OnSurfaceSourceChanged(int new_value);
    void OnSurfaceAddressChanged(qint64 new_value);
    void OnSurfaceWidthChanged(int new_value);
    void OnSurfaceHeightChanged(int new_value);
    void OnSurfaceFormatChanged(int new_value);
    void OnSurfacePickerXChanged(int new_value);
    void OnSurfacePickerYChanged(int new_value);
    void OnUpdate();

signals:
    void Update();

private:
    void OnBreakPointHit(Tegra::DebugContext::Event event, void* data) override;
    void OnResumed() override;

    void SaveSurface();

    QComboBox* surface_source_list;
    CSpinBox* surface_address_control;
    QSpinBox* surface_width_control;
    QSpinBox* surface_height_control;
    QComboBox* surface_format_control;

    SurfacePicture* surface_picture_label;
    QSpinBox* surface_picker_x_control;
    QSpinBox* surface_picker_y_control;
    QLabel* surface_info_label;
    QPushButton* save_surface;

    Source surface_source;
    GPUVAddr surface_address;
    unsigned surface_width;
    unsigned surface_height;
    Tegra::Texture::TextureFormat surface_format;
    int surface_picker_x = 0;
    int surface_picker_y = 0;
};
