/************************************************************************
**
**  Copyright (C) 2015-2023 Kevin B, Hendricks, Stratford Ontario Canada
**  Copyright (C) 2012-2013 John Schember <john@nachtimwald.com>
**  Copyright (C) 2012-2013 Dave Heiland
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include <QApplication>
#include <QGuiApplication>
#include <QtCore/QFileInfo>
#include <QEventLoop>
#include <QImage>
#include <QPixmap>
#include <QtWidgets/QLayout>
#include <QtWebEngineWidgets>
#include <QtWebEngineCore>
#include <QWebEngineView>
#include <QWebEngineSettings>
#include <QWebEngineProfile>

#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "Misc/WebProfileMgr.h"
#include "sigil_constants.h"
#include "ViewEditors/SimplePage.h"
#include "Dialogs/SelectFiles.h"

static const int COL_NAME = 0;
static const int COL_IMAGE = 1;

static const int THUMBNAIL_SIZE = 100;
static const int THUMBNAIL_SIZE_INCREMENT = 50;

static QString SETTINGS_GROUP = "select_images";

const QString IMAGE_HTML_BASE_PREVIEW =
    "<html>"
    "<head>"
    "<style type=\"text/css\">"
    "body { -webkit-user-select: none; }"
    "img { display: block; margin-left: auto; margin-right: auto; border-style: solid; border-width: 1px; max-width: 95%; max-height: 95%}"
    "</style>"
    "</head>"
    "<body>"
    "<div><img src=\"%1\" /></div>"
    "</body>"
    "</html>";

const QString AUDIO_HTML_BASE =
    "<html>"
    "<head>"
    "<style type=\"text/css\">"
    "body { -webkit-user-select: none; }"
    "audio { display: block; margin-left: auto; margin-right: auto; }"
    "</style>"
    "</head>"
    "<body>"
    "<p><audio controls=\"controls\" src=\"%1\"></audio></p>"
    "</body>"
    "</html>";

const QString VIDEO_HTML_BASE =
    "<html>"
    "<head>"
    "<style type=\"text/css\">"
    "body { -webkit-user-select: none; }"
    "video { display: block; margin-left: auto; margin-right: auto; }"
    "</style>"
    "</head>"
    "<body>"
    "<p><video controls=\"controls\" width=\"560\" src=\"%1\"></video></p>"
    "</body>"
    "</html>";


SelectFiles::SelectFiles(QString title, QList<Resource *> media_resources, QString default_selected_image, QWidget *parent) :
    QDialog(parent),
    m_MediaResources(media_resources),
    m_SelectFilesModel(new QStandardItemModel),
    m_PreviewReady(false),
    m_PreviewLoaded(false),
    m_DefaultSelectedImage(default_selected_image),
    m_ThumbnailSize(THUMBNAIL_SIZE),
    m_IsInsertFromDisk(false),
    m_WebView(new QWebEngineView(this))
{
    ui.setupUi(this);
    setWindowTitle(title);
    QWebEngineProfile* profile = WebProfileMgr::instance()->GetOneTimeProfile();
    m_WebView->setPage(new SimplePage(profile, m_WebView));
    m_WebView->setContextMenuPolicy(Qt::NoContextMenu);
    m_WebView->setFocusPolicy(Qt::NoFocus);
    m_WebView->setAcceptDrops(false);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    m_WebView->page()->settings()->setAttribute(QWebEngineSettings::ShowScrollBars,false);
#endif
    ui.avLayout->addWidget(m_WebView);

    ReadSettings();

    m_AllItem = new QListWidgetItem(tr("All"), ui.FileTypes);
    m_ImageItem = new QListWidgetItem(tr("Images"), ui.FileTypes);
    m_VideoItem = new QListWidgetItem(tr("Video"), ui.FileTypes);
    m_AudioItem = new QListWidgetItem(tr("Audio"), ui.FileTypes);

    ui.FileTypes->setCurrentItem(m_AllItem);

    SetImages();

    connectSignalsSlots();

    SetPreviewImage();
}

SelectFiles::~SelectFiles()
{
    WriteSettings();
}

bool SelectFiles::IsInsertFromDisk()
{
    return m_IsInsertFromDisk;
}

QStringList SelectFiles::SelectedImages()
{
    QList<QString> selected_images;

    // Shift-click order is top to bottom regardless of starting position
    // Ctrl-click order is first clicked to last clicked (included shift-clicks stay ordered as is)
    if (ui.imageTree->selectionModel()->hasSelection()) {
        QModelIndexList selected_indexes = ui.imageTree->selectionModel()->selectedRows(0);
        foreach(QModelIndex index, selected_indexes) {
            selected_images.append(m_SelectFilesModel->itemFromIndex(index)->text());
        }
    }

    return selected_images;
}

void SelectFiles::SetImages()
{
    ui.Details->clear();
    QString html = "<html><head><title></title></head><body></body></html>";
    if (Utility::IsDarkMode()) {
        html = Utility::AddDarkCSS(html);
    }
    m_WebView->setHtml(html, QUrl());

    ui.imageTree->reset();
    m_SelectFilesModel->clear();
    QStringList header;
    header.append(tr("Files In the Book"));

    if (m_ThumbnailSize) {
        header.append(tr("Thumbnails"));
    }

    m_SelectFilesModel->setHorizontalHeaderLabels(header);
    ui.imageTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.imageTree->setModel(m_SelectFilesModel);
    QSize icon_size(m_ThumbnailSize, m_ThumbnailSize);
    ui.imageTree->setIconSize(icon_size);
    ui.imageTree->setSortingEnabled(true);
    int row = 0;

    foreach(Resource *resource, m_MediaResources) {
        // Don't show resources not matching the selected type
        Resource::ResourceType type = resource->Type();
        if ((m_ImageItem->isSelected() && type != Resource::ImageResourceType && type != Resource::SVGResourceType) ||
            (m_VideoItem->isSelected() && type != Resource::VideoResourceType) ||
            (m_AudioItem->isSelected() && type != Resource::AudioResourceType)) {
            continue;
        }

        QString filepath = resource->GetRelativePath();
        QList<QStandardItem *> rowItems;
        QStandardItem *name_item = new QStandardItem();
        name_item->setText(filepath);
        name_item->setToolTip(resource->ShortPathName());
        name_item->setData(static_cast<int>(type), Qt::UserRole);
        name_item->setData(resource->GetFullPath(), Qt::UserRole + 1);
        name_item->setEditable(false);
        rowItems << name_item;

        // Do not show thumbnail if file is not an image
        if ((type == Resource::ImageResourceType || type == Resource::SVGResourceType) && m_ThumbnailSize) {
            QImage image;
            if (type == Resource::ImageResourceType) {
                image.load(resource->GetFullPath());
            } else {
                image = Utility::RenderSvgToImage(resource->GetFullPath());
            }
            QPixmap pixmap = QPixmap::fromImage(image);
            if (pixmap.height() > m_ThumbnailSize || pixmap.width() > m_ThumbnailSize) {
                pixmap = pixmap.scaled(QSize(m_ThumbnailSize, m_ThumbnailSize), Qt::KeepAspectRatio);
            }
            QStandardItem *icon_item = new QStandardItem();
            icon_item->setData(QVariant(pixmap), Qt::DecorationRole);
            icon_item->setEditable(false);
            rowItems << icon_item;
        }

        m_SelectFilesModel->appendRow(rowItems);
        row++;
    }
    ui.imageTree->header()->setStretchLastSection(true);

    for (int i = 0; i < ui.imageTree->header()->count(); i++) {
        ui.imageTree->resizeColumnToContents(i);
    }

    // Sort by filename by default.
    ui.imageTree->header()->setSortIndicator(0, Qt::AscendingOrder);

    FilterEditTextChangedSlot(ui.Filter->text());
    SelectDefaultImage();
}

void SelectFiles::SelectDefaultImage()
{
    QStandardItem *root_item = m_SelectFilesModel->invisibleRootItem();
    QModelIndex parent_index;

    // Set the default to the first image if no default is set
    if (m_DefaultSelectedImage.isEmpty() && root_item->rowCount() > 0) {
        m_DefaultSelectedImage = m_SelectFilesModel->itemFromIndex(m_SelectFilesModel->index(0, 0, parent_index))->text();
    }

    for (int row = 0; row < root_item->rowCount(); row++) {
        if (root_item->child(row, COL_NAME)->text() == m_DefaultSelectedImage) {
            ui.imageTree->selectionModel()->select(m_SelectFilesModel->index(row, 0, parent_index), QItemSelectionModel::Select | QItemSelectionModel::Rows);
            ui.imageTree->setFocus();
            ui.imageTree->setCurrentIndex(root_item->child(row, 0)->index());
            break;
        }
    }
}

void SelectFiles::IncreaseThumbnailSize()
{
    m_ThumbnailSize += THUMBNAIL_SIZE_INCREMENT;
    ui.ThumbnailDecrease->setEnabled(true);
    m_DefaultSelectedImage = GetLastSelectedImageName();
    SetImages();
}

void SelectFiles::DecreaseThumbnailSize()
{
    m_ThumbnailSize -= THUMBNAIL_SIZE_INCREMENT;

    if (m_ThumbnailSize <= 0) {
        m_ThumbnailSize = 0;
        ui.ThumbnailDecrease->setEnabled(false);
    }

    m_DefaultSelectedImage = GetLastSelectedImageName();
    SetImages();
}

void SelectFiles::ReloadPreview()
{
    // Make sure we don't load when initial painting is resizing
    if (m_PreviewReady) {
        SetPreviewImage();
    }
}

void SelectFiles::SelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    ReloadPreview();
}

void SelectFiles::SplitterMoved(int pos, int index)
{
    ReloadPreview();
}

void SelectFiles::resizeEvent(QResizeEvent *event)
{
    ReloadPreview();
}

void SelectFiles::SetPreviewImage()
{
    m_PreviewReady = false;
    QPixmap pixmap;
    QString details = "";
    QStandardItem *item = GetLastSelectedImageItem();
    
    ui.Details->clear();
    QString html = "<html><head><title></title></head><body></body></html>";
    if (Utility::IsDarkMode()) {
        html = Utility::AddDarkCSS(html);
    }
    m_WebView->setHtml(html, QUrl());

    if (!item || item->text().isEmpty()) {
        m_PreviewReady = true;
        return;
    }

    Resource::ResourceType resource_type = static_cast<Resource::ResourceType>(item->data(Qt::UserRole).toInt());

    // Basic file details
    QString path = item->data(Qt::UserRole + 1).toString();
    const QFileInfo fileInfo = QFileInfo(path);
    const double ffsize = fileInfo.size() / 1024.0;
    const QString fsize = QLocale().toString(ffsize, 'f', 2);
    const double ffmbsize = ffsize / 1024.0;
    const QString fmbsize = QLocale().toString(ffmbsize, 'f', 2);

    bool loading_resources = false;
    
    // Images
    if (resource_type == Resource::ImageResourceType || resource_type == Resource::SVGResourceType) {

        // Define detailed information label
        const QImage img(path);
        const QUrl imgUrl = QUrl::fromLocalFile(path);
        QString colors_shades = img.isGrayscale() ? tr("shades") : tr("colors");
        QString grayscale_color = img.isGrayscale() ? tr("Grayscale") : tr("Color");
        QString colorsInfo = "";

        if (img.depth() == 32) {
            colorsInfo = QString(" %1bpp").arg(img.bitPlaneCount());
        } else if (img.depth() > 0) {
            colorsInfo = QString(" %1bpp (%2 %3)").arg(img.bitPlaneCount()).arg(img.colorCount()).arg(colors_shades);
        }

        details = QString("%2x%3px | %4 KB | %5%6").arg(img.width()).arg(img.height())
                  .arg(fsize).arg(grayscale_color).arg(colorsInfo);

        // MainWindow::clearMemoryCaches();
        const QUrl resourceUrl = QUrl::fromLocalFile(path);
        QString html = IMAGE_HTML_BASE_PREVIEW.arg(resourceUrl.toString());
        if (Utility::IsDarkMode()) {
            html = Utility::AddDarkCSS(html);
        }
        m_WebView->page()->setBackgroundColor(Utility::WebViewBackgroundColor());
        m_PreviewLoaded = false;
        m_WebView->setHtml(html, resourceUrl);
        loading_resources = true;
    }

    if (resource_type == Resource::VideoResourceType) {
        QString html;
        const QUrl resourceUrl = QUrl::fromLocalFile(path);
        MainWindow::clearMemoryCaches();
        html = VIDEO_HTML_BASE.arg(resourceUrl.toString());
        m_PreviewLoaded = false;
        if (Utility::IsDarkMode()) {
            html = Utility::AddDarkCSS(html);
        }
        m_WebView->page()->setBackgroundColor(Utility::WebViewBackgroundColor());
        m_PreviewLoaded = false;
        m_WebView->setHtml(html, resourceUrl);
        loading_resources = true;
        details = QString("%1 MB").arg(fmbsize);
    } else if (resource_type == Resource::AudioResourceType) {
        QString html;
        const QUrl resourceUrl = QUrl::fromLocalFile(path);
        // MainWindow::clearMemoryCaches();
        html = AUDIO_HTML_BASE.arg(resourceUrl.toString());
        if (Utility::IsDarkMode()) {
            html = Utility::AddDarkCSS(html);
        }
        m_WebView->page()->setBackgroundColor(Utility::WebViewBackgroundColor());
        m_PreviewLoaded = false;
        m_WebView->setHtml(html, resourceUrl);
        loading_resources = true;
        details = QString("%1 MB").arg(fmbsize);
    }

    // Technically, we need to wait until Preview is actually loaded
    // because setHtml loads external resources asynchronously
    if (loading_resources) {
        while(!IsPreviewLoaded()) {
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    ui.Details->setText(details);
    m_PreviewReady = true;
}

void SelectFiles::PreviewLoadComplete(bool okay) 
{
    if (!okay) {
        m_WebView->stop();
    }
    m_PreviewLoaded = true;
}

bool SelectFiles::IsPreviewLoaded()
{
    return m_PreviewLoaded;
}

void SelectFiles::FilterEditTextChangedSlot(const QString &text)
{
    const QString lowercaseText = text.toLower();
    QStandardItem *root_item = m_SelectFilesModel->invisibleRootItem();
    QModelIndex parent_index;
    // Hide rows that don't contain the filter text
    int first_visible_row = -1;

    for (int row = 0; row < root_item->rowCount(); row++) {
        if (text.isEmpty() || root_item->child(row, COL_NAME)->text().toLower().contains(lowercaseText)) {
            ui.imageTree->setRowHidden(row, parent_index, false);

            if (first_visible_row == -1) {
                first_visible_row = row;
            }
        } else {
            ui.imageTree->setRowHidden(row, parent_index, true);
        }
    }

    if (!text.isEmpty() && first_visible_row != -1) {
        // Select the first non-hidden row
        ui.imageTree->setCurrentIndex(root_item->child(first_visible_row, 0)->index());
    } else {
        // Clear current and selection, which clears preview image
        ui.imageTree->setCurrentIndex(QModelIndex());
    }
}

QStandardItem *SelectFiles::GetLastSelectedImageItem()
{
    QStandardItem *item = NULL;

    if (ui.imageTree->selectionModel()->hasSelection()) {
        QModelIndexList selected_indexes = ui.imageTree->selectionModel()->selectedRows(0);

        if (!selected_indexes.isEmpty()) {
            item = m_SelectFilesModel->itemFromIndex(selected_indexes.last());
        }
    }

    return item;
}

QString SelectFiles::GetLastSelectedImageName()
{
    QString selected_entry = "";
    QStandardItem *item = GetLastSelectedImageItem();

    if (item) {
        selected_entry = item->text();
    }

    return selected_entry;
}

void SelectFiles::InsertFromDisk()
{
    m_IsInsertFromDisk = true;
    ui.imageTree->selectionModel()->clear();
    accept();
}

void SelectFiles::ReadSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    // The size of the window and it's full screen status
    QByteArray geometry = settings.value("geometry").toByteArray();

    if (!geometry.isNull()) {
        restoreGeometry(geometry);
    }

    // The position of the splitter handle
    QByteArray splitter_position = settings.value("splitter").toByteArray();

    if (!splitter_position.isNull()) {
        ui.splitter->restoreState(splitter_position);
    }

    // The thumbnail size
    m_ThumbnailSize = settings.value("thumbnail_size").toInt();

    if (m_ThumbnailSize <= 0) {
        ui.ThumbnailDecrease->setEnabled(false);
    }

    settings.endGroup();
}

void SelectFiles::WriteSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    // The size of the window and it's full screen status
    settings.setValue("geometry", saveGeometry());
    // The position of the splitter handle
    settings.setValue("splitter", ui.splitter->saveState());
    // The thumbnail size
    settings.setValue("thumbnail_size", m_ThumbnailSize);
    settings.endGroup();
}

void SelectFiles::connectSignalsSlots()
{
    QItemSelectionModel *selectionModel = ui.imageTree->selectionModel();
    connect(selectionModel,     SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
            this,               SLOT(SelectionChanged(const QItemSelection &, const QItemSelection &)));
    connect(ui.imageTree,       SIGNAL(doubleClicked(const QModelIndex &)), this, SLOT(accept()));
    connect(ui.Filter,          SIGNAL(textChanged(QString)),
            this,               SLOT(FilterEditTextChangedSlot(QString)));
    connect(ui.ThumbnailIncrease, SIGNAL(clicked()), this, SLOT(IncreaseThumbnailSize()));
    connect(ui.ThumbnailDecrease, SIGNAL(clicked()), this, SLOT(DecreaseThumbnailSize()));
    connect(ui.InsertFromDisk,  SIGNAL(clicked()), this, SLOT(InsertFromDisk()));
    connect(ui.FileTypes,       SIGNAL(itemSelectionChanged()), this, SLOT(SetImages()));
    connect(m_WebView,          SIGNAL(loadFinished(bool)), this, SLOT(PreviewLoadComplete(bool)));
    connect(ui.splitter,    SIGNAL(splitterMoved(int, int)), this, SLOT(SplitterMoved(int, int)));
}
