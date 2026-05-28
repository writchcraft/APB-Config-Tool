#include "gui/PageGradientMaker.h"

namespace apb::gui {

IPage* makeGradientMakerPage() {
    return new PageGradientMaker;
}

} // namespace apb::gui
