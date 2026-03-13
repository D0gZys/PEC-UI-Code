#include <QApplication>

#include "ui/MainWindow.hpp"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("LaserBench");
    app.setApplicationName("LaserBench");
    app.setApplicationDisplayName("LaserBench");

    laserbench::ui::MainWindow window;
    window.show();

    return app.exec();
}
