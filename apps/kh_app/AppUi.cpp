#include "AppUi.h"

#include "Icons.h"

namespace kh::app {

void AppUi::showProblems(const Diagnostics& problems) {
    if (problemSink_) {
        problemSink_(problems);
    } else {
        heldProblems_.insert(heldProblems_.end(), problems.begin(), problems.end());
    }
}

QIcon AppUi::icon(std::string_view svg) const {
    return svgIcon(svg);
}

void AppUi::showMessage(const QString& message) {
    if (messageSink_) messageSink_(message);
}

void AppUi::setSinks(std::function<void(const Diagnostics&)> problems,
                     std::function<void(const QString&)> message) {
    problemSink_ = std::move(problems);
    messageSink_ = std::move(message);
    if (problemSink_ && !heldProblems_.empty()) {
        problemSink_(heldProblems_);
        heldProblems_.clear();
    }
}

}  // namespace kh::app
