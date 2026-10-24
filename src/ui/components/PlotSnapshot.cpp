#include "PlotGallery.h"

namespace fa::ui
{
void writePlotGallerySnapshot (const juce::File& file, int width, int height)
{
    PlotGallery gallery (false);
    gallery.setSize (width, height);
    auto image = gallery.createComponentSnapshot (gallery.getLocalBounds(), false, 1.0f);
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    juce::FileOutputStream out (file);
    if (out.openedOk())
        juce::PNGImageFormat().writeImageToStream (image, out);
}
} // namespace fa::ui
