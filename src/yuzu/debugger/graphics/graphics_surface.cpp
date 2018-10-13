// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QBoxLayout>
#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include "common/vector_math.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"
#include "video_core/textures/decoders.h"
#include "video_core/textures/texture.h"
#include "yuzu/debugger/graphics/graphics_surface.h"
#include "yuzu/util/spinbox.h"

static Tegra::Texture::TextureFormat ConvertToTextureFormat(
    Tegra::RenderTargetFormat render_target_format) {
    switch (render_target_format) {
    case Tegra::RenderTargetFormat::RGBA8_UNORM:
        return Tegra::Texture::TextureFormat::A8R8G8B8;
    case Tegra::RenderTargetFormat::RGB10_A2_UNORM:
        return Tegra::Texture::TextureFormat::A2B10G10R10;
    default:
        UNIMPLEMENTED_MSG("Unimplemented RT format");
    }
}

SurfacePicture::SurfacePicture(QWidget* parent, GraphicsSurfaceWidget* surface_widget_)
    : QLabel(parent), surface_widget(surface_widget_) {}

SurfacePicture::~SurfacePicture() = default;

void SurfacePicture::mousePressEvent(QMouseEvent* event) {
    // Only do something while the left mouse button is held down
    if (!(event->buttons() & Qt::LeftButton))
        return;

    if (pixmap() == nullptr)
        return;

    if (surface_widget)
        surface_widget->Pick(event->x() * pixmap()->width() / width(),
                             event->y() * pixmap()->height() / height());
}

void SurfacePicture::mouseMoveEvent(QMouseEvent* event) {
    // We also want to handle the event if the user moves the mouse while holding down the LMB
    mousePressEvent(event);
}

GraphicsSurfaceWidget::GraphicsSurfaceWidget(std::shared_ptr<Tegra::DebugContext> debug_context,
                                             QWidget* parent)
    : BreakPointObserverDock(debug_context, tr("Maxwell Surface Viewer"), parent),
      surface_source(Source::RenderTarget0) {
    setObjectName("MaxwellSurface");

    surface_source_list = new QComboBox;
    surface_source_list->addItem(tr("Render Target 0"));
    surface_source_list->addItem(tr("Render Target 1"));
    surface_source_list->addItem(tr("Render Target 2"));
    surface_source_list->addItem(tr("Render Target 3"));
    surface_source_list->addItem(tr("Render Target 4"));
    surface_source_list->addItem(tr("Render Target 5"));
    surface_source_list->addItem(tr("Render Target 6"));
    surface_source_list->addItem(tr("Render Target 7"));
    surface_source_list->addItem(tr("Z Buffer"));
    surface_source_list->addItem(tr("Custom"));
    surface_source_list->setCurrentIndex(static_cast<int>(surface_source));

    surface_address_control = new CSpinBox;
    surface_address_control->SetBase(16);
    surface_address_control->SetRange(0, 0x7FFFFFFFFFFFFFFF);
    surface_address_control->SetPrefix("0x");

    unsigned max_dimension = 16384; // TODO: Find actual maximum

    surface_width_control = new QSpinBox;
    surface_width_control->setRange(0, max_dimension);

    surface_height_control = new QSpinBox;
    surface_height_control->setRange(0, max_dimension);

    surface_picker_x_control = new QSpinBox;
    surface_picker_x_control->setRange(0, max_dimension - 1);

    surface_picker_y_control = new QSpinBox;
    surface_picker_y_control->setRange(0, max_dimension - 1);

    surface_format_control = new QComboBox;

    // Color formats sorted by Maxwell texture format index
    surface_format_control->addItem(tr("None"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("A8R8G8B8"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("Unknown"));
    surface_format_control->addItem(tr("DXT1"));
    surface_format_control->addItem(tr("DXT23"));
    surface_format_control->addItem(tr("DXT45"));
    surface_format_control->addItem(tr("DXN1"));
    surface_format_control->addItem(tr("DXN2"));

    surface_info_label = new QLabel();
    surface_info_label->setWordWrap(true);

    surface_picture_label = new SurfacePicture(0, this);
    surface_picture_label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    surface_picture_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    surface_picture_label->setScaledContents(false);

    auto scroll_area = new QScrollArea();
    scroll_area->setBackgroundRole(QPalette::Dark);
    scroll_area->setWidgetResizable(false);
    scroll_area->setWidget(surface_picture_label);

    save_surface = new QPushButton(QIcon::fromTheme("document-save"), tr("Save"));

    // Connections
    connect(this, &GraphicsSurfaceWidget::Update, this, &GraphicsSurfaceWidget::OnUpdate);
    connect(surface_source_list,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &GraphicsSurfaceWidget::OnSurfaceSourceChanged);
    connect(surface_address_control, &CSpinBox::ValueChanged, this,
            &GraphicsSurfaceWidget::OnSurfaceAddressChanged);
    connect(surface_width_control, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &GraphicsSurfaceWidget::OnSurfaceWidthChanged);
    connect(surface_height_control, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &GraphicsSurfaceWidget::OnSurfaceHeightChanged);
    connect(surface_format_control,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &GraphicsSurfaceWidget::OnSurfaceFormatChanged);
    connect(surface_picker_x_control, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &GraphicsSurfaceWidget::OnSurfacePickerXChanged);
    connect(surface_picker_y_control, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &GraphicsSurfaceWidget::OnSurfacePickerYChanged);
    connect(save_surface, &QPushButton::clicked, this, &GraphicsSurfaceWidget::SaveSurface);

    auto main_widget = new QWidget;
    auto main_layout = new QVBoxLayout;
    {
        auto sub_layout = new QHBoxLayout;
        sub_layout->addWidget(new QLabel(tr("Source:")));
        sub_layout->addWidget(surface_source_list);
        main_layout->addLayout(sub_layout);
    }
    {
        auto sub_layout = new QHBoxLayout;
        sub_layout->addWidget(new QLabel(tr("GPU Address:")));
        sub_layout->addWidget(surface_address_control);
        main_layout->addLayout(sub_layout);
    }
    {
        auto sub_layout = new QHBoxLayout;
        sub_layout->addWidget(new QLabel(tr("Width:")));
        sub_layout->addWidget(surface_width_control);
        main_layout->addLayout(sub_layout);
    }
    {
        auto sub_layout = new QHBoxLayout;
        sub_layout->addWidget(new QLabel(tr("Height:")));
        sub_layout->addWidget(surface_height_control);
        main_layout->addLayout(sub_layout);
    }
    {
        auto sub_layout = new QHBoxLayout;
        sub_layout->addWidget(new QLabel(tr("Format:")));
        sub_layout->addWidget(surface_format_control);
        main_layout->addLayout(sub_layout);
    }
    main_layout->addWidget(scroll_area);

    auto info_layout = new QHBoxLayout;
    {
        auto xy_layout = new QVBoxLayout;
        {
            {
                auto sub_layout = new QHBoxLayout;
                sub_layout->addWidget(new QLabel(tr("X:")));
                sub_layout->addWidget(surface_picker_x_control);
                xy_layout->addLayout(sub_layout);
            }
            {
                auto sub_layout = new QHBoxLayout;
                sub_layout->addWidget(new QLabel(tr("Y:")));
                sub_layout->addWidget(surface_picker_y_control);
                xy_layout->addLayout(sub_layout);
            }
        }
        info_layout->addLayout(xy_layout);
        surface_info_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        info_layout->addWidget(surface_info_label);
    }
    main_layout->addLayout(info_layout);

    main_layout->addWidget(save_surface);
    main_widget->setLayout(main_layout);
    setWidget(main_widget);

    // Load current data - TODO: Make sure this works when emulation is not running
    if (debug_context && debug_context->at_breakpoint) {
        emit Update();
        widget()->setEnabled(debug_context->at_breakpoint);
    } else {
        widget()->setEnabled(false);
    }
}

void GraphicsSurfaceWidget::OnBreakPointHit(Tegra::DebugContext::Event event, void* data) {
    emit Update();
    widget()->setEnabled(true);
}

void GraphicsSurfaceWidget::OnResumed() {
    widget()->setEnabled(false);
}

void GraphicsSurfaceWidget::OnSurfaceSourceChanged(int new_value) {
    surface_source = static_cast<Source>(new_value);
    emit Update();
}

void GraphicsSurfaceWidget::OnSurfaceAddressChanged(qint64 new_value) {
    if (surface_address != new_value) {
        surface_address = static_cast<Tegra::GPUVAddr>(new_value);

        surface_source_list->setCurrentIndex(static_cast<int>(Source::Custom));
        emit Update();
    }
}

void GraphicsSurfaceWidget::OnSurfaceWidthChanged(int new_value) {
    if (surface_width != static_cast<unsigned>(new_value)) {
        surface_width = static_cast<unsigned>(new_value);

        surface_source_list->setCurrentIndex(static_cast<int>(Source::Custom));
        emit Update();
    }
}

void GraphicsSurfaceWidget::OnSurfaceHeightChanged(int new_value) {
    if (surface_height != static_cast<unsigned>(new_value)) {
        surface_height = static_cast<unsigned>(new_value);

        surface_source_list->setCurrentIndex(static_cast<int>(Source::Custom));
        emit Update();
    }
}

void GraphicsSurfaceWidget::OnSurfaceFormatChanged(int new_value) {
    if (surface_format != static_cast<Tegra::Texture::TextureFormat>(new_value)) {
        surface_format = static_cast<Tegra::Texture::TextureFormat>(new_value);

        surface_source_list->setCurrentIndex(static_cast<int>(Source::Custom));
        emit Update();
    }
}

void GraphicsSurfaceWidget::OnSurfacePickerXChanged(int new_value) {
    if (surface_picker_x != new_value) {
        surface_picker_x = new_value;
        Pick(surface_picker_x, surface_picker_y);
    }
}

void GraphicsSurfaceWidget::OnSurfacePickerYChanged(int new_value) {
    if (surface_picker_y != new_value) {
        surface_picker_y = new_value;
        Pick(surface_picker_x, surface_picker_y);
    }
}

void GraphicsSurfaceWidget::Pick(int x, int y) {
    surface_picker_x_control->setValue(x);
    surface_picker_y_control->setValue(y);

    if (x < 0 || x >= static_cast<int>(surface_width) || y < 0 ||
        y >= static_cast<int>(surface_height)) {
        surface_info_label->setText(tr("Pixel out of bounds"));
        surface_info_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return;
    }

    surface_info_label->setText(QString("Raw: <Unimplemented>\n(%1)").arg("<Unimplemented>"));
    surface_info_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
}

void GraphicsSurfaceWidget::OnUpdate() {
    auto& gpu = Core::System::GetInstance().GPU();

    QPixmap pixmap;

    switch (surface_source) {
    case Source::RenderTarget0:
    case Source::RenderTarget1:
    case Source::RenderTarget2:
    case Source::RenderTarget3:
    case Source::RenderTarget4:
    case Source::RenderTarget5:
    case Source::RenderTarget6:
    case Source::RenderTarget7: {
        // TODO: Store a reference to the registers in the debug context instead of accessing them
        // directly...

        const auto& registers = gpu.Maxwell3D().regs;
        const auto& rt = registers.rt[static_cast<std::size_t>(surface_source) -
                                      static_cast<std::size_t>(Source::RenderTarget0)];

        surface_address = rt.Address();
        surface_width = rt.width;
        surface_height = rt.height;
        if (rt.format != Tegra::RenderTargetFormat::NONE) {
            surface_format = ConvertToTextureFormat(rt.format);
        }

        break;
    }

    case Source::Custom: {
        // Keep user-specified values
        break;
    }

    default:
        qDebug() << "Unknown surface source " << static_cast<int>(surface_source);
        break;
    }

    surface_address_control->SetValue(surface_address);
    surface_width_control->setValue(surface_width);
    surface_height_control->setValue(surface_height);
    surface_format_control->setCurrentIndex(static_cast<int>(surface_format));

    if (surface_address == 0) {
        surface_picture_label->hide();
        surface_info_label->setText(tr("(invalid surface address)"));
        surface_info_label->setAlignment(Qt::AlignCenter);
        surface_picker_x_control->setEnabled(false);
        surface_picker_y_control->setEnabled(false);
        save_surface->setEnabled(false);
        return;
    }

    // TODO: Implement a good way to visualize alpha components!

    QImage decoded_image(surface_width, surface_height, QImage::Format_ARGB32);
    boost::optional<VAddr> address = gpu.MemoryManager().GpuToCpuAddress(surface_address);

    // TODO(bunnei): Will not work with BCn formats that swizzle 4x4 tiles.
    // Needs to be fixed if we plan to use this feature more, otherwise we may remove it.
    auto unswizzled_data =
        Tegra::Texture::UnswizzleTexture(*address, 1, Tegra::Texture::BytesPerPixel(surface_format),
                                         surface_width, surface_height, 1U);

    auto texture_data = Tegra::Texture::DecodeTexture(unswizzled_data, surface_format,
                                                      surface_width, surface_height);

    surface_picture_label->show();

    for (unsigned int y = 0; y < surface_height; ++y) {
        for (unsigned int x = 0; x < surface_width; ++x) {
            Math::Vec4<u8> color;
            color[0] = texture_data[x + y * surface_width + 0];
            color[1] = texture_data[x + y * surface_width + 1];
            color[2] = texture_data[x + y * surface_width + 2];
            color[3] = texture_data[x + y * surface_width + 3];
            decoded_image.setPixel(x, y, qRgba(color.r(), color.g(), color.b(), color.a()));
        }
    }

    pixmap = QPixmap::fromImage(decoded_image);
    surface_picture_label->setPixmap(pixmap);
    surface_picture_label->resize(pixmap.size());

    // Update the info with pixel data
    surface_picker_x_control->setEnabled(true);
    surface_picker_y_control->setEnabled(true);
    Pick(surface_picker_x, surface_picker_y);

    // Enable saving the converted pixmap to file
    save_surface->setEnabled(true);
}

void GraphicsSurfaceWidget::SaveSurface() {
    QString png_filter = tr("Portable Network Graphic (*.png)");
    QString bin_filter = tr("Binary data (*.bin)");

    QString selectedFilter;
    QString filename = QFileDialog::getSaveFileName(
        this, tr("Save Surface"),
        QString("texture-0x%1.png").arg(QString::number(surface_address, 16)),
        QString("%1;;%2").arg(png_filter, bin_filter), &selectedFilter);

    if (filename.isEmpty()) {
        // If the user canceled the dialog, don't save anything.
        return;
    }

    if (selectedFilter == png_filter) {
        const QPixmap* pixmap = surface_picture_label->pixmap();
        ASSERT_MSG(pixmap != nullptr, "No pixmap set");

        QFile file(filename);
        file.open(QIODevice::WriteOnly);
        if (pixmap)
            pixmap->save(&file, "PNG");
    } else if (selectedFilter == bin_filter) {
        auto& gpu = Core::System::GetInstance().GPU();
        boost::optional<VAddr> address = gpu.MemoryManager().GpuToCpuAddress(surface_address);

        const u8* buffer = Memory::GetPointer(*address);
        ASSERT_MSG(buffer != nullptr, "Memory not accessible");

        QFile file(filename);
        file.open(QIODevice::WriteOnly);
        int size = surface_width * surface_height * Tegra::Texture::BytesPerPixel(surface_format);
        QByteArray data(reinterpret_cast<const char*>(buffer), size);
        file.write(data);
    } else {
        UNREACHABLE_MSG("Unhandled filter selected");
    }
}
